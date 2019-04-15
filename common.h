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

// Protocol constants:
#define PROT_REQ_FILELIST (1)
#define PROT_REQ_FILECHUNK (2)

#define PROT_RESP_FILELIST (1)
#define PROT_RESP_FILECHUNK_ERROR (2)
#define PROT_RESP_FILECHUNK_OK (3)

#define FREQ_ERROR_ON_SUCH_FILE (1)
#define FREQ_ERROR_OUT_OF_RANGE (2)
#define FREQ_ERROR_ZERO_LEN (3)

// Default port for both programs.
static char const* const default_port = "6543";

// Split message into small buffers, not greater that 512 bytes long, and send
// it to the sock.
#define SND_SINGLE_BLOCK_SIZE (512)

// If check expression evaluates to negative number, kill the program. EXPR is
// assumed to set an errno in that case.
#define CHECK(EXPR)                                                     \
    do {                                                                \
        int reterr_ = (EXPR);                                           \
        if (reterr_ < 0)                                                \
            die_witherrno(__FILE__, __LINE__);                          \
    } while(0)

// Kill the program displaying errno to the user.
#define FAILWITH_ERRNO()                                                \
    do {                                                                \
        die_witherrno(__FILE__, __LINE__);                              \
    } while(0)

// Kill the program printing the errno code and lie file + linum when the error
// happend.
void die_witherrno(char const* filename, int line);

// Print the usage message to the console, and close the program.
void bad_usage(char const* usage_msg);

// Read exacly count bytes from the descriptor. If read will return less bytes
// than [count] it will be called again until exacly [count] bytes are
// read. [buffer] is assumed to be at least [count] bytes long. Returns the
// number of loaded bytes or -1 if there was a system errror.
ssize_t try_rcv_total(int fd, uint8* buffer, size_t count);

// Same as try_rcv_total, but treats reading insufficient number of bytes as an
// error (This means that the socket was closed unexpetedly). In that case sets
// an errno to indicate that this happend. Returns 0 on sucess or -1 on error.
int rcv_total(int fd, uint8* buffer, size_t count);

// Sends [count] bytes from the [buffer] to the given socket. Bytes are chopped
// into chunks of size [SND_SINGLE_BLOCK_SIZE] and then sent. Returns 0 on
// sucess or -1 on failure (either if io failure or if socket was closed
// unexpetedly). In both cases errno is set.
int snd_total(int fd, uint8* buffer, size_t count);

// Im not entierly sure if they are needed, but I'm using them for
// safetly. These are used to convert a string of bytes with random aligment to
// the integers.
uint16 unaligned_load_int16be(uint8* data);
uint32 unaligned_load_int32be(uint8* data);

#endif // COMMON_H
