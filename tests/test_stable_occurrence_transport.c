#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "space.h"
#include "space_match_backend.h"
#include "stats.h"
#include "symbol.h"
#include "native_cost.h"
#include "storage_census.h"
#include "eval.h"
#include "library.h"
#include "parser.h"
#include <inttypes.h>

static void init_symbols(SymbolTable *symbols) {
    symbol_table_init(symbols);
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
}

static Atom *binary(Arena *arena, SymbolId head, Atom *left, Atom *right) {
    Atom *items[3] = {
        atom_symbol_id(arena, head),
        left,
        right,
    };
    return atom_expr(arena, items, 3u);
}

static Atom *edge_value(Arena *arena, SymbolId edge, int64_t value) {
    return binary(arena, edge, atom_int(arena, value),
                  atom_int(arena, value + 1000));
}

static AtomId store_atom(TermUniverse *universe, Atom *atom) {
    AtomId id = term_universe_store_atom_id(universe, NULL, atom);
    assert(id != CETTA_ATOM_ID_NONE);
    return id;
}

static void assert_atom_ids(const Space *space, const AtomId *expected,
                            CettaCount expected_len) {
    assert(space_length64(space) == expected_len);
    for (CettaIndex i = 0u; i < expected_len; i++)
        assert(space_get_atom_id_at64(space, i) == expected[i]);
}

static void assert_query_indices(Space *space, Arena *scratch, Atom *query,
                                 const CettaIndex *expected,
                                 CettaIndex expected_len) {
    SubstMatchSet matches;
    space_match_backend_query(space, scratch, query, &matches);
    assert(matches.len == expected_len);
    for (CettaIndex i = 0u; i < expected_len; i++) {
        assert(matches.items[i].atom_idx == expected[i]);
        assert(matches.items[i].atom_idx < space_length64(space));
    }
    smset_free(&matches);
}

static void realize_both_indexes(Space *space, Arena *scratch, Atom *query) {
    space_match_native_ensure_trie(space);
    CettaIndex *candidates = NULL;
    CettaIndex candidate_len =
        space_match_backend_candidates64(space, query, &candidates);
    assert(candidate_len != 0u);
    for (CettaIndex i = 0u; i < candidate_len; i++)
        assert(candidates[i] < space_length64(space));
    free(candidates);

    SubstMatchSet matches;
    space_match_backend_query(space, scratch, query, &matches);
    smset_free(&matches);
    assert(space->match_backend.native.match_trie != NULL);
    assert(!space->match_backend.native.match_trie_dirty);
    assert(space->match_backend.native.stree != NULL);
    assert(!space->match_backend.native.stree_dirty);
}

static void test_duplicate_order_and_open_patterns(
        TermUniverse *universe, Arena *scratch) {
    Space space;
    SymbolId edge = symbol_intern_cstr(g_symbols, "transport-edge");
    SymbolId open = symbol_intern_cstr(g_symbols, "transport-open");
    SymbolId a = symbol_intern_cstr(g_symbols, "transport-a");
    SymbolId b = symbol_intern_cstr(g_symbols, "transport-b");
    SymbolId tail = symbol_intern_cstr(g_symbols, "transport-tail");
    SymbolId x_spelling = symbol_intern_cstr(g_symbols, "transport-x");
    AtomId source[24];

    for (CettaIndex i = 0u; i < 20u; i++) {
        int64_t value = (i == 2u || i == 3u) ? 7 : (int64_t)(100u + i);
        source[i] = store_atom(universe, edge_value(scratch, edge, value));
    }
    Atom *x = atom_var_with_spelling(scratch, x_spelling, UINT64_C(70001));
    source[20] = store_atom(universe, binary(scratch, open, x, x));
    source[21] = store_atom(
        universe, binary(scratch, open, x, atom_symbol_id(scratch, tail)));
    source[22] = store_atom(
        universe, binary(scratch, open, atom_symbol_id(scratch, a),
                         atom_symbol_id(scratch, a)));
    source[23] = store_atom(
        universe, binary(scratch, open, atom_symbol_id(scratch, a),
                         atom_symbol_id(scratch, b)));

    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    for (CettaIndex i = 0u; i < 24u; i++)
        space_add_atom_id(&space, source[i]);

    Atom *edge7 = edge_value(scratch, edge, 7);
    realize_both_indexes(&space, scratch, edge7);

    uint8_t remove_mask[24] = {0};
    remove_mask[1] = 1u;
    remove_mask[2] = 1u;
    remove_mask[5] = 1u;
    remove_mask[21] = 1u;
    CettaCount removed = 0u;
    assert(space_remove_occurrence_mask_stable(
        &space, remove_mask, 24u, &removed));
    assert(removed == 4u);

    AtomId expected[20];
    CettaIndex target = 0u;
    for (CettaIndex source_index = 0u; source_index < 24u; source_index++) {
        if (remove_mask[source_index] == 0u)
            expected[target++] = source[source_index];
    }
    assert(target == 20u);
    assert_atom_ids(&space, expected, 20u);
    assert(space.match_backend.native.match_trie != NULL);
    assert(!space.match_backend.native.match_trie_dirty);
    assert(space.match_backend.native.match_trie_stale_occurrences == 4u);
    assert(space.match_backend.native.stree != NULL);
    assert(!space.match_backend.native.stree_dirty);
    assert(space.match_backend.native.stree_stale_occurrences == 4u);

    CettaIndex edge7_indices[] = {1u};
    assert_query_indices(&space, scratch, edge7, edge7_indices, 1u);
    Atom *open_aa = binary(
        scratch, open, atom_symbol_id(scratch, a),
        atom_symbol_id(scratch, a));
    CettaIndex open_aa_indices[] = {17u, 18u};
    assert_query_indices(&space, scratch, open_aa, open_aa_indices, 2u);

    CettaIndex *candidates = NULL;
    CettaIndex candidate_len =
        space_match_backend_candidates64(&space, edge7, &candidates);
    assert(candidate_len == 1u);
    assert(candidates[0] == 1u);
    free(candidates);

    uint8_t retain_all[20] = {0};
    uint64_t revision = space.revision;
    removed = 99u;
    assert(space_remove_occurrence_mask_stable(
        &space, retain_all, 20u, &removed));
    assert(removed == 0u);
    assert(space.revision == revision);
    assert_atom_ids(&space, expected, 20u);

    CettaIndex invalid_map[20];
    for (CettaIndex i = 0u; i < 20u; i++)
        invalid_map[i] = i;
    invalid_map[0] = 1u;
    invalid_map[1] = 0u;
    SpaceStableOccurrenceTransport invalid = {
        .source_to_target = invalid_map,
        .source_len = 20u,
        .target_len = 20u,
    };
    assert(space_match_backend_transport_stable_occurrence_coordinates(
               &space, &invalid) == SPACE_BACKEND_BATCH_ERROR);
    assert_query_indices(&space, scratch, edge7, edge7_indices, 1u);

    space_free(&space);
}

static void test_remove_all_releases_small_indexes(
        TermUniverse *universe, Arena *scratch) {
    Space space;
    SymbolId edge = symbol_intern_cstr(g_symbols, "transport-all-edge");
    Atom *query = edge_value(scratch, edge, 3);
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    for (CettaIndex i = 0u; i < 24u; i++)
        space_add_atom_id(
            &space, store_atom(universe, edge_value(scratch, edge, (int64_t)i)));
    realize_both_indexes(&space, scratch, query);

    uint8_t remove_all[24];
    memset(remove_all, 1, sizeof(remove_all));
    CettaCount removed = 0u;
    assert(space_remove_occurrence_mask_stable(
        &space, remove_all, 24u, &removed));
    assert(removed == 24u);
    assert(space_length64(&space) == 0u);
    assert(space.match_backend.native.match_trie == NULL);
    assert(!space.match_backend.native.match_trie_dirty);
    assert(space.match_backend.native.stree == NULL);
    assert(!space.match_backend.native.stree_dirty);
    space_free(&space);
}

static void test_queue_linearization(
        TermUniverse *universe, Arena *scratch) {
    Space space;
    SymbolId edge = symbol_intern_cstr(g_symbols, "transport-queue-edge");
    AtomId source[24];
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_QUEUE;
    for (CettaIndex i = 0u; i < 24u; i++) {
        source[i] = store_atom(
            universe, edge_value(scratch, edge, (int64_t)i));
        space_add_atom_id(&space, source[i]);
    }
    Atom *discarded = NULL;
    assert(space_pop(&space, &discarded));
    assert(space.native.start == 1u);
    Atom *query = edge_value(scratch, edge, 13);
    realize_both_indexes(&space, scratch, query);

    uint8_t remove_mask[23] = {0};
    remove_mask[2] = 1u;
    remove_mask[11] = 1u;
    remove_mask[22] = 1u;
    CettaCount removed = 0u;
    assert(space_remove_occurrence_mask_stable(
        &space, remove_mask, 23u, &removed));
    assert(removed == 3u);
    assert(space.native.start == 0u);

    AtomId expected[20];
    CettaIndex target = 0u;
    for (CettaIndex logical = 0u; logical < 23u; logical++) {
        if (remove_mask[logical] == 0u)
            expected[target++] = source[logical + 1u];
    }
    assert_atom_ids(&space, expected, 20u);
    CettaIndex query_indices[] = {10u};
    assert_query_indices(&space, scratch, query, query_indices, 1u);
    space_free(&space);
}

static void test_partial_remove_releases_small_indexes(
        TermUniverse *universe, Arena *scratch) {
    Space space;
    SymbolId edge = symbol_intern_cstr(g_symbols, "partial-small-edge");
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    for (int64_t i = 0; i < 24; i++)
        space_add_atom_id(&space, store_atom(universe, edge_value(scratch, edge, i)));
    Atom *remaining = edge_value(scratch, edge, 23);
    realize_both_indexes(&space, scratch, remaining);
    uint8_t mask[24];
    memset(mask, 1, sizeof(mask));
    mask[23] = 0;
    CettaCount removed = 0;
    assert(space_remove_occurrence_mask_stable(&space, mask, 24, &removed));
    assert(removed == 23 && space_length64(&space) == 1);
    assert(space.match_backend.native.match_trie == NULL);
    assert(space.match_backend.native.stree == NULL);
    CettaIndex expected[] = {0};
    assert_query_indices(&space, scratch, remaining, expected, 1);
    space_free(&space);
}

static Atom *shared_tree(Arena *arena, unsigned depth, unsigned width, Atom *leaf) {
    assert(width <= 32u);
    Atom *items[33];
    items[0] = atom_symbol(arena, "bounded-index-node");
    for (unsigned level = 0; level < depth; level++) {
        for (unsigned i = 1; i <= width; i++)
            items[i] = leaf;
        leaf = atom_expr(arena, items, width + 1u);
    }
    return leaf;
}

static void test_bounded_graph_index(TermUniverse *universe, Arena *scratch) {
    /* Depth and total expansion are independent controls: 32^6 occurrences
     * cannot be bounded by a depth cutoff of eight alone. */
    const unsigned depths[] = {18u, 6u};
    const unsigned widths[] = {2u, 32u};
    for (unsigned specimen = 0; specimen < 2u; specimen++) {
        Space space;
        space_init_with_universe(&space, universe);
        space.kind = SPACE_KIND_STACK;
        Atom *shared = shared_tree(scratch, depths[specimen], widths[specimen],
                                  atom_symbol(scratch, "bounded-index-leaf"));
        SymbolId head = symbol_intern_cstr(g_symbols, "bounded-index-row");
        SymbolId end = symbol_intern_cstr(g_symbols, "bounded-index-end");
        Atom *wanted = binary(scratch, head, shared,
                             binary(scratch, end,
                               shared_tree(scratch, 2u, 32u, atom_int(scratch, 7)),
                               atom_int(scratch, 8)));
        for (int64_t i = 0; i < 24; i++) {
            Atom *row = (i == 2 || i == 17) ? wanted :
                binary(scratch, head, shared,
                       binary(scratch, end,
                         shared_tree(scratch, 2u, 32u, atom_int(scratch, i + 100)),
                         atom_int(scratch, 8)));
            AtomId id = term_universe_store_atom_id_from_source_arena(
                universe, NULL, scratch, row);
            assert(id != CETTA_ATOM_ID_NONE);
            space_add_atom_id(&space, id);
        }
        space_match_native_ensure_trie(&space);
        CettaIndex *candidates = NULL;
        CettaIndex count = space_match_backend_candidates64(&space, wanted, &candidates);
        bool first = false, second = false;
        for (CettaIndex i = 0; i < count; i++) {
            if (i) assert(candidates[i - 1] < candidates[i]);
            first |= candidates[i] == 2u;
            second |= candidates[i] == 17u;
        }
        assert(first && second);
        /* The wide graph exhausts the budget before the final discriminator.
         * False candidates must be filtered, never counted as answers. */
        if (specimen == 1u) assert(count > 2u);
        free(candidates);
        CettaIndex expected[] = {2u, 17u};
        assert_query_indices(&space, scratch, wanted, expected, 2u);
        uint64_t answers = UINT64_MAX;
        CettaIndex examined = 0;
        assert(!space_match_count_flat_linear64(&space, scratch, wanted, &answers, &examined));
        CettaIndex direct_count = UINT64_MAX;
        assert(!disc_count_rigid_exact_path(space.match_backend.native.match_trie,
                                           wanted, &direct_count));
        space_free(&space);
    }
}

static void test_repeated_churn_is_amortized(
        TermUniverse *universe, Arena *scratch) {
    Space space;
    SymbolId edge = symbol_intern_cstr(g_symbols, "transport-churn-edge");
    AtomId expected[24];
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    for (CettaIndex i = 0u; i < 24u; i++) {
        expected[i] = store_atom(
            universe, edge_value(scratch, edge, (int64_t)i));
        space_add_atom_id(&space, expected[i]);
    }
    realize_both_indexes(
        &space, scratch, edge_value(scratch, edge, 23));

    for (CettaIndex round = 0u; round < 72u; round++) {
        uint8_t remove_first[24] = {0};
        remove_first[0] = 1u;
        CettaCount removed = 0u;
        assert(space_remove_occurrence_mask_stable(
            &space, remove_first, 24u, &removed));
        assert(removed == 1u);
        memmove(expected, expected + 1u, sizeof(expected[0]) * 23u);

        int64_t value = (int64_t)(1000u + round);
        Atom *latest = edge_value(scratch, edge, value);
        expected[23] = store_atom(universe, latest);
        space_add_atom_id(&space, expected[23]);
        assert_atom_ids(&space, expected, 24u);

        CettaIndex latest_index[] = {23u};
        assert_query_indices(
            &space, scratch, latest, latest_index, 1u);
        CettaIndex *candidates = NULL;
        CettaIndex candidate_len =
            space_match_backend_candidates64(&space, latest, &candidates);
        assert(candidate_len == 1u);
        assert(candidates[0] == 23u);
        free(candidates);
        assert(!space.match_backend.native.match_trie_dirty);
        assert(!space.match_backend.native.stree_dirty);
        assert(space.match_backend.native.match_trie_stale_occurrences <
               space_length64(&space));
        assert(space.match_backend.native.stree_stale_occurrences <
               space_length64(&space));
    }
    assert_atom_ids(&space, expected, 24u);
    space_free(&space);
}

