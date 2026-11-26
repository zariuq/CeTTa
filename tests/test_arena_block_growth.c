/*
 * An arena whose blocks grow with what it holds.
 *
 * Properties under test:
 *   - the first block has the requested size, and each later block claimed
 *     from the allocator is as large as all the blocks the arena holds, up
 *     to ARENA_BLOCK_SIZE, so what it holds stays within a constant factor
 *     of what it allocated;
 *   - an allocation larger than the current size gets a block of its own
 *     size;
 *   - an arena that gives back every block starts small again;
 *   - without growth an arena keeps its fixed block size: full-size blocks
 *     by default, or the capacity it was given;
 *   - growth to powers of two: each later block brings what the arena
 *     holds to the least power of two that leaves the block at least the
 *     least size, up to ARENA_BLOCK_SIZE, whatever the arena held first --
 *     a lent block of any size among it -- while growth by doubling after
 *     the same lent block holds amounts that are no powers of two; an
 *     allocation larger than that still gets a block of its own size, and
 *     the block after it brings the arena back to a power of two.
 */
#include "atom.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>

static int g_failures;
static int g_checks;

static void check(bool condition, const char *what) {
    g_checks++;
    if (!condition) {
        g_failures++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

/* Block capacities from the oldest block to the newest. */
static size_t block_capacities(const Arena *a, size_t *out, size_t cap) {
    size_t len = 0u;
    for (const ArenaBlock *block = a->head; block; block = block->next)
        len++;
    size_t index = len;
    for (const ArenaBlock *block = a->head; block; block = block->next) {
        index--;
        if (index < cap)
            out[index] = block->capacity;
    }
    return len;
}

static void test_geometric_blocks(void) {
    Arena a;
    arena_init(&a);
    arena_set_block_growth(&a, 256u);
    (void)arena_alloc(&a, 16u);
    check(a.reserved_bytes == 256u, "the first block has the first size");
    size_t allocated = 16u;
    while (a.reserved_bytes < 4u * ARENA_BLOCK_SIZE) {
        (void)arena_alloc(&a, 64u);
        allocated += 64u;
    }
    size_t caps[64];
    size_t len = block_capacities(&a, caps, 64u);
    check(len < 64u && caps[0] == 256u, "the oldest block is the first one");
    size_t held = 0u;
    bool geometric = true;
    for (size_t index = 0u; index < len && index < 64u; index++) {
        size_t expected = index == 0u ? 256u
            : held < ARENA_BLOCK_SIZE ? (held > 256u ? held : 256u)
                                      : ARENA_BLOCK_SIZE;
        geometric = geometric && caps[index] == expected;
        held += caps[index];
    }
    check(geometric,
          "each later block is as large as the blocks before it, up to "
          "full size");
    check(held == a.reserved_bytes, "the blocks are all the arena holds");
    /* Each block is claimed only when the one before it is full but for less
     * than one allocation, and is at most as large as all before it. */
    check(a.reserved_bytes <= 2u * allocated + 256u + 64u * len,
          "the arena holds at most about twice what it allocated");
    arena_free(&a);
}

static void test_large_allocation(void) {
    Arena a;
    arena_init(&a);
    arena_set_block_growth(&a, 256u);
    (void)arena_alloc(&a, 10000u);
    check(a.reserved_bytes == 10000u,
          "an allocation larger than the next block gets a block of its size");
    (void)arena_alloc(&a, 8u);
    check(a.reserved_bytes == 20000u,
          "the block after it is as large as what the arena holds");
    arena_free(&a);
}

static void test_starts_small_again(void) {
    Arena a;
    arena_init(&a);
    arena_set_block_growth(&a, 256u);
    ArenaMark empty = arena_mark(&a);
    for (int round = 0; round < 3; round++) {
        for (int index = 0; index < 4000; index++)
            (void)arena_alloc(&a, 64u);
        check(a.reserved_bytes >= ARENA_BLOCK_SIZE,
              "a growing arena reaches full-size blocks");
        arena_reset(&a, empty);
        arena_release_spare(&a, 0u);
        check(a.reserved_bytes == 0u && a.block_count == 0u,
              "an arena that gives back every block holds nothing");
        (void)arena_alloc(&a, 16u);
        check(a.reserved_bytes == 256u,
              "its next block has the first size again");
        arena_reset(&a, empty);
    }
    arena_free(&a);
}

static void test_without_growth(void) {
    Arena full;
    arena_init(&full);
    (void)arena_alloc(&full, 16u);
    check(full.reserved_bytes == ARENA_BLOCK_SIZE,
          "by default a small allocation takes a full-size block");
    arena_free(&full);

    Arena fixed;
    arena_init(&fixed);
    arena_set_block_capacity(&fixed, 1024u);
    for (int index = 0; index < 1000; index++)
        (void)arena_alloc(&fixed, 64u);
    size_t caps[128];
    size_t len = block_capacities(&fixed, caps, 128u);
    bool all_fixed = len > 1u && len <= 128u;
    for (size_t index = 0u; all_fixed && index < len; index++)
        all_fixed = caps[index] == 1024u;
    check(all_fixed, "a fixed capacity keeps every block at that size");
    arena_free(&fixed);

    Arena off;
    arena_init(&off);
    arena_set_block_growth(&off, ARENA_BLOCK_SIZE);
    (void)arena_alloc(&off, 16u);
    check(off.reserved_bytes == ARENA_BLOCK_SIZE && !off.block_growth,
          "growth from a full-size first block is no growth");
    arena_free(&off);
}

/* Whether X is a power of two. */
static bool is_pow2(size_t x) {
    return x != 0u && (x & (x - 1u)) == 0u;
}

/* Allocate 64 bytes at a time until A holds at least LIMIT, and whether
   every block claimed after the first BLOCKS brought A to a power of two,
   no block below LEAST nor above full size. */
static bool grows_to_powers(Arena *a, size_t blocks, size_t least,
                            size_t limit) {
    bool powers = true;
    size_t seen = a->block_count;
    while (a->reserved_bytes < limit) {
        (void)arena_alloc(a, 64u);
        if (a->block_count != seen) {
            seen = a->block_count;
            if (seen > blocks)
                powers = powers && is_pow2(a->reserved_bytes) &&
                         a->head->capacity >= least &&
                         a->head->capacity <= ARENA_BLOCK_SIZE;
        }
    }
    return powers;
}

static void test_pow2_blocks(void) {
    Arena a;
    arena_init(&a);
    arena_set_block_growth_pow2(&a, 256u);
    (void)arena_alloc(&a, 16u);
    check(a.reserved_bytes == 256u && a.block_growth == ARENA_GROWTH_POW2,
          "growth to powers of two takes a first block of the least size");
    check(grows_to_powers(&a, 1u, 256u, ARENA_BLOCK_SIZE),
          "each later block brings the arena to a power of two");
    arena_free(&a);

    /* After a lent block of 704 bytes the blocks are 320, 1024, 2048, ...:
       the arena holds 1024, 2048, 4096, ... as blocks of powers of two
       would. */
    void *lent = malloc(728u);
    Arena b;
    arena_init(&b);
    arena_set_block_growth_pow2(&b, 256u);
    arena_lend_block(&b, lent, 728u);
    check(b.reserved_bytes == 704u, "the lent block holds 704 bytes");
    size_t caps[64];
    bool powers = grows_to_powers(&b, 1u, 256u, 8192u);
    size_t len = block_capacities(&b, caps, 64u);
    check(powers && len == 5u && caps[1] == 320u && caps[2] == 1024u &&
              caps[3] == 2048u && caps[4] == 4096u &&
              b.reserved_bytes == 8192u,
          "after a lent block the blocks bring the arena to powers of two");
    arena_free_lent(&b, lent);

    /* Doubling after the same lent block holds 1432, 2864, ...: no power
       of two. */
    Arena c;
    arena_init(&c);
    arena_set_block_growth(&c, 728u);
    arena_lend_block(&c, lent, 728u);
    bool any_power = false;
    size_t seen = c.block_count;
    while (c.reserved_bytes < 8192u) {
        (void)arena_alloc(&c, 64u);
        if (c.block_count != seen) {
            seen = c.block_count;
            any_power = any_power || is_pow2(c.reserved_bytes);
        }
    }
    check(!any_power,
          "doubling after a lent block of 704 bytes holds no power of two");
    arena_free_lent(&c, lent);
    free(lent);
}

static void test_pow2_least_and_large(void) {
    /* Just below a power of two, the next block still has the least
       size: 1000 bytes lent, then a block of 1048 to 2048, not of 24. */
    void *lent = malloc(1024u);
    Arena a;
    arena_init(&a);
    arena_set_block_growth_pow2(&a, 256u);
    arena_lend_block(&a, lent, 1024u);
    check(a.reserved_bytes == 1000u, "the lent block holds 1000 bytes");
    while (a.block_count == 1u)
        (void)arena_alloc(&a, 64u);
    check(a.head->capacity == 1048u && a.reserved_bytes == 2048u,
          "a block below the least size goes on to the next power of two");
    arena_free_lent(&a, lent);
    free(lent);

    Arena b;
    arena_init(&b);
    arena_set_block_growth_pow2(&b, 256u);
    (void)arena_alloc(&b, 10000u);
    check(b.reserved_bytes == 10000u,
          "an allocation larger than the next block gets a block of its size");
    (void)arena_alloc(&b, 8u);
    check(b.reserved_bytes == 16384u && b.head->capacity == 6384u,
          "the block after it brings the arena back to a power of two");
    while (b.reserved_bytes < 4u * ARENA_BLOCK_SIZE)
        (void)arena_alloc(&b, 64u);
    check(b.head->capacity == ARENA_BLOCK_SIZE &&
              b.reserved_bytes == 4u * ARENA_BLOCK_SIZE,
          "blocks stop at full size");
    arena_free(&b);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    test_geometric_blocks();
    test_large_allocation();
    test_starts_small_again();
    test_without_growth();
    test_pow2_blocks();
    test_pow2_least_and_large();
    if (g_failures) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: arena blocks grow with what the arena holds (%d checks)\n",
           g_checks);
    return 0;
}
