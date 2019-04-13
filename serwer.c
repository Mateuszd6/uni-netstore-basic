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

static char*
read_file_offset(char const* filename, size_t offset, size_t len)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
        return NULL;

    fseek(f, offset, SEEK_SET);

    char *string = malloc(len + 1);

    // TODO: I guess no bytes should be read when the address is oor, but check
    // it out!
    int bytes_read = fread(string, 1, len, f);

    fclose(f);

    string[bytes_read] = 0;
    return string;
}

int main(int argc, char** argv)
{
    input_data idata = parse_input(argc, argv);

    int sock, msg_sock;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    sock = socket(PF_INET, SOCK_STREAM, 0); // creating IPv4 TCP socket
    if (sock < 0)
        assert(!("socket"));
    // after socket() call; we should close(sock) on any execution path;
    // since all execution paths exit immediately, sock would be closed when
    // program terminates

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(atoi(idata.port)); // listening on port PORT_NUM

    // bind the socket to a concrete address
    if (bind(sock, (struct sockaddr*)&server_address, sizeof(server_address)) < 0)
    {
        fprintf(stderr, "BIND: %d %s\n", errno, strerror(errno));
        assert(!("bind"));
    }

    // switch to listening (passive open)
    if (listen(sock, 5) < 0) // TODO: 5 was macroed as QUEUE_LEN
        assert(!("listen"));

    printf("accepting client connections on port %hu\n",
           ntohs(server_address.sin_port));

    for (;;)
    {
        printf("I wait for the next one!\n");

        client_address_len = sizeof(client_address);
        // get client connection from the socket
        msg_sock = accept(sock,
                          (struct sockaddr*)&client_address,
                          &client_address_len);

        fprintf(stderr, "Accepted!\n");

        if (msg_sock < 0)
            assert(!("accept"));

        // TODO: Discover, why this 0 bytes packet is so important.
        {
            char buffer[2];
            ssize_t len = read(msg_sock, buffer, 2);
            if (len == 0) // TODO: Handle this case, and handle -1!!
            {
                fprintf(stderr, "Got 0, doing nothing!\n");
                break;
            }

            int16 action_type = ntohs(* ((int16*)buffer));
            fprintf(stderr, "rcved action type: %d\n", (int32)action_type);

            exbuffer ebuf;
            int16 num_to_send = htons(1);
            int32 sizeof_filenames = 0; // We dont know yet how much space.
            exbuffer_init(&ebuf, 0);
            exbuffer_append(&ebuf, (uint8*)(&num_to_send), 2);
            exbuffer_append(&ebuf, (uint8*)(&sizeof_filenames), 4);

            for (int i =0; i < 6; ++i)
                fprintf(stderr, "%u ", ebuf.data[i]);
            fprintf(stderr, "\n");

            DIR *d;
            struct dirent *dir;
            d = opendir(idata.dirname);
            if (d)
            {
                while ((dir = readdir(d)) != NULL)
                {
                    // TODO: Ask if only files, or what to do with dirs!
                    char separator[] = "|";
                    exbuffer_append(&ebuf, (uint8*)(dir->d_name), strlen(dir->d_name));
                    exbuffer_append(&ebuf, (uint8*)separator, 1); // TODO: ARRAY_COUNT
                }

                closedir(d);
            }
            else
                assert(!("No such directory!"));

            // Now we know how much space filenames really take.
            sizeof_filenames = htonl(ebuf.size - 6);
            memcpy(ebuf.data + 2, (uint8*)(&sizeof_filenames), 4);

            size_t snd_len = write(msg_sock, ebuf.data, ebuf.size);
            if (snd_len != ebuf.size)
                assert(!("writing to client socket"));
            fprintf(stderr, "data was sent\n");
            exbuffer_free(&ebuf);

            uint8 header_buffer[10];
            int loaded = read_bytes(msg_sock, header_buffer, 10);
            if (loaded == -1)
                syserr("read");
            if (loaded != 10)
                fprintf(stderr, "Could not read everything!\n");

            uint32 addr_from = ntohl(*((uint32*)header_buffer));
            uint32 addr_len = ntohl(*((uint32*)(header_buffer + 4)));
            uint16 filename_len = ntohs(*((uint16*)(header_buffer + 8)));

            fprintf(stderr, "[%u - %u], filename has %u chars\n",
                    addr_from, addr_len, (uint16)filename_len);

            uint8 name_buffer[filename_len + 1];
            loaded = read_bytes(msg_sock, name_buffer, filename_len);
            if (loaded == -1)
                syserr("read");
            if (loaded != filename_len)
                fprintf(stderr, "Could not read everything!\n");

            name_buffer[filename_len] = '\0';
            fprintf(stderr, "Filename is: %s\n", name_buffer);

            exbuffer_init(&ebuf, 1);
            char* reqfile = read_file_offset((char*)name_buffer, addr_from, addr_len);
            fprintf(stderr, "Requested str has len: %ld\n", strlen(reqfile));
            // TODO: Check offsets, etc, etc...

            if (reqfile)
            {
                int16 msg_code = htons(3);
                int32 msg_filename = htonl(strlen(reqfile)); // TODO: strip the file.

                exbuffer_append(&ebuf, (uint8*)(&msg_code), 2);
                exbuffer_append(&ebuf, (uint8*)(&msg_filename), 4);
                exbuffer_append(&ebuf, (uint8*)reqfile, strlen(reqfile));
            }
            else
            {
                int16 msg_code = htons(2);
                int32 msg_reason = htonl(1); // TODO: strip the file.

                exbuffer_append(&ebuf, (uint8*)(&msg_code), 2);
                exbuffer_append(&ebuf, (uint8*)(&msg_reason), 4);
            }

            if (reqfile)
                free(reqfile);
            write(msg_sock, ebuf.data, ebuf.size);

            // This is received while end.
            ssize_t end_msg_len = read(msg_sock, buffer, 2);
            if (end_msg_len != 0) // TODO: Handle this case, and handle -1!!
                fprintf(stderr, "Exit message has something! This is kind of bad!\n");
            else
                fprintf(stderr, "Client has eneded.\n");
        }

        printf("ending connection\n");
        close(msg_sock); // TODO: error.
    }

    return 0;
}