static void test_occurrence_cursor_exact_clone(
        TermUniverse *universe, Arena *scratch) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    for (int64_t value = 0; value < 4; value++) {
        space_add_atom_id(
            &space, store_atom(universe, atom_int(scratch, value)));
    }

    Atom *query = atom_var_with_spelling(
        scratch, symbol_intern_cstr(g_symbols, "cursor-clone-x"),
        UINT64_C(81001));
    SpaceOccurrenceCursor source;
    SpaceOccurrenceCursor clone;
    assert(space_occurrence_cursor_init(&space, query, &source));
    CettaIndex index = UINT64_MAX;
    assert(space_occurrence_cursor_next(&source, &index) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 0u);

    /* Later appends are outside both logical-update ceilings. */
    space_add_atom_id(
        &space, store_atom(universe, atom_int(scratch, 99)));
    assert(space_occurrence_cursor_clone(&source, &clone));
    for (CettaIndex expected = 1u; expected < 4u; expected++) {
        CettaIndex source_index = UINT64_MAX;
        CettaIndex clone_index = UINT64_MAX;
        assert(space_occurrence_cursor_next(&source, &source_index) ==
               SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(space_occurrence_cursor_next(&clone, &clone_index) ==
               SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(source_index == expected);
        assert(clone_index == expected);
    }
    assert(space_occurrence_cursor_next(&source, &index) ==
           SPACE_OCCURRENCE_CURSOR_END);
    assert(space_occurrence_cursor_next(&clone, &index) ==
           SPACE_OCCURRENCE_CURSOR_END);
    space_occurrence_cursor_release(&clone);

    /* A cursor whose pinned prefix is no longer authoritative cannot be
     * cloned into a continuation. */
    uint64_t prefix_epoch = source.prefix_epoch;
    source.prefix_epoch++;
    assert(!space_occurrence_cursor_clone(&source, &clone));
    source.prefix_epoch = prefix_epoch;
    space_occurrence_cursor_release(&source);
    space_free(&space);
}

static void test_atom_id_removal_preserves_pinned_view(
        TermUniverse *universe, Arena *scratch) {
    Space space;
    AtomId rows[3];
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    for (int64_t value = 0; value < 3; value++) {
        rows[value] = store_atom(universe, atom_int(scratch, value));
        space_add_atom_id(&space, rows[value]);
    }

    Atom *query = atom_var_with_spelling(
        scratch, symbol_intern_cstr(g_symbols, "cursor-remove-x"),
        UINT64_C(81002));
    SpaceOccurrenceCursor before_remove;
    SpaceOccurrenceCursor after_remove;
    assert(space_occurrence_cursor_init(
        &space, query, &before_remove));
    CettaIndex index = UINT64_MAX;
    assert(space_occurrence_cursor_next(&before_remove, &index) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 0u);

    uint64_t pinned_prefix = before_remove.prefix_epoch;
    assert(space_remove_atom_id(&space, rows[1]));
    assert(space.prefix_epoch != pinned_prefix);
    assert(before_remove.prefix_epoch == pinned_prefix);
    assert(!space_remove_atom_id(&space, rows[1]));

    SpaceOccurrenceCursor detached_clone;
    assert(space_occurrence_cursor_clone(&before_remove, &detached_clone));

    /* The old cursor owns the pre-removal view.  A cursor opened afterward
     * reads the compacted live rows, with coordinates local to that view. */
    assert(space_occurrence_cursor_init(
        &space, query, &after_remove));
    assert(space_occurrence_cursor_next(&after_remove, &index) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 0u);
    assert(atom_eq(space_occurrence_cursor_atom(&after_remove, index),
                   term_universe_get_atom(universe, rows[0])));
    assert(space_occurrence_cursor_next(&after_remove, &index) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 1u);
    assert(atom_eq(space_occurrence_cursor_atom(&after_remove, index),
                   term_universe_get_atom(universe, rows[2])));
    assert(space_occurrence_cursor_next(&after_remove, &index) ==
           SPACE_OCCURRENCE_CURSOR_END);

    assert(space_occurrence_cursor_next(&before_remove, &index) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 1u);
    assert(atom_eq(space_occurrence_cursor_atom(&before_remove, index),
                   term_universe_get_atom(universe, rows[1])));
    assert(space_occurrence_cursor_next(&before_remove, &index) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 2u);
    assert(atom_eq(space_occurrence_cursor_atom(&before_remove, index),
                   term_universe_get_atom(universe, rows[2])));
    assert(space_occurrence_cursor_next(&before_remove, &index) ==
           SPACE_OCCURRENCE_CURSOR_END);

    space_occurrence_cursor_release(&after_remove);
    space_occurrence_cursor_release(&before_remove);
    for (CettaIndex expected = 1u; expected < 3u; expected++) {
        assert(space_occurrence_cursor_next(&detached_clone, &index) ==
               SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(index == expected);
        assert(atom_eq(space_occurrence_cursor_atom(&detached_clone, index),
                       term_universe_get_atom(universe, rows[expected])));
    }
    assert(space_occurrence_cursor_next(&detached_clone, &index) ==
           SPACE_OCCURRENCE_CURSOR_END);
    space_occurrence_cursor_release(&detached_clone);
    AtomId expected[] = {rows[0], rows[2]};
    assert_atom_ids(&space, expected, 2u);
    space_free(&space);
}

static void test_cold_candidate_cursor(TermUniverse *universe, Arena *scratch) {
    Space space;
    space_init_with_universe(&space, universe);
    SymbolId edge = symbol_intern_cstr(g_symbols, "cold-edge");
    SymbolId other = symbol_intern_cstr(g_symbols, "cold-other");
    AtomId rows[23];
    for (CettaIndex i = 0u; i < 20u; i++)
        rows[i] = store_atom(universe, edge_value(scratch, edge, (int64_t)i));
    rows[20] = rows[7];
    Atom *variable = atom_var_with_spelling(scratch,
        symbol_intern_cstr(g_symbols, "cold-x"), UINT64_C(91001));
    rows[21] = store_atom(universe, binary(scratch, edge, variable, variable));
    rows[22] = store_atom(universe, binary(scratch, other, variable, variable));
    uint64_t revision = space.revision;
    assert(space_add_atom_ids_batch(&space, rows, 23u));
    assert(space.native.len == 23u && space.revision == revision + 1u);
    Atom *query = binary(scratch, edge, atom_int(scratch, 7),
        atom_var_with_spelling(scratch,
            symbol_intern_cstr(g_symbols, "cold-y"), UINT64_C(91002)));
    SpaceOccurrenceCursor cursor, clone;
    assert(space_occurrence_cursor_init(&space, query, &cursor));
    assert(cursor.flat_mode && cursor.node_len == 0u);
    assert(space.match_backend.native.match_trie == NULL);
    assert(space_occurrence_cursor_clone(&cursor, &clone));
    /* The encoded selector is conservative for stored correlations. Its
     * unrelated variable-bearing row is rejected by canonical matching. */
    const CettaIndex candidates[] = {7u, 20u, 21u, 22u};
    const CettaIndex matches[] = {7u, 20u, 21u};
    assert_query_indices(&space, scratch, query, matches, 3u);
    assert(space_remove_atom_id(&space, rows[7]));
    AtomId appended[] = {rows[7], rows[7]};
    assert(space_add_atom_ids_batch(&space, appended, 2u));
    for (size_t i = 0u; i < 4u; i++) {
        CettaIndex index = UINT64_MAX, cloned = UINT64_MAX;
        assert(space_occurrence_cursor_next(&cursor, &index) == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(space_occurrence_cursor_next(&clone, &cloned) == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(index == candidates[i] && cloned == index);
        assert(atom_eq(space_occurrence_cursor_atom(&cursor, index),
                       term_universe_get_atom(universe, rows[index])));
    }
    CettaIndex end = UINT64_MAX;
    assert(space_occurrence_cursor_next(&cursor, &end) == SPACE_OCCURRENCE_CURSOR_END);
    assert(space_occurrence_cursor_next(&clone, &end) == SPACE_OCCURRENCE_CURSOR_END);
    space_occurrence_cursor_release(&clone);
    space_occurrence_cursor_release(&cursor);
    /* Exhausting the startup scan allowance returns to the reusable trie. */
    for (unsigned i = 0u; i < 5u; i++) {
        CettaIndex *selected = NULL;
        (void)space_match_backend_candidates64(&space, query, &selected);
        free(selected);
    }
    assert(space.match_backend.native.match_trie != NULL);
    assert(space_add_atom_ids_batch(&space, appended, 2u));
    const CettaIndex later_matches[] = {20u, 21u, 22u, 23u, 24u, 25u};
    assert_query_indices(&space, scratch, query, later_matches, 6u);
    space_free(&space);
}

typedef struct {
    Space *space;
    CettaIndex indices[8];
    size_t length;
} QueryWitnesses;

static bool collect_query_witness(SpaceReadToken read, CettaIndex index, void *raw) {
    QueryWitnesses *witnesses = raw;
    assert(read.space == witnesses->space && space_read_token_is_current(read));
    assert(index < space_length64(witnesses->space));
    assert(witnesses->length < 8u);
    witnesses->indices[witnesses->length++] = index;
    return true;
}

static void test_retained_query_rows(TermUniverse *universe, Arena *scratch) {
    Space space;
    space_init_with_universe(&space, universe);
    SymbolId edge = symbol_intern_cstr(g_symbols, "cursor-edge");
    Atom *x = atom_var_with_spelling(scratch,
        symbol_intern_cstr(g_symbols, "cursor-x"), UINT64_C(81001));
    Atom *y = atom_var_with_spelling(scratch,
        symbol_intern_cstr(g_symbols, "cursor-y"), UINT64_C(81002));
    Atom *e12 = binary(scratch, edge, atom_int(scratch, 1), atom_int(scratch, 2));
    Atom *e23 = binary(scratch, edge, atom_int(scratch, 2), atom_int(scratch, 3));
    space_add_atom_id(&space, store_atom(universe, e12));
    space_add_atom_id(&space, store_atom(universe, e12));
    space_add_atom_id(&space, store_atom(universe, e23));
    Atom *pattern = binary(scratch, edge, atom_int(scratch, 1), x);
    CettaNativeCostLedger cost;
    cetta_native_cost_init(&cost);
    CettaNativeCostScope scope;
    cetta_native_cost_scope_enter(&scope, &cost, 1u);
    SpaceQueryCursor *cursor = space_query_cursor_new(&space, scratch, pattern, NULL);
    assert(cursor && space_query_cursor_incremental(cursor));
    assert(cost.total.counts[CETTA_COST_INDEX_VISIT] == 0u);
    assert(cost.total.counts[CETTA_COST_MATCH_ATTEMPT] == 0u);
    Bindings row;
    bindings_init(&row);
    uint64_t empty_grant_work=cost.total.work;
    assert(space_query_cursor_step_with_budget(cursor,0u,&row) == SPACE_QUERY_CURSOR_PROGRESS);
    assert(cost.total.work == empty_grant_work);
    QueryWitnesses witnesses = {.space = &space};
    assert(!space_query_cursor_visit_witnesses(cursor, collect_query_witness, &witnesses));
    for (unsigned i = 0u; i < 3u; i++)
        assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_PROGRESS);
    assert(cost.total.counts[CETTA_COST_INDEX_VISIT] == 1u);
    assert(cost.total.counts[CETTA_COST_MATCH_ATTEMPT] == 1u);
    uint64_t before_publish = cost.total.work;
    assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_ROW);
    assert(cost.total.work == before_publish);
    assert(atom_eq(bindings_apply(&row, scratch, x), atom_int(scratch, 2)));
    assert(space_query_cursor_visit_witnesses(cursor, collect_query_witness, &witnesses));
    assert(witnesses.length == 1u && witnesses.indices[0] == 0u);
    bindings_free(&row);
    bindings_init(&row);
    unsigned rows = 1u;
    for (unsigned steps = 0u; steps < 100u; steps++) {
        SpaceQueryCursorStep step = space_query_cursor_step(cursor, &row);
        if (step == SPACE_QUERY_CURSOR_END) break;
        assert(step == SPACE_QUERY_CURSOR_PROGRESS || step == SPACE_QUERY_CURSOR_ROW);
        if (step == SPACE_QUERY_CURSOR_ROW) {
            witnesses.length = 0u;
            assert(space_query_cursor_visit_witnesses(cursor, collect_query_witness, &witnesses));
            assert(witnesses.length == 1u && witnesses.indices[0] == rows);
            rows++;
            assert(atom_eq(bindings_apply(&row, scratch, x), atom_int(scratch, 2)));
            bindings_free(&row);bindings_init(&row);
        } else {
            assert(!space_query_cursor_visit_witnesses(cursor, collect_query_witness, &witnesses));
        }
    }
    assert(rows == 2u && space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_END);
    assert(cost.total.counts[CETTA_COST_INDEX_VISIT] == 3u);
    assert(cost.total.counts[CETTA_COST_MATCH_ATTEMPT] == 3u);
    space_query_cursor_free(cursor);
    cetta_native_cost_scope_leave(&scope);
    cetta_native_cost_free(&cost);

    Atom *second = binary(scratch, edge, x, y);
    Atom *choice = binary(scratch, g_builtin_syms.pipe, second, second);
    Atom *query = binary(scratch, g_builtin_syms.comma, pattern, choice);
    BindingSet oracle;
    assert(space_query_bindings(&space, scratch, query, NULL, &oracle));
    assert(oracle.len == 4u);
    cursor = space_query_cursor_new(&space, scratch, query, NULL);
    assert(cursor);
    rows = 0u;
    for (unsigned steps = 0u; steps < 200u; steps++) {
        SpaceQueryCursorStep step = space_query_cursor_step(cursor, &row);
        if (step == SPACE_QUERY_CURSOR_END) break;
        assert(step == SPACE_QUERY_CURSOR_PROGRESS || step == SPACE_QUERY_CURSOR_ROW);
        if (step != SPACE_QUERY_CURSOR_ROW) continue;
        assert(rows < oracle.len);
        assert(atom_eq(bindings_apply(&row,scratch,x), bindings_apply(&oracle.items[rows],scratch,x)));
        assert(atom_eq(bindings_apply(&row,scratch,y), bindings_apply(&oracle.items[rows],scratch,y)));
        witnesses.length = 0u;
        assert(space_query_cursor_visit_witnesses(cursor,collect_query_witness,&witnesses));
        assert(witnesses.length == 2u && witnesses.indices[0] == rows / 2u &&
               witnesses.indices[1] == 2u);
        CettaStorageCensus census = {0};
        space_query_cursor_storage_census(cursor, &census);
        cetta_storage_finish(&census);
        assert(!census.incomplete && census.bytes && census.repeated);
        cetta_storage_census_free(&census);
        rows++;
        bindings_free(&row);bindings_init(&row);
    }
    assert(rows == oracle.len);
    binding_set_free(&oracle);
    space_query_cursor_free(cursor);

    const SpaceMatchBackendOps *native_ops = space.match_backend.ops;
    SpaceMatchBackendOps fallback_ops = *native_ops;
    space.match_backend.ops = &fallback_ops;
    cursor = space_query_cursor_new(&space, scratch, pattern, NULL);
    assert(cursor && !space_query_cursor_incremental(cursor));
    assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_PROGRESS);
    for (unsigned i = 0u; i < 2u; i++) {
        assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_ROW);
        assert(!space_query_cursor_visit_witnesses(cursor, collect_query_witness, &witnesses));
        assert(atom_eq(bindings_apply(&row, scratch, x), atom_int(scratch, 2)));
        bindings_free(&row);bindings_init(&row);
    }
    assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_END);
    space_query_cursor_free(cursor);
    space.match_backend.ops = native_ops;
    bindings_free(&row);
    space_free(&space);
    puts("PASS: retained query rows preserve order, physical witnesses and connective bindings");
}

typedef struct {
    SpaceReadToken reads[8];
    size_t length;
} QueryReads;

static bool collect_query_read(SpaceReadToken read, void *context) {
    QueryReads *reads=context;
    assert(reads->length<8u);
    reads->reads[reads->length++]=read;
    return true;
}

static void assert_query_reads_equal(const QueryReads *left, const QueryReads *right) {
    assert(left->length==right->length);
    for (size_t i=0u;i<left->length;i++) {
        const SpaceReadToken *a=&left->reads[i], *b=&right->reads[i];
        assert(a->space==b->space && a->instance_id==b->instance_id &&
            a->revision==b->revision && a->prefix_epoch==b->prefix_epoch &&
            a->base_prefix_epoch==b->base_prefix_epoch &&
            a->base_dependency_epoch==b->base_dependency_epoch);
    }
}

typedef struct {
    SpaceQueryCursorStep steps[1024];
    size_t length;
    Atom *values[16];
    QueryWitnesses witnesses[16];
    size_t rows;
    uint64_t nodes, attempts, visits, candidates;
} QueryCloneTrace;

static void assert_query_owned_world(Bindings *row) {
    const PrimeNeedSnapshot *need=bindings_need_view(row);
    PrimeNeedCellView view;
    Atom *origin=bindings_lookup_value_id(row,UINT64_C(984404)).skeleton;
    Atom *cell=bindings_lookup_value_id(row,UINT64_C(984405)).skeleton;
    Atom *watermark=bindings_lookup_value_id(row,UINT64_C(984406)).skeleton;
    assert(cell && cell->kind==ATOM_GROUNDED && cell->ground.gkind==GV_INT &&
        cell->ground.ival>0);
    assert(watermark && watermark->kind==ATOM_GROUNDED &&
        watermark->ground.gkind==GV_INT && watermark->ground.ival>0);
    assert(origin && need && need->max_storage_key==(uint64_t)watermark->ground.ival &&
        prime_need_snapshot_lookup(need,(uint64_t)cell->ground.ival,&view));
    assert(view.origin==origin && view.cached==origin &&
        view.cache_state==PRIME_NEED_CACHE_VALUE && need->max_storage_key>view.storage_key);
    assert(bindings_occurrence_token(row)==811u);
    const CettaWeightLedger *weights=bindings_weight_view(row);
    assert(weights && weights->length==1u && weights->claim_count==1u &&
        weights->last->occurrence==701u && weights->last->dependency==71u &&
        weights->last->value->kind==ATOM_GROUNDED &&
        weights->last->value->ground.gkind==GV_INT &&
        weights->last->value->ground.ival==3);
    assert(cetta_weight_ledger_handler(weights,701u)==901u);
}

static void seed_query_owned_world(Arena *arena, Bindings *seed) {
    Atom *origin=atom_expr2(arena,atom_symbol(arena,"owned-query-origin"),
        atom_int(arena,23));
    assert(bindings_add_id(seed,UINT64_C(984404),
        symbol_intern_cstr(g_symbols,"owned-query-origin-binding"),origin));
    PrimeNeedSnapshot empty,need;
    prime_need_snapshot_init(&empty);
    assert(prime_need_snapshot_begin(&empty));
    uint64_t cell=0u;
    assert(prime_need_snapshot_allocate(arena,&empty,origin,&need,&cell));
    assert(cell && cell<=INT64_MAX);
    assert(bindings_add_id(seed,UINT64_C(984405),
        symbol_intern_cstr(g_symbols,"owned-query-cell-binding"),atom_int(arena,(int64_t)cell)));
    assert(prime_need_snapshot_start_evaluation(arena,&need,cell,31u,&need));
    assert(prime_need_snapshot_resolve_value(arena,&need,cell,31u,origin,&need));
    assert(need.max_storage_key<=INT64_MAX-911u);
    need.max_storage_key+=911u;
    assert(bindings_add_id(seed,UINT64_C(984406),
        symbol_intern_cstr(g_symbols,"owned-query-watermark-binding"),
        atom_int(arena,(int64_t)need.max_storage_key)));
    CettaWeightLedger weights={0},prefix={0},selected={0};
    assert(cetta_weight_ledger_append(arena,&weights,701u,71u,atom_int(arena,3),&weights));
    assert(cetta_weight_ledger_handle(arena,&weights,&prefix,901u,&weights,&selected));
    bindings_prime_set(seed,&need,NULL,811u,NULL,&weights);
    /* Assert actual stored aliases before comparing their transported image. */
    assert(bindings_lookup_value_id(seed,UINT64_C(984404)).skeleton==origin);
    assert_query_owned_world(seed);
}

static void trace_query_clone(SpaceQueryCursor *cursor, Space *space,
        Arena *arena, Atom *value, QueryCloneTrace *trace,
        VarId retained_id, Atom *retained_atom) {
    *trace=(QueryCloneTrace){0};
    CettaNativeCostLedger cost; CettaNativeCostScope scope;
    cetta_native_cost_init(&cost);cetta_native_cost_scope_enter(&scope,&cost,1u);
    Bindings row;bindings_init(&row);
    for (;;) {
        assert(trace->length<1024u);
        SpaceQueryCursorStep step=space_query_cursor_step_with_budget(cursor,1u,&row);
        trace->steps[trace->length++]=step;
        if (step==SPACE_QUERY_CURSOR_END) break;
        assert(step==SPACE_QUERY_CURSOR_PROGRESS || step==SPACE_QUERY_CURSOR_ROW);
        if (step==SPACE_QUERY_CURSOR_ROW) {
            assert(trace->rows<16u);
            if (retained_atom) {
                assert(bindings_lookup_value_id(&row,retained_id).skeleton==retained_atom);
                assert_query_owned_world(&row);
            }
            trace->values[trace->rows]=bindings_apply(&row,arena,value);
            QueryWitnesses *witness=&trace->witnesses[trace->rows++];
            witness->space=space;
            assert(space_query_cursor_visit_witnesses(cursor,collect_query_witness,witness));
            bindings_free(&row);bindings_init(&row);
        }
    }
    trace->nodes=cost.total.counts[CETTA_COST_MATCH_NODE];
    trace->attempts=cost.total.counts[CETTA_COST_MATCH_ATTEMPT];
    trace->visits=cost.total.counts[CETTA_COST_INDEX_VISIT];
    trace->candidates=cost.total.counts[CETTA_COST_CANDIDATE_ROW];
    bindings_free(&row);cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&cost);
}

