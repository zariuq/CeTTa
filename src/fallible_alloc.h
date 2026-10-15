#ifndef CETTA_FALLIBLE_ALLOC_H
#define CETTA_FALLIBLE_ALLOC_H

#include <stddef.h>

/* Allocations whose callers recover from failure: a control structure that
 * reports capacity to its owner instead of ending the process.  They live in
 * their own unit, out of line, so that a test can fail exactly these
 * requests and no other allocation, whatever form a compiler gives the rest
 * (a malloc followed by a memset of zero may become a calloc). */
void *cetta_fallible_malloc(size_t size);
void *cetta_fallible_calloc(size_t count, size_t width);
void *cetta_fallible_realloc(void *pointer, size_t size);

#endif
