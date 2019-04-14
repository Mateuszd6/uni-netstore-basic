#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef float real32;
typedef double real64;

// Endian swap macros:
#define BSWAP16(VAL)                            \
    do {                                        \
        assert(sizeof(VAL) == 2);               \
        VAL = (VAL >> 8) | (VAL << 8);          \
    } while (0)

#define BSWAP32(VAL)                            \
    do {                                        \
        assert(sizeof(VAL) == 4);               \
        VAL = ((VAL >> 24) & 0xff) |            \
            ((VAL << 8) & 0xff0000) |           \
            ((VAL >> 8) & 0xff00) |             \
            ((VAL << 24) & 0xff000000);         \
    } while (0)

// Default port for both programs.
static char const* const default_port = "6543";

// Print the usage message to the console, and close the program.
void bad_usage(char const* usage_msg);

// Read exacly count bytes from the descriptor. If read will return less bytes
// than [count] it will be called again until exacly [count] bytes are
// read. [buffer] is assumed to be at least [count] bytes long.  Returns -1 on
// error, -2 when the eof is reached, but insufficient number of bytes have been
// read, or 0 on success.
int read_total(int fd, uint8* buffer, size_t count);

#define CHECK(EXPR)                                                     \
    do {                                                                \
        int reterr_ = (EXPR);                                           \
        if (reterr_ == -1)                                              \
            die_witherrno(__FILE__, __LINE__);                          \
    } while(0)

#define FAILWITH_ERRNO()                                                \
    do {                                                                \
        die_witherrno(__FILE__, __LINE__);                              \
    } while(0)

void
die_witherrno(char const* filename, int line);

#endif // COMMON_H
