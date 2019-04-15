#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "err.h"

// Split message into small buffers, not greater that 512 bytes long, and send
// it to the sock. TODO: MAKE IT BIGGER DADDY!!
#define BLOCK_SIZE (5)

void
bad_usage(char const* usage_msg)
{
    fprintf(stderr, "Usage: %s\n", usage_msg);
    exit(0);
}

int
read_total(int fd, uint8* buffer, size_t count)
{
    if (count == 0)
        return 0;

    size_t remained = count;
    size_t loaded = 0;
    while (remained > 0)
    {
        // if read failed, return -1, the caller can check errno.
        int bytes_red = read(fd, buffer + loaded, remained);
        if (bytes_red == -1)
            return -1;

        if (bytes_red == 0)
        {
            fprintf(stderr, "Counldn't read exacly %ld bytes!\n", count);
            errno = ESTRPIPE;
            return -1;
        }

        assert((size_t)bytes_red <= remained);
        fprintf(stderr, "-> read_total: Got %d bytes: '%.*s'\n",
                bytes_red, bytes_red, buffer + loaded);
        remained -= bytes_red;
        loaded += bytes_red;
    }

    return 0;
}

int
send_total(int fd, uint8* buffer, size_t count)
{
    for (size_t i = 0; i < count; i += BLOCK_SIZE)
    {
        ssize_t chunk_len = (i + BLOCK_SIZE > count ? count - i : BLOCK_SIZE);
        ssize_t send_data = 0;
        send_data = write(fd, buffer + i, chunk_len);
        if (send_data == -1)
        {
            return -1;
        }

        if (send_data != chunk_len)
        {
            fprintf(stderr, "Counldn't send exacly %ld bytes!\n", count);
            errno = ESTRPIPE;
            return -1;
        }
    }

    return 0;
}

void
die_witherrno(char const* filename, int line)
{
    fprintf(stderr, "%s:%d: ERROR: function failed with: %d (%s).\n",
           filename, line, errno, strerror(errno));

    exit(2);
}

uint16
unaligned_load_int16be(uint8* data)
{
    uint16 retval = 0;
    retval += (((uint16)(*data++)) << 8);
    retval += (((uint16)(*data++)) << 0);

    return retval;
}

uint32
unaligned_load_int32be(uint8* data)
{
    uint32 retval = 0;
    retval += (((uint32)(*data++)) << 24);
    retval += (((uint32)(*data++)) << 16);
    retval += (((uint32)(*data++)) << 8);
    retval += (((uint32)(*data++)) << 0);

    return retval;
}
