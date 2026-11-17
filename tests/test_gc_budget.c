#include "gc_budget.h"

#include <assert.h>
#include <stdio.h>

/* Wide arithmetic is an independent oracle for the size-word boundary. */
static size_t bounded(__uint128_t value) {
    return value > SIZE_MAX ? SIZE_MAX : (size_t)value;
}

int main(void) {
    const size_t seed = 16u * ARENA_BLOCK_SIZE;
    const size_t values[] = {
        0u, 1u, ARENA_BLOCK_SIZE - 1u, ARENA_BLOCK_SIZE,
        seed - 1u, seed, seed + 1u, 16u * seed, 64u * seed,
        SIZE_MAX / 2u, SIZE_MAX / 2u + 1u, SIZE_MAX - 1u, SIZE_MAX,
    };
    size_t checks = 0u;
    for (size_t i = 0u; i < sizeof(values) / sizeof(*values); i++) {
        size_t configured = values[i];
        for (size_t j = 0u; j < sizeof(values) / sizeof(*values); j++) {
            size_t live = values[j];
            size_t nursery = cetta_gc_nursery_budget(configured, live);
            assert(nursery <= configured);
            assert(live <= cetta_gc_collection_budget(configured, live));
            if (live < seed) {
                assert(nursery == (configured < seed ? configured : seed));
            } else if (live < configured) {
                assert(nursery == live);
            } else {
                assert(nursery == configured);
            }
            assert(cetta_gc_collection_budget(configured, live) ==
                bounded((__uint128_t)live + nursery));
            for (size_t k = 0u; k < sizeof(values) / sizeof(*values); k++) {
                size_t burst = values[k];
                assert(cetta_gc_budget_add(live, burst) ==
                    bounded((__uint128_t)live + burst));
                assert(cetta_gc_budget_product(live, burst) ==
                    bounded((__uint128_t)live * burst));
                checks++;
            }
        }
    }
    assert(cetta_gc_nursery_budget(256u * seed, 0u) == seed);
    assert(cetta_gc_nursery_budget(256u * seed, 128u * seed) == 128u * seed);
    assert(cetta_gc_collection_budget(seed, 4u * seed) == 5u * seed);
    assert(cetta_gc_budget_product(SIZE_MAX, 0u) == 0u);
    printf("PASS: %zu nursery and saturation boundary checks\n", checks);
    return 0;
}
