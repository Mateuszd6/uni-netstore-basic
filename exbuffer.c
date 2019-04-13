#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include "common.h"
#include "exbuffer.h"

// TODO: Return -1 and set errno when oom.
int
exbuffer_init(exbuffer* self, size_t initial_capacity)
{
    // We init 16 elements at start to avoid really small allocs.
    // TODO: MORE HERE!!!!
    if (initial_capacity < 1)
        initial_capacity = 1;

    self->data = malloc(initial_capacity); // TODO: Check malloc!
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
    self->data = realloc(self->data, self->capacity); // TODO: Check realloc!

    return 0;
}

int
exbuffer_append(exbuffer* self, uint8* data, size_t len)
{
    exbuffer_reserve(self, self->capacity + len);
    for (size_t i = 0; i != len; ++i)
        self->data[self->size++] = data[i];

    assert(self->size <= self->capacity);

    return 0;
}
