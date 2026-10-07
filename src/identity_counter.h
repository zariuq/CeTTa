#ifndef CETTA_IDENTITY_COUNTER_H
#define CETTA_IDENTITY_COUNTER_H

#include <stdatomic.h>
#include <stdint.h>

/* UINT64_MAX is a permanent exhausted state. Zero is never issued and a
 * namespace never wraps, including when several workers exhaust it together.
 * Refusal leaves the allocator unchanged; callers retain the blocked work. */
static inline uint64_t cetta_identity_try_take(_Atomic uint64_t *next) {
    uint64_t current = atomic_load_explicit(next, memory_order_relaxed);
    while (current != 0u && current != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(next, &current, current + 1u,
                memory_order_relaxed, memory_order_relaxed))
            return current;
    }
    return 0u;
}

#endif
