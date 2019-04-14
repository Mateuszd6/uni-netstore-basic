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

typedef struct
{
    char const* dirname;
    char const* port;
} server_input_data;

static server_input_data
parse_input(int argc, char** argv)
{
    server_input_data retval;
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

typedef struct
{
    char* content;
    size_t size;
    int error_code;
} load_file_result;

// If error_code of the returned structure is 0, then content and size contains
// chunk of file that has to be sent to the client, otherwise the error code
// should be sent in the refuse message.
static load_file_result
try_load_requested_chunk(char const* name, size_t addr_from, size_t addr_len)
{
    load_file_result retval;
    retval.content = 0;
    retval.size = 0;
    retval.error_code = 0;

    if (addr_len == 0)
    {
        fprintf(stderr, "ERROR: Given length is 0.\n");
        retval.error_code = 3;
    }
    else
    {
        FILE* reqfile_ptr = fopen(name, "r");
        if (!reqfile_ptr)
        {
            fprintf(stderr, "ERROR: File %s does not exists.\n", name);
            retval.error_code = 1;
        }
        else
        {
            size_t reqfile_size = get_file_size(reqfile_ptr);
            if (addr_from >= reqfile_size)
            {
                fprintf(stderr, "ERROR: Address is out of range.\n");
                retval.error_code = 2;
            }
            else
            {
                CHECK(fseek(reqfile_ptr, addr_from, SEEK_SET));
                retval.content = malloc(addr_len + 1);
                retval.size = fread(retval.content, 1, addr_len, reqfile_ptr);
                retval.content[retval.size] = 0;
            }

            fclose(reqfile_ptr);
        }
    }

    return retval;
}

int
main(int argc, char** argv)
{
    server_input_data idata = parse_input(argc, argv);
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

        load_file_result load_result =
            try_load_requested_chunk((char const*)name_buffer,
                                     addr_from,
                                     addr_len);

        // If there was no error, send the file to the client.
        if (load_result.error_code == 0)
        {
            int16 msg_code = htons(3); // TODO: CONSTANT
            int32 msg_filelen = htonl(load_result.size);

            CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_code), 2));
            CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_filelen), 4));
            write(msg_sock, ebuf.data, ebuf.size);

            size_t send_len = load_result.size;
            size_t block_size = 512;
            for (size_t i = 0; i < send_len; i += block_size)
            {
                size_t chunk_len = (i + block_size > send_len
                                    ? send_len - i
                                    : block_size);

                size_t send_data = 0;
                CHECK(send_data = write(msg_sock, load_result.content + i, chunk_len));
                // TODO: Check if all bytes were sent.
            }
        }
        else // Otherwise send the refuse with an error code as a reason.
        {
            int16 msg_code = htons(2);
            int32 msg_reason = htonl(load_result.error_code);

            CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_code), 2));
            CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_reason), 4));
            write(msg_sock, ebuf.data, ebuf.size);
        }

        if (load_result.content)
            free(load_result.content);

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
