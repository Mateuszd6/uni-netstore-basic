#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#include "common.h"

void
bad_usage(char const* usage_msg)
{
    fprintf(stderr, "Usage: %s\n", usage_msg);
    exit(0);
}

int
read_bytes(int fd, uint8* buffer, size_t count)
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
            // TODO: What now?
            fprintf(stderr, "Counldn't read exacly %ld bytes!\n", count);
        }

        assert((size_t)bytes_red <= remained);
        remained -= bytes_red;
        loaded += bytes_red;
    }

    return 0;
}
