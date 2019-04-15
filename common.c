#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "err.h"

void die_witherrno(char const *filename, int line) {
    fprintf(stderr, "%s:%d: ERROR: function failed with: %d (%s).\n", filename,
            line, errno, strerror(errno));

    exit(2);
}

void bad_usage(char const *usage_msg) {
    fprintf(stderr, "Usage: %s\n", usage_msg);
    exit(0);
}

ssize_t try_rcv_total(int fd, uint8 *buffer, size_t count) {
    if (count == 0)
        return 0;

    ssize_t remained = count;
    ssize_t loaded = 0;
    while (remained > 0) {
        // if read failed, return -1, the caller can check errno.
        int bytes_red = read(fd, buffer + loaded, remained);
        if (bytes_red == -1)
            return -1;

        if (bytes_red == 0) {
            return loaded;
        }

        assert(bytes_red <= remained);
        remained -= bytes_red;
        loaded += bytes_red;
    }

    return loaded;
}

int rcv_total(int fd, uint8 *buffer, size_t count) {
    ssize_t retval = try_rcv_total(fd, buffer, count);

    if (retval != (ssize_t)count) {
        if (retval != -1) {
            // There wasn't any system error, but we counldn't read all bytes.
            // This means that the socket is destroyed and we set an errno and
            // return an error.
            errno = ESTRPIPE;
        }

        return -1;
    }
    return 0;
}

int snd_total(int fd, uint8 *buffer, size_t count) {
    for (size_t i = 0; i < count; i += SND_SINGLE_BLOCK_SIZE) {
        ssize_t chunk_len =
            (i + SND_SINGLE_BLOCK_SIZE > count ? count - i
                                               : SND_SINGLE_BLOCK_SIZE);
        ssize_t send_data = 0;
        send_data = write(fd, buffer + i, chunk_len);
        if (send_data == -1) {
            return -1;
        }

        if (send_data != chunk_len) {
            errno = ESTRPIPE;
            return -1;
        }
    }

    return 0;
}

uint16 unaligned_load_int16be(uint8 *data) {
    uint16 retval = 0;
    retval += (((uint16)(*data++)) << 8);
    retval += (((uint16)(*data++)) << 0);

    return retval;
}

uint32 unaligned_load_int32be(uint8 *data) {
    uint32 retval = 0;
    retval += (((uint32)(*data++)) << 24);
    retval += (((uint32)(*data++)) << 16);
    retval += (((uint32)(*data++)) << 8);
    retval += (((uint32)(*data++)) << 0);

    return retval;
}
