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
 *     by default, or the capacity it was given.
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

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    test_geometric_blocks();
    test_large_allocation();
    test_starts_small_again();
    test_without_growth();
    if (g_failures) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: arena blocks grow with what the arena holds (%d checks)\n",
           g_checks);
    return 0;
}
