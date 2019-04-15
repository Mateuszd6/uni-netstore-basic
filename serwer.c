#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "exbuffer.h"

#define USAGE_MSG                                                              \
    "netstore-server <nazwa-katalogu-z-plikami> [<numer-portu-serwera>]"

typedef struct {
    char const *dirname;
    char const *port;
} server_input_data;

typedef struct {
    char *content;
    size_t size;
    int error_code;
} load_file_result;

typedef struct {
    uint32 addr_from;
    uint32 addr_len;
    char *filename;
    uint16 filename_len;
} chunk_request;

void load_file_result_free(load_file_result *self) {
    if (self->content)
        free(self->content);
}

void chunk_request_free(chunk_request *self) {
    if (self->filename)
        free(self->filename);
}

static server_input_data parse_input(int argc, char **argv) {
    server_input_data retval;
    if (argc < 2 || argc > 3)
        bad_usage(USAGE_MSG);

    retval.dirname = argv[1];
    retval.port = (argc == 3 ? argv[2] : default_port);
    return retval;
}

// Write to the exbuffer all filenames from the directory with the given
// name. The names are splited with '|'.
static char const *get_folder_filenames(char const *dirname,
                                        exbuffer *ebufptr) {
    DIR *d;
    if ((d = opendir(dirname))) {
        struct dirent *dir;
        int first_appended = 0;
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {
                if (first_appended) {
                    char separator[] = "|";
                    CHECK(exbuffer_append(ebufptr, (uint8 *)separator,
                                          sizeof(separator) - 1));
                }

                first_appended = 1;
                CHECK(exbuffer_append(ebufptr, (uint8 *)(dir->d_name),
                                      strlen(dir->d_name)));
            }
        }

        closedir(d);
    }
    else {
        fprintf(stderr, "ERROR: Directory does not exists\n");
        errno = ENOTDIR;
        FAILWITH_ERRNO();
    }
    return 0;
}

// Returns the size of the file under the given pointer.
static size_t get_file_size(FILE *fileptr) {
    size_t retval = 0;
    CHECK(fseek(fileptr, 0, SEEK_END));
    CHECK(retval = ftell(fileptr));

    return retval;
}

// If error_code of the returned structure is 0, then content and size contains
// chunk of file that has to be sent to the client, otherwise the error code
// should be sent in the refuse message.
static load_file_result try_load_requested_chunk(char const *dirname,
                                                 char const *name,
                                                 size_t addr_from,
                                                 size_t addr_len) {
    load_file_result retval;
    retval.content = 0;
    retval.size = 0;
    retval.error_code = 0;

    if (addr_len == 0) {
        fprintf(stderr, "BAD REQUEST: Given length is 0\n");
        retval.error_code = FREQ_ERROR_ZERO_LEN;
    }
    else {
        size_t dirname_len = strlen(dirname);
        size_t name_len = strlen(name);

        // If dirname is absolute, we don't add ./ prefix.
        char relative_prefix[] = "./";
        char absolute_prefix[] = "";
        char *prefix = (dirname[0] == '/' || dirname[0] == '~')
                           ? absolute_prefix
                           : relative_prefix;
        size_t prefix_len = strlen(prefix);
        char separator[] = "/";
        size_t separator_len = 1;

        char path_combined[prefix_len + dirname_len + 1 + name_len + 1];
        strcpy(path_combined, prefix);
        strcpy(&path_combined[prefix_len], dirname);
        strcpy(&path_combined[prefix_len + dirname_len], separator);
        strcpy(&path_combined[prefix_len + dirname_len + separator_len], name);

        FILE *reqfile_ptr = fopen(path_combined, "r");
        if (!reqfile_ptr) {
            fprintf(stderr, "BAD REQUEST: File %s does not exists\n",
                    path_combined);
            retval.error_code = FREQ_ERROR_ON_SUCH_FILE;
        }
        else {
            size_t reqfile_size = get_file_size(reqfile_ptr);
            if (addr_from >= reqfile_size) {
                fprintf(stderr, "BAD REQUEST: Address is out of range\n");
                retval.error_code = FREQ_ERROR_OUT_OF_RANGE;
            }
            else {
                CHECK(fseek(reqfile_ptr, addr_from, SEEK_SET));
                retval.content = malloc(addr_len + 1);
                if (!retval.content) {
                    // Handle out of memory.
                    errno = ENOMEM;
                    FAILWITH_ERRNO();
                }

                retval.size = fread(retval.content, 1, addr_len, reqfile_ptr);
                retval.content[retval.size] = 0;

                fprintf(stderr,
                        "REQUEST OK: File %s is available and in range\n",
                        path_combined);
            }

            fclose(reqfile_ptr);
        }
    }

    return retval;
}

static int snd_filenames(int msg_sock, char const *dirname) {
    int16 num_to_send = htons(PROT_RESP_FILELIST);
    int32 sizeof_filenames = 0; // We dont know yet how much space.
    exbuffer ebuf;
    CHECK(exbuffer_init(&ebuf));
    CHECK(exbuffer_append(&ebuf, (uint8 *)(&num_to_send), 2));
    CHECK(exbuffer_append(&ebuf, (uint8 *)(&sizeof_filenames), 4));

    // This will write finenames to the buffer.
    get_folder_filenames(dirname, &ebuf);

    // Now we know how much space filenames really take, so we override
    // previously skipped bytes in the msg.
    sizeof_filenames = htonl(ebuf.size - 6);
    memcpy(ebuf.data + 2, (uint8 *)(&sizeof_filenames), 4);

    int snd_error;
    snd_error = snd_total(msg_sock, ebuf.data, ebuf.size);
    exbuffer_free(&ebuf);

    return snd_error;
}

