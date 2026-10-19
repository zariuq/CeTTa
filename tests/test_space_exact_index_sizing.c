/* The exact-membership index is sized to the rows it files.  It is built by
 * its first reader at EXACT_INDEX_MIN_SLOTS (16) slots, doubles when a row
 * would take it past seven tenths full, and halves when removals leave it
 * an eighth full or less, so a row change at either boundary never resizes
 * it back and forth.  A replacement table is built whole before it is
 * published: when it cannot be allocated the current table keeps working,
 * and when no table can hold the rows the index gives up and readers answer
 * from the rows themselves until a table can be built again.  Answers are
 * the rows in ascending order, every occurrence of a duplicate included,
 * whatever the table's size; overlays and checkpoint images read the same
 * rows through every capacity. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "checkpoint_compare.h"
#include "space.h"
#include "term_universe.h"

static Arena g_arena;
static Atom *g_head;

static Atom *row(long k) {
    return atom_expr2(&g_arena, g_head, atom_int(&g_arena, k));
}

/* The slot count the sizing rule gives `rows` rows grown one at a time. */
static size_t grown_slots(size_t rows) {
    size_t slots = 16u;
    while (rows > slots / 10u * 7u)
        slots *= 2u;
    return slots;
}

/* The exact index's answer for `query` equals a scan of the rows. */
static void check_query(Space *s, Atom *query) {
    CettaIndex *indexed = NULL;
    CettaIndex n = space_exact_match_indices64(s, query, &indexed);
    CettaCount len = space_length64(s);
    CettaIndex expected = 0u;
    for (CettaIndex i = 0u; i < len; i++) {
        Atom *candidate = space_get_at64(s, i);
        if (candidate && atom_eq(candidate, query)) {
            assert(expected < n && indexed[expected] == i);
            expected++;
        }
    }
    assert(n == expected);
    assert(space_contains_exact(s, query) == (expected != 0u));
    free(indexed);
}

/* The table never runs past its bound, and only the smallest table is
 * an eighth full or less. */
static void check_shape(const Space *s) {
    size_t slots = space_exact_index_test_slots(s);
    size_t used = space_exact_index_test_used(s);
    assert(slots >= 16u && (slots & (slots - 1u)) == 0u);
    assert(used <= slots / 10u * 7u);
    assert(slots == 16u || used > slots / 8u);
}

static void promotion_boundaries(TermUniverse *universe) {
    Space s;
    space_init_with_universe(&s, universe);
    space_add(&s, row(0));
    assert(space_exact_index_test_slots(&s) == 0u);
    check_query(&s, row(0));
    assert(space_exact_index_test_slots(&s) == 16u);
    size_t resizes = space_exact_index_test_resizes();
    size_t promotions = 0u;
    for (long k = 1; k < 400; k++) {
        size_t before = space_exact_index_test_slots(&s);
        space_add(&s, row(k));
        size_t after = space_exact_index_test_slots(&s);
        assert(space_exact_index_test_current(&s));
        assert(space_exact_index_test_used(&s) == (size_t)k + 1u);
        assert(after == grown_slots((size_t)k + 1u));
        if (after != before) {
            /* The row that crosses the bound promotes the table. */
            assert(after == 2u * before && (size_t)k + 1u == before / 10u * 7u + 1u);
            promotions++;
        }
    }
    /* 16 -> 32 at 8 rows, then at 22, 43, 85, 176 and 358 rows. */
    assert(promotions == 6u);
    assert(space_exact_index_test_resizes() == resizes + promotions);
    for (long k = 0; k < 400; k += 37)
        check_query(&s, row(k));
    check_query(&s, row(400));
    space_free(&s);
}

static void duplicates_in_order(TermUniverse *universe) {
    Space s;
    space_init_with_universe(&s, universe);
    Atom *x = row(1), *y = row(2);
    Atom *order[] = {x, y, x, y, x, row(3), x};
    for (size_t i = 0u; i < sizeof order / sizeof order[0]; i++)
        space_add(&s, order[i]);
    CettaIndex *rows = NULL;
    assert(space_exact_match_indices64(&s, x, &rows) == 4u);
    assert(rows[0] == 0u && rows[1] == 2u && rows[2] == 4u && rows[3] == 6u);
    free(rows);
    /* Each removal takes one occurrence; the rest stay, in order. */
    for (CettaIndex left = 4u; left > 0u; left--) {
        assert(space_remove(&s, x));
        assert(space_exact_match_indices64(&s, x, NULL) == left - 1u);
        check_query(&s, x);
        check_query(&s, y);
        check_query(&s, row(3));
    }
    assert(!space_remove(&s, x));
    space_free(&s);
}

