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
        space_get_atom_id_at64(space, position), &bindings, arena, 77u);
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
            space_get_atom_id_at64(space, position), &reference, arena, 77u);
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
    assert(space_occurrence_cursor_next(&first, &position) == SPACE_OCCURRENCE_CURSOR_ITEM);
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
    assert(!space_path_indexes_info(&space, &info));
    compare_reference(&space, arena, query);
    space_occurrence_cursor_release(&first);
    space_free(&space);
    for (CettaIndex index = 1u; index < captured_len; index++) {
        assert(space_occurrence_cursor_next(&exact, &position) == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(position == captured[index]);
        assert(space_occurrence_cursor_atom(&exact, position));
    }
    for (CettaIndex index = 0u; index < captured_len; index++) {
        assert(space_occurrence_cursor_next(&rewind, &position) == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(position == captured[index]);
    }
    assert(space_occurrence_cursor_next(&exact, &position) == SPACE_OCCURRENCE_CURSOR_END);
    assert(space_occurrence_cursor_next(&rewind, &position) == SPACE_OCCURRENCE_CURSOR_END);
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
    CettaIndex *positions = NULL, count = 0u, capacity = 0u;
    assert(space_path_index_candidates(&space, query, &positions, &count, &capacity));
    assert(count == 13u);
    assert(capacity == 77u);
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
    assert(space_path_indexes_info(&space, &info));
    assert(info.path_length == DEPTH + 1u && info.uncertain_rows == 3u);
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
        assert(space_path_index_candidates(&space, query, &positions, &count, NULL));
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
        CettaIndex *positions = NULL, count = 0u, capacity = 0u;
        Atom *query = queries[query_index];
        assert(space_match_native_try_cold_candidates(
            &space, query, &positions, &count, &capacity));
        assert(capacity >= count);
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

static void test_string_prefixes_collisions_and_binary_values(TermUniverse *universe,
                                                              Arena *arena) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    SymbolId head = symbol_intern_cstr(g_symbols, "string-path-row");
    for (int64_t index = 0; index < 512; index++) {
        char key[32];
        snprintf(key, sizeof(key), "string-key-%lld", (long long)index);
        add(&space, row(arena, head, atom_string(arena, key), atom_int(arena, index)));
    }
    Atom *left = atom_string(arena, "!c");
    Atom *collision = atom_string(arena, "\"@");
    assert(atom_hash(left) == atom_hash(collision));
    assert(!atom_eq(left, collision));
    add(&space, row(arena, head, left, atom_int(arena, 1)));
    CettaIndex collision_position = space_length64(&space);
    add(&space, row(arena, head, collision, atom_int(arena, 2)));
    add(&space, row(arena, head, left, atom_int(arena, 1)));
    add(&space, row(arena, head, atom_var(arena, "open-key"), atom_int(arena, 3)));
    add(&space, atom_var(arena, "whole-row"));
    add(&space, row(arena, head, atom_int(arena, 9), atom_int(arena, 4)));
    const char binary[] = {'x', '\0', (char)0xff};
    Atom *bytes = atom_string_n(arena, binary, sizeof(binary));
    add(&space, row(arena, head, bytes, atom_int(arena, 5)));
    /* Exercise the maintained index rather than the first cold flat probes. */
    space.match_backend.native.cold_flat_scans = 4u;
    Atom *query = row(arena, head, left, atom_var(arena, "result"));
    compare_reference(&space, arena, query);
    CettaSpacePathIndexInfo info;
    assert(space_path_indexes_info(&space, &info));
    assert(info.key_kind == CETTA_SPACE_PATH_STRING_HASH_KEY);
    CettaIndex *candidates = NULL, count = 0u;
    assert(space_path_index_candidates(&space, query, &candidates, &count));
    assert(count < 8u);
    bool included_collision = false;
    for (CettaIndex index = 0; index < count; index++)
        if (candidates[index] == collision_position) included_collision = true;
    assert(included_collision);
    assert(!direct_match(&space, arena, query, collision_position));
    free(candidates);
    compare_reference(&space, arena, row(arena, head, bytes, atom_var(arena, "binary-result")));
    compare_reference(&space, arena, row(arena, head, atom_string(arena, "x"), atom_var(arena, "prefix-result")));
    add(&space, row(arena, head, left, atom_int(arena, 6)));
    compare_reference(&space, arena, query);
    assert(space_remove_atom_id(&space, space_get_atom_id_at64(&space, 0u)));
    assert(!space_path_indexes_info(&space, &info));
    compare_reference(&space, arena, query);
    space_free(&space);
}

static void test_unrecorded_string_paths(Arena *arena) {
    Arena persistent;
    arena_init_detached(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe,&persistent);
    Space space;
    space_init_with_universe(&space,&universe);
    space.kind=SPACE_KIND_STACK;
    Atom *head=atom_symbol_data_n(arena,"unrecorded-string-row",21u);
    for (int64_t index=0; index<512; ++index) {
        char key[32];
        snprintf(key,sizeof(key),"id-%lld",(long long)index);
        Atom *children[]={head,atom_string(arena,key),atom_int(arena,index)};
        assert(space_admit_atom(&space,&persistent,atom_expr(arena,children,3u)));
    }
    assert(!tu_hdr(&universe,space_get_atom_id_at64(&space,0u)));
    Atom *children[]={head,atom_string(arena,"id-17"),atom_var(arena,"value")};
    Atom *pattern=atom_expr(arena,children,3u);
    space.match_backend.native.cold_flat_scans=4u;
    compare_reference(&space,arena,pattern);
    CettaIndex *positions=NULL,count=0u;
    assert(space_path_index_candidates(&space,pattern,&positions,&count));
    assert(count==1u && positions[0]==17u);
    free(positions);
    space_free(&space);
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
    test_string_prefixes_collisions_and_binary_values(&universe, &scratch);
    test_unrecorded_string_paths(&scratch);
    test_flat_and_captured_readers(&universe, &scratch);
    test_uncertain_rows_keep_rigid_prefixes(&universe, &scratch);
    test_nested_prefix_admission(&universe, &scratch);
    test_nested_paths(&universe, &scratch);
    test_unrelated_rows(&universe, &scratch, 1024u);
    test_unrelated_rows(&universe, &scratch, 2048u);
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
