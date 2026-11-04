#ifndef CETTA_ALLOCATOR_INFO_H
#define CETTA_ALLOCATOR_INFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 33)
#define CETTA_ALLOCATOR_HAS_MALLINFO2 1
#include <malloc.h>
#endif
#endif

typedef struct {
    bool available;
    size_t in_use, free, mapped;
} CettaAllocatorInfo;

/* Optional allocator observations never determine language behavior. */
static inline CettaAllocatorInfo cetta_allocator_info(void) {
#if defined(CETTA_ALLOCATOR_HAS_MALLINFO2)
    struct mallinfo2 info = mallinfo2();
    return (CettaAllocatorInfo){true, info.uordblks, info.fordblks, info.hblkhd};
#else
    return (CettaAllocatorInfo){0};
#endif
}

#endif