static int snd_filechunk(int msg_sock, char const *dirname,
                         chunk_request *request) {
    exbuffer ebuf;
    CHECK(exbuffer_init(&ebuf));

    load_file_result load_result = try_load_requested_chunk(
        dirname, request->filename, request->addr_from, request->addr_len);

    int16 msg_code;
    int32 msg_filelen_or_refuse_reason;

    // If there was no error, send the file to the client.
    if (load_result.error_code == 0) {
        msg_code = htons(PROT_RESP_FILECHUNK_OK);
        msg_filelen_or_refuse_reason = htonl(load_result.size);
    }
    else {
        msg_code = htons(PROT_RESP_FILECHUNK_ERROR);
        msg_filelen_or_refuse_reason = htonl(load_result.error_code);
    }

    CHECK(exbuffer_append(&ebuf, (uint8 *)(&msg_code), 2));
    CHECK(exbuffer_append(&ebuf, (uint8 *)(&msg_filelen_or_refuse_reason), 4));
    // load_result.size will be zero on error.
    CHECK(
        exbuffer_append(&ebuf, (uint8 *)load_result.content, load_result.size));

    int snd_error;
    snd_error = snd_total(msg_sock, ebuf.data, ebuf.size);

    exbuffer_free(&ebuf);
    load_file_result_free(&load_result);

    // Return result of snd_total, as it returns 0 on success, and -1 on fail.
    return snd_error;
}

static int rcv_chunk_request(int msg_sock, chunk_request *req) {
    uint8 header_buffer[10];

    if (rcv_total(msg_sock, header_buffer, 10) == -1)
        return -1;

    req->addr_from = unaligned_load_int32be(header_buffer);
    req->addr_len = unaligned_load_int32be(header_buffer + 4);
    req->filename_len = unaligned_load_int16be(header_buffer + 8);

    req->filename = malloc(req->filename_len + 1);
    if (rcv_total(msg_sock, (uint8 *)req->filename, req->filename_len) == -1) {
        chunk_request_free(req);
        return -1;
    }

    req->filename[req->filename_len] = '\0';

    fprintf(stderr, "Received request for file: %s (%u bytes from %u)\n",
            req->filename, req->addr_from, req->addr_len);

    return 0;
}

static int init_and_bind(server_input_data *idata) {
    int sock;
    struct sockaddr_in server_address;

    // Create an IPv4 socket.
    CHECK((sock = socket(PF_INET, SOCK_STREAM, 0)));

    // IPv4, all interfaces, and port taken from input data.
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(atoi(idata->port));

    // Bind the socket to a concrete address, and switch for listening.
    CHECK(
        bind(sock, (struct sockaddr *)&server_address, sizeof(server_address)));
    CHECK(listen(sock, SOMAXCONN));

    printf("Accepting clients on port %hu\n", ntohs(server_address.sin_port));

    return sock;
}

#define DROP_CONN()                                                            \
    {                                                                          \
        fprintf(stderr, "Connection droped\n");                                \
        close(msg_sock);                                                       \
        break;                                                                 \
    }                                                                          \
    do {                                                                       \
    } while (0)

#define DROP_CONN_WITH_ROUGE()                                                 \
    {                                                                          \
        fprintf(stderr, "Client is out of contract. Connection droped\n");     \
        close(msg_sock);                                                       \
        break;                                                                 \
    }                                                                          \
    do {                                                                       \
    } while (0)

int main(int argc, char **argv) {
    server_input_data idata = parse_input(argc, argv);
    int sock = init_and_bind(&idata);

    struct sockaddr_in client_address;
    socklen_t client_address_len;
    for (;;) {
        fprintf(stderr, "Server awaits for the next clinet\n");

        client_address_len = sizeof(client_address);
        // get client connection from the socket
        int msg_sock;
        CHECK(msg_sock = accept(sock, (struct sockaddr *)&client_address,
                                &client_address_len));

        for (;;) {
            uint8 buffer[2];
            int try_rcv_total_result;
            try_rcv_total_result = try_rcv_total(msg_sock, buffer, 2);
            if (try_rcv_total_result == -1) {
                DROP_CONN();
            }
            else if (try_rcv_total_result == 0) {
                fprintf(stderr, "Client has ended connection\n");
                CHECK(close(msg_sock));
                break;
            }

            int16 action_type = unaligned_load_int16be(buffer);

            if (action_type == PROT_REQ_FILELIST) {
                fprintf(stderr, "Received request for a filelist\n");
                if (snd_filenames(msg_sock, idata.dirname) == -1) {
                    DROP_CONN();
                }
                else {
                    fprintf(stderr, "Filenames response has been sent\n");
                }
            }
            else if (action_type == PROT_REQ_FILECHUNK) {
                fprintf(stderr, "Received request for a filechunk\n");
                chunk_request request;
                if ((rcv_chunk_request(msg_sock, &request)) == -1) {
                    DROP_CONN();
                }

                if ((snd_filechunk(msg_sock, idata.dirname, &request)) == -1) {
                    DROP_CONN();
                }
                else {
                    fprintf(stderr, "Filechunk response has been sent\n");
                }

                chunk_request_free(&request);
            }
            else {
                // We are out of contract, so break a conn with rouge client.
                DROP_CONN_WITH_ROUGE();
            }
        }
    }

    return 0;
}