/* Random additions and removals over a small vocabulary: long probe runs
 * of one key, runs that meet, gaps closed in the middle of runs, growth
 * and shrinking, each checked against the rows. */
static void collisions_and_removals(TermUniverse *universe) {
    Space s;
    space_init_with_universe(&s, universe);
    enum { WORDS = 23, STEPS = 6000 };
    Atom *words[WORDS];
    for (long k = 0; k < WORDS; k++)
        words[k] = row(1000 + k);
    space_add(&s, words[0]);
    check_query(&s, words[0]);
    uint64_t state = 0x9E3779B97F4A7C15u;
    size_t grew = 0u, shrank = 0u;
    for (int step = 0; step < STEPS; step++) {
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        Atom *word = words[state % WORDS];
        size_t before = space_exact_index_test_slots(&s);
        /* Phases of filling (three adds in four) and draining (seven
         * removals in eight) move the size both ways. */
        bool fill = (step / 1000) % 2 == 0;
        bool add = fill ? (state >> 32) % 4u != 0u : (state >> 32) % 8u == 0u;
        if (add)
            space_add(&s, word);
        else
            (void)space_remove(&s, word);
        size_t after = space_exact_index_test_slots(&s);
        grew += after > before;
        shrank += after < before;
        assert(space_exact_index_test_current(&s));
        assert(space_exact_index_test_used(&s) == space_length64(&s));
        check_shape(&s);
        check_query(&s, word);
        if (step % 97 == 0) {
            for (long k = 0; k < WORDS; k++)
                check_query(&s, words[k]);
        }
    }
    assert(grew >= 3u && shrank >= 3u);
    /* Emptied, the table is back at its smallest size. */
    for (long k = 0; k < WORDS; k++) {
        while (space_remove(&s, words[k]))
            check_shape(&s);
    }
    assert(space_length64(&s) == 0u);
    assert(space_exact_index_test_current(&s));
    assert(space_exact_index_test_slots(&s) == 16u);
    space_free(&s);
}

static void no_oscillation(TermUniverse *universe) {
    Space s;
    space_init_with_universe(&s, universe);
    /* 21 rows fill 32 slots to the bound; the 22nd promotes to 64. */
    for (long k = 0; k < 22; k++)
        space_add(&s, row(k));
    check_query(&s, row(0));
    assert(space_exact_index_test_slots(&s) == 64u);
    size_t resizes = space_exact_index_test_resizes();
    for (int i = 0; i < 1000; i++) {
        assert(space_remove(&s, row(21)));
        space_add(&s, row(21));
    }
    assert(space_exact_index_test_slots(&s) == 64u);
    assert(space_exact_index_test_resizes() == resizes);
    /* Down to 8 rows: an eighth of 64, halved to 32. */
    for (long k = 21; k >= 8; k--)
        assert(space_remove(&s, row(k)));
    assert(space_exact_index_test_slots(&s) == 32u);
    resizes = space_exact_index_test_resizes();
    for (int i = 0; i < 1000; i++) {
        space_add(&s, row(8));
        assert(space_remove(&s, row(8)));
    }
    assert(space_exact_index_test_slots(&s) == 32u);
    assert(space_exact_index_test_resizes() == resizes);
    for (long k = 0; k < 24; k++)
        check_query(&s, row(k));
    space_free(&s);
}

