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

#define USAGE_MSG "netstore_client <nazwa-lub-adres-IP4-serwera> [<numer-portu-serwera>]"

typedef struct
{
    char const* host;
    char const* port;
} client_input_data;

char const refuse_invalid_name[] = "Invalid file name.";
char const refuse_invalid_address[] = "Invalid starting file address (out of range).";
char const refuse_invalid_len[] = "Region has length 0.";

static inline char const*
file_refuse_tostr(int32 refuse_code)
{
    if (refuse_code == 1)
        return refuse_invalid_name;
    else if (refuse_code == 2)
        return refuse_invalid_address;
    else
        return refuse_invalid_len;
}

static inline void
make_temp_dir_if_not_exists()
{
    int result = mkdir("./tmp", 0777);
    if (result == -1 && errno != EEXIST)
    {
        FAILWITH_ERRNO();
    }
}

static client_input_data
parse_input(int argc, char** argv)
{
    client_input_data retval;
    if (argc < 2 || argc > 3)
        bad_usage(USAGE_MSG);

    retval.host = argv[1];
    retval.port = (argc == 3 ? argv[2] : default_port);
    return retval;
}

int
main(int argc, char** argv)
{
    client_input_data idata = parse_input(argc, argv);
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
        int16 msg_get = htons(1);
        write(sock, &msg_get, 2);
        uint8 b[1024];

        {
            int read_total_err = 0;
            CHECK(read_total_err = read_total(sock, (uint8*)b, 6));
            if (read_total_err != 0) // Not a system error.
            {
                // TODO!
                fprintf(stderr, "Could not read everything!\n");
                exit(-1);
            }
        }


        fprintf(stderr, "Buffer contains: ");
        for (int i =0; i < 6; ++i)
            fprintf(stderr, "%u ", (unsigned char)(b[i]));
        fprintf(stderr, "\n");

        int16 msg_type = unaligned_load_int16be(b);
        int32 dirnames_size = unaligned_load_int32be(b + 2);

        fprintf(stderr, "Received back: action: %d size: %d.\n", msg_type, dirnames_size);

        char* names = malloc(dirnames_size + 1);
        {
            int read_total_err = 0;
            CHECK(read_total_err = read_total(sock, (uint8*)names, dirnames_size));
            if (read_total_err != 0) // Not a system error.
            {
                // TODO!
                fprintf(stderr, "Could not read everything!\n");
                exit(-1);
            }
        }

        fprintf(stderr, "Dir contains:\n");
        char* txt = names;
        char* next = txt;
        char* end = names + dirnames_size;
        *end = '\0'; // Because we've allocated one more byte for [names].
        int32 idx = 0;

        while ((txt = next) != end)
        {
            while(*next != '|')
                ++next;
            *next++ = '\0';

            printf("%d.%.*s\n", idx++, (int32)(next - 1 - txt), txt);
        }

        uint32 number, addr_from, addr_to;
#if 1
        scanf("%u", &number);
        scanf("%u", &addr_from);
        scanf("%u", &addr_to);
#else
        number = 3;
        addr_from = 2;
        addr_to = 12;
#endif

        // TODO: Handle the case, when msg_len is less than 0.

        char const* sel_name = names;
        for (uint64 i = 0; i != number; ++i)
        {
            sel_name = strchr(sel_name, '\0');
            ++sel_name;
            if (sel_name == names + dirnames_size)
            {
                fprintf(stderr, "Out of range. Exitting.\n");
                CHECK(close(sock));
                exit(2);
            }
        }

        uint16 choosen_name_len = (uint16)strlen(sel_name);
        printf("Loaded number: %u. Addr: %d - %d. File: %s\n",
               number, addr_from, addr_to, sel_name);

        // Prepare and byteswap values to send.
        uint32 msg_addr_from = htonl(addr_from);
        uint32 msg_addr_len = htonl(addr_to - addr_from);
        uint16 msg_str_len = htons(choosen_name_len);

        exbuffer ebuf;
        size_t total_msg_size = 4 + 4 + 2 + choosen_name_len;
        exbuffer_init(&ebuf, total_msg_size);
        exbuffer_append(&ebuf, (uint8*)(&msg_addr_from), 4);
        exbuffer_append(&ebuf, (uint8*)(&msg_addr_len), 4);
        exbuffer_append(&ebuf, (uint8*)(&msg_str_len), 2);
        exbuffer_append(&ebuf, (uint8*)sel_name, choosen_name_len);

        assert(ebuf.size == total_msg_size);
        assert(ebuf.capacity == total_msg_size);
        write(sock, ebuf.data, ebuf.size); // TODO: CHECK!

        uint8 rcv_header[6];
        int read_total_err = 0;
        CHECK(read_total_err = read_total(sock, rcv_header, 6));
        if (read_total_err != 0) // Not a system error.
        {
            // TODO!
            fprintf(stderr, "Could not read everything!\n");
            exit(-1);
        }

        int16 code = unaligned_load_int16be(rcv_header);
        int32 following = unaligned_load_int32be(rcv_header + 2);

        fprintf(stderr, "Got response, code: %d, following: %d\n", code, following);

        // Refuse
        if (code == 2)
        {
            fprintf(stderr, "Server refused, reason: %s\n",
                    file_refuse_tostr(following));
        }
        else
        {
            uint8 rcv_file_buffer[following];
            fprintf(stderr, "Got %u bytes of file.\n", following);
            CHECK(read_total_err = read_total(sock, rcv_file_buffer, following));
            if (read_total_err != 0) // Not a system error.
            {
                // TODO!
                fprintf(stderr, "Could not read everything!\n");
                exit(-1);
            }

            make_temp_dir_if_not_exists();

            // TODO: array count!
            char path_combined[strlen("./tmp/") + strlen(sel_name) + 1];
            strcpy(path_combined, "./tmp/");
            strcpy(&path_combined[strlen("./tmp/")], sel_name);
            fprintf(stderr, "output path: %s\n", path_combined);

            FILE *f = fopen(path_combined , "r+");
            if (!f)
            {
                f = fopen(path_combined, "w+");
                fprintf(stderr, "File is created, because it does not exist.\n");
            }

            // TODO: Better error handling/
            if (!f)
                FAILWITH_ERRNO();

            CHECK(fseek(f, addr_from, SEEK_SET));

            fprintf(stderr, "Writing %u bytes\n", following);
            CHECK(fwrite(rcv_file_buffer, 1, following, f));
            CHECK(fclose(f));
        }

        exbuffer_free(&ebuf);
    } // endof TEST AREA.

    CHECK(close(sock)); // socket would be closed anyway when the program ends
    return 0;
}
