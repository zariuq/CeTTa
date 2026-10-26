#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "space.h"
#include "space_match_backend.h"
#include "space_path_index.h"
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
    space_match_native_ensure_trie(space);
    CettaIndex *candidates = NULL;
    CettaIndex candidate_len =
        space_match_backend_candidates64(space, query, &candidates);
    assert(candidate_len != 0u);
    for (CettaIndex i = 0u; i < candidate_len; i++)
        assert(candidates[i] < space_length64(space));
    free(candidates);

    /* The substitution tree is rented: queries answer from the trie's
     * candidates until those have cost as many matches as the space has
     * rows, and then it is built. */
    CettaCount rows = space_length64(space);
    for (CettaCount query_count = 0u;
         query_count <= rows && !space->match_backend.native.stree;
         query_count++) {
        SubstMatchSet matches;
        space_match_backend_query(space, scratch, query, &matches);
        smset_free(&matches);
    }
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
    Atom *x = atom_var_with_spelling(scratch, x_spelling, var_id_from_base(UINT64_C(70001)));
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

static void assert_trie_matches_ground_rows(
        DiscNode *trie, Atom *const *rows, CettaIndex len,
        Atom *const *queries, CettaIndex query_count) {
    for (CettaIndex q = 0u; q < query_count; q++) {
        CettaIndex *actual = NULL, count = 0u, capacity = 0u;
        disc_lookup(trie, queries[q], &actual, &count, &capacity);
        CettaIndex found = 0u;
        for (CettaIndex i = 0u; i < len; i++) {
            if (!atom_eq(rows[i], queries[q]))
                continue;
            assert(found < count && actual[found] == i);
            found++;
        }
        assert(found == count);
        CettaIndex counted = UINT64_MAX;
        assert(disc_count_rigid_exact_path(trie, queries[q], &counted));
        assert(counted == found);
        free(actual);
    }
}

