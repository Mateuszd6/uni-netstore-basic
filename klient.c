#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "exbuffer.h"

#define USAGE_MSG                                                              \
    "netstore_client <nazwa-lub-adres-IP4-serwera> [<numer-portu-serwera>]"

char const refuse_invalid_name[] = "Invalid file name.";
char const refuse_invalid_address[] =
    "Invalid starting file address (out of range).";
char const refuse_invalid_len[] = "Region has length 0.";

typedef struct {
    char const *host;
    char const *port;
} client_input_data;

typedef struct {
    char *filenames;
    size_t num_files;
} filelist_response;

typedef struct {
    uint8 *data;
    size_t data_len;
    int32 error_code;
} filechunk_response;

void filelist_response_free(filelist_response *self) {
    if (self->filenames)
        free(self->filenames);
}

void filechunk_response_free(filechunk_response *self) {
    if (self->data)
        free(self->data);
}

static client_input_data parse_input(int argc, char **argv) {
    client_input_data retval;
    if (argc < 2 || argc > 3)
        bad_usage(USAGE_MSG);

    retval.host = argv[1];
    retval.port = (argc == 3 ? argv[2] : default_port);
    return retval;
}

static inline char const *file_refuse_tostr(int32 refuse_code) {
    if (refuse_code == FREQ_ERROR_ON_SUCH_FILE)
        return refuse_invalid_name;
    else if (refuse_code == FREQ_ERROR_OUT_OF_RANGE)
        return refuse_invalid_address;
    else // FREQ_ERROR_ZERO_LEN
        return refuse_invalid_len;
}

static void write_to_tmp_file_at_offset(char const *filename, size_t offset,
                                        uint8 *data, size_t len) {
    int mkdir_result = mkdir("./tmp", 0777);
    if (mkdir_result == -1 && errno != EEXIST) {
        // If makedir returned other error than one indicating that dir exists,
        // we can't do much so we exit with an error.
        FAILWITH_ERRNO();
    }

    char const outputdir[] = "./tmp";
    size_t outputdir_len = sizeof(outputdir) - 1;
    char path_combined[outputdir_len + 1 + strlen(filename) + 1];
    strcpy(path_combined, outputdir);
    strcpy(&path_combined[outputdir_len], "/");
    strcpy(&path_combined[outputdir_len + 1], filename);

    FILE *fileptr = fopen(path_combined, "r+");
    if (!fileptr)
        fileptr = fopen(path_combined, "w+");

    // If we failed creating file, system error has occured.
    if (!fileptr)
        FAILWITH_ERRNO();

    CHECK(fseek(fileptr, offset, SEEK_SET));
    CHECK(fwrite(data, 1, len, fileptr));
    CHECK(fclose(fileptr));

    fprintf(stderr, "Sucesfully wrote %lu bytes to file %s\n", len,
            path_combined);
}

// This will exit if user-inserted values are invalid.
static inline void sanitize_selected_file_input(int32 filenum, int32 addr_from,
                                                int32 addr_to,
                                                int32 total_files) {
    if (filenum < 0 || filenum >= total_files) {
        fprintf(stderr, "ERROR: File number out of range\n");
        exit(1);
    }
    else if (addr_from < 0 || addr_to < 0) {
        fprintf(stderr, "ERROR: Invalid addres. Can't be negative\n");
        exit(1);
    }
    else if (addr_to < addr_from) {
        fprintf(stderr, "ERROR: Invalid addres. Last is less that First\n");
        exit(1);
    }
}

static void rcv_filelist(int msg_sock, filelist_response *req) {
    uint8 header_buf[6];
    CHECK(rcv_total(msg_sock, (uint8 *)header_buf, 6));

    int16 msg_type = unaligned_load_int16be(header_buf);
    if (msg_type != PROT_RESP_FILELIST) {
        fprintf(stderr, "ERROR: Unexpeted response from server\n");
        exit(1);
    }

    fprintf(stderr, "Received filelist from the server\n");

    int32 dirnames_size = unaligned_load_int32be(header_buf + 2);
    if (dirnames_size == 0) {
        fprintf(stderr,
                "Directory contains no files. There is nothing to do\n");
        exit(0);
    }

    char *names = malloc(dirnames_size + 1);
    if (!names) {
        errno = ENOMEM;
        FAILWITH_ERRNO();
    }

    CHECK(rcv_total(msg_sock, (uint8 *)names, dirnames_size));
    char *curr = names;
    char *next = names;
    char *end = names + dirnames_size;
    int32 idx = 0;

    // Because we've allocated one more byte for [names].
    *end++ = '|';

    // Now we replace all '|' with zeros, so filenames are null separated.
    while ((curr = next) != end) {
        while (*next != '|')
            ++next;
        *next++ = '\0';
        idx++;
    }

    req->filenames = names;
    req->num_files = idx;
}

