/* alloc_count.so - an LD_PRELOAD counter of heap allocations, for
 * tests/mm0/bench.py.  It counts malloc, calloc and realloc calls with the
 * bytes they request, and free calls, and writes the three numbers to the
 * file named by $MM0_ALLOC_COUNT_FILE when the process exits.  glibc only
 * (it forwards to __libc_malloc and friends).
 *
 *   cc -O2 -shared -fPIC tests/mm0/alloc_count.c -o alloc_count.so */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

extern void *__libc_malloc(size_t n);
extern void *__libc_calloc(size_t count, size_t n);
extern void *__libc_realloc(void *p, size_t n);
extern void __libc_free(void *p);

static unsigned long long g_calls, g_bytes, g_frees;

void *malloc(size_t n) {
    ++g_calls;
    g_bytes += n;
    return __libc_malloc(n);
}

void *calloc(size_t count, size_t n) {
    ++g_calls;
    g_bytes += count * n;
    return __libc_calloc(count, n);
}

void *realloc(void *p, size_t n) {
    ++g_calls;
    g_bytes += n;
    return __libc_realloc(p, n);
}

void free(void *p) {
    if (p) ++g_frees;
    __libc_free(p);
}

__attribute__((destructor)) static void report(void) {
    unsigned long long calls = g_calls, bytes = g_bytes, frees = g_frees;
    const char *path = getenv("MM0_ALLOC_COUNT_FILE");
    FILE *f = path ? fopen(path, "w") : NULL;
    if (!f) return;
    fprintf(f, "%llu %llu %llu\n", calls, bytes, frees);
    fclose(f);
}
