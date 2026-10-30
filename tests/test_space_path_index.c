#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "space.h"
#include "space_path_index.h"
#include "stats.h"
#include "symbol.h"

static Atom *row(Arena *arena, SymbolId head, Atom *first, Atom *second) {
    Atom *children[] = {atom_symbol_id(arena, head), first, second};
    return atom_expr(arena, children, 3u);
}

static Atom *row4(Arena *arena, Atom *head, Atom *first, Atom *second, Atom *third) {
    Atom *children[] = {head, first, second, third};
    return atom_expr(arena, children, 4u);
}

static void add(Space *space, Atom *term) {
    AtomId id = term_universe_store_atom_id(space->native.universe, NULL, term);
    assert(id != CETTA_ATOM_ID_NONE);
    space_add_atom_id(space, id);
}

static bool direct_match(Space *space, Arena *arena, Atom *pattern,
                         CettaIndex position) {
    Bindings bindings;
    bindings_init(&bindings);
    bool matches = match_atoms_atom_id_epoch(pattern, space->native.universe,
        space_get_atom_id_at64(space, position), &bindings, arena, (CettaFrameIdentity){77u});
    bindings_free(&bindings);
    return matches;
}

/* The reference visits every source occurrence with the general matcher. It
 * neither consults the path index nor deduplicates equal immutable ids. */
static void compare_query_reference(Space *space, Arena *arena, Atom *pattern) {
    SubstMatchSet matches;
    space_subst_query(space, arena, pattern, &matches);
    CettaIndex next = 0u;
    for (CettaIndex position = 0u; position < space_length64(space); position++) {
        Bindings reference;
        bindings_init(&reference);
        bool expected = match_atoms_atom_id_epoch(pattern, space->native.universe,
            space_get_atom_id_at64(space, position), &reference, arena, (CettaFrameIdentity){77u});
        if (expected) {
            Bindings actual;
            bool found = false;
            while (next < matches.len) {
                SubstMatch *match = &matches.items[next++];
                if (!space_subst_match_with_seed(space, pattern, match, NULL, arena, &actual))
                    continue;
                assert(match->atom_idx == position);
                found = true;
                break;
            }
            assert(found);
            Atom *left = bindings_apply(&reference, arena, pattern);
            Atom *right = bindings_apply(&actual, arena, pattern);
            assert(atom_alpha_eq(left, right));
            bindings_free(&actual);
        }
        bindings_free(&reference);
    }
    while (next < matches.len) {
        Bindings unexpected;
        bool matched = space_subst_match_with_seed(space, pattern,
            &matches.items[next++], NULL, arena, &unexpected);
        if (matched)
            bindings_free(&unexpected);
        assert(!matched);
    }
    smset_free(&matches);
}

static void compare_reference(Space *space, Arena *arena, Atom *pattern) {
    compare_query_reference(space, arena, pattern);
    CettaIndex *candidates = NULL;
    CettaIndex count = space_match_backend_candidates64(space, pattern, &candidates);
    CettaIndex next = 0u;
    for (CettaIndex position = 0u; position < space_length64(space); position++) {
        while (next < count && candidates[next] < position) {
            if (next != 0u)
                assert(candidates[next - 1u] < candidates[next]);
            assert(!direct_match(space, arena, pattern, candidates[next]));
            next++;
        }
        if (!direct_match(space, arena, pattern, position))
            continue;
        assert(next < count && candidates[next] == position);
        next++;
    }
    while (next < count) {
        assert(candidates[next] < space_length64(space));
        assert(!direct_match(space, arena, pattern, candidates[next]));
        next++;
    }
    for (CettaIndex index = 1u; index < count; index++)
        assert(candidates[index - 1u] < candidates[index]);
    free(candidates);
}

