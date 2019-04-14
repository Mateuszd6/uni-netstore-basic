#ifndef EXBUFFER_H
#define EXBUFFER_H

// An implementation of the expandable buffer so that its easier to write bytes
// into a buffer when the length of the buffer is not known.

#include "common.h"

typedef struct
{
    uint8* data;
    size_t size;
    size_t capacity;
} exbuffer;

// -1 is returned when malloc failes, otherwise 0.
int exbuffer_init(exbuffer* self, size_t initial_capacity);

void exbuffer_free(exbuffer* self);

// -1 is returned when malloc/realloc failes, otherwise 0.
int exbuffer_reserve(exbuffer* self, size_t min_capacity_after);

// -1 is returned when malloc/realloc failes, otherwise 0.
int exbuffer_append(exbuffer* self, uint8* data, size_t len);

#endif // EXBUFFER_H
