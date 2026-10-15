#include "fallible_alloc.h"

#include <stdlib.h>

void *cetta_fallible_malloc(size_t size) {
    return malloc(size);
}

void *cetta_fallible_calloc(size_t count, size_t width) {
    return calloc(count, width);
}

void *cetta_fallible_realloc(void *pointer, size_t size) {
    return realloc(pointer, size);
}