static void test_flat_and_captured_readers(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    SymbolId head = symbol_intern_cstr(g_symbols, "path-index-row");
    for (int64_t index = 0; index < 128; index++) {
        /* Positions 7 and 24 carry identical payloads; both must be delivered. */
        add(&space, row(arena, head, atom_int(arena, index % 17),
                         atom_int(arena, index % 17)));
    }
    Atom *correlated = atom_var(arena, "path-correlated");
    add(&space, row(arena, head, correlated, correlated));
    add(&space, atom_var(arena, "path-whole-variable"));
    add(&space, row(arena, head, atom_int(arena, 900), atom_float(arena, 7.0)));
    add(&space, row(arena, head, atom_int(arena, 901), atom_float(arena, 8.0)));
    add(&space, row(arena, head, atom_int(arena, 902), atom_string(arena, "opaque-to-int")));
    space_match_native_ensure_trie(&space);
    Atom *query = row(arena, head, atom_var(arena, "path-output"), atom_int(arena, 7));
    compare_reference(&space, arena, query);
    CettaSpacePathIndexInfo info;
    assert(space_path_indexes_info(&space, &info));
    assert(info.index_count == 1u && info.path_length == 1u);
    assert(info.key_kind == CETTA_SPACE_PATH_INTEGER_KEY);
    assert(info.uncertain_rows == 5u && info.keyed_rows == 128u);
    for (int64_t key = 0; key < 19; key++)
        compare_reference(&space, arena,
            row(arena, head, atom_var(arena, "path-output"), atom_int(arena, key)));
    /* Correlation remains the matcher's decision even though one field is keyed. */
    compare_reference(&space, arena, row(arena, head, atom_int(arena, 2), atom_int(arena, 7)));

    CettaIndex *captured = NULL;
    CettaIndex captured_len = space_match_backend_candidates64(&space, query, &captured);
    SpaceOccurrenceCursor first, exact, rewind;
    assert(space_occurrence_cursor_init(&space, query, &first));
    assert(first.flat_mode && captured_len > 2u);
    CettaIndex position;
    assert(space_occurrence_cursor_next(&first, &position, NULL) == SPACE_OCCURRENCE_CURSOR_ITEM);
    assert(position == captured[0]);
    assert(space_occurrence_cursor_clone(&first, &exact));
    assert(space_occurrence_cursor_clone_unstarted(&first, &rewind));
    add(&space, row(arena, head, atom_int(arena, 10000), atom_int(arena, 7)));
    assert(!space_occurrence_cursor_clone_unstarted(&first, &(SpaceOccurrenceCursor){0}));
    compare_reference(&space, arena, query);
    uint8_t *removed = calloc((size_t)space_length64(&space), 1u);
    assert(removed);
    removed[captured[1]] = 1u;
    CettaCount removed_count;
    assert(space_remove_occurrence_mask_stable(&space, removed,
        space_length64(&space), &removed_count));
    assert(removed_count == 1u);
    free(removed);
    /* A stable removal carries the index to the surviving coordinates: it
     * keeps its one keyed field and still accounts for every row, the added
     * row in and the removed one out, and it answers as the reference does. */
    assert(space_path_indexes_info(&space, &info));
    assert(info.index_count == 1u && info.path_length == 1u);
    assert(info.key_kind == CETTA_SPACE_PATH_INTEGER_KEY);
    assert(info.uncertain_rows + info.keyed_rows == 5u + 128u);
    compare_reference(&space, arena, query);
    space_occurrence_cursor_release(&first);
    space_free(&space);
    for (CettaIndex index = 1u; index < captured_len; index++) {
        assert(space_occurrence_cursor_next(&exact, &position, NULL) == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(position == captured[index]);
        assert(space_occurrence_cursor_atom(&exact, position));
    }
    for (CettaIndex index = 0u; index < captured_len; index++) {
        assert(space_occurrence_cursor_next(&rewind, &position, NULL) == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(position == captured[index]);
    }
    assert(space_occurrence_cursor_next(&exact, &position, NULL) == SPACE_OCCURRENCE_CURSOR_END);
    assert(space_occurrence_cursor_next(&rewind, &position, NULL) == SPACE_OCCURRENCE_CURSOR_END);
    space_occurrence_cursor_release(&exact);
    space_occurrence_cursor_release(&rewind);
    free(captured);
}

static Atom *nested(Arena *arena, SymbolId wrapper, Atom *leaf, size_t depth) {
    for (size_t index = 0u; index < depth; index++) {
        Atom *children[] = {atom_symbol_id(arena, wrapper), leaf};
        leaf = atom_expr(arena, children, 2u);
    }
    return leaf;
}

static void test_uncertain_rows_keep_rigid_prefixes(TermUniverse *universe,
                                                   Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    SymbolId head = symbol_intern_cstr(g_symbols, "prefix-refinement-row");
    SymbolId other = symbol_intern_cstr(g_symbols, "prefix-refinement-other");
    for (int64_t index = 0; index < 128; index++)
        add(&space, row(arena, head, atom_int(arena, index),
                       atom_int(arena, index % 17)));
    Atom *open = atom_var(arena, "prefix-refinement-open");
    for (size_t index = 0u; index < 64u; index++)
        add(&space, row(arena, other, open, open));
    Atom *unknown_head = atom_expr3(arena,
        atom_var(arena, "prefix-refinement-head"), atom_int(arena, 700),
        atom_int(arena, 7));
    add(&space, unknown_head);
    add(&space, atom_var(arena, "prefix-refinement-whole"));
    add(&space, row(arena, head, open, open));
    /* A keyed row with a different constructor remains the full matcher's
     * responsibility when early refinement is restricted to uncertainty. */
    add(&space, row(arena, other, atom_int(arena, 701), atom_int(arena, 7)));
    add(&space, atom_expr3(arena,
        atom_var(arena, "prefix-refinement-open-head"), atom_int(arena, 702),
        atom_var(arena, "prefix-refinement-open-value")));
    space_match_native_ensure_trie(&space);
    Atom *query = row(arena, head, atom_var(arena, "prefix-refinement-output"),
                      atom_int(arena, 7));
    CettaIndex *positions = NULL, count = 0u;
    assert(space_path_index_candidates(&space, query, &positions, &count));
    assert(count == 13u);
    /* Eight matching ground occurrences, then the variable-headed row,
     * whole-row variable and correlated row of the right constructor. */
    for (CettaIndex index = 0u; index < 8u; index++)
        assert(positions[index] == 7u + 17u * index);
    assert(positions[8] == 192u && positions[9] == 193u && positions[10] == 194u);
    assert(positions[11] == 195u);
    assert(positions[12] == 196u);
    assert(!direct_match(&space, arena, query, positions[11]));
    free(positions);
    compare_reference(&space, arena, query);
    /* A variable query head cannot refute the other constructor's rows. */
    compare_reference(&space, arena, atom_expr3(arena,
        atom_var(arena, "prefix-refinement-query-head"),
        atom_var(arena, "prefix-refinement-output"), atom_int(arena, 7)));
    space_free(&space);
    puts("PASS: uncertain path rows retain variable heads and correlations; rigid outer clashes avoid canonical matching");
}

static void test_nested_prefix_admission(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    SymbolId head = symbol_intern_cstr(g_symbols, "path-bound-prefix-row");
    SymbolId wrapper = symbol_intern_cstr(g_symbols, "path-bound-prefix-wrapper");
    for (int64_t index = 0; index < 24; index++)
        add(&space, row(arena, head,
            nested(arena, wrapper, atom_int(arena, index), 257u),
            atom_int(arena, index % 5)));
    add(&space, atom_var(arena, "path-bound-prefix-whole"));
    space_match_native_ensure_trie(&space);
    Atom *query = row(arena, head,
        nested(arena, wrapper, atom_int(arena, 7), 257u),
        atom_var(arena, "path-bound-prefix-output"));
    assert(!space_path_index_may_select(query));
    compare_reference(&space, arena, query);
    CettaSpacePathIndexInfo info;
    assert(!space_path_indexes_info(&space, &info));
    query = row(arena, head,
        nested(arena, wrapper, atom_var(arena, "path-nested-prefix-output"), 257u),
        atom_int(arena, 2));
    assert(space_path_index_may_select(query));
    compare_reference(&space, arena, query);
    assert(space_path_indexes_info(&space, &info));
    assert(info.path_length == 1u && info.uncertain_rows == 1u);
    space_free(&space);
    puts("PASS: bound nested prefixes use primary access; nested variables admit later rigid coordinates");
}

static void test_nested_paths(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    SymbolId head = symbol_intern_cstr(g_symbols, "path-nested-row");
    SymbolId wrapper = symbol_intern_cstr(g_symbols, "path-wrapper");
    SymbolId selected = symbol_intern_cstr(g_symbols, "path-selected");
    SymbolId rejected = symbol_intern_cstr(g_symbols, "path-rejected");
    enum { DEPTH = 257 };
    for (int64_t index = 0; index < 24; index++)
        add(&space, row(arena, head, atom_int(arena, index), nested(arena, wrapper,
            atom_symbol_id(arena, index % 8 == 0 ? selected : rejected), DEPTH)));
    add(&space, row(arena, head, atom_int(arena, 25), atom_var(arena, "path-open-prefix")));
    add(&space, atom_var(arena, "path-nested-whole"));
    Atom *list_items[] = {atom_symbol_id(arena, head), atom_int(arena, 30),
        nested(arena, wrapper, atom_symbol_id(arena, selected), DEPTH)};
    add(&space, atom_list(arena, list_items, 3u));
    space_match_native_ensure_trie(&space);
    Atom *query = row(arena, head, atom_var(arena, "path-nested-output"),
        nested(arena, wrapper, atom_symbol_id(arena, selected), DEPTH));
    compare_reference(&space, arena, query);
    CettaSpacePathIndexInfo info;
    /* A ground term of symbols after a variable is one id however deep: its
     * column is keyed by that id.  The row open there, the whole variable
     * and the list carrier stay open. */
    assert(space_path_indexes_info(&space, &info));
    assert(info.path_length == 1u && info.key_kind == CETTA_SPACE_PATH_TERM_KEY &&
           info.uncertain_rows == 3u);
    /* A rigid constructor is also a usable coordinate when it has no children. */
    for (int64_t index = 0; index < 24; index++) {
        Atom *child = atom_symbol_id(arena, index % 8 == 0 ? selected : rejected);
        add(&space, row(arena, head, atom_int(arena, index), atom_expr(arena, &child, 1u)));
    }
    Atom *child = atom_symbol_id(arena, selected);
    compare_reference(&space, arena, row(arena, head, atom_var(arena, "head-output"),
                                       atom_expr(arena, &child, 1u)));
    assert(space_path_indexes_info(&space, &info));
    assert(info.index_count == 2u && info.path_length == 2u);
    /* A deep term ending in a number is no id a query names: the index keys
     * the number at its deepest leaf. */
    for (int64_t index = 0; index < 24; index++)
        add(&space, row(arena, head, atom_int(arena, 100 + index),
            nested(arena, wrapper, atom_int(arena, index % 8), DEPTH)));
    compare_reference(&space, arena, row(arena, head, atom_var(arena, "deep-output"),
        nested(arena, wrapper, atom_int(arena, 3), DEPTH)));
    assert(space_path_indexes_info(&space, &info));
    assert(info.index_count == 3u && info.path_length == DEPTH + 1u);
    space_free(&space);
}

static void test_unrelated_rows(TermUniverse *universe, Arena *arena, CettaCount rows) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    SymbolId head = symbol_intern_cstr(g_symbols, "path-scaled-row");
    for (CettaIndex index = 0u; index < rows; index++)
        add(&space, row(arena, head, atom_int(arena, (int64_t)index),
            atom_int(arena, index < 3u ? 42 : (int64_t)(1000u + index))));
    add(&space, atom_var(arena, "scaled-whole-variable"));
    space_match_native_ensure_trie(&space);
    Atom *query = row(arena, head, atom_var(arena, "scaled-output"), atom_int(arena, 42));
    compare_reference(&space, arena, query);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats before, after;
    cetta_runtime_stats_snapshot(&before);
#endif
    for (size_t repeat = 0u; repeat < 12u; repeat++) {
        CettaIndex *positions = NULL, count = 0u;
        assert(space_path_index_candidates(&space, query, &positions, &count));
        assert(count == 4u && positions[0] == 0u && positions[1] == 1u &&
               positions[2] == 2u && positions[3] == rows);
        free(positions);
    }
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_snapshot(&after);
    assert(after.counters[CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_BUILD_ROW] ==
           before.counters[CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_BUILD_ROW]);
    assert(after.counters[CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_CANDIDATE] -
           before.counters[CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_CANDIDATE] == 48u);
#endif
    CettaSpacePathIndexInfo info;
    assert(space_path_indexes_info(&space, &info));
    printf("PASS: path index %llu rows; 12 lookups retain 4 occurrence candidates; %zu bytes\n",
           (unsigned long long)rows, info.retained_bytes);
    space_free(&space);
}

/* A flat shape that binds two columns after its variable reads the shorter
 * posting.  The constant relation column is indexed first; once its lookups
 * have read as many rows as a build reads, the object column they vary is
 * indexed over the shape's head and serves them.  Rows open at either
 * column, a variable head, another head and a whole variable stay
 * candidates wherever they may match, through removals and adds.  A shape
 * whose first column already selects builds nothing more. */
static void test_shortest_column(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    Atom *head = atom_symbol_id(arena, symbol_intern_cstr(g_symbols, "column-choice-row"));
    Atom *relations[4];
    for (int index = 0; index < 4; index++) {
        char name[32];
        snprintf(name, sizeof(name), "column-choice-r%d", index);
        relations[index] = atom_symbol_id(arena, symbol_intern_cstr(g_symbols, name));
    }
    Atom *selected = relations[1];
    /* Object k's rows are k, k + 203, ...: one of each relation. */
    for (int64_t index = 0; index < 800; index++)
        add(&space, row4(arena, head, atom_int(arena, index), relations[index % 4],
                         atom_int(arena, index % 203)));
    Atom *correlated = atom_var(arena, "column-choice-correlated");
    add(&space, row4(arena, head, atom_int(arena, 900), selected,
                     atom_var(arena, "column-choice-open-object")));
    add(&space, row4(arena, head, atom_int(arena, 901),
                     atom_var(arena, "column-choice-open-relation"), atom_int(arena, 7)));
    add(&space, row4(arena, head, correlated, selected, correlated));
    add(&space, row4(arena, atom_var(arena, "column-choice-open-head"),
                     atom_int(arena, 902), selected, atom_int(arena, 7)));
    add(&space, row4(arena, atom_symbol_id(arena, symbol_intern_cstr(g_symbols,
                         "column-choice-other")), atom_int(arena, 903), selected,
                     atom_int(arena, 7)));
    add(&space, atom_var(arena, "column-choice-whole"));
    space_match_native_ensure_trie(&space);
    Atom *output = atom_var(arena, "column-choice-output");
    for (int64_t object = 0; object < 203; object++)
        compare_reference(&space, arena,
            row4(arena, head, output, selected, atom_int(arena, object)));
    CettaSpacePathIndexInfo info;
    assert(space_path_indexes_info(&space, &info));
    assert(info.index_count == 2u);
    /* Object 7's four rows and the row open at its relation, the variable
     * head and the whole variable, and the two rows of the head open at the
     * object; the relation column would offer over two hundred. */
    Atom *query = row4(arena, head, output, selected, atom_int(arena, 7));
    CettaIndex *positions = NULL, count = 0u;
    assert(space_path_index_candidates(&space, query, &positions, &count));
    assert(count == 9u);
    free(positions);
    /* A variable head reads only indexes over every row. */
    for (int64_t object = 0; object < 24; object++)
        compare_reference(&space, arena, row4(arena,
            atom_var(arena, "column-choice-query-head"), output, selected,
            atom_int(arena, object)));
    uint8_t *removed = calloc((size_t)space_length64(&space), 1u);
    assert(removed);
    for (CettaIndex position = 0u; position < 800u; position += 7u)
        removed[position] = 1u;
    CettaCount removed_count;
    assert(space_remove_occurrence_mask_stable(&space, removed,
        space_length64(&space), &removed_count));
    free(removed);
    for (int64_t object = 0; object < 203; object += 5)
        compare_reference(&space, arena,
            row4(arena, head, output, selected, atom_int(arena, object)));
    for (int64_t index = 0; index < 40; index++)
        add(&space, row4(arena, head, atom_int(arena, 1000 + index), selected,
                         atom_int(arena, index)));
    for (int64_t object = 0; object < 40; object++)
        compare_reference(&space, arena,
            row4(arena, head, output, selected, atom_int(arena, object)));
    space_free(&space);

    Space selective;
    space_init_with_universe(&selective, universe);
    selective.kind = SPACE_KIND_STACK;
    Atom *keyed = atom_symbol_id(arena, symbol_intern_cstr(g_symbols, "column-choice-keyed"));
    Atom *tag = atom_symbol_id(arena, symbol_intern_cstr(g_symbols, "column-choice-tag"));
    for (int64_t index = 0; index < 800; index++)
        add(&selective, row4(arena, keyed, atom_int(arena, index),
                             atom_int(arena, index % 400), tag));
    space_match_native_ensure_trie(&selective);
    for (int64_t repeat = 0; repeat < 2; repeat++) {
        for (int64_t value = 0; value < 400; value++)
            compare_reference(&selective, arena,
                row4(arena, keyed, output, atom_int(arena, value), tag));
    }
    assert(space_path_indexes_info(&selective, &info));
    assert(info.index_count == 1u);
    space_free(&selective);
    puts("PASS: a flat shape reads its shortest column; its varied column is built once its lookups have read a build's rows");
}

/* A pattern that binds a prefix after its head reads the index of that
 * prefix, where the trie would hold every row of the space: lookups bound
 * on one and on two leading columns answer as a full scan does, with rows
 * open in the prefix, rows of another head, a variable head and a whole
 * variable among them, through removals and adds, and counted.  No trie is
 * built; once one is, the prefix indexes it supersedes are retired. */
static void test_bound_prefix(TermUniverse *universe, Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    Atom *head = atom_symbol_id(arena, symbol_intern_cstr(g_symbols, "prefix-row"));
    Atom *other = atom_symbol_id(arena, symbol_intern_cstr(g_symbols, "prefix-other"));
    for (int64_t index = 0; index < 600; index++)
        add(&space, row4(arena, head, atom_int(arena, index % 30),
                         atom_int(arena, index % 7), atom_int(arena, index)));
    add(&space, row4(arena, head, atom_var(arena, "prefix-open-first"),
                     atom_int(arena, 3), atom_int(arena, 900)));
    add(&space, row4(arena, head, atom_int(arena, 4),
                     atom_var(arena, "prefix-open-second"), atom_int(arena, 901)));
    add(&space, row4(arena, head, atom_int(arena, 4), atom_float(arena, 3.0),
                     atom_int(arena, 902)));
    add(&space, row4(arena, atom_var(arena, "prefix-open-head"), atom_int(arena, 4),
                     atom_int(arena, 3), atom_int(arena, 903)));
    add(&space, row4(arena, other, atom_int(arena, 4), atom_int(arena, 3),
                     atom_int(arena, 904)));
    add(&space, atom_var(arena, "prefix-whole"));
    Atom *output = atom_var(arena, "prefix-output");
    /* Spend the one-shot allowance as cold scans, then read by indexes. */
    for (int64_t first = 0; first < 30; first++) {
        compare_reference(&space, arena,
            row4(arena, head, atom_int(arena, first), output,
                 atom_var(arena, "prefix-rest")));
        for (int64_t second = 0; second < 7; second++)
            compare_reference(&space, arena,
                row4(arena, head, atom_int(arena, first), atom_int(arena, second),
                     output));
    }
    assert(!space.match_backend.native.match_trie);
    CettaIndex *positions = NULL, count = 0u;
    Atom *query = row4(arena, head, atom_int(arena, 4), atom_int(arena, 3), output);
    assert(space_path_index_prefix_candidates(&space, query, false, &positions, &count));
    /* The rows of (4 3): 600 / 210 of them, and the rows open in the
     * prefix or holding a float there, the variable head and the whole
     * variable; never the other head's row. */
    for (CettaIndex next = 0u; next < count; next++)
        assert(space_get_atom_id_at64(&space, positions[next]) !=
               term_universe_lookup_atom_id(universe,
                   row4(arena, other, atom_int(arena, 4), atom_int(arena, 3),
                        atom_int(arena, 904))));
    assert(count <= 3u + 6u);
    free(positions);
    uint64_t counted = 0u;
    CettaIndex examined = 0u;
    if (space_match_count_flat_linear64(&space, arena, query, &counted, &examined)) {
        uint64_t expected = 0u;
        for (CettaIndex position = 0u; position < space_length64(&space); position++)
            expected += direct_match(&space, arena, query, position);
        assert(counted == expected);
    }
    assert(!space.match_backend.native.match_trie);
    uint8_t *removed = calloc((size_t)space_length64(&space), 1u);
    assert(removed);
    for (CettaIndex position = 0u; position < 600u; position += 11u)
        removed[position] = 1u;
    CettaCount removed_count;
    assert(space_remove_occurrence_mask_stable(&space, removed,
        space_length64(&space), &removed_count));
    free(removed);
    for (int64_t index = 0; index < 60; index++)
        add(&space, row4(arena, head, atom_int(arena, index % 30),
                         atom_int(arena, 3), atom_int(arena, 1000 + index)));
    for (int64_t first = 0; first < 30; first += 3)
        compare_reference(&space, arena,
            row4(arena, head, atom_int(arena, first), atom_int(arena, 3), output));
    CettaSpacePathIndexInfo info;
    assert(space_path_indexes_info(&space, &info));
    space_match_native_ensure_trie(&space);
    assert(space.match_backend.native.match_trie);
    assert(!space_path_indexes_info(&space, &info) ||
           info.key_kind != CETTA_SPACE_PATH_PREFIX_KEY);
    compare_reference(&space, arena,
        row4(arena, head, atom_int(arena, 4), atom_int(arena, 3), output));
    space_free(&space);
    puts("PASS: a bound prefix reads its index, not the trie, and answers as a scan does");
}

/* Compare the unindexed candidate stream with the complete matcher in both
 * record widths, including duplicate occurrences and undecodable records. */
static void test_cold_candidates(Arena *arena, TermUniverseStoreFormat format) {
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    assert(term_universe_init_with_store_format(&universe, format));
    term_universe_set_persistent_arena(&universe, &persistent);
    SymbolId head = symbol_intern_cstr(g_symbols, "cold-record-row");
    SymbolId other = symbol_intern_cstr(g_symbols, "cold-record-other");
    Atom *correlated = atom_var(arena, "cold-correlated");
    Atom *nested_children[] = {atom_symbol_id(arena, other), correlated};
    Atom *short_children[] = {atom_symbol_id(arena, head), atom_int(arena, 7)};
    Atom *duplicate = row(arena, head, atom_int(arena, 2), atom_int(arena, 7));
    Atom *terms[] = {
        duplicate, row(arena, head, atom_int(arena, 3), atom_int(arena, 8)),
        duplicate, row(arena, head, correlated, correlated),
        atom_var(arena, "cold-whole-variable"),
        row(arena, head, atom_int(arena, 4), atom_float(arena, 7.0)),
        row(arena, head, atom_int(arena, 5), atom_bool(arena, true)),
        row(arena, head, atom_int(arena, 6), atom_string(arena, "cold-string")),
        row(arena, head, atom_int(arena, 9), atom_symbol_id(arena, other)),
        row(arena, head, atom_expr(arena, nested_children, 2u), atom_int(arena, 7)),
        row(arena, other, atom_int(arena, 2), atom_int(arena, 7)),
        atom_symbol_id(arena, head), atom_int(arena, 7),
        atom_expr(arena, short_children, 2u),
        row(arena, head, atom_space(arena, &universe), atom_int(arena, 7)),
    };
    Atom *output = atom_var(arena, "cold-output");
    Atom *queries[] = {
        row(arena, head, output, atom_int(arena, 7)),
        row(arena, head, atom_int(arena, 2), atom_int(arena, 7)),
        row(arena, head, output, atom_float(arena, 7.0)),
        row(arena, head, output, atom_bool(arena, true)),
        row(arena, head, output, atom_string(arena, "cold-string")),
        row(arena, head, output, atom_symbol_id(arena, other)),
        row(arena, head, output, atom_int(arena, 900)),
    };
    for (size_t query_index = 0u;
         query_index < sizeof(queries) / sizeof(queries[0]); query_index++) {
        Space space;
        space_init_with_universe(&space, &universe);
        space.kind = SPACE_KIND_STACK;
        for (size_t i = 0u; i < sizeof(terms) / sizeof(terms[0]); i++)
            add(&space, terms[i]);
        assert(!space.match_backend.native.match_trie);
        assert(!tu_hdr(&universe, space_get_atom_id_at64(&space, 14u)));
        CettaIndex *positions = NULL, count = 0u;
        Atom *query = queries[query_index];
        assert(space_match_native_try_cold_candidates(&space, query, &positions, &count));
        assert(!space.match_backend.native.match_trie);
        CettaIndex next = 0u;
        for (CettaIndex position = 0u; position < space_length64(&space); position++) {
            bool included = next < count && positions[next] == position;
            if (direct_match(&space, arena, query, position))
                assert(included);
            if (included)
                next++;
        }
        assert(next == count);
        assert(count < space_length64(&space));
        for (CettaIndex i = 1u; i < count; i++)
            assert(positions[i - 1u] < positions[i]);
        free(positions);
        space_free(&space);
    }
    term_universe_free(&universe);
    arena_free(&persistent);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena persistent, scratch;
    arena_init(&persistent);
    arena_init(&scratch);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_enable();
    cetta_runtime_stats_reset();
#endif
    test_cold_candidates(&scratch, TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1);
    test_cold_candidates(&scratch, TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1);
    test_flat_and_captured_readers(&universe, &scratch);
    test_uncertain_rows_keep_rigid_prefixes(&universe, &scratch);
    test_nested_prefix_admission(&universe, &scratch);
    test_nested_paths(&universe, &scratch);
    test_unrelated_rows(&universe, &scratch, 1024u);
    test_unrelated_rows(&universe, &scratch, 2048u);
    test_shortest_column(&universe, &scratch);
    test_bound_prefix(&universe, &scratch);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats stats;
    cetta_runtime_stats_snapshot(&stats);
    assert(stats.counters[CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_ALLOC_BYTES] ==
           stats.counters[CETTA_RUNTIME_COUNTER_MATCH_POSITION_INDEX_FREE_BYTES]);
    cetta_runtime_stats_disable();
#endif
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: substitution queries preserve canonical binding images, numeric matches and ordered duplicate occurrences");
    return 0;
}
