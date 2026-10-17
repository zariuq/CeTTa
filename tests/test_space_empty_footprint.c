/* An empty space is its header.  Its indexes (the exact-membership table,
 * 256 equation buckets, two 256-bucket bridge indexes) are arrays allocated
 * when the first row is filed in them, so a space that never holds such rows
 * costs sizeof(Space) and nothing more, and reading an empty space builds
 * nothing.  The exact-membership table starts small and grows with its
 * rows. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "space.h"
#include "storage_census.h"
#include "term_universe.h"

static size_t owned_bytes(const Space *s) {
    CettaStorageCensus census = {0};
    space_storage_census(s, &census);
    cetta_storage_finish(&census);
    assert(!census.incomplete && !census.overflow);
    size_t bytes = census.bytes;
    cetta_storage_census_free(&census);
    return bytes;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena arena;
    arena_init(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);

    /* The header bound covers every inline field; the indexes are not. */
    assert(sizeof(Space) <= 1024u);
    assert(sizeof(EqIndex) < sizeof(EqBucket) * 8u);
    assert(sizeof(ExactAtomIndex) <= 16u * sizeof(void *));

    enum { SPACES = 64 };
    Space spaces[SPACES];
    for (unsigned i = 0; i < SPACES; i++) {
        space_init_with_universe(&spaces[i], &universe);
        assert(owned_bytes(&spaces[i]) == 0u);
    }

    /* Reading an empty space allocates no index. */
    Space *empty = &spaces[0];
    Atom *row_head = atom_symbol(&arena, "footprint-row");
    Atom *fn = atom_symbol(&arena, "footprint-fn");
    Atom *x = atom_var(&arena, "x");
    Atom *pattern = atom_expr2(&arena, row_head, x);
    CettaIndex *candidates = NULL;
    assert(space_match_candidates64(empty, pattern, &candidates) == 0u);
    free(candidates);
    Atom *fact = atom_expr2(&arena, row_head, atom_int(&arena, 7));
    assert(!space_contains_exact(empty, fact));
    QueryResults results;
    query_results_init(&results);
    query_equations(empty, atom_expr(&arena, (Atom *[]){fn}, 1u), &arena, &results);
    assert(results.len == 0u);
    query_results_free(&results);
    assert(owned_bytes(empty) == 0u);

    /* A fact queried for exact membership allocates the exact table; an
     * equation queried by its head allocates the equation buckets. */
    Space *used = &spaces[1];
    space_add(used, fact);
    assert(space_contains_exact(used, fact));
    size_t with_fact = owned_bytes(used);
    assert(with_fact >= 16u * sizeof(ExactAtomNarrowSlot));
    Atom *eq_items[] = {atom_symbol(&arena, "="),
                        atom_expr(&arena, (Atom *[]){fn}, 1u),
                        atom_int(&arena, 9)};
    space_add(used, atom_expr(&arena, eq_items, 3u));
    query_results_init(&results);
    query_equations(used, atom_expr(&arena, (Atom *[]){fn}, 1u), &arena, &results);
    assert(results.len == 1u);
    query_results_free(&results);
    assert(owned_bytes(used) >= with_fact + EQ_INDEX_BUCKETS * sizeof(EqBucket));
    for (unsigned i = 2; i < SPACES; i++)
        assert(owned_bytes(&spaces[i]) == 0u);

    for (unsigned i = 0; i < SPACES; i++)
        space_free(&spaces[i]);
    term_universe_free(&universe);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("PASS: an empty space is its %zu-byte header; indexes allocate with their first row\n",
           sizeof(Space));
    return 0;
}
