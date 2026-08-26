#include "atom.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Minimal fail-fast allocator compatibility for standalone native build tools.
 * Runtime CeTTa obtains these symbols from atom.c; linking this object instead
 * keeps bootstrap tools independent of the evaluator object graph.
 */
static void cetta_standalone_oom_v1(size_t size) {
    fprintf(stderr, "fatal: out of memory allocating %zu bytes\n", size);
    abort();
}

void *cetta_malloc(size_t size) {
    void *ptr = malloc(size == 0u ? 1u : size);
    if (!ptr)
        cetta_standalone_oom_v1(size);
    return ptr;
}

void *cetta_realloc(void *ptr, size_t size) {
    void *out = realloc(ptr, size == 0u ? 1u : size);
    if (!out)
        cetta_standalone_oom_v1(size);
    return out;
}
