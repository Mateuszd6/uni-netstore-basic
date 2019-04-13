#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

#define USAGE_MSG "netstore_client <nazwa-lub-adres-IP4-serwera> [<numer-portu-serwera>]"

#define CHECK(VAL)                              \
    do {                                        \
        int checked_val_ = (VAL);               \
        if (checked_val_ != 0) {                                 \
            fprintf(stderr, "ERROR, %s:%d: val: %d\n", __FILE__, __LINE__, checked_val_); \
            exit(1);                                             \
        }                                                        \
    } while(0)

typedef struct
{
    char const* host;
    char const* port;
} input_data;

static input_data
parse_input(int argc, char** argv)
{
    input_data retval;
    if (argc < 2 || argc > 3)
        bad_usage(USAGE_MSG);

    retval.host = argv[1];
    retval.port = (argc == 3 ? argv[2] : default_port);
    return retval;
}

int
main(int argc, char** argv)
{
    input_data idata = parse_input(argc, argv);
    printf("Input: host: %s, port: %s\n", idata.host, idata.port);

    // 'converting' host/port in string to struct addrinfo
    struct addrinfo addr_hints;
    struct addrinfo* addr_result;
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    CHECK(getaddrinfo(idata.host, idata.port, &addr_hints, &addr_result));
    // TODO: Handle the error
    //       (system error != other error (e.g. invalid address)).

    int sock = socket(addr_result->ai_family,
                      addr_result->ai_socktype,
                      addr_result->ai_protocol);
    fprintf(stderr, "SOCK: %d\n", sock);
    // TODO: Check that: sock >= 0.

    // connect socket to the server
    CHECK(connect(sock, addr_result->ai_addr, addr_result->ai_addrlen));

    freeaddrinfo(addr_result);

    { // TEST AREA: DANGER, DONT ENTER!
        write(sock, "Mateusz", sizeof("Mateusz"));
        char b[1024];

        int err = read_bytes(sock, (uint8*)b, 6);
        if (err != 0)
            CHECK(-1);

        fprintf(stderr, "Buffer contains: ");
        for (int i =0; i < 6; ++i)
            fprintf(stderr, "%u ", (unsigned char)(b[i]));
        fprintf(stderr, "\n");

        int16 msg_type = ntohs(*((int16*)b));
        int32 dirnames_size = ntohl(*((int32*)(b + 2)));
        fprintf(stderr, "Received back: action: %d size: %d.\n", msg_type, dirnames_size);

        err = read_bytes(sock, (uint8*)b, dirnames_size);
        if (err != 0)
            CHECK(-1);

        fprintf(stderr, "Dir contains:\n");
        char const* txt = b;
        char const* end = b + dirnames_size;
        do
        {
            char const* next = txt;
            while(next != end && *next != '|')
                ++next;

            fprintf(stderr, "  %.*s\n", (int32)(next - txt), txt);
            if (next != end)
                ++next;
            txt = next;
        } while(txt != end);
    } // endof TEST AREA.

    CHECK(close(sock)); // socket would be closed anyway when the program ends
    return 0;
}