static void test_retained_query_clone(TermUniverse *universe, Arena *arena,
                                     SpaceEngine engine, bool overlay) {
    Space base,middle,outer;
    space_init_with_universe(&base,universe);
    assert(space_match_backend_try_set(&base,engine));
    SymbolId edge=symbol_intern_cstr(g_symbols,"clone-query-edge");
    Atom *e12=binary(arena,edge,atom_int(arena,1),atom_int(arena,2));
    space_add_atom_id(&base,store_atom(universe,e12));
    space_add_atom_id(&base,store_atom(universe,e12));
    space_add_atom_id(&base,store_atom(universe,
        binary(arena,edge,atom_int(arena,2),atom_int(arena,3))));
    Space *space=&base;
    if (overlay) {
        space_init_overlay(&middle,&base);space_init_overlay(&outer,&middle);
        assert(space_match_backend_try_set(&middle,engine));
        assert(space_match_backend_try_set(&outer,engine));
        space=&outer;
    }
    Atom *x=atom_var(arena,"clone-query-x"), *y=atom_var(arena,"clone-query-y");
    Atom *query=binary(arena,g_builtin_syms.pipe,
        binary(arena,g_builtin_syms.comma,
            binary(arena,edge,atom_int(arena,1),x),binary(arena,edge,x,y)),
        binary(arena,edge,atom_int(arena,1),y));
    SpaceQueryCursor *cursor=space_query_cursor_new(space,arena,query,NULL);
    QueryCloneTrace baseline;
    trace_query_clone(cursor,space,arena,y,&baseline,0u,NULL);
    space_query_cursor_free(cursor);
    assert(baseline.rows==4u);
    const int64_t values[]={3,3,2,2};
    for (size_t i=0u;i<4u;i++) {
        assert(atom_eq(baseline.values[i],atom_int(arena,values[i])));
        assert(baseline.witnesses[i].length==(i<2u ? 2u : 1u));
        assert(baseline.witnesses[i].indices[0]==i%2u);
        if (i<2u) assert(baseline.witnesses[i].indices[1]==2u);
    }
    for (size_t cut=0u;cut<=baseline.length;cut++) {
        cursor=space_query_cursor_new(space,arena,query,NULL);
        Bindings row;bindings_init(&row);
        for (size_t i=0u;i<cut;i++) {
            assert(space_query_cursor_step_with_budget(cursor,1u,&row)==baseline.steps[i]);
            bindings_free(&row);bindings_init(&row);
        }
        QueryReads before={0},after={0};
        assert(space_query_cursor_visit_reads(cursor,collect_query_read,&before));
        QueryWitnesses a={.space=space},b={.space=space};
        bool witness=space_query_cursor_visit_witnesses(cursor,collect_query_witness,&a);
        CettaNativeCostLedger cost;CettaNativeCostScope scope;
        cetta_native_cost_init(&cost);cetta_native_cost_scope_enter(&scope,&cost,1u);
        SpaceQueryCursor *copy=space_query_cursor_clone(cursor);
        assert(copy && !space_query_cursor_clone(NULL));
        assert(cost.total.counts[CETTA_COST_MATCH_NODE]==0u &&
            cost.total.counts[CETTA_COST_INDEX_VISIT]==0u);
        assert(space_query_cursor_step_with_budget(copy,0u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
        assert(cost.total.counts[CETTA_COST_MATCH_NODE]==0u);
        cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&cost);
        assert(space_query_cursor_visit_reads(copy,collect_query_read,&after));
        assert_query_reads_equal(&before,&after);
        assert(space_query_cursor_visit_witnesses(copy,collect_query_witness,&b)==witness);
        assert(a.length==b.length && memcmp(a.indices,b.indices,a.length*sizeof(*a.indices))==0);
        QueryCloneTrace first,second;
        trace_query_clone(cursor,space,arena,y,&first,0u,NULL);
        space_query_cursor_free(cursor);
        trace_query_clone(copy,space,arena,y,&second,0u,NULL);
        assert(first.length==second.length && first.rows==second.rows);
        assert(memcmp(first.steps,second.steps,first.length*sizeof(*first.steps))==0);
        assert(first.nodes==second.nodes && first.attempts==second.attempts &&
            first.visits==second.visits && first.candidates==second.candidates);
        for (size_t i=0u;i<first.rows;i++) {
            assert(atom_eq(first.values[i],second.values[i]));
            assert(first.witnesses[i].length==second.witnesses[i].length);
            assert(memcmp(first.witnesses[i].indices,second.witnesses[i].indices,
                first.witnesses[i].length*sizeof(*first.witnesses[i].indices))==0);
        }
        CettaStorageCensus census={0};space_query_cursor_storage_census(copy,&census);
        cetta_storage_finish(&census);assert(!census.incomplete && census.bytes);
        cetta_storage_census_free(&census);
        space_query_cursor_free(copy);bindings_free(&row);
    }
    if (overlay) {space_free(&outer);space_free(&middle);}
    space_free(&base);
    printf("PASS: query clones retain %zu cut positions, ordered duplicate witnesses and exact remaining work (%s)\n",
        baseline.length+1u,overlay ? "overlay" : "native");
}

static Atom *owned_query_pattern(Arena *arena, SymbolId edge, Atom **value,
                                 Atom **cached) {
    Atom *x=atom_var_with_spelling(arena,symbol_intern_cstr(g_symbols,"owned-query-x"),UINT64_C(984401));
    *value=atom_var_with_spelling(arena,symbol_intern_cstr(g_symbols,"owned-query-y"),UINT64_C(984402));
    *cached=atom_var_with_spelling(arena,symbol_intern_cstr(g_symbols,"owned-query-cached"),UINT64_C(984403));
    return binary(arena,g_builtin_syms.pipe,
        binary(arena,g_builtin_syms.comma,
            binary(arena,edge,atom_int(arena,1),x),binary(arena,edge,x,*value)),
        binary(arena,g_builtin_syms.comma,*cached,
            binary(arena,edge,atom_int(arena,1),*value)));
}

static void test_retained_query_relocation(TermUniverse *universe, Arena *external,
                                          SpaceEngine engine, bool overlay) {
    Space base,outer;
    space_init_with_universe(&base,universe);
    assert(space_match_backend_try_set(&base,engine));
    SymbolId edge=symbol_intern_cstr(g_symbols,"owned-query-edge");
    Atom *row12=binary(external,edge,atom_int(external,1),atom_int(external,2));
    AtomId id12=store_atom(universe,row12);
    space_add_atom_id(&base,id12);space_add_atom_id(&base,id12);
    space_add_atom_id(&base,store_atom(universe,
        binary(external,edge,atom_int(external,2),atom_int(external,3))));
    Space *space=&base;
    if (overlay) {
        space_init_overlay(&outer,&base);
        assert(space_match_backend_try_set(&outer,engine));space=&outer;
    }
    Atom *stored=space_get_at64(&base,0u);
    assert(stored && stored==space_get_at64(&base,1u));
    assert(atom_graph_is_closed_for_arena(external,stored));
    size_t cuts=1u;
    for (size_t cut=0u;cut<cuts;cut++) {
        Arena input,result,copied_input,copied_result;
        arena_init(&input);arena_init(&result);
        arena_init(&copied_input);arena_init(&copied_result);
        arena_set_runtime_kind(&input,CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_runtime_kind(&result,CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_runtime_kind(&copied_input,CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_runtime_kind(&copied_result,CETTA_ARENA_RUNTIME_KIND_EVAL);
        Atom *value,*cached;
        Atom *pattern=owned_query_pattern(&input,edge,&value,&cached);
        Bindings seed,row;bindings_init(&seed);bindings_init(&row);
        assert(bindings_add_var(&seed,cached,stored));
        assert(bindings_lookup_value_id(&seed,cached->var_id).skeleton==stored);
        seed_query_owned_world(&result,&seed);
        SpaceQueryCursor *cursor=space_query_cursor_new(space,&result,pattern,&seed);
        assert(cursor && space_query_cursor_incremental(cursor));
        for (size_t i=0u;i<cut;i++) {
            SpaceQueryCursorStep step=space_query_cursor_step_with_budget(cursor,1u,&row);
            assert(step==SPACE_QUERY_CURSOR_PROGRESS || step==SPACE_QUERY_CURSOR_ROW ||
                (i+1u==cut && step==SPACE_QUERY_CURSOR_END));
            bindings_free(&row);bindings_init(&row);
        }
        QueryReads before={0},after={0};
        assert(space_query_cursor_visit_reads(cursor,collect_query_read,&before));
        QueryWitnesses old_witness={.space=space},new_witness={.space=space};
        bool has_witness=space_query_cursor_visit_witnesses(cursor,collect_query_witness,&old_witness);
        Arena missing_input,missing_result;
        arena_init(&missing_input);arena_init(&missing_result);
        arena_set_runtime_kind(&missing_input,CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_runtime_kind(&missing_result,CETTA_ARENA_RUNTIME_KIND_EVAL);
        AtomDeepCopySession *missing=atom_deep_copy_session_new(&missing_result);
        const AtomDeepCopyArenaPair absent[]={
            {&input,&missing_input},{&result,&missing_result}};
        assert(atom_deep_copy_session_set_arena_map(missing,absent,2u));
        assert(!space_query_cursor_copy_to_arena(cursor,&missing_result,missing));
        atom_deep_copy_session_free(missing);
        arena_free(&missing_result);arena_free(&missing_input);
        AtomDeepCopySession *atoms=atom_deep_copy_session_new(&copied_result);
        const AtomDeepCopyArenaPair pairs[]={
            {&input,&copied_input},{&result,&copied_result},{external,external}};
        assert(atom_deep_copy_session_set_arena_map(atoms,pairs,3u));
        CettaNativeCostLedger cost;CettaNativeCostScope scope;
        cetta_native_cost_init(&cost);cetta_native_cost_scope_enter(&scope,&cost,1u);
        SpaceQueryCursor *copy=space_query_cursor_copy_to_arena(cursor,&copied_result,atoms);
        assert(copy);
        assert(cost.total.counts[CETTA_COST_MATCH_NODE]==0u &&
               cost.total.counts[CETTA_COST_MATCH_ATTEMPT]==0u &&
               cost.total.counts[CETTA_COST_INDEX_VISIT]==0u);
        assert(space_query_cursor_step_with_budget(copy,0u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
        assert(cost.total.counts[CETTA_COST_MATCH_NODE]==0u);
        cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&cost);
        assert(space_query_cursor_visit_reads(copy,collect_query_read,&after));
        assert_query_reads_equal(&before,&after);
        assert(space_query_cursor_visit_witnesses(copy,collect_query_witness,&new_witness)==has_witness);
        assert(old_witness.length==new_witness.length && memcmp(old_witness.indices,
            new_witness.indices,old_witness.length*sizeof(*old_witness.indices))==0);
        Atom *copied_value=atom_deep_copy_session_forwarded(atoms,value);
        assert(copied_value && atom_deep_copy_session_forwarded(atoms,stored)==stored);
        VarId retained_id=cached->var_id;
        QueryCloneTrace first,second;
        trace_query_clone(cursor,space,&result,value,&first,retained_id,stored);
        if (!cut) {cuts=first.length+1u;assert(first.rows==6u);}
        int64_t values[16];
        for (size_t i=0u;i<first.rows;i++) {
            assert(first.values[i]->kind==ATOM_GROUNDED && first.values[i]->ground.gkind==GV_INT);
            values[i]=first.values[i]->ground.ival;
        }
        space_query_cursor_free(cursor);bindings_free(&row);bindings_free(&seed);
        atom_deep_copy_session_free(atoms);
        arena_free(&result);arena_free(&input);
        assert(stored==space_get_at64(&base,0u) && stored==space_get_at64(&base,1u));
        trace_query_clone(copy,space,&copied_result,copied_value,&second,retained_id,stored);
        assert(first.length==second.length && first.rows==second.rows);
        assert(memcmp(first.steps,second.steps,first.length*sizeof(*first.steps))==0);
        assert(first.nodes==second.nodes && first.attempts==second.attempts &&
            first.visits==second.visits && first.candidates==second.candidates);
        for (size_t i=0u;i<first.rows;i++) {
            assert(second.values[i]->kind==ATOM_GROUNDED && second.values[i]->ground.gkind==GV_INT &&
                   second.values[i]->ground.ival==values[i]);
            assert(first.witnesses[i].length==second.witnesses[i].length);
            assert(memcmp(first.witnesses[i].indices,second.witnesses[i].indices,
                first.witnesses[i].length*sizeof(*first.witnesses[i].indices))==0);
        }
        CettaStorageCensus census={0};space_query_cursor_storage_census(copy,&census);
        cetta_storage_finish(&census);assert(!census.incomplete && census.bytes);
        cetta_storage_census_free(&census);space_query_cursor_free(copy);
        arena_free(&copied_result);arena_free(&copied_input);
    }
    if (overlay) space_free(&outer);
    space_free(&base);
    printf("PASS: owned query retires private arenas at %zu cuts, retaining external row aliases, witnesses and exact future work (%s)\n",
           cuts,overlay ? "overlay" : "native");
}

static void test_owned_query_authority(TermUniverse *universe, Arena *external,
                                       SpaceEngine engine) {
    const unsigned cuts[]={0u,1u,4u};
    SymbolId head=symbol_intern_cstr(g_symbols,"owned-authority-edge");
    for (unsigned changed=0u;changed<5u;changed++)
    for (unsigned empty=0u;empty<2u;empty++)
    for (unsigned before_copy=0u;before_copy<2u;before_copy++)
    for (size_t cut=0u;cut<sizeof(cuts)/sizeof(*cuts);cut++) {
        Space base,middle,outer,unrelated;
        space_init_with_universe(&base,universe);
        space_init_with_universe(&unrelated,universe);
        assert(space_match_backend_try_set(&base,engine));
        Atom *initial=empty ? atom_symbol(external,"owned-authority-other")
                            : edge_value(external,head,7);
        space_add_atom_id(&base,store_atom(universe,initial));
        space_init_overlay(&middle,&base);space_init_overlay(&outer,&middle);
        assert(space_match_backend_try_set(&middle,engine));
        assert(space_match_backend_try_set(&outer,engine));
        Arena input,result,copied_input,copied_result;
        arena_init(&input);arena_init(&result);
        arena_init(&copied_input);arena_init(&copied_result);
        arena_set_runtime_kind(&input,CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_runtime_kind(&result,CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_runtime_kind(&copied_input,CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_runtime_kind(&copied_result,CETTA_ARENA_RUNTIME_KIND_EVAL);
        Atom *x=atom_var(&input,"owned-authority-x");
        Atom *pattern=binary(&input,head,x,atom_var(&input,"owned-authority-y"));
        SpaceQueryCursor *cursor=space_query_cursor_new(&outer,&result,pattern,NULL);
        assert(cursor);
        QueryReads before={0},after={0};
        assert(space_query_cursor_visit_reads(cursor,collect_query_read,&before));
        assert(before.length==3u);
        Bindings row;bindings_init(&row);
        unsigned delivered=0u;
        for (unsigned i=0u;i<cuts[cut];i++) {
            SpaceQueryCursorStep step=space_query_cursor_step_with_budget(cursor,1u,&row);
            assert(step==SPACE_QUERY_CURSOR_PROGRESS || step==SPACE_QUERY_CURSOR_ROW ||
                   step==SPACE_QUERY_CURSOR_END);
            delivered+=step==SPACE_QUERY_CURSOR_ROW;
            bindings_free(&row);bindings_init(&row);
        }
        Space *target=changed==0u || changed==3u ? &base :
            changed==1u ? &middle : changed==2u ? &outer : &unrelated;
        if (before_copy) {
            if (changed!=4u) assert(space_remove(target,initial));
            space_add_atom_id(target,store_atom(universe,
                changed==3u ? initial : edge_value(external,head,8)));
        }
        AtomDeepCopySession *atoms=atom_deep_copy_session_new(&copied_result);
        const AtomDeepCopyArenaPair pairs[]={
            {&input,&copied_input},{&result,&copied_result},{external,external}};
        assert(atom_deep_copy_session_set_arena_map(atoms,pairs,3u));
        SpaceQueryCursor *copy=space_query_cursor_copy_to_arena(cursor,&copied_result,atoms);
        assert(copy);
        Atom *copied_x=atom_deep_copy_session_forwarded(atoms,x);
        assert(copied_x && space_query_cursor_visit_reads(copy,collect_query_read,&after));
        assert_query_reads_equal(&before,&after);
        space_query_cursor_free(cursor);bindings_free(&row);
        atom_deep_copy_session_free(atoms);arena_free(&result);arena_free(&input);
        bindings_init(&row);
        if (!before_copy) {
            if (changed!=4u) assert(space_remove(target,initial));
            space_add_atom_id(target,store_atom(universe,
                changed==3u ? initial : edge_value(external,head,8)));
        }
        CettaNativeCostLedger cost;CettaNativeCostScope scope;
        cetta_native_cost_init(&cost);cetta_native_cost_scope_enter(&scope,&cost,1u);
        assert(space_query_cursor_step_with_budget(copy,0u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
        assert(cost.total.work==0u);
        if (changed!=4u) {
            CettaStorageCensus retained={0};
            for (unsigned repeat=0u;repeat<3u;repeat++) {
                assert(space_query_cursor_step_with_budget(copy,1u,&row)==SPACE_QUERY_CURSOR_INVALIDATED);
                assert(cost.total.work==0u);
                after=(QueryReads){0};
                assert(space_query_cursor_visit_reads(copy,collect_query_read,&after));
                assert_query_reads_equal(&before,&after);
                CettaStorageCensus census={0};
                space_query_cursor_storage_census(copy,&census);cetta_storage_finish(&census);
                assert(!census.incomplete && census.bytes);
                if (repeat) assert(census.bytes==retained.bytes &&
                    census.retained==retained.retained && census.length==retained.length);
                else {retained.bytes=census.bytes;retained.retained=census.retained;
                      retained.length=census.length;}
                cetta_storage_census_free(&census);
                assert(space_query_cursor_step_with_budget(copy,0u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
                assert(cost.total.work==0u);
            }
        } else {
            QueryCloneTrace trace;
            trace_query_clone(copy,&outer,&copied_result,copied_x,&trace,0u,NULL);
            assert(trace.rows+delivered==(empty ? 0u : 1u));
            for (size_t i=0u;i<trace.rows;i++)
                assert(atom_eq(trace.values[i],atom_int(&copied_result,7)));
        }
        cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&cost);
        bindings_free(&row);space_query_cursor_free(copy);
        arena_free(&copied_result);arena_free(&copied_input);
        space_free(&outer);space_free(&middle);space_free(&base);space_free(&unrelated);
    }
    puts("PASS: owned query preserves stale and empty read authority across private retirement, masking and equal replacement");
}

static void test_retained_overlay_query_authority(TermUniverse *universe, Arena *scratch,
                                                 SpaceEngine engine) {
    SymbolId head=symbol_intern_cstr(g_symbols,"overlay-query-edge");
    Atom *x=atom_var_with_spelling(scratch,
        symbol_intern_cstr(g_symbols,"overlay-query-x"),UINT64_C(81101));
    Atom *y=atom_var_with_spelling(scratch,
        symbol_intern_cstr(g_symbols,"overlay-query-y"),UINT64_C(81102));
    Atom *query=binary(scratch,head,x,y);
    Atom *seven=edge_value(scratch,head,7), *eight=edge_value(scratch,head,8);
    for (unsigned changed=0u;changed<4u;changed++)
    for (unsigned empty=0u;empty<2u;empty++)
    for (unsigned cut=0u;cut<2u;cut++) {
        Space base,middle,outer,unrelated;
        space_init_with_universe(&base,universe);
        assert(space_match_backend_try_set(&base,engine));
        space_init_with_universe(&unrelated,universe);
        Atom *initial=empty?atom_symbol(scratch,"overlay-query-other"):seven;
        space_add_atom_id(&base,store_atom(universe,initial));
        space_init_overlay(&middle,&base);space_init_overlay(&outer,&middle);
        assert(space_match_backend_try_set(&middle,engine));
        assert(space_match_backend_try_set(&outer,engine));
        SpaceQueryCursor *cursor=space_query_cursor_new(&outer,scratch,query,NULL);
        assert(cursor);
        QueryReads reads={0};
        assert(space_query_cursor_visit_reads(cursor,collect_query_read,&reads));
        assert(reads.length==3u && reads.reads[0].space==&outer &&
            reads.reads[1].space==&middle && reads.reads[2].space==&base);
        for(size_t i=0u;i<reads.length;i++) assert(space_read_token_is_current(reads.reads[i]));
        CettaNativeCostLedger cost;
        CettaNativeCostScope scope;
        cetta_native_cost_init(&cost);cetta_native_cost_scope_enter(&scope,&cost,1u);
        Bindings row;bindings_init(&row);
        for(unsigned i=0u;i<cut;i++)
            assert(space_query_cursor_step_with_budget(cursor,1u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
        uint64_t before=cost.total.work;
        Space *target=changed==0u?&base:changed==1u?&middle:changed==2u?&outer:&unrelated;
        if(changed<3u) assert(space_remove(target,initial));
        space_add_atom_id(target,store_atom(universe,eight));
        SpaceQueryCursor *copy=space_query_cursor_clone(cursor);
        assert(copy);
        space_query_cursor_free(cursor);cursor=copy;
        /* Copying bindings has its own representation charge. The following
         * zero grant and invalidation still accept no query/matcher work. */
        before=cost.total.work;
        assert(space_query_cursor_step_with_budget(cursor,0u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
        assert(cost.total.work==before);
        if(changed<3u) {
            for(unsigned repeat=0u;repeat<3u;repeat++)
                assert(space_query_cursor_step_with_budget(cursor,1u,&row)==SPACE_QUERY_CURSOR_INVALIDATED);
            assert(cost.total.work==before);
            QueryReads after={0};
            assert(space_query_cursor_visit_reads(cursor,collect_query_read,&after));
            assert(after.length==reads.length);
            for(size_t i=0u;i<after.length;i++) {
                assert(after.reads[i].instance_id==reads.reads[i].instance_id);
                assert(after.reads[i].revision==reads.reads[i].revision);
            }
        } else {
            unsigned rows=0u;bool ended=false;
            for(unsigned i=0u;i<30u;i++) {
                SpaceQueryCursorStep step=space_query_cursor_step(cursor,&row);
                if(step==SPACE_QUERY_CURSOR_END) {ended=true;break;}
                assert(step==SPACE_QUERY_CURSOR_PROGRESS || step==SPACE_QUERY_CURSOR_ROW);
                if(step==SPACE_QUERY_CURSOR_ROW) {
                    assert(atom_eq(bindings_apply(&row,scratch,x),atom_int(scratch,7)));
                    rows++;bindings_free(&row);bindings_init(&row);
                }
            }
            assert(ended && rows==(empty?0u:1u));
        }
        cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&cost);
        space_query_cursor_free(cursor);bindings_free(&row);
        space_free(&outer);space_free(&middle);space_free(&base);space_free(&unrelated);
    }

    /* Frozen visibility, masking and duplicate payloads are independent of
     * source-current validation. Compare the actual canonical overlay view. */
    Space base,overlay,nested;
    space_init_with_universe(&base,universe);
    assert(space_match_backend_try_set(&base,engine));
    space_add_atom_id(&base,store_atom(universe,seven));
    space_add_atom_id(&base,store_atom(universe,seven));
    space_add_atom_id(&base,store_atom(universe,eight));
    space_init_overlay(&overlay,&base);
    assert(space_match_backend_try_set(&overlay,engine));
    assert(space_remove(&overlay,eight));
    space_add_atom_id(&overlay,store_atom(universe,edge_value(scratch,head,9)));
    space_add_atom_id(&base,store_atom(universe,edge_value(scratch,head,10)));
    space_init_overlay(&nested,&overlay);
    assert(space_match_backend_try_set(&nested,engine));
    space_add_atom_id(&nested,store_atom(universe,edge_value(scratch,head,11)));
    BindingSet oracle;
    assert(space_query_bindings(&nested,scratch,query,NULL,&oracle));
    assert(oracle.len==4u);
    const int64_t expected[]={7,7,9,11};
    for(unsigned i=0u;i<4u;i++)
        assert(atom_eq(bindings_apply(&oracle.items[i],scratch,x),atom_int(scratch,expected[i])));
    SpaceQueryCursor *cursor=space_query_cursor_new(&nested,scratch,query,NULL);
    Bindings row;bindings_init(&row);
    unsigned rows=0u;bool ended=false;
    for(unsigned i=0u;i<80u;i++) {
        SpaceQueryCursorStep step=space_query_cursor_step(cursor,&row);
        if(step==SPACE_QUERY_CURSOR_END) {ended=true;break;}
        assert(step==SPACE_QUERY_CURSOR_PROGRESS || step==SPACE_QUERY_CURSOR_ROW);
        if(step==SPACE_QUERY_CURSOR_ROW) {
            assert(rows<oracle.len && atom_eq(bindings_apply(&row,scratch,x),
                bindings_apply(&oracle.items[rows],scratch,x)));
            rows++;bindings_free(&row);bindings_init(&row);
        }
    }
    assert(ended && rows==oracle.len);
    CettaStorageCensus census={0};space_query_cursor_storage_census(cursor,&census);
    cetta_storage_finish(&census);assert(!census.incomplete && census.bytes);
    cetta_storage_census_free(&census);
    space_query_cursor_free(cursor);bindings_free(&row);binding_set_free(&oracle);
    /* Cyclic authority cannot be captured as a complete read. */
    base.overlay_base=&nested;
    assert(!space_query_cursor_new(&nested,scratch,query,NULL));
    base.overlay_base=NULL;
    space_free(&nested);space_free(&overlay);space_free(&base);
    puts("PASS: retained overlay queries keep inherited authority, masking and duplicate rows");
}

static void test_overlay_occurrence_scan(TermUniverse *universe, Arena *scratch,
                                         SpaceEngine engine) {
    Space base, middle, outer;
    space_init_with_universe(&base, universe);
    assert(space_match_backend_try_set(&base,engine));
    Atom *seven=atom_int(scratch,7), *eight=atom_int(scratch,8);
    space_add_atom_id(&base,store_atom(universe,seven));
    space_add_atom_id(&base,store_atom(universe,seven));
    space_add_atom_id(&base,store_atom(universe,eight));
    space_init_overlay(&middle,&base);
    assert(space_match_backend_try_set(&middle,engine));
    assert(space_remove(&middle,eight));
    space_add_atom_id(&middle,store_atom(universe,atom_int(scratch,9)));
    space_add_atom_id(&middle,store_atom(universe,seven));
    space_init_overlay(&outer,&middle);
    assert(space_match_backend_try_set(&outer,engine));
    space_add_atom_id(&outer,store_atom(universe,atom_int(scratch,10)));

    SpaceOccurrenceCursor scan, exact, unstarted;
    assert(space_occurrence_cursor_init_scan(&outer,&scan));
    assert(scan.logical_scan && !scan.pinned && !scan.occurrences);
    assert(scan.ceiling==5u && scan.logical_reads.base_count==2u);
    CettaIndex index=UINT64_MAX;
    assert(space_occurrence_cursor_next(&scan,&index)==SPACE_OCCURRENCE_CURSOR_ITEM && index==0u);
    assert(atom_eq(space_occurrence_cursor_atom(&scan,index),seven));
    assert(space_occurrence_cursor_clone(&scan,&exact));
    assert(space_occurrence_cursor_clone_unstarted(&scan,&unstarted));
    assert(exact.full_next==1u && unstarted.full_next==0u);
    for(size_t i=0u;i<scan.logical_reads.base_count;i++) {
        assert(exact.logical_reads.bases[i].revision==scan.logical_reads.bases[i].revision);
        assert(unstarted.logical_reads.bases[i].instance_id==scan.logical_reads.bases[i].instance_id);
    }
    space_occurrence_cursor_release(&scan);
    const int64_t values[]={7,7,9,7,10};
    for(unsigned i=1u;i<5u;i++) {
        assert(space_occurrence_cursor_next(&exact,&index)==SPACE_OCCURRENCE_CURSOR_ITEM && index==i);
        assert(atom_eq(space_occurrence_cursor_atom(&exact,index),atom_int(scratch,values[i])));
    }
    assert(space_occurrence_cursor_next(&exact,&index)==SPACE_OCCURRENCE_CURSOR_END);
    for(unsigned i=0u;i<5u;i++) {
        assert(space_occurrence_cursor_next(&unstarted,&index)==SPACE_OCCURRENCE_CURSOR_ITEM && index==i);
        assert(atom_eq(space_occurrence_cursor_atom(&unstarted,index),atom_int(scratch,values[i])));
    }
    assert(space_occurrence_cursor_next(&unstarted,&index)==SPACE_OCCURRENCE_CURSOR_END);
    space_occurrence_cursor_release(&exact);space_occurrence_cursor_release(&unstarted);

    assert(space_occurrence_cursor_init_scan(&outer,&scan));
    assert(space_occurrence_cursor_next(&scan,&index)==SPACE_OCCURRENCE_CURSOR_ITEM);
    SpaceReadToken captured=scan.logical_reads.bases[1];
    assert(space_remove(&base,seven));
    assert(space_occurrence_cursor_clone(&scan,&exact));
    assert(space_occurrence_cursor_clone_unstarted(&scan,&unstarted));
    assert(exact.logical_reads.bases[1].revision==captured.revision &&
           unstarted.logical_reads.bases[1].revision==captured.revision);
    for(unsigned repeat=0u;repeat<3u;repeat++) {
        assert(space_occurrence_cursor_next(&scan,&index)==SPACE_OCCURRENCE_CURSOR_INVALIDATED);
        assert(space_occurrence_cursor_next(&exact,&index)==SPACE_OCCURRENCE_CURSOR_INVALIDATED);
        assert(space_occurrence_cursor_next(&unstarted,&index)==SPACE_OCCURRENCE_CURSOR_INVALIDATED);
        assert(scan.full_next==1u && exact.full_next==1u && unstarted.full_next==0u);
        assert(!space_occurrence_cursor_atom(&scan,0u));
    }
    CettaStorageCensus census={0};space_occurrence_cursor_storage_census(&scan,&census);
    cetta_storage_finish(&census);assert(!census.incomplete && census.bytes);
    cetta_storage_census_free(&census);
    space_occurrence_cursor_release(&scan);space_occurrence_cursor_release(&exact);
    space_occurrence_cursor_release(&unstarted);

    /* Only the explicit live scan gains overlay support. Indexed cursors
     * retain the established captured-row contract used by ordinary hosts. */
    assert(!space_occurrence_cursor_init(&outer,seven,&scan));
    assert(space_occurrence_cursor_init_scan(&base,&scan));
    assert(scan.pinned && !scan.logical_scan && !scan.logical_reads.bases);
    space_occurrence_cursor_release(&scan);
    const SpaceMatchBackendOps *native=base.match_backend.ops;
    SpaceMatchBackendOps opaque=*native;
    Space *levels[]={&outer,&middle,&base};
    for(unsigned i=0u;i<3u;i++) {
        levels[i]->match_backend.ops=&opaque;
        assert(!space_occurrence_cursor_init_scan(&outer,&scan));
        SpaceQueryCursor *query=space_query_cursor_new(&outer,scratch,seven,NULL);
        assert(query && !space_query_cursor_incremental(query));
        space_query_cursor_free(query);
        levels[i]->match_backend.ops=native;
    }
    space_free(&outer);space_free(&middle);space_free(&base);
    puts("PASS: logical overlay scans preserve masks, duplicate positions, clone authority and invalidated progress");
}

static void test_incremental_overlay_query(TermUniverse *universe, Arena *scratch,
                                           SpaceEngine engine) {
    Space base, overlay, outer;
    space_init_with_universe(&base,universe);
    assert(space_match_backend_try_set(&base,engine));
    Atom *head=atom_symbol(scratch,"overlay-deep-node");
    Atom *x=atom_var_with_spelling(scratch,
        symbol_intern_cstr(g_symbols,"overlay-deep-x"),UINT64_C(81103));
    Atom *pattern=x, *value=atom_int(scratch,7);
    for(unsigned i=0u;i<96u;i++) {
        pattern=atom_expr2(scratch,head,pattern);
        value=atom_expr2(scratch,head,value);
    }
    for(unsigned i=0u;i<24u;i++) space_add_atom_id(&base,store_atom(universe,value));
    space_init_overlay(&overlay,&base);space_init_overlay(&outer,&overlay);
    assert(space_match_backend_try_set(&overlay,engine));
    assert(space_match_backend_try_set(&outer,engine));
    uint64_t counts[3]={0},work[3]={0};
    const uint64_t grants[]={1u,17u,UINT64_MAX};
    for(unsigned run=0u;run<3u;run++) {
        CettaNativeCostLedger cost;CettaNativeCostScope scope;
        cetta_native_cost_init(&cost);cetta_native_cost_scope_enter(&scope,&cost,1u);
        SpaceQueryCursor *cursor=space_query_cursor_new(&outer,scratch,pattern,NULL);
        assert(cursor && space_query_cursor_incremental(cursor));
        assert(cost.total.counts[CETTA_COST_INDEX_VISIT]==0u &&
               cost.total.counts[CETTA_COST_MATCH_ATTEMPT]==0u);
        Bindings row;bindings_init(&row);
        unsigned rows=0u;bool ended=false;
        for(unsigned steps=0u;steps<8000u;steps++) {
            uint64_t before=cost.total.work, nodes=cost.total.counts[CETTA_COST_MATCH_NODE];
            assert(space_query_cursor_step_with_budget(cursor,0u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
            assert(cost.total.work==before);
            SpaceQueryCursorStep step=space_query_cursor_step_with_budget(cursor,grants[run],&row);
            assert(cost.total.counts[CETTA_COST_MATCH_NODE]-nodes<=grants[run]);
            if(step==SPACE_QUERY_CURSOR_END) {ended=true;break;}
            assert(step==SPACE_QUERY_CURSOR_PROGRESS || step==SPACE_QUERY_CURSOR_ROW);
            if(step!=SPACE_QUERY_CURSOR_ROW) continue;
            assert(atom_eq(bindings_apply(&row,scratch,x),atom_int(scratch,7)));
            QueryWitnesses witnesses={.space=&outer};
            assert(space_query_cursor_visit_witnesses(cursor,collect_query_witness,&witnesses));
            assert(witnesses.length==1u && witnesses.indices[0]==rows);
            rows++;
            /* A first answer has inspected only its own physical row. */
            assert(cost.total.counts[CETTA_COST_INDEX_VISIT]==rows &&
                   cost.total.counts[CETTA_COST_MATCH_ATTEMPT]==rows);
            bindings_free(&row);bindings_init(&row);
        }
        assert(ended && rows==24u);
        counts[run]=cost.total.counts[CETTA_COST_MATCH_NODE];work[run]=cost.total.work;
        space_query_cursor_free(cursor);bindings_free(&row);
        cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&cost);
    }
    assert(counts[0]>4000u && counts[0]==counts[1] && counts[1]==counts[2]);
    assert(work[0]==work[1] && work[1]==work[2]);

    /* Invalidation after a partial candidate keeps its paid node frontier. */
    CettaNativeCostLedger cost;CettaNativeCostScope scope;
    cetta_native_cost_init(&cost);cetta_native_cost_scope_enter(&scope,&cost,1u);
    SpaceQueryCursor *cursor=space_query_cursor_new(&outer,scratch,pattern,NULL);
    Bindings row;bindings_init(&row);
    for(unsigned step=0u;step<9u;step++)
        assert(space_query_cursor_step_with_budget(cursor,1u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
    assert(cost.total.counts[CETTA_COST_MATCH_ATTEMPT]==1u &&
           cost.total.counts[CETTA_COST_MATCH_NODE]>0u);
    uint64_t before=cost.total.work;
    space_add_atom_id(&base,store_atom(universe,value));
    for(unsigned repeat=0u;repeat<3u;repeat++) {
        assert(space_query_cursor_step_with_budget(cursor,1u,&row)==SPACE_QUERY_CURSOR_INVALIDATED);
        assert(cost.total.work==before);
    }
    CettaStorageCensus census={0};space_query_cursor_storage_census(cursor,&census);
    cetta_storage_finish(&census);assert(!census.incomplete && census.bytes);
    cetta_storage_census_free(&census);
    space_query_cursor_free(cursor);bindings_free(&row);
    cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&cost);
    space_free(&outer);space_free(&overlay);space_free(&base);
    puts("PASS: overlay query preparation and inner matcher resume without eager rows or repeated node work");
}

static void test_overlay_query_join(TermUniverse *universe, Arena *scratch,
                                   SpaceEngine engine) {
    Space base,middle,outer;
    space_init_with_universe(&base,universe);
    assert(space_match_backend_try_set(&base,engine));
    SymbolId edge=symbol_intern_cstr(g_symbols,"overlay-join-edge");
    Atom *e12=binary(scratch,edge,atom_int(scratch,1),atom_int(scratch,2));
    Atom *e23=binary(scratch,edge,atom_int(scratch,2),atom_int(scratch,3));
    space_add_atom_id(&base,store_atom(universe,e12));
    space_init_overlay(&middle,&base);
    assert(space_match_backend_try_set(&middle,engine));
    space_add_atom_id(&middle,store_atom(universe,e12));
    space_add_atom_id(&middle,store_atom(universe,e23));
    space_init_overlay(&outer,&middle);
    assert(space_match_backend_try_set(&outer,engine));
    space_add_atom_id(&outer,store_atom(universe,e23));
    Atom *x=atom_var_with_spelling(scratch,symbol_intern_cstr(g_symbols,"overlay-join-x"),81104);
    Atom *y=atom_var_with_spelling(scratch,symbol_intern_cstr(g_symbols,"overlay-join-y"),81105);
    Atom *second=binary(scratch,edge,x,y);
    Atom *query=binary(scratch,g_builtin_syms.comma,
        binary(scratch,edge,atom_int(scratch,1),x),
        binary(scratch,g_builtin_syms.pipe,second,second));
    BindingSet oracle;assert(space_query_bindings(&outer,scratch,query,NULL,&oracle));
    assert(oracle.len==8u);
    SpaceQueryCursor *cursor=space_query_cursor_new(&outer,scratch,query,NULL);
    assert(cursor && space_query_cursor_incremental(cursor));
    Bindings row;bindings_init(&row);unsigned rows=0u;bool ended=false;
    for(unsigned poll=0u;poll<500u;poll++) {
        SpaceQueryCursorStep step=space_query_cursor_step_with_budget(cursor,1u,&row);
        if(step==SPACE_QUERY_CURSOR_END) {ended=true;break;}
        assert(step==SPACE_QUERY_CURSOR_PROGRESS || step==SPACE_QUERY_CURSOR_ROW);
        if(step!=SPACE_QUERY_CURSOR_ROW) continue;
        assert(rows<8u && atom_eq(bindings_apply(&row,scratch,x),atom_int(scratch,2)) &&
            atom_eq(bindings_apply(&row,scratch,y),atom_int(scratch,3)));
        assert(atom_eq(bindings_apply(&row,scratch,x),bindings_apply(&oracle.items[rows],scratch,x)) &&
            atom_eq(bindings_apply(&row,scratch,y),bindings_apply(&oracle.items[rows],scratch,y)));
        /* A zero grant keeps the last row's witness view valid. */
        assert(space_query_cursor_step_with_budget(cursor,0u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
        QueryWitnesses witnesses={.space=&outer};
        assert(space_query_cursor_visit_witnesses(cursor,collect_query_witness,&witnesses));
        assert(witnesses.length==2u && witnesses.indices[0]==rows/4u &&
               witnesses.indices[1]==2u+rows%2u);
        rows++;bindings_free(&row);bindings_init(&row);
    }
    assert(ended && rows==8u);
    space_query_cursor_free(cursor);binding_set_free(&oracle);
    /* Change an inherited source after one joined row; no remaining
     * correlated branch may publish under the obsolete complete read. */
    cursor=space_query_cursor_new(&outer,scratch,query,NULL);
    unsigned poll=0u;
    for(;poll<100u;poll++) {
        SpaceQueryCursorStep step=space_query_cursor_step(cursor,&row);
        if(step==SPACE_QUERY_CURSOR_ROW) break;
        assert(step==SPACE_QUERY_CURSOR_PROGRESS);
    }
    assert(poll<100u);
    assert(space_remove(&middle,e23));
    assert(space_query_cursor_step(cursor,&row)==SPACE_QUERY_CURSOR_INVALIDATED);
    space_query_cursor_free(cursor);bindings_free(&row);
    space_free(&outer);space_free(&middle);space_free(&base);
    puts("PASS: overlay conjunctions retain correlated bindings, physical duplicate joins and child-read authority");
}

static unsigned fallback_query_calls;
static SymbolId fallback_query_refuse;
static SymbolId fallback_query_empty;

static void query_fallback_with_refusal(Space *space, Arena *arena,
        Atom **patterns, CettaExprLen length, const Bindings *seed, BindingSet *out) {
    (void)space; (void)arena;
    bool refuse=false;
    fallback_query_calls++;
    space_match_backend_clear_error();
    binding_set_init(out);
    for (CettaExprIndex i=0u;i<length;i++) {
        if (atom_is_symbol_id(patterns[i], fallback_query_empty)) return;
        refuse |= atom_is_symbol_id(patterns[i], fallback_query_refuse);
    }
    Bindings unit;
    bindings_init(&unit);
    assert(binding_set_push(out, seed ? seed : &unit));
    bindings_free(&unit);
    /* This failing invocation's partial row is deliberately unqualified.
     * A later successful invocation clears the shared diagnostic channel. */
    if (refuse)
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
}

static void test_retained_query_fallback_refusal(TermUniverse *universe, Arena *scratch) {
    Space space;
    space_init_with_universe(&space, universe);
    const SpaceMatchBackendOps *native_ops=space.match_backend.ops;
    SpaceMatchBackendOps custom=*native_ops;
    custom.query_conjunction=query_fallback_with_refusal;
    space.match_backend.ops=&custom;
    fallback_query_refuse=symbol_intern_cstr(g_symbols,"cursor-refuse");
    fallback_query_empty=symbol_intern_cstr(g_symbols,"cursor-empty");
    Atom *refuse=atom_symbol_id(scratch,fallback_query_refuse);
    Atom *empty=atom_symbol_id(scratch,fallback_query_empty);
    Atom *accept=atom_symbol(scratch,"cursor-accept");
    Atom *choice=binary(scratch,g_builtin_syms.pipe,refuse,accept);
    struct { Atom *query; unsigned rows; unsigned calls; bool complete; } cases[]={
        {choice,1u,2u,false},
        {binary(scratch,g_builtin_syms.pipe,accept,refuse),1u,2u,false},
        {binary(scratch,g_builtin_syms.comma,
            binary(scratch,g_builtin_syms.pipe,empty,accept),choice),1u,4u,false},
        {refuse,0u,1u,false},
        {binary(scratch,g_builtin_syms.comma,refuse,accept),0u,1u,false},
        {binary(scratch,g_builtin_syms.pipe,
            binary(scratch,g_builtin_syms.comma,refuse,accept),accept),1u,2u,false},
        {binary(scratch,g_builtin_syms.comma,choice,accept),1u,3u,false},
        {binary(scratch,g_builtin_syms.pipe,empty,accept),1u,2u,true},
        {binary(scratch,g_builtin_syms.pipe,empty,empty),0u,2u,true},
    };
    for (size_t i=0u;i<sizeof(cases)/sizeof(cases[0]);i++) {
        fallback_query_calls=0u;
        SpaceQueryCursor *cursor=space_query_cursor_new(&space,scratch,cases[i].query,NULL);
        assert(cursor && !space_query_cursor_incremental(cursor));
        Bindings row;
        bindings_init(&row);
        assert(space_query_cursor_step_with_budget(cursor,0u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
        assert(fallback_query_calls==0u);
        SpaceQueryCursor *copy=space_query_cursor_clone(cursor);
        assert(copy && fallback_query_calls==0u);
        space_query_cursor_free(cursor);cursor=copy;
        SpaceQueryCursorStep prepared=space_query_cursor_step(cursor,&row);
        assert(prepared==SPACE_QUERY_CURSOR_PROGRESS || prepared==SPACE_QUERY_CURSOR_INCOMPLETE);
        copy=space_query_cursor_clone(cursor);
        assert(copy && fallback_query_calls==cases[i].calls);
        space_query_cursor_free(cursor);cursor=copy;
        unsigned rows=0u;
        SpaceQueryCursorStep step=SPACE_QUERY_CURSOR_PROGRESS;
        for (unsigned poll=0u;poll<8u;poll++) {
            step=space_query_cursor_step(cursor,&row);
            if (step==SPACE_QUERY_CURSOR_ROW) {
                rows++;
                bindings_free(&row);bindings_init(&row);
            } else if (step!=SPACE_QUERY_CURSOR_PROGRESS) break;
        }
        if (rows!=cases[i].rows || step!=(cases[i].complete
                ? SPACE_QUERY_CURSOR_END : SPACE_QUERY_CURSOR_INCOMPLETE))
            fprintf(stderr,"fallback case %zu: rows=%u terminal=%u calls=%u\n",
                i,rows,(unsigned)step,fallback_query_calls);
        assert(rows==cases[i].rows);
        assert(step==(cases[i].complete ? SPACE_QUERY_CURSOR_END : SPACE_QUERY_CURSOR_INCOMPLETE));
        assert(fallback_query_calls==cases[i].calls);
        for (unsigned poll=0u;poll<3u;poll++) {
            copy=space_query_cursor_clone(cursor);
            assert(copy && fallback_query_calls==cases[i].calls);
            space_query_cursor_free(cursor);cursor=copy;
            space_match_backend_clear_error();
            assert(space_query_cursor_step(cursor,&row)==step);
            assert(fallback_query_calls==cases[i].calls);
        }
        if (!cases[i].complete) {
            space_add_atom_id(&space,store_atom(universe,atom_symbol(scratch,"mutation")));
            assert(space_query_cursor_step(cursor,&row)==SPACE_QUERY_CURSOR_INVALIDATED);
            assert(fallback_query_calls==cases[i].calls);
        }
        bindings_free(&row);
        space_query_cursor_free(cursor);
    }
    space.match_backend.ops=native_ops;
    space_match_backend_clear_error();
    space_free(&space);
    puts("PASS: atomic fallback preserves complete sibling rows and sticky unreplayed refusal");
}

typedef struct {
    SpaceQueryCursorStep steps[8];
    size_t length,rows;
    unsigned calls;
} QueryFallbackTrace;

static QueryFallbackTrace trace_owned_fallback(SpaceQueryCursor *cursor) {
    QueryFallbackTrace trace={0};
    unsigned before=fallback_query_calls;
    Bindings row;bindings_init(&row);
    for (;;) {
        assert(trace.length<sizeof(trace.steps)/sizeof(*trace.steps));
        SpaceQueryCursorStep step=space_query_cursor_step_with_budget(cursor,1u,&row);
        trace.steps[trace.length++]=step;
        if (step==SPACE_QUERY_CURSOR_END || step==SPACE_QUERY_CURSOR_INCOMPLETE) break;
        assert(step==SPACE_QUERY_CURSOR_PROGRESS || step==SPACE_QUERY_CURSOR_ROW);
        if (step==SPACE_QUERY_CURSOR_ROW) {
            assert_query_owned_world(&row);trace.rows++;
            bindings_free(&row);bindings_init(&row);
        }
    }
    bindings_free(&row);trace.calls=fallback_query_calls-before;
    return trace;
}

static void test_owned_query_fallback(TermUniverse *universe, Arena *external) {
    for (unsigned variant=0u;variant<3u;variant++)
    for (unsigned cut=0u;cut<4u;cut++) {
        Space space;
        space_init_with_universe(&space,universe);
        const SpaceMatchBackendOps *native_ops=space.match_backend.ops;
        SpaceMatchBackendOps custom=*native_ops;
        custom.query_conjunction=query_fallback_with_refusal;
        space.match_backend.ops=&custom;
        Arena input,result,copied_input,copied_result;
        arena_init(&input);arena_init(&result);
        arena_init(&copied_input);arena_init(&copied_result);
        arena_set_runtime_kind(&input,CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_runtime_kind(&result,CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_runtime_kind(&copied_input,CETTA_ARENA_RUNTIME_KIND_EVAL);
        arena_set_runtime_kind(&copied_result,CETTA_ARENA_RUNTIME_KIND_EVAL);
        Atom *left=atom_symbol_id(&input,variant ? fallback_query_empty : fallback_query_refuse);
        Atom *right=variant==1u ? atom_symbol_id(&input,fallback_query_empty)
                              : atom_symbol(&input,"cursor-accept");
        Atom *pattern=binary(&input,g_builtin_syms.pipe,left,right);
        Bindings seed,row;bindings_init(&seed);bindings_init(&row);
        seed_query_owned_world(&result,&seed);
        fallback_query_calls=0u;
        SpaceQueryCursor *cursor=space_query_cursor_new(&space,&result,pattern,&seed);
        assert(cursor && !space_query_cursor_incremental(cursor));
        unsigned delivered=0u;
        for (unsigned i=0u;i<cut;i++) {
            SpaceQueryCursorStep step=space_query_cursor_step_with_budget(cursor,1u,&row);
            assert(step==SPACE_QUERY_CURSOR_PROGRESS || step==SPACE_QUERY_CURSOR_ROW ||
                step==(variant ? SPACE_QUERY_CURSOR_END : SPACE_QUERY_CURSOR_INCOMPLETE));
            if (step==SPACE_QUERY_CURSOR_ROW) {assert_query_owned_world(&row);delivered++;}
            bindings_free(&row);bindings_init(&row);
        }
        QueryReads before={0},after={0};
        assert(space_query_cursor_visit_reads(cursor,collect_query_read,&before));
        AtomDeepCopySession *atoms=atom_deep_copy_session_new(&copied_result);
        const AtomDeepCopyArenaPair pairs[]={
            {&input,&copied_input},{&result,&copied_result},{external,external}};
        assert(atom_deep_copy_session_set_arena_map(atoms,pairs,3u));
        unsigned calls=fallback_query_calls;
        SpaceQueryCursor *copy=space_query_cursor_copy_to_arena(cursor,&copied_result,atoms);
        assert(copy && !space_query_cursor_incremental(copy) && fallback_query_calls==calls);
        assert(space_query_cursor_visit_reads(copy,collect_query_read,&after));
        assert_query_reads_equal(&before,&after);
        assert(space_query_cursor_step_with_budget(copy,0u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
        assert(fallback_query_calls==calls);
        QueryFallbackTrace first=trace_owned_fallback(cursor);
        assert(delivered+first.rows==(variant==1u ? 0u : 1u));
        assert(first.calls==(cut ? 0u : 2u));
        space_query_cursor_free(cursor);bindings_free(&seed);bindings_free(&row);
        atom_deep_copy_session_free(atoms);arena_free(&result);arena_free(&input);
        QueryFallbackTrace second=trace_owned_fallback(copy);
        assert(first.length==second.length && first.rows==second.rows && first.calls==second.calls);
        assert(memcmp(first.steps,second.steps,first.length*sizeof(*first.steps))==0);
        calls=fallback_query_calls;bindings_init(&row);
        CettaNativeCostLedger cost;CettaNativeCostScope scope;
        cetta_native_cost_init(&cost);cetta_native_cost_scope_enter(&scope,&cost,1u);
        for (unsigned repeat=0u;repeat<3u;repeat++) {
            space_match_backend_clear_error();
            assert(space_query_cursor_step_with_budget(copy,0u,&row)==SPACE_QUERY_CURSOR_PROGRESS);
            assert(space_query_cursor_step_with_budget(copy,1u,&row)==
                (variant ? SPACE_QUERY_CURSOR_END : SPACE_QUERY_CURSOR_INCOMPLETE));
            assert(fallback_query_calls==calls && cost.total.work==0u);
        }
        space_add_atom_id(&space,store_atom(universe,atom_symbol(external,"owned-fallback-change")));
        assert(space_query_cursor_step_with_budget(copy,1u,&row)==SPACE_QUERY_CURSOR_INVALIDATED);
        assert(fallback_query_calls==calls && cost.total.work==0u);
        after=(QueryReads){0};
        assert(space_query_cursor_visit_reads(copy,collect_query_read,&after));
        assert_query_reads_equal(&before,&after);
        cetta_native_cost_scope_leave(&scope);cetta_native_cost_free(&cost);
        bindings_free(&row);space_query_cursor_free(copy);
        arena_free(&copied_result);arena_free(&copied_input);
        space.match_backend.ops=native_ops;space_free(&space);
    }
    space_match_backend_clear_error();
    puts("PASS: owned atomic fallback retains prepared rows, Need/factor claims, empty completion and parked refusal without replay");
}

#if CETTA_PRIME_EVAL_STACK
typedef struct {
    uint64_t work;
    CettaCount answers;
    SymbolId status;
    bool satisfied,closed;
} QueryObservation;

static int64_t query_test_integer(const Atom *value) {
    assert(value && value->kind==ATOM_GROUNDED && value->ground.gkind==GV_INT);
    return value->ground.ival;
}

static bool query_test_boolean(const Atom *value) {
    assert(value && value->kind==ATOM_SYMBOL &&
           (value->sym_id==g_builtin_syms.true_text || value->sym_id==g_builtin_syms.false_text));
    return value->sym_id==g_builtin_syms.true_text;
}

static Atom *query_authority_parse(Arena *arena, const char *text) {
    size_t position=0u;
    Atom *value=parse_sexpr(arena,text,&position);
    assert(value);
    return value;
}

static void test_overlay_query_control_handoff(void) {
    VarInternTable variables;
    var_intern_init(&variables);
    VarInternTable *previous=g_var_intern;g_var_intern=&variables;
    struct { CettaLanguageId language; const CettaProfile *profile; } hosts[]={
        {CETTA_LANGUAGE_PRIME,cetta_profile_prime_default()},
        {CETTA_LANGUAGE_HE,cetta_profile_he_extended()},
        {CETTA_LANGUAGE_PETTA,cetta_profile_petta_extended()},
    };
    for(size_t host=0u;host<sizeof(hosts)/sizeof(*hosts);host++)
    for(unsigned width=1u;width<=2u;width++)
    for(unsigned empty=0u;empty<2u;empty++)
    for(unsigned changed=0u;changed<4u;changed++) {
        Arena persistent,arena;
        TermUniverse universe;
        Space main_space,base,middle,outer,unrelated;
        Registry registry;
        CettaLibraryContext *library=cetta_malloc(sizeof(*library));
        arena_init(&persistent);arena_init(&arena);
        arena_set_runtime_kind(&persistent,CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
        arena_set_runtime_kind(&arena,CETTA_ARENA_RUNTIME_KIND_EVAL);
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe,&persistent);
        space_init_with_universe(&main_space,&universe);
        space_init_with_universe(&base,&universe);
        space_init_with_universe(&unrelated,&universe);
        Atom *initial=query_authority_parse(&persistent,empty?"(Other 7)":"(Edge 7)");
        space_add(&base,initial);
        space_init_overlay(&middle,&base);space_init_overlay(&outer,&middle);
        registry_init(&registry);
        registry_bind_id(&registry,g_builtin_syms.self,atom_space(&persistent,&main_space));
        registry_bind(&registry,"&view",atom_space(&persistent,&outer));
        assert(resolve_space(&registry,query_authority_parse(&persistent,"&view"))==&outer);
        cetta_library_context_init_for_language_profile(library,hosts[host].language,hosts[host].profile);
        eval_set_library_context(library);
        char source[256];
        snprintf(source,sizeof(source),"(select 7 (search-policy (control FIFO (workers %u) "
            "(budget 200))) (match &view (Edge $x) $x))",width);
        ResultSet first;result_set_init(&first);
        eval_top_with_registry(&main_space,&arena,&persistent,&registry,
            query_authority_parse(&persistent,source),&first);
        assert(first.len==1u);
        Atom *observation=first.items[0];
        assert(observation->kind==ATOM_EXPR && observation->expr.len==7u &&
            atom_is_symbol(observation->expr.elems[0],"Observation"));
        assert(observation->expr.elems[1]->expr.len==(empty?0u:1u));
        if(!empty) assert(query_test_integer(observation->expr.elems[1]->expr.elems[0])==7);
        assert(query_test_boolean(observation->expr.elems[3]->expr.elems[1]));
        Atom *residual=observation->expr.elems[4]->expr.elems[1];
        uint64_t work=(uint64_t)query_test_integer(observation->expr.elems[5]->expr.elems[1]);
        Space *target=changed==0u?&base:changed==1u?&middle:changed==2u?&outer:&unrelated;
        if(changed<3u) assert(space_remove(target,initial));
        space_add(target,query_authority_parse(&persistent,"(Edge 8)"));
        for(unsigned repeat=0u;repeat<3u;repeat++) {
            snprintf(source,sizeof(source),"(search-policy (control FIFO (workers %u) (budget %u)))",
                width,repeat==0u?0u:200u);
            Atom *parts[]={atom_symbol(&arena,"select"),atom_int(&arena,7),
                query_authority_parse(&arena,source),residual};
            ResultSet next;result_set_init(&next);
            eval_top_with_registry(&main_space,&arena,&persistent,&registry,
                atom_expr(&arena,parts,4u),&next);
            assert(next.len==1u);
            Atom *after=next.items[0];
            assert(after->kind==ATOM_EXPR && after->expr.len==7u &&
                atom_is_symbol(after->expr.elems[0],"Observation"));
            assert(after->expr.elems[1]->expr.len==0u);
            assert(atom_eq(after->expr.elems[4],observation->expr.elems[4]));
            assert((uint64_t)query_test_integer(after->expr.elems[5]->expr.elems[1])==work);
            assert(query_test_boolean(after->expr.elems[3]->expr.elems[1])==(changed==3u));
            assert(atom_is_symbol(after->expr.elems[6]->expr.elems[1],
                changed==3u?"closed":"invalidated"));
            result_set_free(&next);
        }
        result_set_free(&first);eval_set_library_context(NULL);
        cetta_library_context_free(library);free(library);registry_free(&registry);
        space_free(&outer);space_free(&middle);space_free(&base);space_free(&unrelated);
        space_free(&main_space);term_universe_free(&universe);
        arena_free(&arena);arena_free(&persistent);
    }
    g_var_intern=previous;var_intern_free(&variables);
    puts("PASS: all host controls retain inherited reads after query retirement, including empty reads");
}

static void query_test_values(const Atom *values,bool weighted,
                              CettaCount *count,uint64_t *previous_factor) {
    assert(values && values->kind==ATOM_EXPR);
    for(CettaExprIndex i=0u;i<values->expr.len;i++) {
        const Atom *value=values->expr.elems[i];
        if(weighted) {
            assert(value->kind==ATOM_EXPR && value->expr.len==4u &&
                   atom_is_symbol(value->expr.elems[0],"Weighted"));
            assert(query_test_integer(value->expr.elems[2])==2);
            const Atom *provenance=value->expr.elems[3];
            assert(provenance->kind==ATOM_EXPR && provenance->expr.len==2u &&
                   atom_is_symbol(provenance->expr.elems[0],"provenance"));
            const Atom *factors=provenance->expr.elems[1];
            assert(factors->kind==ATOM_EXPR && factors->expr.len==1u);
            const Atom *factor=factors->expr.elems[0];
            assert(factor->kind==ATOM_EXPR && factor->expr.len==4u &&
                   atom_is_symbol(factor->expr.elems[0],"Factor"));
            uint64_t id=(uint64_t)query_test_integer(factor->expr.elems[1]);
            assert(id && id!=*previous_factor);
            assert(query_test_integer(factor->expr.elems[2])>0);
            assert(query_test_integer(factor->expr.elems[3])==2);
            *previous_factor=id;
            value=value->expr.elems[1];
        }
        assert(query_test_integer(value)==42);
        (*count)++;
    }
}

static QueryObservation query_fallback_observe(const char *source,
        CettaLanguageId language,const CettaProfile *profile,
        bool weighted,bool split,unsigned expected_calls) {
    Arena persistent,arena;
    TermUniverse universe;
    Space space;
    Registry registry;
    CettaLibraryContext *library=cetta_malloc(sizeof(*library));
    arena_init(&persistent);arena_init(&arena);
    arena_set_runtime_kind(&persistent,CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_runtime_kind(&arena,CETTA_ARENA_RUNTIME_KIND_EVAL);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe,&persistent);
    space_init_with_universe(&space,&universe);
    registry_init(&registry);
    registry_bind_id(&registry,g_builtin_syms.self,atom_space(&persistent,&space));
    cetta_library_context_init_for_language_profile(library,language,profile);
    eval_set_library_context(library);
    const SpaceMatchBackendOps *original=space.match_backend.ops;
    SpaceMatchBackendOps custom=*original;
    custom.query_conjunction=query_fallback_with_refusal;
    space.match_backend.ops=&custom;
    size_t position=0u;
    Atom *expression=parse_sexpr(&persistent,source,&position);
    assert(expression);
    ResultSet result;
    result_set_init(&result);fallback_query_calls=0u;
    eval_top_with_registry(&space,&arena,&persistent,&registry,expression,&result);
    assert(result.len==1u);
    const Atom *observation=result.items[0];
    QueryObservation actual={0};
    uint64_t factor=0u;
    if(split) {
        assert(observation->kind==ATOM_EXPR && observation->expr.len==3u &&
               atom_is_symbol(observation->expr.elems[0],"Split"));
        query_test_values(observation->expr.elems[1],weighted,&actual.answers,&factor);
        observation=observation->expr.elems[2];
    }
    assert(observation->kind==ATOM_EXPR && observation->expr.len==7u &&
           atom_is_symbol(observation->expr.elems[0],"Observation"));
    query_test_values(observation->expr.elems[1],weighted,&actual.answers,&factor);
    actual.satisfied=query_test_boolean(observation->expr.elems[2]->expr.elems[1]);
    actual.closed=query_test_boolean(observation->expr.elems[3]->expr.elems[1]);
    actual.work=(uint64_t)query_test_integer(observation->expr.elems[5]->expr.elems[1]);
    actual.status=observation->expr.elems[6]->expr.elems[1]->sym_id;
    if(fallback_query_calls!=expected_calls)
        fprintf(stderr,"fallback calls %u expected %u: %s\n",
            fallback_query_calls,expected_calls,source);
    assert(fallback_query_calls==expected_calls);
    result_set_free(&result);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);free(library);
    registry_free(&registry);
    space.match_backend.ops=original;
    space_match_backend_clear_error();
    space_free(&space);term_universe_free(&universe);
    arena_free(&arena);arena_free(&persistent);
    return actual;
}

static void test_retained_query_fallback_body_progress(void) {
    VarInternTable variables;
    var_intern_init(&variables);
    VarInternTable *previous=g_var_intern;
    g_var_intern=&variables;
    fallback_query_refuse=symbol_intern_cstr(g_symbols,"cursor-refuse");
    fallback_query_empty=symbol_intern_cstr(g_symbols,"cursor-empty");
    const char *modes[]={"FIFO","WO (key $term 0)","PO (key $term a)"};
    struct { CettaLanguageId language; const CettaProfile *profile; } hosts[]={
        {CETTA_LANGUAGE_PRIME,cetta_profile_prime_default()},
        {CETTA_LANGUAGE_HE,cetta_profile_he_extended()},
        {CETTA_LANGUAGE_PETTA,cetta_profile_petta_extended()},
    };
    const unsigned widths[]={1u,2u,4u},cuts[]={0u,1u,7u,9u,13u,23u,35u,71u,500u};
    for(size_t host=0u;host<sizeof(hosts)/sizeof(*hosts);host++)
    for(size_t mode=0u;mode<sizeof(modes)/sizeof(*modes);mode++)
    for(size_t width=0u;width<sizeof(widths)/sizeof(*widths);width++)
    for(unsigned weighted=0u;weighted<2u;weighted++) {
        char body[256],whole[1024],part[1536];
        snprintf(body,sizeof(body),"%s(match &self (| cursor-refuse cursor-accept cursor-accept) "
            "(+ 1 (+ 1 (+ 1 (+ 1 (+ 1 37)))))%s)%s",
            weighted?"(weight:read (algebra 0 1 + *) ":"",
            weighted?" (where 2)":"",weighted?")":"");
        snprintf(whole,sizeof(whole),"(select 7 (search-policy (control %s (workers %u) "
            "(budget 1000))) %s)",modes[mode],widths[width],body);
        QueryObservation expected=query_fallback_observe(whole,hosts[host].language,
            hosts[host].profile,weighted,false,3u);
        if(expected.answers!=2u || expected.closed || expected.satisfied)
            fprintf(stderr,"fallback unfinished cohort failed: answers=%llu closed=%d satisfied=%d %s\n",
                (unsigned long long)expected.answers,expected.closed,expected.satisfied,whole);
        assert(expected.answers==2u && !expected.closed && !expected.satisfied);
        for(size_t cut=0u;cut<sizeof(cuts)/sizeof(*cuts);cut++) {
            snprintf(part,sizeof(part),"(let (Observation $p (satisfied $s) (closed $c) "
                "(residual $r) (work $w) (status $t)) "
                "(select 7 (search-policy (control %s (workers %u) (budget %u))) %s) "
                "(Split $p (select 7 (search-policy (control %s (workers %u) (budget %u))) $r)))",
                modes[mode],widths[width],cuts[cut],body,modes[mode],widths[width],1000u-cuts[cut]);
            QueryObservation actual=query_fallback_observe(part,hosts[host].language,
                hosts[host].profile,weighted,true,3u);
            assert(actual.answers==expected.answers && actual.closed==expected.closed &&
                   actual.satisfied==expected.satisfied && actual.work==expected.work &&
                   actual.status==expected.status);
        }
    }
    g_var_intern=previous;var_intern_free(&variables);
    puts("PASS: refused query retains duplicate graded siblings, fixed-controller split work and parked closure");
}
#endif

static void test_retained_query_invalidation(TermUniverse *universe, Arena *scratch) {
    Space space, unrelated;
    space_init_with_universe(&space, universe);
    space_init_with_universe(&unrelated, universe);
    Atom *query = atom_int(scratch, 7);
    Bindings row;
    bindings_init(&row);
    SpaceQueryCursor *cursor = space_query_cursor_new(&space, scratch, query, NULL);
    assert(cursor && space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_PROGRESS);
    space_add_atom_id(&space, store_atom(universe, query));
    assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_INVALIDATED);
    assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_INVALIDATED);
    space_query_cursor_free(cursor);

    Atom *conjunction = binary(scratch, g_builtin_syms.comma, query, query);
    cursor = space_query_cursor_new(&space, scratch, conjunction, NULL);
    for (unsigned i = 0u; i < 4u; i++)
        assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_PROGRESS);
    space_add_atom_id(&space, store_atom(universe, query));
    assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_INVALIDATED);
    space_query_cursor_free(cursor);

    cursor = space_query_cursor_new(&space, scratch, query, NULL);
    for (unsigned i = 0u; i < 3u; i++)
        assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_PROGRESS);
    space_add_atom_id(&unrelated, store_atom(universe, query));
    assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_ROW);
    space_query_cursor_free(cursor);
    bindings_free(&row);bindings_init(&row);

    for (unsigned cut = 0u; cut < 6u; cut++) {
        cursor = space_query_cursor_new(&space, scratch, conjunction, NULL);
        for (unsigned i = 0u; i < cut; i++)
            assert(space_query_cursor_step(cursor, &row) == SPACE_QUERY_CURSOR_PROGRESS);
        space_query_cursor_free(cursor);
    }
    Atom *empty_comma_items[] = {atom_symbol_id(scratch, g_builtin_syms.comma)};
    Atom *empty_pipe_items[] = {atom_symbol_id(scratch, g_builtin_syms.pipe)};
    cursor = space_query_cursor_new(&space, scratch, atom_expr(scratch,empty_comma_items,1u), NULL);
    assert(space_query_cursor_step(cursor,&row) == SPACE_QUERY_CURSOR_PROGRESS);
    assert(space_query_cursor_step(cursor,&row) == SPACE_QUERY_CURSOR_ROW);
    assert(row.len == 0u);
    assert(space_query_cursor_step(cursor,&row) == SPACE_QUERY_CURSOR_END);
    space_query_cursor_free(cursor);
    cursor = space_query_cursor_new(&space, scratch, atom_expr(scratch,empty_pipe_items,1u), NULL);
    assert(space_query_cursor_step(cursor,&row) == SPACE_QUERY_CURSOR_PROGRESS);
    assert(space_query_cursor_step(cursor,&row) == SPACE_QUERY_CURSOR_PROGRESS);
    assert(space_query_cursor_step(cursor,&row) == SPACE_QUERY_CURSOR_END);
    space_query_cursor_free(cursor);
    bindings_free(&row);
    space_free(&unrelated);
    space_free(&space);
    puts("PASS: retained query cancellation, pre-publication cuts and complete-read invalidation");
}

static void test_retained_list_match(Arena *arena) {
    Atom *seven=atom_int(arena,7), *nine=atom_int(arena,9);
    Atom *tail=atom_var_with_spelling(arena,
        symbol_intern_cstr(g_symbols,"cursor-list-tail"),UINT64_C(83001));
    Atom *one=atom_list(arena,&seven,1u);
    Atom *two=atom_list(arena,(Atom *[]){seven,nine},2u);
    Atom *empty=atom_list(arena,NULL,0u);
    Atom *open=atom_list_with_rest(arena,&seven,1u,tail);
    struct { Atom *left,*right,*rest; bool matched; } cases[]={
        {empty,one,NULL,false}, {one,two,NULL,false}, {two,one,NULL,false},
        {one,atom_expr(arena,&seven,1u),NULL,false},
        {open,two,atom_list(arena,&nine,1u),true}, {open,one,empty,true},
        {one,open,NULL,true}, {empty,empty,NULL,true},
    };
    for (size_t i=0u;i<sizeof(cases)/sizeof(*cases);i++) {
        CettaMatchCursor *cursor=match_atoms_cursor_new(cases[i].left,cases[i].right,NULL,arena);
        assert(cursor);
        assert(match_atoms_cursor_step(cursor,0u)==CETTA_MATCH_CURSOR_PROGRESS);
        CettaMatchCursorStep step=CETTA_MATCH_CURSOR_PROGRESS;
        for (unsigned n=0u;n<100u && step==CETTA_MATCH_CURSOR_PROGRESS;n++)
            step=match_atoms_cursor_step(cursor,1u);
        assert(step==(cases[i].matched ? CETTA_MATCH_CURSOR_MATCHED : CETTA_MATCH_CURSOR_REFUTED));
        Bindings answer;
        bindings_init(&answer);
        assert(match_atoms_cursor_take(cursor,&answer)==cases[i].matched);
        if (cases[i].rest)
            assert(atom_eq(bindings_apply(&answer,arena,tail),cases[i].rest));
        bindings_free(&answer);
        match_atoms_cursor_free(cursor);
    }
    puts("PASS: retained list matcher separates rigid refutation from unfinished primitives");
}

typedef struct {
    CettaMatchCursor *cursor;
    CettaMatchCursorStep result;
} ColdMatchStep;

static void *cold_match_step(void *raw) {
    ColdMatchStep *job=raw;
    job->result=match_atoms_cursor_step(job->cursor,4096u);
    return NULL;
}

static void test_retained_match_cold_thread(Arena *arena) {
    Bindings seed;bindings_init(&seed);
    for(unsigned i=0u;i<80u;i++) {
        char name[40];snprintf(name,sizeof(name),"cursor-cold-seed-%u",i);
        assert(bindings_add_var(&seed,atom_var(arena,name),atom_int(arena,i)));
    }
    for(unsigned success=0u;success<2u;success++) {
        Atom *left[130],*right[130];
        left[0]=right[0]=atom_symbol(arena,"ColdCursor");
        for(unsigned i=1u;i<129u;i++) {
            char name[40];snprintf(name,sizeof(name),"cursor-cold-new-%u",i);
            left[i]=atom_var(arena,name);right[i]=atom_int(arena,i);
        }
        left[129]=atom_symbol(arena,"End");
        right[129]=atom_symbol(arena,success?"End":"Different");
        CettaMatchCursor *cursor=match_atoms_cursor_new(
            atom_expr(arena,left,130u),atom_expr(arena,right,130u),&seed,arena);
        assert(cursor && match_atoms_cursor_step(cursor,16u)==CETTA_MATCH_CURSOR_PROGRESS);
        uint32_t epoch=match_atoms_cursor_epoch(cursor);assert(epoch);
        CettaMatchCursor *copy=match_atoms_cursor_clone(cursor);
        assert(copy && match_atoms_cursor_epoch(copy)==epoch);
        match_atoms_cursor_free(cursor);
        cursor=copy;
        ColdMatchStep job={.cursor=cursor};pthread_t thread;
        assert(pthread_create(&thread,NULL,cold_match_step,&job)==0);
        assert(pthread_join(thread,NULL)==0);
        assert(job.result==(success?CETTA_MATCH_CURSOR_MATCHED:CETTA_MATCH_CURSOR_REFUTED));
        assert(match_atoms_cursor_epoch(cursor)==epoch);
        Bindings answer;bindings_init(&answer);
        assert(match_atoms_cursor_take(cursor,&answer)==(success!=0u));
        if(success)assert(atom_eq(bindings_apply(&answer,arena,left[128]),right[128]));
        bindings_free(&answer);match_atoms_cursor_free(cursor);
        assert(seed.len==80u);
        assert(atom_eq(bindings_apply(&seed,arena,left[1]),left[1]));
    }
    bindings_free(&seed);
    puts("PASS: cold-thread matcher resume preserves its captured cache profile, binding trail and rigid-refutation boundary");
}

static void test_retained_match_clone(Arena *arena) {
    Atom *head=atom_symbol(arena,"cursor-clone");
    Atom *left=atom_int(arena,7), *right=atom_int(arena,7);
    for (unsigned i=0u;i<160u;i++) {
        left=atom_expr2(arena,head,left);
        right=atom_expr2(arena,head,right);
    }
    Atom *x=atom_var(arena,"cursor-clone-value");
    Atom *query=atom_expr(arena,(Atom *[]){head,x,left,x},4u);
    Bindings seed;
    bindings_init(&seed);
    Atom *kept=atom_var(arena,"cursor-clone-seed");
    assert(bindings_add_var(&seed,kept,atom_int(arena,23)));
    assert(bindings_invalidate_after_key_rewrite(&seed));
    PrimeNeedSnapshot root,need,completed;
    PrimeNeedBranchState branch;
    prime_need_snapshot_init(&root);prime_need_branch_state_init(&branch);
    assert(prime_need_snapshot_begin(&root) && prime_need_branch_state_begin(arena,&branch));
    uint64_t cell=0u,pending=0u;
    assert(prime_need_snapshot_allocate(arena,&root,atom_symbol(arena,"clone-lazy-origin"),&need,&cell));
    assert(prime_need_snapshot_start_evaluation(arena,&need,cell,31u,&completed));
    assert(prime_need_snapshot_resolve_value(arena,&completed,cell,31u,atom_int(arena,29),&need));
    assert(prime_need_snapshot_allocate(arena,&need,atom_symbol(arena,"clone-still-pending"),&completed,&pending));
    CettaWeightLedger weights={0},selected={0};
    assert(cetta_weight_ledger_append(arena,&weights,701u,71u,atom_int(arena,0),&weights));
    assert(cetta_weight_ledger_append(arena,&weights,702u,72u,atom_int(arena,3),&weights));
    CettaWeightLedger prefix={0};
    assert(cetta_weight_ledger_handle(arena,&weights,&prefix,901u,&weights,&selected));
    PrimeNeedReceipt *receipt_ptr=NULL;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    PrimeNeedReceipt receipt;
    prime_need_receipt_init(&receipt);
    assert(prime_need_receipt_begin(arena,&receipt));
    assert(prime_need_receipt_observe_cell(arena,&receipt,completed.session_id,cell,
        atom_int(arena,29),&receipt));
    receipt_ptr=&receipt;
#endif
    bindings_prime_set(&seed,&completed,&branch,811u,receipt_ptr,&weights);
    const unsigned cuts[]={0u,1u,3u,17u,100u,200u,320u};
    for (unsigned success=0u;success<2u;success++) {
        Atom *row=atom_expr(arena,(Atom *[]){head,atom_int(arena,11),right,
            atom_int(arena,success ? 11 : 12)},4u);
        for (size_t cut=0u;cut<sizeof(cuts)/sizeof(*cuts);cut++) {
            CettaMatchCursor *original=match_atoms_cursor_new(query,row,&seed,arena);
            assert(original && !match_atoms_cursor_clone(NULL));
            assert(match_atoms_cursor_step(original,cuts[cut])==CETTA_MATCH_CURSOR_PROGRESS);
            CettaNativeCostLedger cost;
            CettaNativeCostScope scope;
            cetta_native_cost_init(&cost);
            cetta_native_cost_scope_enter(&scope,&cost,1u);
            CettaMatchCursor *copy=match_atoms_cursor_clone(original);
            assert(copy && match_atoms_cursor_epoch(copy)==match_atoms_cursor_epoch(original));
            assert(cost.total.counts[CETTA_COST_MATCH_NODE]==0u);
            assert(cost.total.counts[CETTA_COST_MATCH_ATTEMPT]==0u);
            assert(match_atoms_cursor_step(copy,0u)==CETTA_MATCH_CURSOR_PROGRESS);
            assert(cost.total.counts[CETTA_COST_MATCH_NODE]==0u);
            uint64_t nodes[2], attempts[2];
            CettaMatchCursor *cursors[]={original,copy};
            for (unsigned run=0u;run<2u;run++) {
                uint64_t before=cost.total.counts[CETTA_COST_MATCH_NODE];
                uint64_t attempt=cost.total.counts[CETTA_COST_MATCH_ATTEMPT];
                CettaMatchCursorStep step=CETTA_MATCH_CURSOR_PROGRESS;
                for (unsigned poll=0u;poll<1000u && step==CETTA_MATCH_CURSOR_PROGRESS;poll++)
                    step=match_atoms_cursor_step(cursors[run],run ? 1u : 17u);
                assert(step==(success ? CETTA_MATCH_CURSOR_MATCHED : CETTA_MATCH_CURSOR_REFUTED));
                nodes[run]=cost.total.counts[CETTA_COST_MATCH_NODE]-before;
                attempts[run]=cost.total.counts[CETTA_COST_MATCH_ATTEMPT]-attempt;
                CettaMatchCursor *settled=match_atoms_cursor_clone(cursors[run]);
                assert(settled && match_atoms_cursor_step(settled,UINT64_MAX)==step);
                match_atoms_cursor_free(cursors[run]);
                Bindings answer;
                bindings_init(&answer);
                assert(match_atoms_cursor_take(settled,&answer)==(success!=0u));
                if (success) {
                    assert(atom_eq(bindings_apply(&answer,arena,x),atom_int(arena,11)));
                    assert(atom_eq(bindings_apply(&answer,arena,kept),atom_int(arena,23)));
                    assert(bindings_occurrence_token(&answer)==811u);
                    const PrimeNeedSnapshot *copied=bindings_need_view(&answer);
                    assert(copied->session_id==completed.session_id && copied->top==completed.top);
                    PrimeNeedCellView cached,unforced;
                    assert(prime_need_snapshot_lookup(copied,cell,&cached) &&
                        cached.cache_state==PRIME_NEED_CACHE_VALUE &&
                        atom_eq(cached.cached,atom_int(arena,29)));
                    assert(prime_need_snapshot_lookup(copied,pending,&unforced) &&
                        unforced.cache_state==PRIME_NEED_CACHE_EMPTY && !unforced.cached);
                    assert(prime_need_branch_state_equal(bindings_branch_state_view(&answer),&branch));
                    assert(cetta_weight_ledger_equal(bindings_weight_view(&answer),&weights));
                    assert(cetta_weight_ledger_handler(bindings_weight_view(&answer),701u)==901u);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
                    assert(prime_need_receipt_equal(bindings_receipt_view(&answer),&receipt));
#endif
                    CettaMatchCursor *taken=match_atoms_cursor_clone(settled);
                    assert(taken && !match_atoms_cursor_take(taken,&answer));
                    match_atoms_cursor_free(taken);
                }
                bindings_free(&answer);
                match_atoms_cursor_free(settled);
            }
            assert(nodes[0]==nodes[1] && attempts[0]==attempts[1]);
            assert(attempts[0]==(cuts[cut] ? 0u : 1u));
            cetta_native_cost_scope_leave(&scope);
            cetta_native_cost_free(&cost);
            assert(seed.len==1u && atom_eq(bindings_apply(&seed,arena,x),x));
        }
    }
    bindings_free(&seed);
    puts("PASS: cloned matchers preserve pending work, rollback, epochs, Need caches, claims and settled delivery");
}

typedef struct {
    CettaMatchCursor *cursor;
    CettaMatchCursorStep result;
    uint64_t nodes, attempts;
} RelocatedMatchStep;

static void *relocated_match_step(void *raw) {
    RelocatedMatchStep *job=raw;
    CettaNativeCostLedger cost; CettaNativeCostScope scope;
    cetta_native_cost_init(&cost); cetta_native_cost_scope_enter(&scope,&cost,1u);
    job->result=CETTA_MATCH_CURSOR_PROGRESS;
    for (unsigned poll=0u;poll<1000u && job->result==CETTA_MATCH_CURSOR_PROGRESS;poll++)
        job->result=match_atoms_cursor_step(job->cursor,1u);
    job->nodes=cost.total.counts[CETTA_COST_MATCH_NODE];
    job->attempts=cost.total.counts[CETTA_COST_MATCH_ATTEMPT];
    cetta_native_cost_scope_leave(&scope); cetta_native_cost_free(&cost);
    return NULL;
}

static void test_retained_match_relocation(void) {
    const unsigned cuts[] = {0u, 1u, 3u, 17u, 100u, 200u, 320u};
    for (unsigned profile = 0u; profile < 4u; profile++) {
    for (unsigned success = 0u; success < 2u; success++) {
        for (size_t cut = 0u; cut < sizeof(cuts)/sizeof(*cuts); cut++) {
            Arena inputs, source, destination, destination_inputs;
            arena_init(&inputs); arena_init(&source); arena_init(&destination);
            arena_init(&destination_inputs);
            if (profile > 0u) {
                arena_set_runtime_kind(&inputs,CETTA_ARENA_RUNTIME_KIND_EVAL);
                arena_set_runtime_kind(&source,CETTA_ARENA_RUNTIME_KIND_EVAL);
                arena_set_runtime_kind(&destination_inputs,CETTA_ARENA_RUNTIME_KIND_EVAL);
                arena_set_runtime_kind(&destination,CETTA_ARENA_RUNTIME_KIND_EVAL);
            }
            if (profile == 2u) {
                arena_set_scalar_cache(&inputs,true); arena_set_scalar_cache(&source,true);
                arena_set_scalar_cache(&destination_inputs,true); arena_set_scalar_cache(&destination,true);
            }
            if (profile == 3u) {
                arena_set_older_generation(&source,&inputs);
                arena_set_older_generation(&destination,&destination_inputs);
            }
            /* A later contextual copy must hit the same captured canonical
             * result symbol, even though its raw row lives in another arena. */
            (void)atom_symbol(&source,"Payload");
            Atom *head = atom_symbol(&inputs, "relocated-cursor");
            Atom *left = atom_int(&inputs, 7), *right = atom_int(&inputs, 7);
            for (unsigned depth = 0u; depth < 160u; depth++) {
                left = atom_expr2(&inputs, head, left);
                right = atom_expr2(&inputs, head, right);
            }
            Atom *x = atom_var(&inputs, "relocated-cursor-x");
            Atom *value = atom_expr2(&inputs, atom_symbol(&inputs, "Payload"),
                                     atom_int(&inputs, 11));
            Atom *last = success ? value : atom_expr2(&inputs,
                atom_symbol(&inputs, "Payload"), atom_int(&inputs, 12));
            Atom *query = atom_expr(&inputs, (Atom *[]){head,x,left,x}, 4u);
            Atom *row = atom_expr(&inputs, (Atom *[]){head,value,right,last}, 4u);
            Atom *kept = atom_var(&source, "relocated-cursor-kept");
            Atom *origin = atom_expr2(&source, atom_symbol(&source, "Origin"),
                                      atom_int(&source, 23));
            Bindings seed; bindings_init(&seed);
            assert(bindings_add_var(&seed, kept, origin));
            /* Force hashed active-path coverage instead of relying solely
             * on the derived acyclic fast path. */
            assert(bindings_invalidate_after_key_rewrite(&seed));
            PrimeNeedSnapshot empty, need;
            prime_need_snapshot_init(&empty);
            assert(prime_need_snapshot_begin(&empty));
            uint64_t cell = 0u;
            assert(prime_need_snapshot_allocate(&source,&empty,origin,&need,&cell));
            assert(prime_need_snapshot_start_evaluation(&source,&need,cell,31u,&need));
            assert(prime_need_snapshot_resolve_value(&source,&need,cell,31u,origin,&need));
            need.max_storage_key = 911u;
            CettaWeightLedger weights={0}, prefix={0}, selected={0};
            assert(cetta_weight_ledger_append(&source,&weights,701u,71u,
                                              atom_int(&source,3),&weights));
            assert(cetta_weight_ledger_handle(&source,&weights,&prefix,901u,
                                              &weights,&selected));
            bindings_prime_set(&seed,&need,NULL,811u,NULL,&weights);
            assert(bindings_lookup_value_id(&seed,kept->var_id).skeleton==origin);
            CettaMatchCursor *original = match_atoms_cursor_new(query,row,&seed,&source);
            assert(original && match_atoms_cursor_step(original,cuts[cut])==
                   CETTA_MATCH_CURSOR_PROGRESS);
            uint32_t epoch=match_atoms_cursor_epoch(original);
            AtomDeepCopySession *session=atom_deep_copy_session_new(&destination);
            assert(!match_atoms_cursor_copy_to_arena(original,&source,session));
            assert(!match_atoms_cursor_copy_to_arena(original,&destination,session));
            const AtomDeepCopyArenaPair pairs[] = {
                {&source, &destination}, {&inputs, &destination_inputs}};
            assert(atom_deep_copy_session_set_arena_map(session,pairs,2u));
            CettaNativeCostLedger cost; CettaNativeCostScope scope;
            cetta_native_cost_init(&cost); cetta_native_cost_scope_enter(&scope,&cost,1u);
            Arena refused, refused_inputs;
            arena_init(&refused); arena_init(&refused_inputs);
            arena_set_runtime_kind(&refused,source.runtime_kind);
            arena_set_runtime_kind(&refused_inputs,inputs.runtime_kind);
            arena_set_scalar_cache(&refused,source.scalar_cache);
            arena_set_scalar_cache(&refused_inputs,inputs.scalar_cache);
            if (profile==3u) arena_set_older_generation(&refused,&refused_inputs);
            AtomDeepCopySession *missing=atom_deep_copy_session_new(&refused);
            const AtomDeepCopyArenaPair missing_pair={&source,&refused};
            assert(atom_deep_copy_session_set_arena_map(missing,&missing_pair,1u)==(profile!=3u));
            assert(!match_atoms_cursor_copy_to_arena(original,&refused,missing));
            atom_deep_copy_session_free(missing);
            arena_free(&refused); arena_free(&refused_inputs);
            arena_init(&refused); arena_init(&refused_inputs);
            arena_set_runtime_kind(&refused,source.runtime_kind);
            arena_set_runtime_kind(&refused_inputs,inputs.runtime_kind);
            arena_set_scalar_cache(&refused,source.scalar_cache);
            arena_set_scalar_cache(&refused_inputs,inputs.scalar_cache);
            if (profile==3u) arena_set_older_generation(&refused,&refused_inputs);
            AtomDeepCopySession *stale=atom_deep_copy_session_new(&refused);
            const AtomDeepCopyArenaPair stale_pairs[]={{&source,&refused},{&inputs,&refused_inputs}};
            assert(atom_deep_copy_session_set_arena_map(stale,stale_pairs,2u));
            arena_reset(&refused_inputs,arena_mark(&refused_inputs));
            assert(!match_atoms_cursor_copy_to_arena(original,&refused,stale));
            atom_deep_copy_session_free(stale);
            arena_free(&refused); arena_free(&refused_inputs);
            CettaMatchCursor *copy=match_atoms_cursor_copy_to_arena(original,&destination,session);
            assert(copy && match_atoms_cursor_epoch(copy)==epoch);
            assert(cost.total.counts[CETTA_COST_MATCH_NODE]==0u &&
                   cost.total.counts[CETTA_COST_MATCH_ATTEMPT]==0u);
            assert(match_atoms_cursor_step(copy,0u)==CETTA_MATCH_CURSOR_PROGRESS);
            Atom *copied_x=atom_deep_copy_session_copy(session,x);
            Atom *copied_kept=atom_deep_copy_session_copy(session,kept);
            Atom *copied_origin=atom_deep_copy_session_copy(session,origin);
            Atom *copied_value=atom_deep_copy_session_copy(session,value);
            assert(copied_x && copied_kept && copied_origin && copied_value);
            atom_deep_copy_session_free(session);
            uint64_t before=cost.total.counts[CETTA_COST_MATCH_NODE];
            uint64_t attempt=cost.total.counts[CETTA_COST_MATCH_ATTEMPT];
            CettaMatchCursorStep result=match_atoms_cursor_step(original,UINT64_MAX);
            assert(result==(success ? CETTA_MATCH_CURSOR_MATCHED : CETTA_MATCH_CURSOR_REFUTED));
            uint64_t nodes=cost.total.counts[CETTA_COST_MATCH_NODE]-before;
            uint64_t attempts=cost.total.counts[CETTA_COST_MATCH_ATTEMPT]-attempt;
            match_atoms_cursor_free(original);
            bindings_free(&seed);
            arena_free(&source); arena_free(&inputs);
            before=cost.total.counts[CETTA_COST_MATCH_NODE];
            attempt=cost.total.counts[CETTA_COST_MATCH_ATTEMPT];
            RelocatedMatchStep job={.cursor=copy};
            if (cut%2u==0u) {
                pthread_t thread;
                assert(pthread_create(&thread,NULL,relocated_match_step,&job)==0);
                assert(pthread_join(thread,NULL)==0);
                assert(cost.total.counts[CETTA_COST_MATCH_NODE]==before &&
                       cost.total.counts[CETTA_COST_MATCH_ATTEMPT]==attempt);
            } else {
                /* The common nested cost scope also delivers to this
                 * containing ledger; no accounting is imported twice. */
                relocated_match_step(&job);
                assert(cost.total.counts[CETTA_COST_MATCH_NODE]-before==job.nodes);
                assert(cost.total.counts[CETTA_COST_MATCH_ATTEMPT]-attempt==job.attempts);
            }
            assert(job.result==result && job.nodes==nodes && job.attempts==attempts);
            Bindings answer; bindings_init(&answer);
            assert(match_atoms_cursor_take(copy,&answer)==(success!=0u));
            if (success) {
                assert(atom_eq(bindings_apply(&answer,&destination,copied_x),copied_value));
                assert(bindings_lookup_value_id(&answer,copied_kept->var_id).skeleton==copied_origin);
                const PrimeNeedSnapshot *retained=bindings_need_view(&answer);
                PrimeNeedCellView view;
                assert(retained && retained->max_storage_key==911u &&
                       prime_need_snapshot_lookup(retained,cell,&view));
                assert(view.origin==copied_origin && view.cached==copied_origin);
                assert(bindings_occurrence_token(&answer)==811u);
                assert(cetta_weight_ledger_handler(bindings_weight_view(&answer),701u)==901u);
            }
            bindings_free(&answer); match_atoms_cursor_free(copy);
            cetta_native_cost_scope_leave(&scope); cetta_native_cost_free(&cost);
            arena_free(&destination);
            arena_free(&destination_inputs);
        }
    }
    }
    puts("PASS: relocated matcher retires source atoms while preserving paths, epochs, carriers and exact remaining node charges");
}

static void test_retained_match_worklist(TermUniverse *universe, Arena *scratch) {
    Atom *head=atom_symbol(scratch,"cursor-deep");
    Atom *left=atom_int(scratch,7), *right=atom_int(scratch,7);
    for (unsigned i=0u;i<160u;i++) {
        left=atom_expr2(scratch,head,left);
        right=atom_expr2(scratch,head,right);
    }
    Atom *x=atom_var_with_spelling(scratch,
        symbol_intern_cstr(g_symbols,"cursor-deep-x"),UINT64_C(82001));
    Atom *query_items[]={head,x,left,x};
    Atom *row_items[]={head,atom_int(scratch,11),right,atom_int(scratch,11)};
    Atom *query=atom_expr(scratch,query_items,4u);
    Atom *row=atom_expr(scratch,row_items,4u);
    uint64_t node_counts[3]={0};
    const uint64_t grants[]={1u,17u,UINT64_MAX};
    Bindings conservative;
    bindings_init(&conservative);
    Atom *seed_var=atom_var_with_spelling(scratch,
        symbol_intern_cstr(g_symbols,"cursor-seed"),UINT64_C(82003));
    assert(bindings_add_var(&conservative,seed_var,atom_int(scratch,13)));
    assert(bindings_invalidate_after_key_rewrite(&conservative));
    for (unsigned run=0u;run<3u;run++) {
        CettaMatchCursor *cursor=match_atoms_cursor_new(query,row,&conservative,scratch);
        assert(cursor);
        CettaNativeCostLedger cost;
        CettaNativeCostScope scope;
        cetta_native_cost_init(&cost);
        cetta_native_cost_scope_enter(&scope,&cost,1u);
        assert(match_atoms_cursor_epoch(cursor)==0u);
        assert(match_atoms_cursor_step(cursor,0u)==CETTA_MATCH_CURSOR_PROGRESS);
        assert(match_atoms_cursor_epoch(cursor)==0u);
        assert(cost.total.work==0u);
        CettaMatchCursorStep step=CETTA_MATCH_CURSOR_PROGRESS;
        uint32_t activation=0u;
        for (unsigned poll=0u;poll<1000u && step==CETTA_MATCH_CURSOR_PROGRESS;poll++) {
            uint64_t previous=cost.total.counts[CETTA_COST_MATCH_NODE];
            step=match_atoms_cursor_step(cursor,grants[run]);
            if(!activation)activation=match_atoms_cursor_epoch(cursor);
            assert(activation && match_atoms_cursor_epoch(cursor)==activation);
            assert(cost.total.counts[CETTA_COST_MATCH_NODE]-previous<=grants[run]);
            uint64_t work=cost.total.work;
            assert(match_atoms_cursor_step(cursor,0u)==step);
            assert(cost.total.work==work);
            CettaStorageCensus census={0};
            match_atoms_cursor_storage_census(cursor,&census);
            cetta_storage_finish(&census);
            assert(!census.incomplete && census.bytes);
            cetta_storage_census_free(&census);
        }
        assert(step==CETTA_MATCH_CURSOR_MATCHED);
        assert(cost.total.counts[CETTA_COST_MATCH_ATTEMPT]==1u);
        node_counts[run]=cost.total.counts[CETTA_COST_MATCH_NODE];
        assert(node_counts[run]>320u);
        Bindings answer;
        bindings_init(&answer);
        assert(match_atoms_cursor_take(cursor,&answer));
        assert(!match_atoms_cursor_take(cursor,&answer));
        match_atoms_cursor_free(cursor);
        assert(atom_eq(bindings_apply(&answer,scratch,x),atom_int(scratch,11)));
        bindings_free(&answer);
        cetta_native_cost_scope_leave(&scope);
        cetta_native_cost_free(&cost);
    }
    bindings_free(&conservative);
    assert(node_counts[0]==node_counts[1] && node_counts[1]==node_counts[2]);
    row_items[3]=atom_int(scratch,12);
    CettaMatchCursor *cursor=match_atoms_cursor_new(query,atom_expr(scratch,row_items,4u),NULL,scratch);
    CettaMatchCursorStep step=CETTA_MATCH_CURSOR_PROGRESS;
    for (unsigned poll=0u;poll<1000u && step==CETTA_MATCH_CURSOR_PROGRESS;poll++)
        step=match_atoms_cursor_step(cursor,1u);
    assert(step==CETTA_MATCH_CURSOR_REFUTED);
    Bindings answer;
    bindings_init(&answer);
    assert(!match_atoms_cursor_take(cursor,&answer));
    match_atoms_cursor_free(cursor);
    for (unsigned cut=0u;cut<60u;cut++) {
        cursor=match_atoms_cursor_new(query,row,NULL,scratch);
        for (unsigned i=0u;i<cut;i++)
            assert(match_atoms_cursor_step(cursor,1u)==CETTA_MATCH_CURSOR_PROGRESS);
        match_atoms_cursor_free(cursor);
    }

    /* A contextual stored variable is copied by the canonical transport.
     * Its fresh frame is owned after the suspended matcher is destroyed. */
    Atom *v=atom_var_with_spelling(scratch,
        symbol_intern_cstr(g_symbols,"cursor-stored"),UINT64_C(82002));
    Atom *open=atom_expr2(scratch,head,v);
    Atom *open_query=binary(scratch,g_builtin_syms.comma,x,x);
    Atom *open_row=binary(scratch,g_builtin_syms.comma,open,open);
    cursor=match_atoms_cursor_new(open_query,open_row,NULL,scratch);
    step=CETTA_MATCH_CURSOR_PROGRESS;
    for (unsigned poll=0u;poll<100u && step==CETTA_MATCH_CURSOR_PROGRESS;poll++)
        step=match_atoms_cursor_step(cursor,1u);
    assert(step==CETTA_MATCH_CURSOR_MATCHED && match_atoms_cursor_take(cursor,&answer));
    match_atoms_cursor_free(cursor);
    assert(atom_alpha_eq(bindings_apply(&answer,scratch,x),open));
    bindings_free(&answer);bindings_init(&answer);

    /* The checked cursor refuses a corrupt alias store rather than reporting
     * an empty answer set. Polling the refused primitive does not replay it. */
    Bindings bad;
    bindings_init(&bad);
    assert(bindings_add_var(&bad,x,v));
    assert(bindings_add_var(&bad,v,atom_int(scratch,11)));
    assert(bindings_prepare_logical_write(&bad));
    for (uint32_t i=0u;i<bad.len;i++)
        if (bad.entries[i].var_id==v->var_id) bad.entries[i].value.skeleton=x;
    assert(bindings_invalidate_after_key_rewrite(&bad));
    assert(bindings_has_loop(&bad));
    cursor=match_atoms_cursor_new(x,atom_int(scratch,11),&bad,scratch);
    assert(cursor);
    assert(match_atoms_cursor_step(cursor,1u)==CETTA_MATCH_CURSOR_INCOMPLETE);
    CettaMatchCursor *parked=match_atoms_cursor_clone(cursor);
    assert(parked);
    match_atoms_cursor_free(cursor);
    cursor=parked;
    assert(match_atoms_cursor_step(cursor,100u)==CETTA_MATCH_CURSOR_INCOMPLETE);
    assert(!match_atoms_cursor_take(cursor,&answer));
    match_atoms_cursor_free(cursor);
    bindings_free(&bad);

    Space space;
    space_init_with_universe(&space,universe);
    space_add_atom_id(&space,store_atom(universe,row));
    SpaceQueryCursor *query_cursor=space_query_cursor_new(&space,scratch,query,NULL);
    assert(query_cursor);
    CettaNativeCostLedger cost;
    CettaNativeCostScope scope;
    cetta_native_cost_init(&cost);
    cetta_native_cost_scope_enter(&scope,&cost,1u);
    for (unsigned i=0u;i<12u;i++)
        assert(space_query_cursor_step_with_budget(query_cursor,1u,&answer)==SPACE_QUERY_CURSOR_PROGRESS);
    assert(cost.total.counts[CETTA_COST_INDEX_VISIT]==1u);
    assert(cost.total.counts[CETTA_COST_MATCH_ATTEMPT]==1u);
    uint64_t before=cost.total.work;
    assert(space_query_cursor_step_with_budget(query_cursor,0u,&answer)==SPACE_QUERY_CURSOR_PROGRESS);
    assert(cost.total.work==before);
    space_add_atom_id(&space,store_atom(universe,row));
    assert(space_query_cursor_step(query_cursor,&answer)==SPACE_QUERY_CURSOR_INVALIDATED);
    assert(cost.total.counts[CETTA_COST_MATCH_ATTEMPT]==1u);
    space_query_cursor_free(query_cursor);
    cetta_native_cost_scope_leave(&scope);
    cetta_native_cost_free(&cost);
    space_free(&space);
    bindings_free(&answer);
    puts("PASS: canonical matcher retains nodes, binding trail, path and transport across grants");
}

static void test_owned_need_graph(void) {
    Arena source, young, separate, destination, young_destination, separate_destination;
    arena_init_detached(&source); arena_init_detached(&young);
    arena_init_detached(&separate); arena_init_detached(&destination);
    arena_init_detached(&young_destination); arena_init_detached(&separate_destination);
    arena_set_older_generation(&young, &source);
    arena_set_older_generation(&young_destination, &destination);
    Atom *value = atom_expr2(&source, atom_symbol(&source, "owned-value"), atom_int(&source, 7));
    PrimeNeedSnapshot root = {0}, allocated, running, completed;
    uint64_t cell = 0u;
    assert(prime_need_snapshot_begin(&root));
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    VarId captures[] = {101u, 303u};
    assert(prime_need_snapshot_allocate_closure(&source, &root, value,
        captures, 2u, &allocated, &cell));
#else
    assert(prime_need_snapshot_allocate(&source, &root, value, &allocated, &cell));
#endif
    assert(prime_need_snapshot_start_evaluation(&young, &allocated, cell, 19u, &running));
    assert(prime_need_snapshot_resolve_value(&young, &running, cell, 19u, value, &completed));
    completed.max_storage_key += 307u;
    PrimeNeedCellView a, b;
    assert(prime_need_snapshot_lookup(&allocated, cell, &a));
    assert(prime_need_snapshot_lookup(&completed, cell, &b));
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    assert(a.capture_var_ids == b.capture_var_ids && a.capture_var_count == 2u);
#endif
    PrimeNeedSnapshot independent = completed;
    assert(prime_need_snapshot_promote(&separate, &independent));
    assert(independent.top != completed.top);
    AtomDeepCopyArenaPair pairs[] = {{&source, &destination},
        {&young, &young_destination}, {&separate, &separate_destination}};
    AtomDeepCopySession *session = atom_deep_copy_session_new(&young_destination);
    assert(session && atom_deep_copy_session_set_arena_map(session, pairs, 3u));
    PrimeNeedSnapshot first = completed, again = completed, prefix = allocated;
    PrimeNeedSnapshot different = independent;
    assert(prime_need_snapshot_copy_with_session(&young_destination, session, &first));
    assert(prime_need_snapshot_copy_with_session(&young_destination, session, &again));
    assert(prime_need_snapshot_copy_with_session(&young_destination, session, &prefix));
    assert(prime_need_snapshot_copy_with_session(&young_destination, session, &different));
    assert(first.top == again.top && first.top != different.top);
    assert(first.owner == &young_destination && prefix.owner == &destination &&
        different.owner == &separate_destination);
    assert(first.session_id == completed.session_id &&
        first.max_storage_key == completed.max_storage_key);
    assert(prime_need_snapshot_is_ancestor(&prefix, &first));
    assert(prime_need_snapshot_lookup(&prefix, cell, &a) &&
        prime_need_snapshot_lookup(&first, cell, &b));
    assert(a.origin == b.origin && b.origin == b.cached && b.cache_state == PRIME_NEED_CACHE_VALUE);
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    assert(a.capture_var_ids == b.capture_var_ids && b.capture_var_count == 2u &&
        arena_owns_ptr(&destination, b.capture_var_ids));
#endif
#if CETTA_PRIME_NEED_HEAP_INDEX
    assert((first.heap_index == NULL) == (completed.heap_index == NULL));
    assert((first.lineage_index == NULL) == (completed.lineage_index == NULL));
    assert(first.heap_index == again.heap_index && first.lineage_index == again.lineage_index);
    PrimeNeedSnapshot no_index = completed;
    no_index.heap_index = NULL; no_index.lineage_index = NULL;
    assert(prime_need_snapshot_copy_with_session(&young_destination, session, &no_index));
    assert(no_index.top == first.top && !no_index.heap_index && !no_index.lineage_index);
    assert(prime_need_snapshot_lookup(&no_index, cell, &a) && a.cached == b.cached);
#endif
    assert(prime_need_snapshot_excludes_arena(&first, &source) &&
        prime_need_snapshot_excludes_arena(&first, &young) &&
        prime_need_snapshot_excludes_arena(&different, &separate));
    atom_deep_copy_session_free(session);

    Arena refused;
    arena_init_detached(&refused);
    session = atom_deep_copy_session_new(&refused);
    AtomDeepCopyArenaPair missing[] = {{&source, &refused}};
    assert(session && atom_deep_copy_session_set_arena_map(session, missing, 1u));
    PrimeNeedSnapshot untouched = completed;
    assert(!prime_need_snapshot_copy_with_session(&refused, session, &untouched));
    assert(memcmp(&untouched, &completed, sizeof(untouched)) == 0);
    assert(prime_need_snapshot_lookup(&untouched, cell, &a) && a.cached == value);
    atom_deep_copy_session_free(session);
    arena_free(&refused);

    Arena borrowed_target;
    arena_init_detached(&borrowed_target);
    session = atom_deep_copy_session_new(&borrowed_target);
    AtomDeepCopyArenaPair borrowed[] = {{&source, &source}, {&separate, &borrowed_target}};
    assert(session && atom_deep_copy_session_set_arena_map(session, borrowed, 2u));
    untouched = allocated;
    ArenaMark mark = arena_mark(&source);
    assert(!prime_need_snapshot_copy_with_session(&borrowed_target, session, &untouched));
    assert(memcmp(&untouched, &allocated, sizeof(untouched)) == 0 && arena_at_mark(&source, mark));
    atom_deep_copy_session_free(session);
    arena_free(&borrowed_target);

#if CETTA_PRIME_NEED_HEAP_INDEX
    if (independent.heap_index) {
        Arena failed_prefix, failed_suffix;
        arena_init_detached(&failed_prefix); arena_init_detached(&failed_suffix);
        arena_set_older_generation(&failed_suffix, &failed_prefix);
        session = atom_deep_copy_session_new(&failed_suffix);
        AtomDeepCopyArenaPair incomplete[] = {{&source, &failed_prefix}, {&young, &failed_suffix}};
        assert(session && atom_deep_copy_session_set_arena_map(session, incomplete, 2u));
        PrimeNeedSnapshot bad_index = completed;
        bad_index.heap_index = independent.heap_index;
        untouched = bad_index;
        assert(!prime_need_snapshot_copy_with_session(&failed_suffix, session, &untouched));
        assert(memcmp(&untouched, &bad_index, sizeof(untouched)) == 0);
        /* A partial graph cannot be published by retrying the same session. */
        untouched = completed;
        assert(!prime_need_snapshot_copy_with_session(&failed_suffix, session, &untouched));
        assert(memcmp(&untouched, &completed, sizeof(untouched)) == 0 &&
            prime_need_snapshot_lookup(&completed, cell, &a) && a.cached == value);
        atom_deep_copy_session_free(session);
        arena_free(&failed_suffix); arena_free(&failed_prefix);
    }
#endif
    arena_free(&young); arena_free(&source); arena_free(&separate);
    assert(prime_need_snapshot_lookup(&first, cell, &b) &&
        b.origin == b.cached && b.cache_state == PRIME_NEED_CACHE_VALUE &&
        atom_is_symbol(b.cached->expr.elems[0], "owned-value"));
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    assert(b.capture_var_ids[0] == 101u && b.capture_var_ids[1] == 303u);
#endif
    PrimeNeedSnapshot extra;
    uint64_t extra_cell = 0u;
    assert(prime_need_snapshot_allocate(&young_destination, &first, b.cached, &extra, &extra_cell));
    assert(prime_need_snapshot_is_ancestor(&first, &extra) &&
        prime_need_snapshot_lookup(&extra, cell, &a) && a.cached == b.cached);
    arena_free(&young_destination); arena_free(&destination); arena_free(&separate_destination);
}

static void test_owned_builder_need_graph(void) {
    Arena source, destination, wrong;
    arena_init_detached(&source); arena_init_detached(&destination);
    arena_init_detached(&wrong);
    Atom *value=atom_expr2(&source,atom_symbol(&source,"builder-owned"),atom_int(&source,7));
    PrimeNeedSnapshot root={0},allocated,running,completed,current;
    uint64_t cell=0u,second=0u;
    assert(prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(&source,&root,value,&allocated,&cell) &&
        prime_need_snapshot_start_evaluation(&source,&allocated,cell,19u,&running) &&
        prime_need_snapshot_resolve_value(&source,&running,cell,19u,value,&completed) &&
        prime_need_snapshot_allocate(&source,&completed,value,&current,&second));
    Bindings seed;bindings_init(&seed);*bindings_need_mut(&seed)=completed;
    BindingsBuilder original,copy;
    assert(bindings_builder_init(&original,&seed));
    uint32_t mark=bindings_builder_save(&original);
    assert(bindings_builder_add_id_fresh(&original,98701u,SYMBOL_ID_NONE,value));
    *bindings_need_mut(&original.current)=current;
    assert(original.prime_trail_len==1u &&
        original.prime_trail[0].prime_need.top==completed.top &&
        bindings_need_view(&original.current)->top==current.top &&
        completed.top!=current.top);
    assert(bindings_builder_clone(&copy,&original));
    AtomDeepCopySession *atoms=atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pair={&source,&destination};
    assert(atoms && atom_deep_copy_session_set_arena_map(atoms,&pair,1u));
    PrimeNeedSnapshot old_image=completed,current_image=current;
    assert(prime_need_snapshot_copy_with_session(&destination,atoms,&old_image) &&
        prime_need_snapshot_copy_with_session(&destination,atoms,&current_image));
    assert(!bindings_builder_copy_owned_atoms_with_session(&copy,&wrong,atoms) &&
        bindings_need_view(&copy.current)->top==current.top &&
        copy.prime_trail[0].prime_need.top==completed.top);
    assert(bindings_builder_copy_owned_atoms_with_session(&copy,&destination,atoms));
    assert(bindings_need_view(&copy.current)->top==current_image.top &&
        copy.prime_trail[0].prime_need.top==old_image.top);
    assert(bindings_need_view(&original.current)->top==current.top &&
        original.prime_trail[0].prime_need.top==completed.top);
    atom_deep_copy_session_free(atoms);bindings_builder_free(&original);
    bindings_free(&seed);arena_free(&source);arena_free(&wrong);
    PrimeNeedCellView view;
    assert(prime_need_snapshot_lookup(bindings_need_view(&copy.current),second,&view) &&
        view.cache_state==PRIME_NEED_CACHE_EMPTY);
    bindings_builder_rollback(&copy,mark);
    assert(bindings_need_view(&copy.current)->top==old_image.top &&
        !prime_need_snapshot_lookup(bindings_need_view(&copy.current),second,&view) &&
        prime_need_snapshot_lookup(bindings_need_view(&copy.current),cell,&view) &&
        view.cache_state==PRIME_NEED_CACHE_VALUE && view.origin==view.cached &&
        atom_is_symbol(view.cached->expr.elems[0],"builder-owned"));
    bindings_builder_free(&copy);arena_free(&destination);
    puts("PASS: owned builder retains shared current and rollback Need frame images");
}

typedef struct {
    uint64_t identical, reach, self, work, append;
    bool result;
} OwnedBranchFuture;

static OwnedBranchFuture owned_branch_future(PrimeNeedBranchState a,
    const PrimeNeedBranchState *b, StateCell *cell, Arena *arena) {
    CettaNativeCostLedger ledger;
    CettaNativeCostScope scope;
    cetta_native_cost_init(&ledger);
    bool was_enabled=cetta_runtime_stats_is_enabled();
    CettaRuntimeStats prior;cetta_runtime_stats_snapshot(&prior);
    cetta_runtime_stats_enable();
    cetta_native_cost_scope_enter(&scope,&ledger,123u);
    bool accepted=prime_need_branch_state_merge(&a,b);
    Atom *value=NULL;
    bool value_ok=accepted && prime_need_branch_state_value(&a,cell,&value) &&
        atom_eq(value,atom_int(arena,7));
    PrimeNeedBranchState next={0};
    assert(value_ok && prime_need_branch_state_write(arena,&a,cell,value,
        atom_int(arena,9),&next));
    assert(prime_need_branch_state_value(&next,cell,&value) && atom_eq(value,atom_int(arena,9)));
    cetta_native_cost_scope_leave(&scope);
    CettaRuntimeStats stats;cetta_runtime_stats_snapshot(&stats);
    if(!was_enabled)cetta_runtime_stats_disable();
    OwnedBranchFuture result={
        .identical=stats.counters[CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_MERGE_IDENTICAL]-prior.counters[CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_MERGE_IDENTICAL],
        .reach=stats.counters[CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_QUERY]-prior.counters[CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_QUERY],
        .self=stats.counters[CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_SELF_ACCEPT]-prior.counters[CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_SELF_ACCEPT],
        .work=ledger.total.work,.append=ledger.total.counts[CETTA_COST_RECEIPT_APPEND],
        .result=value_ok};
    cetta_native_cost_free(&ledger);return result;
}

static bool owned_ordered_account(const CettaWeightLedger *ledger) {
    const CettaWeightFactor **items=cetta_weight_ledger_ordered(ledger);
    bool ok=ledger->length==2u && ledger->claim_count==2u && items &&
        items[0]->occurrence==401u && items[0]->dependency==501u &&
        items[1]->occurrence==402u && items[1]->dependency==502u &&
        items[0]->value->ground.ival==0 && items[1]->value->ground.ival==3 &&
        ledger->claims && ledger->claims->occurrence==402u && ledger->claims->handler==601u &&
        ledger->claims->previous && ledger->claims->previous->occurrence==401u &&
        ledger->claims->previous->handler==601u && !ledger->claims->previous->previous;
    free(items);return ok;
}

static bool owned_receipt_factor_case(bool mapped,bool distinct,bool reverse) {
    Arena source,separate,destination,separate_destination;
    arena_init_detached(&source);arena_init_detached(&separate);
    arena_init_detached(&destination);arena_init_detached(&separate_destination);
    StateCell cell={0};
    PrimeNeedBranchState root={0},branch={0};
    assert(prime_need_branch_state_begin(&source,&root));
    assert(prime_need_branch_state_write(&source,&root,&cell,
        atom_int(&source,0),atom_int(&source,7),&branch));
    CettaWeightLedger weights={0},prefix={0},selected={0};
    assert(cetta_weight_ledger_append(&source,&weights,401u,501u,atom_int(&source,0),&weights));
    assert(cetta_weight_ledger_append(&source,&weights,402u,502u,atom_int(&source,3),&weights));
    assert(cetta_weight_ledger_handle(&source,&weights,&prefix,601u,&weights,&selected));
    Bindings a,b;bindings_init(&a);bindings_init(&b);
    const PrimeNeedReceipt *receipt=NULL;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    receipt=&branch;
#endif
    bindings_prime_set(&a,NULL,&branch,701u,receipt,&weights);
    assert(bindings_clone(&b,&a));
    if(distinct)assert(bindings_promote_atoms_to_arena(&b,&separate));
    const PrimeNeedBranchState *sa=bindings_branch_state_view(&a),*sb=bindings_branch_state_view(&b);
    const CettaWeightLedger *wa=bindings_weight_view(&a),*wb=bindings_weight_view(&b);
    bool source_shared=sa->top==sb->top;
    assert(source_shared==!distinct && (wa->last==wb->last)==!distinct &&
        (wa->claims==wb->claims)==!distinct && prime_need_branch_state_equal(sa,sb) &&
        cetta_weight_ledger_equal(wa,wb) && owned_ordered_account(wa) && owned_ordered_account(wb));
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    bool receipt_shared_a=bindings_receipt_view(&a)->top==sa->top;
    bool receipt_shared_b=bindings_receipt_view(&b)->top==sb->top;
    /* Ordinary promotion intentionally remains separate: it has already
     * rebuilt b's semantic and observational carriers independently. */
    assert(receipt_shared_a && receipt_shared_b==!distinct);
#endif
    OwnedBranchFuture before=owned_branch_future(*sa,sb,&cell,&source);
    Bindings ca,cb;bindings_init(&ca);bindings_init(&cb);
    assert(bindings_clone(&ca,&a) && bindings_clone(&cb,&b));
    AtomDeepCopySession *copy=atom_deep_copy_session_new(&destination);assert(copy);
    if(mapped) {
        AtomDeepCopyArenaPair pairs[]={{&source,&destination},{&separate,&separate_destination}};
        assert(atom_deep_copy_session_set_arena_map(copy,pairs,2u));
    }
    assert(bindings_copy_owned_atoms_with_session(reverse?&cb:&ca,&destination,copy));
    assert(bindings_copy_owned_atoms_with_session(reverse?&ca:&cb,&destination,copy));
    const PrimeNeedBranchState *da=bindings_branch_state_view(&ca),*db=bindings_branch_state_view(&cb);
    const CettaWeightLedger *dwa=bindings_weight_view(&ca),*dwb=bindings_weight_view(&cb);
    bool receipt_alias=(da->top==db->top)==source_shared;
    bool factor_alias=(dwa->last==dwb->last)==source_shared;
    bool claim_alias=(dwa->claims==dwb->claims)==source_shared;
    assert(da->top!=sa->top && db->top!=sb->top &&
        dwa->last!=wa->last && dwb->last!=wb->last &&
        dwa->claims!=wa->claims && dwb->claims!=wb->claims &&
        arena_owns_ptr(da->owner,da->top) && arena_owns_ptr(db->owner,db->top) &&
        arena_owns_ptr(dwa->owner,dwa->last) && arena_owns_ptr(dwb->owner,dwb->last) &&
        arena_owns_ptr(dwa->owner,dwa->claims) && arena_owns_ptr(dwb->owner,dwb->claims));
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    assert((bindings_receipt_view(&ca)->top==da->top)==receipt_shared_a &&
        (bindings_receipt_view(&cb)->top==db->top)==receipt_shared_b);
#endif
    assert(owned_ordered_account(dwa) && owned_ordered_account(dwb));
    atom_deep_copy_session_free(copy);
    bindings_free(&a);bindings_free(&b);arena_free(&source);arena_free(&separate);
    OwnedBranchFuture after=owned_branch_future(*da,db,&cell,&destination);
    CettaWeightLedger merged=*dwa,handled={0},unclaimed={0};
    assert(cetta_weight_ledger_merge(&merged,dwb) && owned_ordered_account(&merged));
    assert(cetta_weight_ledger_handle(&destination,&merged,&prefix,602u,&handled,&unclaimed));
    assert(unclaimed.length==0u && owned_ordered_account(&handled));
    bool native_cost_same=before.work==after.work && before.append==after.append && before.append==1u;
    bool diagnostic_same=before.identical==after.identical && before.reach==after.reach && before.self==after.self;
    bool passed=receipt_alias && factor_alias && claim_alias && diagnostic_same && native_cost_same;
    printf("{\"mapped\":%s,\"distinct\":%s,\"reverse\":%s,\"source_shared\":%s,"
        "\"receipt_alias\":%s,\"factor_alias\":%s,\"claim_alias\":%s,"
        "\"before_identical\":%"PRIu64",\"after_identical\":%"PRIu64","
        "\"before_reach\":%"PRIu64",\"after_reach\":%"PRIu64","
        "\"native_cost_same\":%s,\"native_work\":%"PRIu64",\"ordered_claims_preserved\":true,"
        "\"semantic_result_preserved\":true,\"passed\":%s}\n",
        mapped?"true":"false",distinct?"true":"false",reverse?"true":"false",source_shared?"true":"false",
        receipt_alias?"true":"false",factor_alias?"true":"false",claim_alias?"true":"false",
        before.identical,after.identical,before.reach,after.reach,native_cost_same?"true":"false",after.work,passed?"true":"false");
    bindings_free(&ca);bindings_free(&cb);arena_free(&destination);arena_free(&separate_destination);
    return passed;
}
static void test_owned_receipt_factor_graph(void) {
    for(unsigned mapped=0u;mapped<2u;mapped++)for(unsigned distinct=0u;distinct<2u;distinct++)
        for(unsigned reverse=0u;reverse<2u;reverse++)assert(owned_receipt_factor_case(mapped,distinct,reverse));
    puts("PASS: owned receipt, factor and claim paths preserve physical sharing, ordered accounts and future work");
}

static void test_owned_receipt_factor_refusal(void) {
    Arena source,destination,other,other_destination;
    arena_init_detached(&source);arena_init_detached(&destination);
    arena_init_detached(&other);arena_init_detached(&other_destination);
    StateCell cell={0};
    PrimeNeedBranchState root={0},branch={0};
    CettaWeightLedger weights={0},prefix={0},selected={0};
    assert(prime_need_branch_state_begin(&source,&root) &&
        prime_need_branch_state_write(&source,&root,&cell,atom_int(&source,0),atom_int(&source,7),&branch) &&
        cetta_weight_ledger_append(&source,&weights,401u,501u,atom_int(&source,0),&weights) &&
        cetta_weight_ledger_append(&source,&weights,402u,502u,atom_int(&source,3),&weights) &&
        cetta_weight_ledger_handle(&source,&weights,&prefix,601u,&weights,&selected));
    ArenaMark destination_base=arena_mark(&destination);
    AtomDeepCopySession *session=atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pairs[]={{&source,&destination},{&other,&other_destination}};
    assert(session && atom_deep_copy_session_set_arena_map(session,pairs,2u));
    PrimeNeedBranchState copied=branch;CettaWeightLedger copied_weights=weights;
    assert(prime_need_branch_state_copy_with_session(&destination,session,&copied) &&
        cetta_weight_ledger_copy_with_session(&destination,session,&copied_weights));
    PrimeNeedBranchState bad=branch;
    bad.session_id++;
    assert(!prime_need_branch_state_copy_with_session(&destination,session,&bad) &&
        bad.top==branch.top && bad.session_id==branch.session_id+1u && bad.owner==branch.owner);
    bad=branch;bad.owner=&other;
    assert(!prime_need_branch_state_copy_with_session(&destination,session,&bad) &&
        bad.top==branch.top && bad.owner==&other);
    CettaWeightLedger wrong=weights;wrong.owner=&other;
    assert(!cetta_weight_ledger_copy_with_session(&destination,session,&wrong) &&
        wrong.last==weights.last && wrong.claims==weights.claims && wrong.owner==&other);
    wrong=weights;wrong.claim_count++;
    assert(!cetta_weight_ledger_copy_with_session(&destination,session,&wrong) &&
        wrong.last==weights.last && wrong.claim_count==weights.claim_count+1u);
    wrong=weights;wrong.length--;
    assert(!cetta_weight_ledger_copy_with_session(&destination,session,&wrong) &&
        wrong.last==weights.last && wrong.length==weights.length-1u);
    arena_reset(&destination,destination_base);
    bad=branch;wrong=weights;
    assert(!prime_need_branch_state_copy_with_session(&destination,session,&bad) &&
        !cetta_weight_ledger_copy_with_session(&destination,session,&wrong) &&
        bad.top==branch.top && wrong.last==weights.last && wrong.claims==weights.claims);
    atom_deep_copy_session_free(session);
    session=atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair borrowed[]={{&other,&destination},{&source,&source}};
    assert(session && atom_deep_copy_session_set_arena_map(session,borrowed,2u));
    ArenaMark mark=arena_mark(&source);
    bad=branch;wrong=weights;
    assert(!prime_need_branch_state_copy_with_session(&destination,session,&bad) &&
        !cetta_weight_ledger_copy_with_session(&destination,session,&wrong));
    ArenaMark after=arena_mark(&source);
    assert(mark.head==after.head && mark.used==after.used && mark.live_bytes==after.live_bytes &&
        mark.persistent_bytes==after.persistent_bytes && bad.top==branch.top && wrong.last==weights.last);
    atom_deep_copy_session_free(session);
    Atom *value=NULL;
    assert(prime_need_branch_state_value(&branch,&cell,&value) && atom_eq(value,atom_int(&source,7)) &&
        owned_ordered_account(&weights));
    arena_free(&source);arena_free(&destination);arena_free(&other);arena_free(&other_destination);
    puts("PASS: owned metadata hits retain source scope, owner, counts and epoch checks; borrowed owners refuse unchanged");
}

static void test_owned_receipt_factor_rollback(void) {
    Arena source,destination;
    arena_init_detached(&source);arena_init_detached(&destination);
    StateCell cell={0};
    PrimeNeedBranchState root={0},branch={0},next={0};
    CettaWeightLedger weights={0},prefix={0},selected={0};
    assert(prime_need_branch_state_begin(&source,&root) &&
        prime_need_branch_state_write(&source,&root,&cell,atom_int(&source,0),atom_int(&source,7),&branch) &&
        prime_need_branch_state_write(&source,&branch,&cell,atom_int(&source,7),atom_int(&source,9),&next) &&
        cetta_weight_ledger_append(&source,&weights,401u,501u,atom_int(&source,0),&weights) &&
        cetta_weight_ledger_append(&source,&weights,402u,502u,atom_int(&source,3),&weights) &&
        cetta_weight_ledger_handle(&source,&weights,&prefix,601u,&weights,&selected));
    Bindings seed;bindings_init(&seed);bindings_prime_set(&seed,NULL,&branch,711u,NULL,&weights);
    BindingsBuilder original,copied;
    assert(bindings_builder_init(&original,&seed));
    uint32_t mark=bindings_builder_save(&original);
    assert(bindings_builder_add_id_fresh(&original,881991u,SYMBOL_ID_NONE,atom_int(&source,4)));
    *bindings_branch_state_mut(&original.current)=next;
    assert(original.prime_trail_len==1u && original.prime_trail[0].branch_state.top==branch.top &&
        original.prime_trail[0].weights.last==weights.last &&
        bindings_weight_view(&original.current)->claims==original.prime_trail[0].weights.claims);
    assert(bindings_builder_clone(&copied,&original));
    AtomDeepCopySession *session=atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pair={&source,&destination};
    assert(session && atom_deep_copy_session_set_arena_map(session,&pair,1u));
    PrimeNeedBranchState old_copy=branch,new_copy=next;CettaWeightLedger weight_copy=weights;
    assert(prime_need_branch_state_copy_with_session(&destination,session,&old_copy) &&
        prime_need_branch_state_copy_with_session(&destination,session,&new_copy) &&
        cetta_weight_ledger_copy_with_session(&destination,session,&weight_copy) &&
        bindings_builder_copy_owned_atoms_with_session(&copied,&destination,session));
    assert(copied.prime_trail[0].branch_state.top==old_copy.top &&
        bindings_branch_state_view(&copied.current)->top==new_copy.top &&
        copied.prime_trail[0].weights.last==weight_copy.last &&
        bindings_weight_view(&copied.current)->claims==weight_copy.claims);
    atom_deep_copy_session_free(session);bindings_builder_free(&original);bindings_free(&seed);arena_free(&source);
    Atom *value=NULL;
    assert(prime_need_branch_state_value(bindings_branch_state_view(&copied.current),&cell,&value) &&
        atom_eq(value,atom_int(&destination,9)));
    bindings_builder_rollback(&copied,mark);
    assert(bindings_branch_state_view(&copied.current)->top==old_copy.top &&
        bindings_weight_view(&copied.current)->last==weight_copy.last &&
        bindings_weight_view(&copied.current)->claims==weight_copy.claims &&
        prime_need_branch_state_value(bindings_branch_state_view(&copied.current),&cell,&value) &&
        atom_eq(value,atom_int(&destination,7)));
    bindings_builder_free(&copied);arena_free(&destination);
    puts("PASS: receipt and coefficient rollback paths share enclosing owned images after source retirement");
}

static void test_reset_releases_storage_after_readers(
        TermUniverse *universe, Arena *scratch) {
    enum { ROWS = 257, EPISODES = 24 };
    AtomId values[2] = {
        store_atom(universe, atom_int(scratch, 17)),
        store_atom(universe, atom_int(scratch, 29)),
    };
    uint8_t mask[ROWS];
    memset(mask, 1, sizeof(mask));
    Atom *query = atom_var(scratch, "reset-retained-reader");
    for (unsigned episode = 0u; episode < EPISODES; episode++) {
        Space space;
        space_init_with_universe(&space, universe);
        space.kind = SPACE_KIND_STACK;
        for (CettaIndex row = 0u; row < ROWS; row++)
            space_add_atom_id(&space, values[row % 2u]);
        space_match_native_ensure_trie(&space);
        SpaceOccurrenceCursor first, second;
        assert(space_occurrence_cursor_init(&space, query, &first));
        assert(space_occurrence_cursor_clone(&first, &second));
        SpaceReadToken token = space_read_token(&space);
        uint64_t identity = space.instance_id;
#if CETTA_BUILD_WITH_RUNTIME_STATS
        CettaRuntimeStats before, after;
        cetta_runtime_stats_snapshot(&before);
        size_t retained_bytes = ROWS *
            cetta_atom_id_storage_width_bytes_from_bits(space.native.atom_id_width_bits);
#endif
        CettaCount removed = 0u;
        assert(space_remove_occurrence_mask_stable(&space, mask, ROWS, &removed));
        assert(removed == ROWS && space_length64(&space) == 0u);
        assert(space.native.atom_ids == NULL && space.native.cap == 0u);
        assert(space.match_backend.native.match_trie == NULL);
        assert(space.match_backend.native.stree == NULL);
        assert(space.instance_id == identity);
        assert(!space_read_token_matches_live_space(token, &space));
        space_add_atom_id(&space, values[1]);
        assert(space_length64(&space) == 1u);
        /* Cancelling one reader does not retire the other reader's view. */
        space_occurrence_cursor_release(&first);
#if CETTA_BUILD_WITH_RUNTIME_STATS
        cetta_runtime_stats_snapshot(&after);
        assert(after.counters[CETTA_RUNTIME_COUNTER_SPACE_RETIRED_OCCURRENCE_RELEASE_BYTES] ==
               before.counters[CETTA_RUNTIME_COUNTER_SPACE_RETIRED_OCCURRENCE_RELEASE_BYTES]);
#endif
        space_free(&space);
        for (CettaIndex expected = 0u; expected < ROWS; expected++) {
            CettaIndex index = UINT64_MAX;
            assert(space_occurrence_cursor_next(&second, &index) ==
                   SPACE_OCCURRENCE_CURSOR_ITEM);
            assert(index == expected);
            assert(atom_eq(space_occurrence_cursor_atom(&second, index),
                term_universe_get_atom(universe, values[expected % 2u])));
        }
        CettaIndex end = UINT64_MAX;
        assert(space_occurrence_cursor_next(&second, &end) == SPACE_OCCURRENCE_CURSOR_END);
        space_occurrence_cursor_release(&second);
#if CETTA_BUILD_WITH_RUNTIME_STATS
        cetta_runtime_stats_snapshot(&after);
        assert(after.counters[CETTA_RUNTIME_COUNTER_SPACE_RETIRED_OCCURRENCE_RELEASE_BYTES] ==
               before.counters[CETTA_RUNTIME_COUNTER_SPACE_RETIRED_OCCURRENCE_RELEASE_BYTES] +
               retained_bytes);
#endif
    }
}

int main(void) {
    SymbolTable symbols;
    Arena persistent;
    Arena scratch;
    TermUniverse universe;

    init_symbols(&symbols);
    arena_init(&persistent);
    arena_init(&scratch);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);

#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
#endif

    test_cold_candidate_cursor(&universe, &scratch);
    test_duplicate_order_and_open_patterns(&universe, &scratch);
    test_remove_all_releases_small_indexes(&universe, &scratch);
    test_partial_remove_releases_small_indexes(&universe, &scratch);
    test_bounded_graph_index(&universe, &scratch);
    test_queue_linearization(&universe, &scratch);
    test_repeated_churn_is_amortized(&universe, &scratch);
    test_occurrence_cursor_exact_clone(&universe, &scratch);
    test_atom_id_removal_preserves_pinned_view(&universe, &scratch);
    test_retained_query_rows(&universe, &scratch);
    const SpaceEngine overlay_engines[]={SPACE_ENGINE_NATIVE,SPACE_ENGINE_NATIVE_CANDIDATE_EXACT};
    for(size_t i=0u;i<sizeof(overlay_engines)/sizeof(*overlay_engines);i++) {
        test_retained_query_clone(&universe,&scratch,overlay_engines[i],false);
        test_retained_query_clone(&universe,&scratch,overlay_engines[i],true);
        test_retained_query_relocation(&universe,&persistent,overlay_engines[i],false);
        test_retained_query_relocation(&universe,&persistent,overlay_engines[i],true);
        test_owned_query_authority(&universe,&persistent,overlay_engines[i]);
        test_retained_overlay_query_authority(&universe, &scratch, overlay_engines[i]);
        test_overlay_occurrence_scan(&universe, &scratch, overlay_engines[i]);
        test_incremental_overlay_query(&universe, &scratch, overlay_engines[i]);
        test_overlay_query_join(&universe, &scratch, overlay_engines[i]);
    }
    test_retained_query_fallback_refusal(&universe, &scratch);
    test_owned_query_fallback(&universe,&persistent);
    test_retained_query_invalidation(&universe, &scratch);
    test_retained_match_worklist(&universe, &scratch);
    test_retained_match_cold_thread(&scratch);
    test_retained_match_clone(&scratch);
    test_retained_match_relocation();
    test_owned_need_graph();
    test_owned_builder_need_graph();
    test_owned_receipt_factor_graph();
    test_owned_receipt_factor_refusal();
    test_owned_receipt_factor_rollback();
    test_retained_list_match(&scratch);
    test_reset_releases_storage_after_readers(&universe, &scratch);

#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats stats;
    cetta_runtime_stats_snapshot(&stats);
    uint64_t attempts = stats.counters[
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_ATTEMPT];
    uint64_t commits = stats.counters[
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_COMMIT];
    uint64_t declines = stats.counters[
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_DECLINE];
    uint64_t errors = stats.counters[
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_ERROR];
    assert(attempts == commits + declines + errors);
    assert(commits > 0u);
    assert(declines == 0u);
    assert(errors == 1u);
    assert(stats.counters[
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_REMOVED_LEAF]
        > 0u);
    assert(stats.counters[
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_RETAINED_MOVE]
        > 0u);
    assert(stats.counters[
        CETTA_RUNTIME_COUNTER_NATIVE_STALE_INDEX_AMORTIZED_REBUILD] > 0u);
    assert(stats.counters[
        CETTA_RUNTIME_COUNTER_NATIVE_STALE_INDEX_SMALL_SPACE_RELEASE] > 0u);
    cetta_runtime_stats_disable();
#endif

#if CETTA_PRIME_EVAL_STACK
    test_retained_query_fallback_body_progress();
    test_overlay_query_control_handoff();
#endif

    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: stable occurrence coordinate transport");
    return 0;
}
