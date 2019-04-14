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

#define USAGE_MSG "netstore-server <nazwa-katalogu-z-plikami> [<numer-portu-serwera>]"
#define QUEUE_LEN (5)

// TODO: Reneme them to client_input_data and server_input_data.
typedef struct
{
    char const* dirname;
    char const* port;
} input_data;

static input_data
parse_input(int argc, char** argv)
{
    input_data retval;
    if (argc < 2 || argc > 3)
        bad_usage(USAGE_MSG);

    retval.dirname = argv[1];
    retval.port = (argc == 3 ? argv[2] : default_port);
    return retval;
}

// TODO(NEXT): Describe.
static char const*
get_folder_filenames(char const* dirname, exbuffer* ebufptr)
{
    DIR *d;
    struct dirent *dir;
    d = opendir(dirname);
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if (dir->d_type == DT_REG)
            {
                // TODO: Ask if only files, or what to do with dirs!
                char separator[] = "|";
                CHECK(exbuffer_append(ebufptr, (uint8*)(dir->d_name), strlen(dir->d_name)));
                CHECK(exbuffer_append(ebufptr, (uint8*)separator, 1)); // TODO: ARRAY_COUNT
            }
        }

        closedir(d);
    }
    else
    {
        fprintf(stderr, "Directory does not exists. Exitting.\n");
        errno = ENOTDIR;
        FAILWITH_ERRNO();
    }
    return 0;
}

// Returns the size of the file under the given pointer.
static size_t
get_file_size(FILE* fileptr)
{
    size_t retval = 0;
    CHECK(fseek(fileptr, 0, SEEK_END));
    CHECK(retval = ftell(fileptr));

    return retval;
}

// In the context this fucntion is called, we know that the len is > 0, and
// offset is less than the size of the file, so out of this contract, the
// behaviour should be considered undefinied.
static char*
read_file_offset(FILE* fileptr, size_t offset, size_t len)
{
    CHECK(fseek(fileptr, offset, SEEK_SET));
    char *string = malloc(len + 1);
    int bytes_read = fread(string, 1, len, fileptr);
    string[bytes_read] = 0;

    return string;
}

