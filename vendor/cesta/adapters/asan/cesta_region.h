#ifndef CESTA_ASAN_REGION_H
#define CESTA_ASAN_REGION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#ifndef CESTA_REGION_DEBUG
#if defined(__SANITIZE_ADDRESS__)
#define CESTA_REGION_DEBUG 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CESTA_REGION_DEBUG 1
#endif
#endif
#endif
#ifndef CESTA_REGION_DEBUG
#define CESTA_REGION_DEBUG 0
#endif

/* Debug notifications for an allocator's real payload and live prefix.
 * The allocator owns the state; this adapter keeps no allocation ledger.
 * Metadata must lie outside the payload. Callers serialize overlapping
 * shadow changes and notify acquisition before writing newly live bytes.
 * Intervals must cover complete ASan shadow granules. An unrepresentable
 * interval refuses before touching shadow memory or neighboring live data.
 */
#if CESTA_REGION_DEBUG
#include <sanitizer/asan_interface.h>

static inline bool cesta_region_debug_range(void *base, size_t capacity,
                                            size_t begin, size_t end) {
    if (begin > end || end > capacity || (!base && capacity)) return false;
    uintptr_t address = (uintptr_t)base;
    if (capacity > UINTPTR_MAX-address) return false;
    if (!capacity) return true;
    size_t scale, offset;
    __asan_get_shadow_mapping(&scale,&offset);
    if (scale >= sizeof(uintptr_t)*CHAR_BIT) return false;
    uintptr_t mask = ((uintptr_t)1 << scale)-1;
    return !((address | capacity | begin | end) & mask);
}

static inline bool cesta_region_debug_initialize(void *base, size_t capacity, size_t live) {
    if (!cesta_region_debug_range(base,capacity,0,live) ||
        !cesta_region_debug_range(base,capacity,live,capacity)) return false;
    if (live) __asan_unpoison_memory_region(base,live);
    if (capacity > live) __asan_poison_memory_region((unsigned char *)base+live,capacity-live);
    return true;
}

static inline bool cesta_region_debug_acquire(void *base, size_t capacity,
                                             size_t old_live, size_t new_live) {
    if (new_live < old_live || !cesta_region_debug_range(base,capacity,old_live,new_live)) return false;
    if (new_live > old_live) __asan_unpoison_memory_region((unsigned char *)base+old_live,new_live-old_live);
    return true;
}

static inline bool cesta_region_debug_reset(void *base, size_t capacity,
                                           size_t old_live, size_t new_live) {
    if (new_live > old_live || !cesta_region_debug_range(base,capacity,new_live,old_live)) return false;
    if (old_live > new_live) __asan_poison_memory_region((unsigned char *)base+new_live,old_live-new_live);
    return true;
}

static inline bool cesta_region_debug_release(void *base, size_t capacity) {
    if (!cesta_region_debug_range(base,capacity,0,capacity)) return false;
    if (capacity) __asan_poison_memory_region(base,capacity);
    return true;
}

#define CESTA_REGION_INITIALIZE(BASE, CAPACITY, LIVE) cesta_region_debug_initialize((BASE),(CAPACITY),(LIVE))
#define CESTA_REGION_ACQUIRE(BASE, CAPACITY, OLD, NEW) cesta_region_debug_acquire((BASE),(CAPACITY),(OLD),(NEW))
#define CESTA_REGION_RESET(BASE, CAPACITY, OLD, NEW) cesta_region_debug_reset((BASE),(CAPACITY),(OLD),(NEW))
#define CESTA_REGION_RELEASE(BASE, CAPACITY) cesta_region_debug_release((BASE),(CAPACITY))
#else
/* Disabled instrumentation evaluates none of its arguments. */
#define CESTA_REGION_INITIALIZE(BASE, CAPACITY, LIVE) (true)
#define CESTA_REGION_ACQUIRE(BASE, CAPACITY, OLD, NEW) (true)
#define CESTA_REGION_RESET(BASE, CAPACITY, OLD, NEW) (true)
#define CESTA_REGION_RELEASE(BASE, CAPACITY) (true)
#endif

#endif