static void allocation_failure(TermUniverse *universe) {
    Space s;
    space_init_with_universe(&s, universe);
    for (long k = 0; k < 7; k++)
        space_add(&s, row(k));
    check_query(&s, row(0));
    assert(space_exact_index_test_slots(&s) == 16u);
    /* No larger table: the current one takes rows while a slot stays
     * vacant, and still answers. */
    space_exact_index_test_refuse(SIZE_MAX);
    for (long k = 7; k < 15; k++) {
        space_add(&s, row(k));
        assert(space_exact_index_test_current(&s));
        assert(space_exact_index_test_slots(&s) == 16u);
        assert(space_exact_index_test_used(&s) == (size_t)k + 1u);
        check_query(&s, row(k));
        check_query(&s, row(0));
    }
    /* The sixteenth row has no room: the index is given up, and readers
     * answer from the rows while no table can be built. */
    space_add(&s, row(15));
    assert(!space_exact_index_test_current(&s));
    assert(space_exact_index_test_slots(&s) == 0u);
    for (long k = 0; k < 17; k++)
        check_query(&s, row(k));
    assert(!space_exact_index_test_current(&s));
    /* With memory again, the next reader builds the index whole. */
    space_exact_index_test_refuse(0u);
    check_query(&s, row(3));
    assert(space_exact_index_test_current(&s));
    assert(space_exact_index_test_slots(&s) == 32u);
    assert(space_exact_index_test_used(&s) == 16u);
    /* A smaller table that cannot be allocated leaves the larger one. */
    for (long k = 16; k < 40; k++)
        space_add(&s, row(k));
    assert(space_exact_index_test_slots(&s) == 64u);
    space_exact_index_test_refuse(SIZE_MAX);
    for (long k = 39; k >= 4; k--)
        assert(space_remove(&s, row(k)));
    assert(space_exact_index_test_current(&s));
    assert(space_exact_index_test_slots(&s) == 64u);
    for (long k = 0; k < 40; k++)
        check_query(&s, row(k));
    space_exact_index_test_refuse(0u);
    /* The next removal finds the table still an eighth full or less. */
    assert(space_remove(&s, row(3)));
    assert(space_exact_index_test_slots(&s) == 32u);
    space_free(&s);
}

static void overlays(TermUniverse *universe) {
    Space base, view;
    space_init_with_universe(&base, universe);
    for (long k = 0; k < 30; k++)
        space_add(&base, row(k % 10));
    check_query(&base, row(0));
    assert(space_exact_index_test_slots(&base) == 64u);
    space_init_overlay(&view, &base);
    for (long k = 0; k < 12; k++)
        space_add(&view, row(5 + k % 8));
    for (long k = 0; k < 20; k++) {
        check_query(&view, row(k));
        check_query(&base, row(k));
    }
    assert(space_remove(&view, row(5)));
    assert(space_remove(&view, row(9)));
    for (long k = 0; k < 20; k++) {
        check_query(&view, row(k));
        check_query(&base, row(k));
    }
    space_free(&view);
    space_free(&base);
}

typedef struct {
    size_t scalar_mismatches, physical_mismatches;
    bool exact_filed_equal, exact_used_equal;
    uint64_t exact_capacity[2];
} Reading;

static bool read_scalar(void *raw, const char *site, uint64_t a, uint64_t b) {
    Reading *r = raw;
    if (strcmp(site, "world.exact.filed") == 0)
        r->exact_filed_equal = a == b;
    if (strcmp(site, "world.exact.used") == 0)
        r->exact_used_equal = a == b;
    r->scalar_mismatches += a != b;
    return true;
}
static bool read_identity(void *raw, const char *site,
                          CettaCheckpointIdentityRole role, uint64_t a,
                          uint64_t b) {
    (void)raw; (void)site; (void)role; (void)a; (void)b;
    return true;
}
static bool read_address(void *raw, const char *site, const void *a,
                         const void *b) {
    (void)raw; (void)site; (void)a; (void)b;
    return true;
}
static bool read_arena(void *raw, const char *site, const Arena *a,
                       const Arena *b) {
    (void)raw; (void)site;
    return a == b;
}
static bool read_atom(void *raw, const char *site, const Atom *a,
                      const Atom *b) {
    (void)raw; (void)site;
    return a == b || (a && b && atom_eq((Atom *)a, (Atom *)b));
}
static bool read_physical(void *raw, const char *site, uint64_t a,
                          uint64_t b) {
    Reading *r = raw;
    if (strcmp(site, "world.exact.capacity") == 0) {
        r->exact_capacity[0] = a;
        r->exact_capacity[1] = b;
    }
    r->physical_mismatches += a != b;
    return true;
}
static bool read_bytes(void *raw, const char *site, const void *a, size_t n,
                       const void *b, size_t m) {
    Reading *r = raw;
    (void)site;
    r->scalar_mismatches += !(n == m && (!n || memcmp(a, b, n) == 0));
    return true;
}
static bool read_service(void *raw, const char *site, size_t work,
                         size_t bytes) {
    (void)raw; (void)site; (void)work; (void)bytes;
    return true;
}

