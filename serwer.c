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

        ssize_t len;
        if (msg_sock < 0)
            assert(!("accept"));
        do
        {
            char buffer[1024];
            len = read(msg_sock, buffer, sizeof(buffer));
            // TODO: Handle read.
#if 0
            if (len < 0)
                assert(!("reading from client socket"));
            else
            {
                printf("read from socket: %zd bytes: %.*s\n", len, (int)len, buffer);
                // snd_len = write(msg_sock, buffer, len);
                snd_len = send(msg_sock, buffer, len, MSG_NOSIGNAL);
                if (snd_len != len)
                    assert(!("writing to client socket"));
            }
#endif
            exbuffer ebuf;
            exbuffer_init(&ebuf, 0);
            int16 num_to_send = htons(1);
            int32 sizeof_filenames = 0; // We dont know yet how much space.
            exbuffer_append(&ebuf, (uint8*)(&num_to_send), 2);
            exbuffer_append(&ebuf, (uint8*)(&sizeof_filenames), 4);

            fprintf(stderr,
                    "Buffer contains: (size: %lu, cap: %lu): ",
                    ebuf.size, ebuf.capacity);

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

            size_t snd_len = send(msg_sock, ebuf.data, ebuf.size, MSG_NOSIGNAL);
            if (snd_len != ebuf.size)
                assert(!("writing to client socket"));

        } while (len > 0);

        printf("ending connection\n");
        if (close(msg_sock) < 0)
            assert(!("close"));
    }

    return 0;
}