static void rvc_filechunk(int msg_sock, filechunk_response *req) {
    uint8 rcv_header[6];
    CHECK(rcv_total(msg_sock, rcv_header, 6));

    int16 code = unaligned_load_int16be(rcv_header);
    int32 following = unaligned_load_int32be(rcv_header + 2);
    fprintf(stderr, "Received response from the server\n");

    if (code == PROT_RESP_FILECHUNK_ERROR) {
        req->data = 0;
        req->data_len = 0;
        req->error_code = following;
    }
    else if (code == PROT_RESP_FILECHUNK_OK) {
        req->data = malloc(following);
        if (!req->data) {
            errno = ENOMEM;
            FAILWITH_ERRNO();
        }

        req->data_len = following;
        req->error_code = 0;
        CHECK(rcv_total(msg_sock, req->data, following));
    }
    else {
        fprintf(stderr, "ERROR: Unexpeted response from server\n");
        exit(1);
    }
}

static void snd_filelist_response(int msg_sock) {
    int16 msg_get = htons(PROT_REQ_FILELIST);
    CHECK(write(msg_sock, &msg_get, 2));
}

static void snd_file_request(int msg_sock, uint32 addr_from, uint32 addr_to,
                             char const *selected_name) {
    uint16 choosen_name_len = (uint16)strlen(selected_name);

    size_t total_msg_size = 2 + 4 + 4 + 2 + choosen_name_len;

    // Prepare and byteswap values to send.
    uint32 msg_request_num = htons(PROT_REQ_FILECHUNK);
    uint32 msg_addr_from = htonl(addr_from);
    uint32 msg_addr_len = htonl(addr_to - addr_from);
    uint16 msg_str_len = htons(choosen_name_len);

    exbuffer ebuf;
    CHECK(exbuffer_init(&ebuf));
    CHECK(exbuffer_append(&ebuf, (uint8 *)(&msg_request_num), 2));
    CHECK(exbuffer_append(&ebuf, (uint8 *)(&msg_addr_from), 4));
    CHECK(exbuffer_append(&ebuf, (uint8 *)(&msg_addr_len), 4));
    CHECK(exbuffer_append(&ebuf, (uint8 *)(&msg_str_len), 2));
    CHECK(exbuffer_append(&ebuf, (uint8 *)selected_name, choosen_name_len));

    assert(ebuf.size == total_msg_size);
    CHECK(write(msg_sock, ebuf.data, ebuf.size));
    exbuffer_free(&ebuf);

    fprintf(stderr, "Request for file %s addr: %u - %u has been sent\n",
            selected_name, addr_from, addr_to);
}

static int init_and_connect(client_input_data *idata) {
    // 'converting' host/port in string to struct addrinfo
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    if (getaddrinfo(idata->host, idata->port, &addr_hints, &addr_result) != 0) {
        // With some reason, getaddrinfo does not set errno, so we have to set
        // it manually before exitting with an error.
        errno = EFAULT;
        FAILWITH_ERRNO();
    }

    int msg_sock;
    CHECK(msg_sock = socket(addr_result->ai_family, addr_result->ai_socktype,
                            addr_result->ai_protocol));

    // connect socket to the server
    CHECK(connect(msg_sock, addr_result->ai_addr, addr_result->ai_addrlen));
    freeaddrinfo(addr_result);

    fprintf(stderr, "Connecting succeeded\n");
    return msg_sock;
}

int main(int argc, char **argv) {
    client_input_data idata = parse_input(argc, argv);
    fprintf(stderr, "Input: host: %s, port: %s\n", idata.host, idata.port);
    int msg_sock = init_and_connect(&idata);

    snd_filelist_response(msg_sock);

    filelist_response filelist;
    rcv_filelist(msg_sock, &filelist);

    printf("Directory contains %lu files:\n", filelist.num_files);
    char const *curname = filelist.filenames;
    for (size_t i = 0; i < filelist.num_files; ++i) {
        printf("%lu. %s\n", i, curname);
        curname = strchr(curname, '\0');
        assert(curname); // We know how many zero is there, so we must succeed.
        curname++;
    }

    int32 number, addr_from, addr_to;
    printf("Select a file: ");
    scanf("%d", &number);
    printf("Address from: ");
    scanf("%d", &addr_from);
    printf("Address to (exclusive): ");
    scanf("%d", &addr_to);

    // If this won't exit program, inserted values are valid.
    sanitize_selected_file_input(number, addr_from, addr_to,
                                 filelist.num_files);

    char const *nameptr = filelist.filenames;
    for (int32 i = 0; i != number; ++i) {
        nameptr = strchr(nameptr, '\0');
        assert(nameptr);
        ++nameptr;
    }

    char *selected_name = strdup(nameptr);
    filelist_response_free(
        &filelist); // We dont need filelist response any more.
    snd_file_request(msg_sock, addr_from, addr_to, selected_name);

    filechunk_response filechunk;
    rvc_filechunk(msg_sock, &filechunk);

    // If error code was set, server has refused.
    if (filechunk.error_code) {
        printf("Server refused, reason: %s\n",
               file_refuse_tostr(filechunk.error_code));
    }
    else {
        write_to_tmp_file_at_offset(selected_name, addr_from, filechunk.data,
                                    filechunk.data_len);
    }

    free(selected_name);
    filechunk_response_free(&filechunk);
    CHECK(close(msg_sock));
    return 0;
}
