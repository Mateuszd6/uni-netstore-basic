#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

#include "common.h"
#include "exbuffer.h"

// TODO: MORE HERE!!!!
#define EXBUFFER_MIN_INITAIL_CAPACITY (1)

int
exbuffer_init(exbuffer* self, size_t initial_capacity)
{
    // We init at least EXBUFFER_MIN_INITAIL_CAPACITY elements at start to avoid
    // really small allocs.
    if (initial_capacity < EXBUFFER_MIN_INITAIL_CAPACITY)
        initial_capacity = EXBUFFER_MIN_INITAIL_CAPACITY;

    self->data = malloc(initial_capacity);
    if (!self->data)
    {
        errno = ENOMEM;
        return -1;
    }

    self->size = 0;
    self->capacity = initial_capacity;

    return 0;
}

void
exbuffer_free(exbuffer* self)
{
    free(self->data);
}

int
exbuffer_reserve(exbuffer* self, size_t min_capacity_after)
{
    while (self->capacity < min_capacity_after)
        self->capacity *= 2;

    assert(self->capacity >= min_capacity_after);
    uint8* new_data = realloc(self->data, self->capacity);
    if (!new_data)
    {
        errno = ENOMEM;
        return -1;
    }
    self->data = new_data;

    return 0;
}

int
exbuffer_append(exbuffer* self, uint8* data, size_t len)
{
    if (exbuffer_reserve(self, self->size + len) == -1)
    {
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i != len; ++i)
        self->data[self->size++] = data[i];

    assert(self->size <= self->capacity);

    return 0;
}
