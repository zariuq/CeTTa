#ifndef CETTA_GC_BUDGET_H
#define CETTA_GC_BUDGET_H

#include "atom.h"

/* A small first nursery measures survival. Later nurseries grow with the
 * measured live graph and root inventory, up to the caller's configured
 * fresh-allocation budget. Retained work is additional headroom, never a
 * live-data cap.
 * The arithmetic and scheduling laws are in Machines.SurvivorBudget. */
static inline size_t cetta_gc_nursery_budget(size_t configured,
                                             size_t retained_bytes) {
    const size_t seed = 16u * ARENA_BLOCK_SIZE;
    size_t measured = retained_bytes > seed ? retained_bytes : seed;
    return configured < measured ? configured : measured;
}

static inline size_t cetta_gc_budget_add(size_t left, size_t right) {
    return right > SIZE_MAX - left ? SIZE_MAX : left + right;
}

static inline size_t cetta_gc_budget_product(size_t count, size_t width) {
    return width != 0u && count > SIZE_MAX / width
        ? SIZE_MAX : count * width;
}

static inline size_t cetta_gc_collection_budget(size_t configured,
                                                size_t retained_bytes) {
    return cetta_gc_budget_add(retained_bytes,
        cetta_gc_nursery_budget(configured, retained_bytes));
}

#endif
