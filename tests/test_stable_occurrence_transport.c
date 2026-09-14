#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "space.h"
#include "space_match_backend.h"
#include "stats.h"
#include "symbol.h"

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

    test_duplicate_order_and_open_patterns(&universe, &scratch);
    test_remove_all_releases_small_indexes(&universe, &scratch);
    test_queue_linearization(&universe, &scratch);
    test_repeated_churn_is_amortized(&universe, &scratch);

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

    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: stable occurrence coordinate transport");
    return 0;
}
