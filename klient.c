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
        // If makedir returned other error than one indicating that dir exists,
        // we can't do much so we exit with an error.
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

// This will exit if user-inserted values are invalid.
static inline void
sanitize_selected_file_input(int32 filenum,
                             int32 addr_from,
                             int32 addr_to,
                             int32 total_files)
{
    if (filenum < 0 || filenum >= total_files)
    {
        fprintf(stderr, "ERROR: File number out of range.\n");
        exit(1);
    }
    else if (addr_from < 0 || addr_to < 0)
    {
        fprintf(stderr, "ERROR: Invalid addres. Can't be negative.\n");
        exit(1);
    }
    else if (addr_to < addr_from)
    {
        fprintf(stderr, "ERROR: Invalid addres. Last is less that First.\n");
        exit(1);
    }
}

typedef struct
{
    char* filenames;
    size_t num_files;
} filelist_request;

static void
rcv_filelist(int msg_sock, filelist_request* req)
{
    int16 msg_get = htons(1); // TODO: Constant
    write(msg_sock, &msg_get, 2);

    uint8 header_buf[6];
    CHECK(read_total(msg_sock, (uint8*)header_buf, 6));

    int16 msg_type = unaligned_load_int16be(header_buf);
    int32 dirnames_size = unaligned_load_int32be(header_buf + 2);

    fprintf(stderr, "Received back: action: %d size: %d.\n", msg_type, dirnames_size);

    char* names = malloc(dirnames_size + 1);
    CHECK(read_total(msg_sock, (uint8*)names, dirnames_size));

    fprintf(stderr, "Dir contains:\n");
    char* curr = names;
    char* next = names;
    char* end = names + dirnames_size;
    int32 idx = 0;

    // Because we've allocated one more byte for [names].
    *end = '\0';

    // Now we replace all '|' with zeros, so filenames are null separated.
    while ((curr = next) != end)
    {
        while(*next != '|')
            ++next;
        *next++ = '\0';
        idx++;
    }

    req->filenames = names;
    req->num_files = idx;
}

static void
send_file_request(int msg_sock, uint32 addr_from, uint32 addr_to,
                  char const* selected_name)
{
    uint16 choosen_name_len = (uint16)strlen(selected_name);
    printf("Selected file: %s Addr: %d - %d. \n",
           selected_name, addr_from, addr_to);

    size_t total_msg_size = 2 + 4 + 4 + 2 + choosen_name_len;

    // Prepare and byteswap values to send.
    uint32 msg_request_num = htons(2); // TODO: Constant!
    uint32 msg_addr_from = htonl(addr_from);
    uint32 msg_addr_len = htonl(addr_to - addr_from);
    uint16 msg_str_len = htons(choosen_name_len);

    exbuffer ebuf;
    CHECK(exbuffer_init(&ebuf, total_msg_size));
    CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_request_num), 2));
    CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_addr_from), 4));
    CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_addr_len), 4));
    CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_str_len), 2));
    CHECK(exbuffer_append(&ebuf, (uint8*)selected_name, choosen_name_len));

    assert(ebuf.size == total_msg_size);
    assert(ebuf.capacity == total_msg_size);
    CHECK(write(msg_sock, ebuf.data, ebuf.size));
    exbuffer_free(&ebuf);
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

    int msg_sock;
    CHECK(msg_sock = socket(addr_result->ai_family,
                            addr_result->ai_socktype,
                            addr_result->ai_protocol));

    // connect socket to the server
    CHECK(connect(msg_sock, addr_result->ai_addr, addr_result->ai_addrlen));

    freeaddrinfo(addr_result);

    filelist_request filelist;
    rcv_filelist(msg_sock, &filelist);

    char const* curname = filelist.filenames;
    for (size_t i = 0; i < filelist.num_files; ++i)
    {
        printf("%lu. %s\n", i, curname);
        curname = strchr(curname, '\0');
        assert(curname); // We know how many zero is there, so we must succeed.
        curname++;
    }

    int32 number, addr_from, addr_to;
    printf("Select a file: ");
    scanf("%d", &number);
    printf("Addr from: ");
    scanf("%d", &addr_from);
    printf("Addr to (exclusive): ");
    scanf("%d", &addr_to);

    // If this won't exit program, inserted values are valid.
    sanitize_selected_file_input(number, addr_from, addr_to, filelist.num_files);

    char const* nameptr = filelist.filenames;
    for (int32 i = 0; i != number; ++i)
    {
        nameptr = strchr(nameptr, '\0');
        assert(nameptr);
        ++nameptr;
    }

    char const* selected_name = strdup(nameptr);
    free(filelist.filenames);

    send_file_request(msg_sock, addr_from, addr_to, selected_name);

    uint8 rcv_header[6];
    int read_total_err = 0;
    CHECK(read_total_err = read_total(msg_sock, rcv_header, 6));
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
        CHECK(read_total_err = read_total(msg_sock, rcv_file_buffer, following));
        if (read_total_err != 0) // Not a system error.
        {
            // TODO!
            fprintf(stderr, "Could not read everything!\n");
            exit(-1);
        }

        make_temp_dir_if_not_exists();

        // TODO: array count!
        char path_combined[strlen("./tmp/") + strlen(selected_name) + 1];
        strcpy(path_combined, "./tmp/");
        strcpy(&path_combined[strlen("./tmp/")], selected_name);
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

    CHECK(close(msg_sock)); // socket would be closed anyway when the program ends
    return 0;
}