static void test_leaf_registry_against_ground_rows(Arena *scratch) {
    enum { QUERY_COUNT = 128, ROW_CAPACITY = 256 };
    Atom *queries[QUERY_COUNT];
    for (unsigned i = 0u; i < QUERY_COUNT; i++) {
        if (i % 4u == 0u) {
            queries[i] = atom_int(scratch, (int64_t)i);
        } else if (i % 4u == 1u) {
            char name[64];
            snprintf(name, sizeof(name), "coordinate-registry-symbol-%u", i);
            queries[i] = atom_symbol(scratch, name);
        } else {
            Atom *items[8];
            unsigned arity = i % 4u == 2u ? 2u : 1u + (i / 4u) % 7u;
            items[0] = atom_int(scratch, (int64_t)i);
            for (unsigned j = 1u; j < arity; j++)
                items[j] = atom_int(scratch, (int64_t)(i + j));
            queries[i] = atom_expr(scratch, items, arity);
        }
    }
    for (unsigned initially_empty = 0u; initially_empty < 2u; initially_empty++) {
        DiscNode *trie = disc_node_new();
        if (initially_empty)
            assert(disc_transport_stable_coordinates(trie, NULL, 0u) == 0u);
        Atom *rows[ROW_CAPACITY];
        CettaIndex len = 0u;
        for (unsigned i = 0u; i < 110u; i++) {
            rows[len] = queries[i < 104u ? i : 0u];
            disc_insert(trie, rows[len], len);
            len++;
        }
        for (unsigned round = 0u; round < 16u; round++) {
            CettaIndex coordinates[ROW_CAPACITY], target = 0u;
            Atom *erased = queries[(round * 7u) % QUERY_COUNT];
            for (CettaIndex i = 0u; i < len; i++) {
                if ((i + round) % 5u == 0u || atom_eq(rows[i], erased)) {
                    coordinates[i] = SPACE_OCCURRENCE_COORDINATE_REMOVED;
                } else {
                    coordinates[i] = target;
                    rows[target++] = rows[i];
                }
            }
            assert(disc_transport_stable_coordinates(trie, coordinates, len)
                   == len - target);
            len = target;
            assert_trie_matches_ground_rows(trie, rows, len, queries, QUERY_COUNT);

            /* Reuse both inline and array leaves after their last occurrence
             * disappears, and create fresh branches after cache preparation. */
            Atom *inserted[] = {erased, erased, queries[104u + round]};
            for (unsigned i = 0u; i < 3u; i++) {
                assert(len < ROW_CAPACITY);
                rows[len] = inserted[i];
                disc_insert(trie, rows[len], len);
                len++;
            }
            assert_trie_matches_ground_rows(trie, rows, len, queries, QUERY_COUNT);
        }
        CettaIndex remove_all[ROW_CAPACITY];
        for (CettaIndex i = 0u; i < len; i++)
            remove_all[i] = SPACE_OCCURRENCE_COORDINATE_REMOVED;
        assert(disc_transport_stable_coordinates(trie, remove_all, len) == len);
        assert_trie_matches_ground_rows(trie, rows, 0u, queries, QUERY_COUNT);
        disc_node_free(trie);
    }
    puts("PASS: leaf registry preserves 8448 ground lookup/count observations through lazy preparation, erasure, reuse and new branches");
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
        var_id_from_base(UINT64_C(81001)));
    SpaceOccurrenceCursor source;
    SpaceOccurrenceCursor clone;
    assert(space_occurrence_cursor_init(&space, query, &source));
    CettaIndex index = UINT64_MAX;
    assert(space_occurrence_cursor_next(&source, &index, NULL) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 0u);

    /* Later appends are outside both logical-update ceilings. */
    space_add_atom_id(
        &space, store_atom(universe, atom_int(scratch, 99)));
    assert(space_occurrence_cursor_clone(&source, &clone));
    for (CettaIndex expected = 1u; expected < 4u; expected++) {
        CettaIndex source_index = UINT64_MAX;
        CettaIndex clone_index = UINT64_MAX;
        assert(space_occurrence_cursor_next(&source, &source_index, NULL) ==
               SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(space_occurrence_cursor_next(&clone, &clone_index, NULL) ==
               SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(source_index == expected);
        assert(clone_index == expected);
    }
    assert(space_occurrence_cursor_next(&source, &index, NULL) ==
           SPACE_OCCURRENCE_CURSOR_END);
    assert(space_occurrence_cursor_next(&clone, &index, NULL) ==
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

static void test_occurrence_cursor_fused_delivery(void) {
    for (unsigned format = 0u; format < 2u; format++)
    for (unsigned backend = 0u; backend < 2u; backend++)
    for (unsigned query_kind = 0u; query_kind < 4u; query_kind++)
    for (unsigned marked = 0u; marked < 2u; marked++) {
        Arena persistent, scratch;
        arena_init(&persistent);
        arena_init(&scratch);
        TermUniverse universe;
        assert(term_universe_init_with_store_format(&universe, format
            ? TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1
            : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1));
        term_universe_set_persistent_arena(&universe, &persistent);
        Space space;
        space_init_with_universe(&space, &universe);
        assert(space_match_backend_try_set(&space, backend
            ? SPACE_ENGINE_NATIVE_CANDIDATE_EXACT : SPACE_ENGINE_NATIVE));
        SymbolId edge = symbol_intern_cstr(g_symbols, "fused-edge");
        AtomId ids[80];
        Atom *expected[80];
        CettaCount visible = 0u;
        for (unsigned i = 0u; i < 80u; i++) {
            ids[i] = store_atom(&universe,
                edge_value(&scratch, edge, i == 17u ? 7 : (int64_t)i));
            space_add_atom_id(&space, ids[i]);
            if (!marked || (i != 3u && i != 6u))
                expected[visible++] = term_universe_get_atom(&universe, ids[i]);
        }
        if (marked) {
            CettaIndex removed[] = {3u, 6u};
            assert(space_remove_logical_positions(&space, removed, 2u));
            assert(space_has_marks(&space));
        }
        Atom *x = atom_var_with_id(&scratch, "fused-x", fresh_var_id());
        Atom *y = atom_var_with_id(&scratch, "fused-y", fresh_var_id());
        Atom *query = query_kind == 0u ? x
            : query_kind == 1u ? binary(&scratch, edge, x, y)
            : query_kind == 2u ? edge_value(&scratch, edge, 7)
            : binary(&scratch, edge, atom_int(&scratch, 7), y);
        SpaceOccurrenceCursor source, reference;
        assert(space_occurrence_cursor_init(&space, query, &source));
        bool cloned = space_occurrence_cursor_clone(&source, &reference);
        if (!cloned)
            fprintf(stderr, "fused cursor clone: format=%u backend=%u query=%u marked=%u prefix=%llu current=%llu ceiling=%llu rows=%llu\n",
                format, backend, query_kind, marked,
                (unsigned long long)source.prefix_epoch,
                (unsigned long long)space.prefix_epoch,
                (unsigned long long)source.ceiling,
                (unsigned long long)space.native.len);
        assert(cloned);
        CettaIndex index = UINT64_MAX;
        Atom *row = expected[0];
        uint64_t epoch = source.prefix_epoch;
        source.prefix_epoch++;
        assert(space_occurrence_cursor_next(&source, &index, &row) ==
               SPACE_OCCURRENCE_CURSOR_INVALIDATED && !row && index == 0u);
        source.prefix_epoch = epoch;
        assert(space_occurrence_cursor_next(NULL, &index, &row) ==
               SPACE_OCCURRENCE_CURSOR_INVALIDATED && !row);
        unsigned received = 0u;
        for (;;) {
            CettaIndex reference_index = UINT64_MAX;
            SpaceOccurrenceCursorStep wanted =
                space_occurrence_cursor_next(&reference, &reference_index, NULL);
            SpaceOccurrenceCursorStep actual =
                space_occurrence_cursor_next(&source, &index, &row);
            assert(actual == wanted && index == reference_index);
            if (actual != SPACE_OCCURRENCE_CURSOR_ITEM) {
                assert(actual == SPACE_OCCURRENCE_CURSOR_END && !row);
                break;
            }
            assert(index < visible && row);
            assert(atom_eq(row, expected[index]));
            assert(atom_eq(row, space_occurrence_cursor_atom(&reference, reference_index)));
            assert(source.last_row == reference.last_row);
            assert(source.last_logical == reference.last_logical);
            if (received++ == 0u) {
                space_add_atom_id(&space, store_atom(&universe,
                    edge_value(&scratch, edge, 999)));
                CettaIndex removed = 5u;
                assert(space_remove_logical_positions(&space, &removed, 1u));
                assert(atom_eq(row, expected[index]));
            }
        }
        assert(received >= 2u);
        space_occurrence_cursor_release(&reference);
        space_occurrence_cursor_release(&source);
        row = expected[0];
        assert(space_occurrence_cursor_next(&source, &index, &row) ==
               SPACE_OCCURRENCE_CURSOR_INVALIDATED && !row);
        space_free(&space);
        term_universe_free(&universe);
        arena_free(&scratch);
        arena_free(&persistent);
    }
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
        var_id_from_base(UINT64_C(81002)));
    SpaceOccurrenceCursor before_remove;
    SpaceOccurrenceCursor after_remove;
    assert(space_occurrence_cursor_init(
        &space, query, &before_remove));
    CettaIndex index = UINT64_MAX;
    assert(space_occurrence_cursor_next(&before_remove, &index, NULL) ==
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
    assert(space_occurrence_cursor_next(&after_remove, &index, NULL) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 0u);
    assert(atom_eq(space_occurrence_cursor_atom(&after_remove, index),
                   term_universe_get_atom(universe, rows[0])));
    assert(space_occurrence_cursor_next(&after_remove, &index, NULL) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 1u);
    assert(atom_eq(space_occurrence_cursor_atom(&after_remove, index),
                   term_universe_get_atom(universe, rows[2])));
    assert(space_occurrence_cursor_next(&after_remove, &index, NULL) ==
           SPACE_OCCURRENCE_CURSOR_END);

    assert(space_occurrence_cursor_next(&before_remove, &index, NULL) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 1u);
    assert(atom_eq(space_occurrence_cursor_atom(&before_remove, index),
                   term_universe_get_atom(universe, rows[1])));
    assert(space_occurrence_cursor_next(&before_remove, &index, NULL) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(index == 2u);
    assert(atom_eq(space_occurrence_cursor_atom(&before_remove, index),
                   term_universe_get_atom(universe, rows[2])));
    assert(space_occurrence_cursor_next(&before_remove, &index, NULL) ==
           SPACE_OCCURRENCE_CURSOR_END);

    space_occurrence_cursor_release(&after_remove);
    space_occurrence_cursor_release(&before_remove);
    for (CettaIndex expected = 1u; expected < 3u; expected++) {
        assert(space_occurrence_cursor_next(&detached_clone, &index, NULL) ==
               SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(index == expected);
        assert(atom_eq(space_occurrence_cursor_atom(&detached_clone, index),
                       term_universe_get_atom(universe, rows[expected])));
    }
    assert(space_occurrence_cursor_next(&detached_clone, &index, NULL) ==
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
        symbol_intern_cstr(g_symbols, "cold-x"), var_id_from_base(UINT64_C(91001)));
    rows[21] = store_atom(universe, binary(scratch, edge, variable, variable));
    rows[22] = store_atom(universe, binary(scratch, other, variable, variable));
    uint64_t revision = space.revision;
    assert(space_add_atom_ids_batch(&space, rows, 23u));
    assert(space.native.len == 23u && space.revision == revision + 1u);
    Atom *query = binary(scratch, edge, atom_int(scratch, 7),
        atom_var_with_spelling(scratch,
            symbol_intern_cstr(g_symbols, "cold-y"), var_id_from_base(UINT64_C(91002))));
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
        assert(space_occurrence_cursor_next(&cursor, &index, NULL) == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(space_occurrence_cursor_next(&clone, &cloned, NULL) == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(index == candidates[i] && cloned == index);
        assert(atom_eq(space_occurrence_cursor_atom(&cursor, index),
                       term_universe_get_atom(universe, rows[index])));
    }
    CettaIndex end = UINT64_MAX;
    assert(space_occurrence_cursor_next(&cursor, &end, NULL) == SPACE_OCCURRENCE_CURSOR_END);
    assert(space_occurrence_cursor_next(&clone, &end, NULL) == SPACE_OCCURRENCE_CURSOR_END);
    space_occurrence_cursor_release(&clone);
    space_occurrence_cursor_release(&cursor);
    /* Exhausting the startup scan allowance returns to a maintained access
     * path: here the bound field's position index, which covers every row
     * and keeps the open row of this head as an uncertain candidate. */
    for (unsigned i = 0u; i < 5u; i++) {
        CettaIndex *selected = NULL;
        (void)space_match_backend_candidates64(&space, query, &selected);
        free(selected);
    }
    CettaSpacePathIndexInfo info = {0};
    assert(space_path_indexes_info(&space, &info));
    assert(info.index_count == 1u && info.uncertain_rows == 1u &&
           info.source_rows == space_length64(&space));
    assert(space_add_atom_ids_batch(&space, appended, 2u));
    const CettaIndex later_matches[] = {20u, 21u, 22u, 23u, 24u, 25u};
    assert_query_indices(&space, scratch, query, later_matches, 6u);
    space_free(&space);
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
            assert(space_occurrence_cursor_next(&second, &index, NULL) ==
                   SPACE_OCCURRENCE_CURSOR_ITEM);
            assert(index == expected);
            assert(atom_eq(space_occurrence_cursor_atom(&second, index),
                term_universe_get_atom(universe, values[expected % 2u])));
        }
        CettaIndex end = UINT64_MAX;
        assert(space_occurrence_cursor_next(&second, &end, NULL) == SPACE_OCCURRENCE_CURSOR_END);
        space_occurrence_cursor_release(&second);
#if CETTA_BUILD_WITH_RUNTIME_STATS
        cetta_runtime_stats_snapshot(&after);
        assert(after.counters[CETTA_RUNTIME_COUNTER_SPACE_RETIRED_OCCURRENCE_RELEASE_BYTES] ==
               before.counters[CETTA_RUNTIME_COUNTER_SPACE_RETIRED_OCCURRENCE_RELEASE_BYTES] +
               retained_bytes);
#endif
    }
}

static void test_lazy_merge_and_independent_clones(
        TermUniverse *universe, Arena *scratch) {
    enum { ROWS = 257, KEYS = 7, PREFIX = 3 };
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    SymbolId edge = symbol_intern_cstr(g_symbols, "merge-edge");
    AtomId rows[ROWS];
    for (CettaIndex row = 0u; row < ROWS; row++) {
        int64_t key = (int64_t)((row * 5u) % KEYS);
        rows[row] = store_atom(universe, edge_value(scratch, edge, key));
    }
    assert(space_add_atom_ids_batch(&space, rows, ROWS));
    /* Leave one unrelated occurrence outside the selected frontier, so this
     * test exercises the lazy branch merge rather than full coverage. */
    space_add_atom_id(&space, store_atom(universe, atom_int(scratch, -1)));
    space_match_native_ensure_trie(&space);
    Atom *query = binary(scratch, edge, atom_var(scratch, "merge-x"),
                         atom_var(scratch, "merge-y"));
    SpaceOccurrenceCursor source, exact, rewind, scan, refused;
    assert(space_occurrence_cursor_init(&space, query, &source));
    assert(source.node_len == KEYS && source.heap_len == KEYS);
    assert(source.heap && !source.flat_mode && !source.flat);
    for (uint32_t node = 0u; node < source.node_len; node++)
        assert(source.leaf_pos[node] == 0u);
    for (CettaIndex expected = 0u; expected < PREFIX; expected++) {
        CettaIndex index;
        assert(space_occurrence_cursor_next(&source, &index, NULL) ==
               SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(index == expected);
    }
    assert(!space_occurrence_cursor_clone(&source, &source));
    assert(!space_occurrence_cursor_clone_unstarted(&source, &source));
    assert(source.pinned && source.heap);
    assert(space_occurrence_cursor_clone(&source, &exact));
    assert(space_occurrence_cursor_clone_unstarted(&source, &rewind));
    assert(space_occurrence_cursor_clone(&source, &scan));
    assert(exact.heap != source.heap && exact.leaf_pos != source.leaf_pos);
    assert(exact.occurrences == source.occurrences);
    /* The allocation-refusal route preserves the same retained positions. */
    free(scan.heap);
    scan.heap = NULL;
    scan.heap_len = 0u;

    space_add_atom_id(&space, rows[0]);
    assert(!space_occurrence_cursor_clone_unstarted(&source, &refused));
    assert(space_remove_atom_id(&space, rows[0]));
    assert(!space_occurrence_cursor_clone_unstarted(&source, &refused));
    /* Detachment keeps the old trie and rows alive after the live space goes. */
    space_free(&space);
    CettaIndex next;
    assert(space_occurrence_cursor_next(&source, &next, NULL) ==
           SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(next == PREFIX);
    space_occurrence_cursor_release(&source);

    SpaceOccurrenceCursor *cursors[] = {&exact, &rewind, &scan};
    for (size_t reader = 0u; reader < 3u; reader++) {
        CettaIndex first = reader == 1u ? 0u : PREFIX;
        for (CettaIndex expected = first; expected < ROWS; expected++) {
            CettaIndex index;
            assert(space_occurrence_cursor_next(cursors[reader], &index, NULL) ==
                   SPACE_OCCURRENCE_CURSOR_ITEM);
            assert(index == expected);
            assert(atom_eq(space_occurrence_cursor_atom(cursors[reader], index),
                           term_universe_get_atom(universe, rows[expected])));
        }
        assert(space_occurrence_cursor_next(cursors[reader], NULL, NULL) ==
               SPACE_OCCURRENCE_CURSOR_END);
        assert(space_occurrence_cursor_next(cursors[reader], NULL, NULL) ==
               SPACE_OCCURRENCE_CURSOR_END);
        space_occurrence_cursor_release(cursors[reader]);
    }
}

static void test_full_frontier_sequence(TermUniverse *universe, Arena *scratch) {
    enum { ROWS = 33, PREFIX = 3 };
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    SymbolId edge = symbol_intern_cstr(g_symbols, "full-frontier-edge");
    AtomId rows[ROWS];
    for (CettaIndex row = 0u; row < ROWS - 2u; row++)
        rows[row] = store_atom(universe, edge_value(scratch, edge, (int64_t)(row % 5u)));
    Atom *stored = atom_var(scratch, "full-frontier-stored");
    rows[ROWS - 2u] = store_atom(universe, binary(scratch, edge, stored, stored));
    rows[ROWS - 1u] = store_atom(universe, atom_var(scratch, "full-frontier-whole"));
    assert(space_add_atom_ids_batch(&space, rows, ROWS));
    space_match_native_ensure_trie(&space);
    Atom *query = binary(scratch, edge, atom_var(scratch, "full-frontier-x"),
                         atom_var(scratch, "full-frontier-y"));
    SpaceOccurrenceCursor source, exact, rewind, partial;
    assert(space_occurrence_cursor_init(&space, query, &source));
    assert(source.full_scan && source.node_len == 0u && !source.heap && !source.flat);
    for (CettaIndex expected = 0u; expected < PREFIX; expected++) {
        CettaIndex index;
        assert(space_occurrence_cursor_next(&source, &index, NULL) == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(index == expected);
    }
    assert(space_occurrence_cursor_clone(&source, &exact));
    assert(space_occurrence_cursor_clone_unstarted(&source, &rewind));
    assert(exact.full_next == PREFIX && rewind.full_next == 0u);
    assert(exact.occurrences == source.occurrences);

    /* The new live frontier is partial. Old readers still own their exact
     * ceiling, so the appended nonmatch is invisible to them. */
    space_add_atom_id(&space, store_atom(universe, atom_int(scratch, -1)));
    assert(space_occurrence_cursor_init(&space, query, &partial));
    assert(!partial.full_scan && partial.node_len > 0u);
    for (CettaIndex expected = 0u; expected < ROWS; expected++) {
        CettaIndex index;
        assert(space_occurrence_cursor_next(&partial, &index, NULL) == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(index == expected);
    }
    assert(space_occurrence_cursor_next(&partial, NULL, NULL) == SPACE_OCCURRENCE_CURSOR_END);
    space_occurrence_cursor_release(&partial);
    assert(space_remove_atom_id(&space, rows[0]));
    space_occurrence_cursor_release(&source);
    space_free(&space);
    SpaceOccurrenceCursor *readers[] = {&exact, &rewind};
    for (size_t reader = 0u; reader < 2u; reader++) {
        for (CettaIndex expected = reader ? 0u : PREFIX; expected < ROWS; expected++) {
            CettaIndex index;
            assert(space_occurrence_cursor_next(readers[reader], &index, NULL) == SPACE_OCCURRENCE_CURSOR_ITEM);
            assert(index == expected);
            assert(atom_eq(space_occurrence_cursor_atom(readers[reader], index),
                           term_universe_get_atom(universe, rows[expected])));
        }
        assert(space_occurrence_cursor_next(readers[reader], NULL, NULL) == SPACE_OCCURRENCE_CURSOR_END);
        space_occurrence_cursor_release(readers[reader]);
    }
    puts("PASS: full frontier uses a pinned occurrence sequence; partial frontiers keep branch selection, clones preserve position and duplicate rows survive removal and cancellation");
}

static void assert_equations_match_rebuild(Space *space, Arena *scratch,
                                           SymbolId head, Atom *query) {
    Space reference;
    space_init_with_universe(&reference, space->native.universe);
    for (CettaIndex i = 0u; i < space_length64(space); i++)
        space_add_atom_id(&reference, space_get_atom_id_at64(space, i));
    /* Force the reference to derive its index from all surviving rows. */
    space_mark_derived_state_dirty(&reference);
    const unsigned forms[] = {CETTA_RULE_SYNTAX_ORDINARY,
        CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL};
    for (unsigned mode = 0u; mode < 2u; mode++) {
        SpaceEquationCursor actual, expected;
        assert(space_rule_cursor_init(space, head, forms[mode], &actual));
        assert(space_rule_cursor_init(&reference, head, forms[mode], &expected));
        for (;;) {
            SpaceEquationOccurrenceId left, right;
            SpaceEquationCursorStep step = space_equation_cursor_next(&actual, &left);
            assert(step == space_equation_cursor_next(&expected, &right));
            if (step == SPACE_EQUATION_CURSOR_END)
                break;
            assert(step == SPACE_EQUATION_CURSOR_ITEM);
            assert(left.logical_index == right.logical_index);
            assert(space_get_atom_id_at64(space, left.logical_index) ==
                   space_get_atom_id_at64(&reference, right.logical_index));
        }
    }
    QueryResults actual, expected;
    query_results_init(&actual);
    query_results_init(&expected);
    query_equations(space, query, scratch, &actual);
    query_equations(&reference, query, scratch, &expected);
    assert(actual.len == expected.len && actual.len > 1u);
    for (CettaIndex i = 0u; i < actual.len; i++)
        assert(atom_eq(actual.items[i].result, expected.items[i].result));
    query_results_free(&actual);
    query_results_free(&expected);
    space_free(&reference);
}

static void test_equation_coordinate_retention(void) {
    for (unsigned format = 0u; format < 2u; format++) {
        for (unsigned warm = 0u; warm < 2u; warm++) {
            for (unsigned backend = 0u; backend < 2u; backend++) {
                Arena persistent, scratch;
                arena_init(&persistent);
                arena_init(&scratch);
                TermUniverse universe;
                assert(term_universe_init_with_store_format(&universe, format
                    ? TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1
                    : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1));
                term_universe_set_persistent_arena(&universe, &persistent);
                Space space;
                space_init_with_universe(&space, &universe);
                assert(space_match_backend_try_set(&space, backend
                    ? SPACE_ENGINE_NATIVE_CANDIDATE_EXACT : SPACE_ENGINE_NATIVE));
                SymbolId head = symbol_intern_cstr(g_symbols, "retained-eq-head");
                Atom *query = binary(&scratch, head, atom_int(&scratch, 7),
                                     atom_int(&scratch, 8));
                Atom *equation = binary(&scratch, g_builtin_syms.equals,
                                        query, atom_int(&scratch, 42));
                for (CettaIndex i = 0u; i < 96u; i++) {
                    Atom *row = i % 2u ? equation : atom_int(&scratch, (int64_t)i);
                    if (i == 11u)
                        row = binary(&scratch, g_builtin_syms.equals,
                            atom_var(&scratch, "retained-open-lhs"), atom_int(&scratch, 91));
                    if (i == 27u)
                        row = binary(&scratch, g_builtin_syms.equals_percent,
                                     query, atom_int(&scratch, 93));
                    if (i == 43u)
                        row = binary(&scratch, g_builtin_syms.equals,
                            binary(&scratch, head, atom_int(&scratch, 99),
                                   atom_int(&scratch, 8)), atom_int(&scratch, 94));
                    space_add_atom_id(&space, store_atom(&universe, row));
                }
                assert_equations_match_rebuild(&space, &scratch, head, query);
                if (!warm)
                    space_mark_derived_state_dirty(&space);
                for (unsigned phase = 0u; phase < 4u; phase++) {
                    uint8_t mask[97] = {0};
                    CettaCount len = space_length64(&space);
                    bool selected = false;
                    for (CettaIndex i = 0u; i < len; i++) {
                        Atom *row = space_get_at64(&space, i);
                        bool data = row->kind == ATOM_GROUNDED &&
                            row->ground.gkind == GV_INT;
                        bool ordinary = row->kind == ATOM_EXPR &&
                            atom_is_symbol_id(row->expr.elems[0], g_builtin_syms.equals);
                        bool directional = row->kind == ATOM_EXPR &&
                            atom_is_symbol_id(row->expr.elems[0], g_builtin_syms.equals_percent);
                        if (!selected && ((phase == 1u && ordinary) ||
                            (phase == 2u && directional) ||
                            ((phase == 0u || phase == 3u) && data))) {
                            mask[i] = 1u;
                            selected = true;
                        }
                    }
                    assert(selected);
                    bool was_dirty = space.native.eq_idx_dirty;
                    uint64_t revision = space.equation_revision;
                    CettaCount removed = 0u;
                    assert(space_remove_occurrence_mask_stable(&space, mask, len, &removed));
                    assert(removed == 1u);
                    bool changed_equations = phase == 1u || phase == 2u;
                    assert(space.native.eq_idx_dirty == (was_dirty || changed_equations));
                    assert(space.equation_revision == revision + (changed_equations ? 1u : 0u));
                    assert_equations_match_rebuild(&space, &scratch, head, query);
                    if (phase == 2u)
                        space_add_atom_id(&space, store_atom(&universe, equation));
                }
                space_free(&space);
                term_universe_free(&universe);
                arena_free(&scratch);
                arena_free(&persistent);
            }
        }
    }
    puts("PASS: data-only contraction retains equation indexes; changed equations rebuild with exact order, duplicate and open-pattern results");
}

static bool row_has_form(Atom *row, SymbolId head) {
    return row->kind == ATOM_EXPR && row->expr.len == 3u &&
        atom_is_symbol_id(row->expr.elems[0], head);
}

static void test_single_erasure_equation_retention(void) {
    const SpaceKind kinds[] = {SPACE_KIND_ATOM, SPACE_KIND_STACK, SPACE_KIND_QUEUE};
    for (unsigned variant = 0u; variant < 48u; variant++) {
        unsigned format = variant % 2u;
        unsigned warm = variant / 2u % 2u;
        unsigned backend = variant / 4u % 2u;
        unsigned kind = variant / 8u % 3u;
        unsigned atom_api = variant / 24u;
        Arena persistent, scratch;
        arena_init(&persistent);
        arena_init(&scratch);
        TermUniverse universe;
        assert(term_universe_init_with_store_format(&universe, format
            ? TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1
            : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1));
        term_universe_set_persistent_arena(&universe, &persistent);
        Space space;
        space_init_with_universe(&space, &universe);
        space.kind = kinds[kind];
        assert(space_match_backend_try_set(&space, backend
            ? SPACE_ENGINE_NATIVE_CANDIDATE_EXACT : SPACE_ENGINE_NATIVE));
        SymbolId head = symbol_intern_cstr(g_symbols, "single-erase-equation");
        Atom *query = binary(&scratch, head, atom_int(&scratch, 7), atom_int(&scratch, 8));
        Atom *equation = binary(&scratch, g_builtin_syms.equals, query, atom_int(&scratch, 42));
        AtomId expected[100];
        CettaCount len = 96u;
        for (CettaIndex i = 0u; i < len; i++) {
            Atom *row = i % 2u ? equation : atom_int(&scratch, (int64_t)i);
            if (i == 11u)
                row = binary(&scratch, g_builtin_syms.equals,
                    atom_var(&scratch, "single-erase-open"), atom_int(&scratch, 91));
            if (i == 27u)
                row = binary(&scratch, g_builtin_syms.equals_percent, query, atom_int(&scratch, 93));
            if (i == 42u)
                row = binary(&scratch, g_builtin_syms.colon,
                    atom_symbol(&scratch, "single-erase-subject"), atom_symbol(&scratch, "Type"));
            expected[i] = store_atom(&universe, row);
            space_add_atom_id(&space, expected[i]);
        }
        for (unsigned phase = 0u; phase < 8u; phase++) {
            if (phase >= 5u) {
                expected[len] = store_atom(&universe, phase == 5u
                    ? equation : atom_int(&scratch, 1000 + (int64_t)phase));
                space_add_atom_id(&space, expected[len++]);
            }
            assert_equations_match_rebuild(&space, &scratch, head, query);
            if (!warm)
                space_mark_derived_state_dirty(&space);
            CettaIndex removed = len;
            for (CettaIndex i = 0u; i < len; i++) {
                Atom *row = term_universe_get_atom(&universe, expected[i]);
                bool selected = phase == 2u ? row_has_form(row, g_builtin_syms.equals) :
                    phase == 3u ? row_has_form(row, g_builtin_syms.equals_percent) :
                    phase == 4u ? row_has_form(row, g_builtin_syms.colon) :
                    phase == 7u ? i + 1u == len : row->kind == ATOM_GROUNDED;
                if (selected) { removed = i; break; }
            }
            assert(removed < len);
            Atom *removed_atom = term_universe_get_atom(&universe, expected[removed]);
            Atom *tail = term_universe_get_atom(&universe, expected[len - 1u]);
            bool moved_tail = kind == 0u && removed + 1u < len;
            bool equation_change = row_has_form(removed_atom, g_builtin_syms.equals) ||
                row_has_form(removed_atom, g_builtin_syms.equals_percent) ||
                (moved_tail && (row_has_form(tail, g_builtin_syms.equals) ||
                               row_has_form(tail, g_builtin_syms.equals_percent)));
            uint64_t revision = space.equation_revision;
            bool ok = atom_api ? space_remove(&space, removed_atom) :
                                 space_remove_atom_id(&space, expected[removed]);
            assert(ok);
            if (kind == 0u)
                expected[removed] = expected[len - 1u];
            else
                memmove(expected + removed, expected + removed + 1u,
                        (size_t)(len - removed - 1u) * sizeof(*expected));
            len--;
            assert_atom_ids(&space, expected, len);
            assert(space.native.eq_idx_dirty == (!warm || equation_change));
            assert(space.equation_revision == revision + (equation_change ? 1u : 0u));
            assert_equations_match_rebuild(&space, &scratch, head, query);
        }
        space_free(&space);
        term_universe_free(&universe);
        arena_free(&scratch);
        arena_free(&persistent);
    }
    puts("PASS: 384 single erasures preserve or rebuild equations against independent ordered rows across APIs, store formats, backends and layouts");
}

/* A head's chain reads the same rows, in the same order, in both indexes. */
static void assert_head_chain_reads_equal(const SpaceHeadChains *left,
                                          const SpaceHeadChain *left_chain,
                                          const SpaceHeadChains *right,
                                          const SpaceHeadChain *right_chain) {
    assert(left_chain->count == right_chain->count);
    uint32_t left_row = left_chain->first, right_row = right_chain->first;
    for (uint32_t k = 0u; k < left_chain->count; k++) {
        assert(left->atom_indices[left_row] == right->atom_indices[right_row]);
        assert(left->atom_ids[left_row] == right->atom_ids[right_row]);
        left_row = left->next[left_row];
        right_row = right->next[right_row];
    }
}

static void assert_declarations_match_rows(Space *space, Arena *scratch,
                                           Atom **subjects, size_t subject_count) {
    Space rebuilt;
    space_init_with_universe(&rebuilt, space->native.universe);
    for (CettaIndex i = 0u; i < space_length64(space); i++)
        space_add_atom_id(&rebuilt, space_get_atom_id_at64(space, i));
    space_mark_derived_state_dirty(&rebuilt);
    for (size_t subject = 0u; subject < subject_count; subject++) {
        Atom **types = NULL, **reference = NULL;
        uint32_t count = space_get_declared_types(space, scratch, subjects[subject], &types);
        uint32_t rebuilt_count = space_get_declared_types(
            &rebuilt, scratch, subjects[subject], &reference);
        uint32_t found = 0u;
        /* Independently scan the authored declarations in current row order. */
        for (CettaIndex i = 0u; i < space_length64(space); i++) {
            Atom *row = space_get_at64(space, i);
            if (!row_has_form(row, g_builtin_syms.colon) ||
                !atom_eq(row->expr.elems[1], subjects[subject]))
                continue;
            assert(found < count && found < rebuilt_count);
            assert(atom_alpha_eq(types[found], row->expr.elems[2]));
            assert(atom_alpha_eq(types[found], reference[found]));
            found++;
        }
        assert(found == count && count == rebuilt_count);
        free(reference);
        free(types);
    }
    assert(space->native.ty_idx.has_open_rows == rebuilt.native.ty_idx.has_open_rows);
    assert(space->native.ty_idx.has_non_symbol_subjects == rebuilt.native.ty_idx.has_non_symbol_subjects);
    const SpaceHeadChains *actual = &space->native.ty_idx.rows;
    const SpaceHeadChains *reference = &rebuilt.native.ty_idx.rows;
    assert(actual->len == reference->len);
    for (CettaIndex j = 0u; j < actual->len; j++) {
        assert(actual->atom_indices[j] == reference->atom_indices[j]);
        assert(actual->atom_ids[j] == reference->atom_ids[j]);
    }
    assert_head_chain_reads_equal(actual, &actual->headless,
                                  reference, &reference->headless);
    assert(actual->chain_len == reference->chain_len);
    for (uint32_t slot = 0u; slot < actual->chain_cap; slot++) {
        const SpaceHeadChain *chain = &actual->chains[slot];
        if (chain->head == SYMBOL_ID_NONE)
            continue;
        const SpaceHeadChain *twin = NULL;
        for (uint32_t other = 0u; !twin && other < reference->chain_cap; other++)
            if (reference->chains[other].head == chain->head)
                twin = &reference->chains[other];
        assert(twin);
        assert_head_chain_reads_equal(actual, chain, reference, twin);
    }
    space_free(&rebuilt);
}

static void test_declaration_retention(void) {
    const SpaceKind kinds[] = {SPACE_KIND_ATOM, SPACE_KIND_STACK, SPACE_KIND_QUEUE};
    for (unsigned variant = 0u; variant < 72u; variant++) {
        unsigned warm = variant / 2u % 2u, kind = variant / 8u % 3u;
        unsigned api = variant / 24u;
        Arena persistent, scratch;
        arena_init(&persistent); arena_init(&scratch);
        TermUniverse universe;
        assert(term_universe_init_with_store_format(&universe, variant % 2u
            ? TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1 : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1));
        term_universe_set_persistent_arena(&universe, &persistent);
        Space space;
        space_init_with_universe(&space, &universe);
        space.kind = kinds[kind];
        assert(space_match_backend_try_set(&space, variant / 4u % 2u
            ? SPACE_ENGINE_NATIVE_CANDIDATE_EXACT : SPACE_ENGINE_NATIVE));
        Atom *subject = atom_symbol(&scratch, "retained-type-subject");
        Atom *compound = binary(&scratch, symbol_intern_cstr(g_symbols, "retained-type-box"),
                                atom_int(&scratch, 7), atom_int(&scratch, 8));
        Atom *subjects[] = {subject, compound, atom_symbol(&scratch, "retained-type-absent")};
        Atom *declaration = binary(&scratch, g_builtin_syms.colon,
                                   subject, atom_symbol(&scratch, "RetainedBasic"));
        AtomId expected[100];
        CettaCount len = 96u;
        for (CettaIndex i = 0u; i < len; i++) {
            Atom *row = i % 2u ? declaration : atom_int(&scratch, (int64_t)i);
            if (i == 11u)
                row = binary(&scratch, g_builtin_syms.colon, subject,
                             atom_symbol(&scratch, "RetainedOther"));
            if (i == 27u) {
                Atom *variable = atom_var(&scratch, "retained-type-parameter");
                row = binary(&scratch, g_builtin_syms.colon, subject,
                    binary(&scratch, symbol_intern_cstr(g_symbols, "RetainedParam"), variable, variable));
            }
            if (i == 41u)
                row = binary(&scratch, g_builtin_syms.colon, compound,
                             atom_symbol(&scratch, "RetainedCompound"));
            if (i == 63u)
                row = binary(&scratch, g_builtin_syms.equals,
                             atom_symbol(&scratch, "retained-type-rule"), atom_int(&scratch, 19));
            expected[i] = store_atom(&universe, row);
            space_add_atom_id(&space, expected[i]);
        }
        for (unsigned phase = 0u; phase < 9u; phase++) {
            if (phase == 3u || phase == 5u || phase == 7u) {
                Atom *row = phase == 3u ? atom_var(&scratch, "retained-type-open-row") :
                    phase == 5u ? declaration : atom_int(&scratch, 1007);
                expected[len] = store_atom(&universe, row);
                space_add_atom_id(&space, expected[len++]);
            }
            assert_declarations_match_rows(&space, &scratch, subjects, 3u);
            bool had_open_rows = space.native.ty_idx.has_open_rows;
            if (!warm) space_mark_derived_state_dirty(&space);
            CettaIndex removed = len;
            for (CettaIndex i = 0u; i < len; i++) {
                Atom *row = term_universe_get_atom(&universe, expected[i]);
                bool selected = phase == 2u ? row_has_form(row, g_builtin_syms.colon) :
                    phase == 4u ? row->kind == ATOM_VAR :
                    phase == 6u ? row_has_form(row, g_builtin_syms.colon) &&
                                     atom_eq(row->expr.elems[1], compound) :
                    phase == 8u ? row_has_form(row, g_builtin_syms.equals) :
                    phase == 7u ? i + 1u == len : row->kind == ATOM_GROUNDED;
                if (selected) { removed = i; break; }
            }
            assert(removed < len);
            Atom *row = term_universe_get_atom(&universe, expected[removed]);
            Atom *tail = term_universe_get_atom(&universe, expected[len - 1u]);
            bool ordered = api == 2u || kind != 0u;
            bool declaration_change = row_has_form(row, g_builtin_syms.colon) ||
                (!ordered && removed + 1u < len && row_has_form(tail, g_builtin_syms.colon));
            uint64_t revision = space.declaration_revision;
            if (api == 2u) {
                uint8_t mask[100] = {0}; mask[removed] = 1u;
                CettaCount count = 0u;
                assert(space_remove_occurrence_mask_stable(&space, mask, len, &count));
                assert(count == 1u);
            } else {
                assert(api ? space_remove(&space, row) : space_remove_atom_id(&space, expected[removed]));
            }
            if (ordered)
                memmove(expected + removed, expected + removed + 1u,
                        (size_t)(len - removed - 1u) * sizeof(*expected));
            else
                expected[removed] = expected[len - 1u];
            len--;
            assert_atom_ids(&space, expected, len);
            assert(space.native.ty_idx_dirty == (!warm || declaration_change || had_open_rows));
            assert(space.declaration_revision == revision + (declaration_change ? 1u : 0u));
            assert_declarations_match_rows(&space, &scratch, subjects, 3u);
        }
        space_free(&space); term_universe_free(&universe);
        arena_free(&scratch); arena_free(&persistent);
    }
    puts("PASS: 648 declaration-index removals preserve ordered types, duplicate declarations, coordinates and open-row certificates against independent scans");
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
    test_leaf_registry_against_ground_rows(&scratch);
    test_occurrence_cursor_exact_clone(&universe, &scratch);
    test_occurrence_cursor_fused_delivery();
    test_atom_id_removal_preserves_pinned_view(&universe, &scratch);
    test_reset_releases_storage_after_readers(&universe, &scratch);
    test_lazy_merge_and_independent_clones(&universe, &scratch);
    test_full_frontier_sequence(&universe, &scratch);
    test_equation_coordinate_retention();
    test_single_erasure_equation_retention();
    test_declaration_retention();

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
