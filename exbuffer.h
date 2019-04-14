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

// TODO: Return -1 and set errno when oom.
int exbuffer_init(exbuffer* self, size_t initial_capacity);

void exbuffer_free(exbuffer* self);

int exbuffer_reserve(exbuffer* self, size_t min_capacity_after);

int exbuffer_append(exbuffer* self, uint8* data, size_t len);

#endif // EXBUFFER_H