static SpaceCheckpointBudget budget(void) {
    return (SpaceCheckpointBudget){.work_limit = SIZE_MAX / 2u,
                                   .byte_limit = SIZE_MAX / 2u};
}

static Reading compare(const Space *a, const Space *b) {
    Reading reading = {0};
    CettaCheckpointCompare read = {
        .context = &reading, .scalar = read_scalar,
        .identity = read_identity, .address = read_address,
        .arena = read_arena, .atom = read_atom, .physical = read_physical,
        .bytes = read_bytes, .service = read_service};
    SpaceCheckpointBudget spent = budget();
    assert(space_checkpoint_native_compare(a, b, &read, &spent));
    return reading;
}

static void checkpoints(TermUniverse *universe) {
    /* An image carries the table's size: it compares equal to its source
     * at every capacity. */
    const long sizes[] = {5, 12, 30, 60};
    const size_t slots[] = {16u, 32u, 64u, 128u};
    for (size_t i = 0u; i < sizeof sizes / sizeof sizes[0]; i++) {
        Space s, image;
        space_init_with_universe(&s, universe);
        for (long k = 0; k < sizes[i]; k++)
            space_add(&s, row(k % 7));
        check_query(&s, row(0));
        assert(space_exact_index_test_slots(&s) == slots[i]);
        bool ready = false;
        SpaceCheckpointBudget spent = budget();
        assert(space_checkpoint_native_copy(&s, &image, &ready, &spent));
        assert(ready);
        assert(space_exact_index_test_slots(&image) == slots[i]);
        assert(space_exact_index_test_used(&image) == (size_t)sizes[i]);
        Reading same = compare(&s, &image);
        assert(same.scalar_mismatches == 0u && same.physical_mismatches == 0u);
        space_free(&image);
        space_free(&s);
    }
    /* The same rows filed in tables of different sizes compare by what
     * they file. */
    Space s, small, large;
    space_init_with_universe(&s, universe);
    for (long k = 0; k < 6; k++)
        space_add(&s, row(k));
    check_query(&s, row(0));
    assert(space_exact_index_test_slots(&s) == 16u);
    bool ready = false;
    SpaceCheckpointBudget spent = budget();
    assert(space_checkpoint_native_copy(&s, &small, &ready, &spent));
    for (long k = 100; k < 130; k++)
        space_add(&s, row(k));
    for (long k = 129; k >= 100; k--)
        assert(space_remove(&s, row(k)));
    assert(space_exact_index_test_slots(&s) == 32u);
    assert(space_exact_index_test_used(&s) == 6u);
    ready = false;
    spent = budget();
    assert(space_checkpoint_native_copy(&s, &large, &ready, &spent));
    Reading across = compare(&small, &large);
    assert(across.exact_filed_equal && across.exact_used_equal);
    assert(across.exact_capacity[0] == 16u && across.exact_capacity[1] == 32u);
    space_free(&large);
    space_free(&small);
    space_free(&s);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&g_arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &g_arena);
    g_head = atom_symbol(&g_arena, "sized-row");

    promotion_boundaries(&universe);
    duplicates_in_order(&universe);
    collisions_and_removals(&universe);
    no_oscillation(&universe);
    allocation_failure(&universe);
    overlays(&universe);
    checkpoints(&universe);

    term_universe_free(&universe);
    arena_free(&g_arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("PASS: the exact-membership index follows its rows: promotion at the "
           "bound, halving at an eighth, no oscillation, a failed allocation "
           "keeps or gives up the index, duplicates, overlays and checkpoints "
           "across capacities\n");
    return 0;
}