int
main(int argc, char** argv)
{
    input_data idata = parse_input(argc, argv);

    int sock, msg_sock;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    CHECK((sock = socket(PF_INET, SOCK_STREAM, 0))); // creating IPv4 TCP socket

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(atoi(idata.port)); // listening on port PORT_NUM

    // bind the socket to a concrete address
    CHECK(bind(sock, (struct sockaddr*)&server_address, sizeof(server_address)));

    // switch to listening (passive open)
    CHECK(listen(sock, QUEUE_LEN));

    printf("accepting client connections on port %hu\n",
           ntohs(server_address.sin_port));

    for (;;)
    {
        printf("I wait for the next one!\n");

        client_address_len = sizeof(client_address);
        // get client connection from the socket
        CHECK(msg_sock = accept(sock,
                                (struct sockaddr*)&client_address,
                                &client_address_len));

        uint8 buffer[2];
        int read_total_err = 0;
        CHECK(read_total_err = read_total(msg_sock, buffer, 2));
        if (read_total_err != 0) // TODO: Handle this case, and handle -1!!
        {
            fprintf(stderr, "Got 0, doing nothing!\n");
            break;
        }

        int16 action_type = unaligned_load_int16be(buffer);
        fprintf(stderr, "rcved action type: %d\n", action_type);

        int16 num_to_send = htons(1);
        int32 sizeof_filenames = 0; // We dont know yet how much space.
        exbuffer ebuf;
        CHECK(exbuffer_init(&ebuf, 0));
        CHECK(exbuffer_append(&ebuf, (uint8*)(&num_to_send), 2));
        CHECK(exbuffer_append(&ebuf, (uint8*)(&sizeof_filenames), 4));

        // This will write finenames to the buffer.
        get_folder_filenames(idata.dirname, &ebuf);

        // Now we know how much space filenames really take, so we override
        // previously skipped bytes in the msg.
        sizeof_filenames = htonl(ebuf.size - 6);
        memcpy(ebuf.data + 2, (uint8*)(&sizeof_filenames), 4);

        // TODO: Proper error handling.
        size_t snd_len;
        CHECK(snd_len = write(msg_sock, ebuf.data, ebuf.size));
        if (snd_len != ebuf.size)
            assert(!("writing to client socket"));
        fprintf(stderr, "data was sent\n");
        exbuffer_free(&ebuf);

        uint8 header_buffer[10];
        read_total_err = 0;
        CHECK(read_total_err = read_total(msg_sock, header_buffer, 10));
        if (read_total_err != 0) // Not a system error.
        {
            // TODO!
            fprintf(stderr, "Could not read everything!\n");
            exit(-1);
        }

        uint32 addr_from = unaligned_load_int32be(header_buffer);
        uint32 addr_len = unaligned_load_int32be(header_buffer + 4);
        uint16 filename_len = unaligned_load_int16be(header_buffer + 8);

        fprintf(stderr, "[%u - %u], filename has %u chars\n",
                addr_from, addr_len, (uint16)filename_len);

        uint8 name_buffer[filename_len + 1];
        CHECK(read_total_err = read_total(msg_sock, name_buffer, filename_len));
        if (read_total_err != 0) // Not a system error.
        {
            // TODO!
            fprintf(stderr, "Could not read everything!\n");
            exit(-1);
        }

        name_buffer[filename_len] = '\0';
        fprintf(stderr, "Filename is: %s\n", name_buffer);

        CHECK(exbuffer_init(&ebuf, 1));

        int error_code = 0;
        char* reqfile = 0;

        if (addr_len == 0)
        {
            fprintf(stderr, "ERROR: Given length is 0.\n");
            error_code = 3;
        }
        else
        {
            FILE* reqfile_ptr = fopen((char*)name_buffer, "r");
            if (!reqfile_ptr)
            {
                fprintf(stderr, "ERROR: File %s does not exists.\n", (char*)name_buffer);
                error_code = 1;
            }
            else
            {
                size_t reqfile_size = get_file_size(reqfile_ptr);
                if (addr_from >= reqfile_size)
                {
                    fprintf(stderr, "ERROR: Address is out of range.\n");
                    error_code = 2;
                }
                else
                {
                    // We'have finally succeeded, so we read the file chunk.
                    reqfile = read_file_offset(reqfile_ptr, addr_from, addr_len);
                }

                fclose(reqfile_ptr);
            }
        }

        // If there was no error, send the file to the client.
        if (error_code == 0)
        {
            int16 msg_code = htons(3);
            int32 msg_filename = htonl(strlen(reqfile));

            CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_code), 2));
            CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_filename), 4));
            write(msg_sock, ebuf.data, ebuf.size);

            size_t send_len = strlen(reqfile);
            size_t block_size = 512;
            for (size_t i = 0; i < send_len; i += block_size)
            {
                size_t chunk_len = (i + block_size > send_len
                                    ? send_len - i
                                    : block_size);

                size_t send_data = 0;
                CHECK(send_data = write(msg_sock, reqfile + i, chunk_len));
                // TODO: Check if all bytes were sent.
            }
        }
        else // Otherwise send the refuse with an error code as a reason.
        {
            int16 msg_code = htons(2);
            int32 msg_reason = htonl(error_code);

            CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_code), 2));
            CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_reason), 4));
            write(msg_sock, ebuf.data, ebuf.size);
        }

        if (reqfile)
            free(reqfile);

        // This is received while end.
        ssize_t end_msg_len = read(msg_sock, buffer, 2);
        if (end_msg_len != 0) // TODO: Handle this case, and handle -1!!
            fprintf(stderr, "Exit message has something! This is kind of bad!\n");
        else
            fprintf(stderr, "Client has eneded.\n");

        printf("ending connection\n");
        close(msg_sock); // TODO: error.
    }

    return 0;
}
