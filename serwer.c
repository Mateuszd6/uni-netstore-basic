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

// Write to the exbuffer all filenames from the directory with the given
// name. The names are splited with '|'.
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
        retval.error_code = 3; // TODO: Constant
    }
    else
    {
        FILE* reqfile_ptr = fopen(name, "r");
        if (!reqfile_ptr)
        {
            fprintf(stderr, "ERROR: File %s does not exists.\n", name);
            retval.error_code = 1; // TODO: Constant
        }
        else
        {
            size_t reqfile_size = get_file_size(reqfile_ptr);
            if (addr_from >= reqfile_size)
            {
                fprintf(stderr, "ERROR: Address is out of range.\n");
                retval.error_code = 2; // TODO: Constant
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

static int
send_filenames(int msg_sock, char const* dirname)
{
    int16 num_to_send = htons(1); // TODO: Constant
    int32 sizeof_filenames = 0; // We dont know yet how much space.
    exbuffer ebuf;
    CHECK(exbuffer_init(&ebuf, 0));
    CHECK(exbuffer_append(&ebuf, (uint8*)(&num_to_send), 2));
    CHECK(exbuffer_append(&ebuf, (uint8*)(&sizeof_filenames), 4));

    // This will write finenames to the buffer.
    get_folder_filenames(dirname, &ebuf);

    // Now we know how much space filenames really take, so we override
    // previously skipped bytes in the msg.
    sizeof_filenames = htonl(ebuf.size - 6);
    memcpy(ebuf.data + 2, (uint8*)(&sizeof_filenames), 4);

    send_total(msg_sock, ebuf.data, ebuf.size);

    fprintf(stderr, "Data was sent.\n");
    exbuffer_free(&ebuf);

    return 0;
}


typedef struct
{
    uint32 addr_from;
    uint32 addr_len;
    char* filename;
    uint16 filename_len;
} chunk_request;

// TODO: Rename to rcv
static int
load_chunk_request(int msg_sock, chunk_request* req)
{
    uint8 header_buffer[10];
    int read_total_err = 0;
    read_total_err = read_total(msg_sock, header_buffer, 10);
    if (read_total_err != 0) // Not a system error.
    {
        return -1;
    }

    req->addr_from = unaligned_load_int32be(header_buffer);
    req->addr_len = unaligned_load_int32be(header_buffer + 4);
    req->filename_len = unaligned_load_int16be(header_buffer + 8);

    fprintf(stderr, "[%u - %u], filename has %u chars\n",
            req->addr_from, req->addr_len, req->filename_len);

    req->filename = malloc(req->filename_len + 1);
    read_total_err = read_total(msg_sock, (uint8*)req->filename, req->filename_len);
    if (read_total_err != 0) // Not a system error.
    {
        free(req->filename);
        return -1;
    }

    req->filename[req->filename_len] = '\0';
    fprintf(stderr, "Filename is: %s\n", req->filename);

    return 0;
}

static int
send_requested_filechunk(int msg_sock, chunk_request* request)
{
    exbuffer ebuf;
    CHECK(exbuffer_init(&ebuf, 1));

    load_file_result load_result =
        try_load_requested_chunk(request->filename,
                                 request->addr_from,
                                 request->addr_len);

    // If there was no error, send the file to the client.
    if (load_result.error_code == 0)
    {
        int16 msg_code = htons(3); // TODO: CONSTANT
        int32 msg_filelen = htonl(load_result.size);

        CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_code), 2));
        CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_filelen), 4));

        // First header, then the message.
        write(msg_sock, ebuf.data, ebuf.size);
        send_total(msg_sock, (uint8*)load_result.content, load_result.size);
    }
    else // Otherwise send the refuse with an error code as a reason.
    {
        int16 msg_code = htons(2); // TODO: CONSTANT
        int32 msg_reason = htonl(load_result.error_code);

        CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_code), 2));
        CHECK(exbuffer_append(&ebuf, (uint8*)(&msg_reason), 4));
        write(msg_sock, ebuf.data, ebuf.size);
    }

    if (load_result.content)
        free(load_result.content);

    return 0;
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
            close(msg_sock); // TODO: error.
            continue;
        }

        int16 action_type = unaligned_load_int16be(buffer);
        fprintf(stderr, "rcved action type: %d\n", action_type);

        if (action_type == 1) // TODO: Constant
        {
            send_filenames(msg_sock, idata.dirname);

            // Now we expect the client to ask for the filechunk. If it does
            // anything else, he should be considered rouge, and connection
            // should be closed.
            // TODO: This is a copypaste.
            CHECK(read_total_err = read_total(msg_sock, buffer, 2));
            if (read_total_err != 0) // TODO: Handle this case, and handle -1!!
            {
                fprintf(stderr, "Got 0, doing nothing!\n");
                close(msg_sock); // TODO: error.
                continue;
            }

            action_type = unaligned_load_int16be(buffer);
        }

        if (action_type != 2) // TODO: Constant
        {
            fprintf(stderr, "Got unexpeted request. Ignoring.\n");
            close(msg_sock); // TODO: error.
            continue;
        }

        // TODO: Don't abort, just close the socket and continue.
        chunk_request request;
        CHECK(load_chunk_request(msg_sock, &request));
        CHECK(send_requested_filechunk(msg_sock, &request));
        free(request.filename);

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
