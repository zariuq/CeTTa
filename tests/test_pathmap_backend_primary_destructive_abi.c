#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "space.h"
#include "space_match_backend.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"

static Arena *g_persistent_arena = NULL;

Arena *eval_current_persistent_arena(void) {
    return g_persistent_arena;
}

static void init_test_symbols(SymbolTable *symbols) {
    symbol_table_init(symbols);
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
}

static Atom *make_edge(Arena *a, SymbolId edge_sym, SymbolId lhs, SymbolId rhs) {
    Atom *elems[3] = {
        atom_symbol_id(a, edge_sym),
        atom_symbol_id(a, lhs),
        atom_symbol_id(a, rhs),
    };
    return atom_expr(a, elems, 3);
}

static Atom *make_edge_with_atom(Arena *a, SymbolId edge_sym,
                                 Atom *lhs, Atom *rhs) {
    Atom *elems[3] = {
        atom_symbol_id(a, edge_sym),
        lhs,
        rhs,
    };
    return atom_expr(a, elems, 3);
}

typedef struct {
    uint32_t visits;
    bool keep_going;
} RowVisitProbe;

typedef struct {
    VarId query_var;
    SymbolId spelling;
    SymbolId payload_sym;
    SymbolId lam_sym;
    SymbolId pair_sym;
    uint32_t visits;
} OpeningCaptureProbe;

static bool count_decoded_row(const Bindings *row, void *raw_probe) {
    RowVisitProbe *probe = raw_probe;
    assert(row != NULL);
    probe->visits++;
    return probe->keep_going;
}

static bool inspect_opened_binder_shaped_payload(
    const Bindings *row, void *raw_probe) {
    OpeningCaptureProbe *probe = raw_probe;
    Atom *payload = bindings_lookup_id((Bindings *)row, probe->query_var);
    assert(payload != NULL);
    assert(payload->kind == ATOM_EXPR && payload->expr.len == 3u);
    assert(atom_is_symbol_id(payload->expr.elems[0], probe->payload_sym));

    Atom *lam = payload->expr.elems[1];
    Atom *pair = payload->expr.elems[2];
    assert(lam->kind == ATOM_EXPR && lam->expr.len == 2u);
    assert(pair->kind == ATOM_EXPR && pair->expr.len == 3u);
    assert(atom_is_symbol_id(lam->expr.elems[0], probe->lam_sym));
    assert(atom_is_symbol_id(pair->expr.elems[0], probe->pair_sym));

    Atom *binder_shaped = lam->expr.elems[1];
    Atom *free0 = pair->expr.elems[1];
    Atom *free1 = pair->expr.elems[2];
    assert(binder_shaped->kind == ATOM_VAR);
    assert(free0->kind == ATOM_VAR && free1->kind == ATOM_VAR);
    assert(binder_shaped->sym_id == probe->spelling);
    assert(free0->sym_id == probe->spelling);
    assert(free1->sym_id == probe->spelling);
    assert(binder_shaped->var_id != free0->var_id);
    assert(free0->var_id == free1->var_id);
    probe->visits++;
    return true;
}

static void test_indexed_opening_spelling_capture_fence(
    Arena *arena, TermUniverse *universe) {
    Space space;
    SymbolId capture_sym = symbol_intern_cstr(g_symbols, "capture");
    SymbolId payload_sym = symbol_intern_cstr(g_symbols, "payload");
    SymbolId lam_sym = symbol_intern_cstr(g_symbols, "Lam");
    SymbolId pair_sym = symbol_intern_cstr(g_symbols, "Pair");
    SymbolId spelling = symbol_intern_cstr(g_symbols, "x");
    SymbolId query_spelling = symbol_intern_cstr(g_symbols, "payload-query");
    VarId query_var_id = UINT64_C(83001);

    Atom *binder_shaped = atom_var_with_spelling(
        arena, spelling, UINT64_C(81001));
    Atom *free = atom_var_with_spelling(
        arena, spelling, UINT64_C(82002));
    Atom *lam = atom_expr2(
        arena, atom_symbol_id(arena, lam_sym), binder_shaped);
    Atom *pair = atom_expr3(
        arena, atom_symbol_id(arena, pair_sym), free, free);
    Atom *payload = atom_expr3(
        arena, atom_symbol_id(arena, payload_sym), lam, pair);
    Atom *stored = atom_expr2(
        arena, atom_symbol_id(arena, capture_sym), payload);
    AtomId stored_id = term_universe_store_atom_id(universe, NULL, stored);
    assert(stored_id != CETTA_ATOM_ID_NONE);

    Atom *query_var = atom_var_with_spelling(
        arena, query_spelling, query_var_id);
    Atom *query = atom_expr2(
        arena, atom_symbol_id(arena, capture_sym), query_var);
    Atom *patterns[] = {query};
    OpeningCaptureProbe probe = {
        .query_var = query_var_id,
        .spelling = spelling,
        .payload_sym = payload_sym,
        .lam_sym = lam_sym,
        .pair_sym = pair_sym,
        .visits = 0u,
    };

    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_HASH;
    assert(space_match_backend_try_set(&space, SPACE_ENGINE_PATHMAP));
    space_add_atom_id(&space, stored_id);
    assert(space_match_backend_try_visit_conjunction_indexed(
               &space, arena, patterns, 1u, NULL,
               inspect_opened_binder_shaped_payload, &probe) ==
           SPACE_MATCH_PULL_VISIT_COMPLETE);
    assert(probe.visits == 1u);
    space_free(&space);
}

static void test_streamed_row_disposition(Arena *arena) {
    Bindings valid;
    Bindings cyclic;
    RowVisitProbe probe = {.visits = 0u, .keep_going = true};
    SymbolId query_spelling = symbol_intern_cstr(g_symbols, "row-query");
    SymbolId f_sym = symbol_intern_cstr(g_symbols, "row-f");
    Atom *query = atom_var_with_spelling(arena, query_spelling, 90001u);
    Atom *cyclic_value = atom_expr2(
        arena, atom_symbol_id(arena, f_sym), query);

    bindings_init(&valid);
    bindings_init(&cyclic);
    assert(bindings_add_id(
        &cyclic, query->var_id, query->sym_id, cyclic_value));
    assert(bindings_has_loop(&cyclic));

    assert(space_match_backend_visit_decoded_row(
               false, &valid, count_decoded_row, &probe) ==
           SPACE_MATCH_DECODED_ROW_FAULT);
    assert(probe.visits == 0u);
    assert(space_match_backend_visit_decoded_row(
               true, &cyclic, count_decoded_row, &probe) ==
           SPACE_MATCH_DECODED_ROW_CONTINUE);
    assert(probe.visits == 0u);
    assert(space_match_backend_visit_decoded_row(
               true, &valid, count_decoded_row, &probe) ==
           SPACE_MATCH_DECODED_ROW_CONTINUE);
    assert(probe.visits == 1u);
    probe.keep_going = false;
    assert(space_match_backend_visit_decoded_row(
               true, &valid, count_decoded_row, &probe) ==
           SPACE_MATCH_DECODED_ROW_STOP);
    assert(probe.visits == 2u);

    bindings_free(&cyclic);
    bindings_free(&valid);
}

static CettaIndex exact_match_count(Space *space, Arena *scratch,
                                    TermUniverse *universe, AtomId atom_id) {
    SubstMatchSet matches;
    Atom *atom = term_universe_get_atom(universe, atom_id);
    assert(atom != NULL);
    space_subst_query(space, scratch, atom, &matches);
    CettaIndex count = matches.len;
    smset_free(&matches);
    return count;
}

static void test_candidate_shadow_append_is_incremental(
    Arena *arena, TermUniverse *universe, AtomId atom_id) {
    Space space;
    CettaIndex *candidates = NULL;
    DiscNode *built_trie;

    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_HASH;
    assert(space_match_backend_try_set(&space, SPACE_ENGINE_PATHMAP));

    for (CettaIndex i = 0; i <= MATCH_TRIE_THRESHOLD; i++)
        space_add_atom_id(&space, atom_id);
    assert(space_length64(&space) == MATCH_TRIE_THRESHOLD + 1u);

    Atom *pattern = term_universe_get_atom(universe, atom_id);
    assert(pattern != NULL);
    assert(space_match_candidates64(&space, pattern, &candidates) ==
           MATCH_TRIE_THRESHOLD + 1u);
    free(candidates);
    candidates = NULL;
    built_trie = space.match_backend.native.match_trie;
    assert(built_trie != NULL);
    assert(!space.match_backend.native.match_trie_dirty);

    space_add_atom_id(&space, atom_id);
    assert(space_length64(&space) == MATCH_TRIE_THRESHOLD + 2u);
    assert(space.match_backend.native.match_trie == built_trie);
    assert(!space.match_backend.native.match_trie_dirty);
    assert(space_match_candidates64(&space, pattern, &candidates) ==
           MATCH_TRIE_THRESHOLD + 2u);
    free(candidates);
    assert(space.match_backend.native.match_trie == built_trie);
    assert(!space.match_backend.native.match_trie_dirty);

    space_free(&space);
    (void)arena;
}

static void test_native_fallback_index_append(Arena *arena,
                                              TermUniverse *universe) {
    Space space;
    CettaIndex *candidates = NULL;
    SubstMatchSet matches;
    DiscNode *built_trie;
    SubstTree *built_stree;
    SymbolId box_sym = symbol_intern_cstr(g_symbols, "fallback-box");
    Atom *boxed = atom_expr2(
        arena, atom_symbol_id(arena, box_sym), atom_space(arena, universe));
    AtomId boxed_id = term_universe_store_atom_id(universe, NULL, boxed);

    assert(boxed_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, boxed_id) == NULL);
    space_init_with_universe(&space, universe);
    for (CettaIndex i = 0; i <= MATCH_TRIE_THRESHOLD; i++)
        space_add_atom_id(&space, boxed_id);

    assert(space_match_candidates64(&space, boxed, &candidates) ==
           MATCH_TRIE_THRESHOLD + 1u);
    free(candidates);
    built_trie = space.match_backend.native.match_trie;
    assert(built_trie != NULL);

    space_subst_query(&space, arena, boxed, &matches);
    assert(matches.len == MATCH_TRIE_THRESHOLD + 1u);
    smset_free(&matches);
    built_stree = space.match_backend.native.stree;
    assert(built_stree != NULL);

    assert(space_admit_atom(&space, g_persistent_arena, boxed));
    assert(space.match_backend.native.match_trie == built_trie);
    assert(!space.match_backend.native.match_trie_dirty);
    assert(space.match_backend.native.stree == built_stree);
    assert(!space.match_backend.native.stree_dirty);

    candidates = NULL;
    assert(space_match_candidates64(&space, boxed, &candidates) ==
           MATCH_TRIE_THRESHOLD + 2u);
    free(candidates);
    space_subst_query(&space, arena, boxed, &matches);
    assert(matches.len == MATCH_TRIE_THRESHOLD + 2u);
    smset_free(&matches);
    space_free(&space);
}

static void test_batch_mutation_transaction(Arena *arena,
                                            TermUniverse *universe,
                                            SymbolId edge_sym,
                                            SymbolId a_sym,
                                            SymbolId b_sym,
                                            SymbolId c_sym,
                                            SymbolId d_sym,
                                            SymbolId e_sym) {
    static const char wide_name[] =
        "this-symbol-is-deliberately-longer-than-the-compact-pathmap-wire-limit-0123456789";
    Space batch;
    Space snapshot;
    AtomId id_ab;
    AtomId id_bc;
    AtomId id_missing;
    AtomId id_variable;
    AtomId id_wide;
    AtomId adds[3];
    AtomId removes[4];
    AtomId fallback[2];
    CettaCount removed = 0;
    uint64_t revision;

    space_init_with_universe(&batch, universe);
    batch.kind = SPACE_KIND_HASH;
    assert(space_match_backend_try_set(&batch, SPACE_ENGINE_PATHMAP));

    id_ab = term_universe_store_atom_id(
        universe, NULL, make_edge(arena, edge_sym, a_sym, b_sym));
    id_bc = term_universe_store_atom_id(
        universe, NULL, make_edge(arena, edge_sym, b_sym, c_sym));
    id_missing = term_universe_store_atom_id(
        universe, NULL, make_edge(arena, edge_sym, d_sym, e_sym));
    id_variable = term_universe_store_atom_id(
        universe, NULL,
        make_edge_with_atom(
            arena, edge_sym,
            atom_var_with_spelling(
                arena, symbol_intern_cstr(g_symbols, "batch-x"), 70001u),
            atom_symbol_id(arena, c_sym)));
    id_wide = term_universe_store_atom_id(
        universe, NULL,
        make_edge_with_atom(
            arena, edge_sym,
            atom_symbol(arena, wide_name),
            atom_symbol_id(arena, a_sym)));
    assert(id_ab != CETTA_ATOM_ID_NONE);
    assert(id_bc != CETTA_ATOM_ID_NONE);
    assert(id_missing != CETTA_ATOM_ID_NONE);
    assert(id_variable != CETTA_ATOM_ID_NONE);
    assert(id_wide != CETTA_ATOM_ID_NONE);

    adds[0] = id_ab;
    adds[1] = id_ab;
    adds[2] = id_bc;
    revision = space_revision(&batch);
    assert(space_add_atom_ids_batch(&batch, adds, 3));
    assert(space_revision(&batch) == revision + 1u);
    assert(space_length64(&batch) == 3);
    assert(exact_match_count(&batch, arena, universe, id_ab) == 2);
    assert(exact_match_count(&batch, arena, universe, id_bc) == 1);

    space_init_with_universe(&snapshot, universe);
    snapshot.kind = SPACE_KIND_HASH;
    assert(space_match_backend_snapshot_clone(&snapshot, &batch));
    assert(space_length64(&snapshot) == 3);

    removes[0] = id_missing;
    removes[1] = id_ab;
    removes[2] = id_missing;
    removes[3] = id_ab;
    revision = space_revision(&batch);
    assert(space_remove_atom_ids_batch(&batch, removes, 4, &removed));
    assert(removed == 2);
    assert(space_revision(&batch) == revision + 1u);
    assert(space_length64(&batch) == 1);
    assert(exact_match_count(&batch, arena, universe, id_ab) == 0);
    assert(exact_match_count(&batch, arena, universe, id_bc) == 1);
    assert(space_length64(&snapshot) == 3);
    assert(exact_match_count(&snapshot, arena, universe, id_ab) == 2);

    revision = space_revision(&batch);
    removed = UINT64_MAX;
    assert(space_remove_atom_ids_batch(
        &batch, &id_missing, 1, &removed));
    assert(removed == 0);
    assert(space_revision(&batch) == revision);

    revision = space_revision(&batch);
    assert(space_add_atom_ids_batch(&batch, adds, 2));
    assert(space_revision(&batch) == revision + 1u);
    assert(exact_match_count(&batch, arena, universe, id_ab) == 2);

    /* Variables and wide symbols are outside the compact transaction
       fragment.  They must replay through the singular semantic oracle, not
       disappear or become lossy bridge encodings. */
    fallback[0] = id_variable;
    fallback[1] = id_wide;
    revision = space_revision(&batch);
    assert(space_add_atom_ids_batch(&batch, fallback, 2));
    assert(space_revision(&batch) == revision + 2u);
    assert(space_contains_atom_id(&batch, id_variable));
    assert(space_contains_atom_id(&batch, id_wide));
    revision = space_revision(&batch);
    assert(space_remove_atom_ids_batch(&batch, fallback, 2, &removed));
    assert(removed == 2);
    assert(space_revision(&batch) == revision + 2u);
    assert(!space_contains_atom_id(&batch, id_variable));
    assert(!space_contains_atom_id(&batch, id_wide));

    space_free(&snapshot);
    space_free(&batch);
}

int main(void) {
    SymbolTable symbols;
    Arena persistent;
    TermUniverse universe;
    Arena arena;
    Space space;
    SymbolId edge_sym;
    SymbolId a_sym;
    SymbolId b_sym;
    SymbolId c_sym;
    SymbolId d_sym;
    SymbolId e_sym;
    AtomId id_ab;
    AtomId id_bc;
    AtomId id_cd;
    AtomId id_de;
    AtomId kept0;
    AtomId kept1;
    AtomId popped_id;
    AtomId survivor;
    Atom *popped = NULL;

    init_test_symbols(&symbols);
    term_universe_init(&universe);
    arena_init(&persistent);
    term_universe_set_persistent_arena(&universe, &persistent);
    g_persistent_arena = &persistent;
    arena_init(&arena);
    space_init_with_universe(&space, &universe);
    space.kind = SPACE_KIND_HASH;

    edge_sym = symbol_intern_cstr(g_symbols, "edge");
    a_sym = symbol_intern_cstr(g_symbols, "a");
    b_sym = symbol_intern_cstr(g_symbols, "b");
    c_sym = symbol_intern_cstr(g_symbols, "c");
    d_sym = symbol_intern_cstr(g_symbols, "d");
    e_sym = symbol_intern_cstr(g_symbols, "e");

    test_streamed_row_disposition(&arena);

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_PATHMAP));
    id_ab = term_universe_store_atom_id(
        &universe, NULL, make_edge(&arena, edge_sym, a_sym, b_sym));
    id_bc = term_universe_store_atom_id(
        &universe, NULL, make_edge(&arena, edge_sym, b_sym, c_sym));
    id_cd = term_universe_store_atom_id(
        &universe, NULL, make_edge(&arena, edge_sym, c_sym, d_sym));
    id_de = term_universe_store_atom_id(
        &universe, NULL, make_edge(&arena, edge_sym, d_sym, e_sym));
    assert(id_ab != CETTA_ATOM_ID_NONE);
    assert(id_bc != CETTA_ATOM_ID_NONE);
    assert(id_cd != CETTA_ATOM_ID_NONE);
    assert(id_de != CETTA_ATOM_ID_NONE);

    test_batch_mutation_transaction(
        &arena, &universe, edge_sym, a_sym, b_sym, c_sym, d_sym, e_sym);
    test_indexed_opening_spelling_capture_fence(&arena, &universe);
    test_candidate_shadow_append_is_incremental(&arena, &universe, id_ab);
    test_native_fallback_index_append(&arena, &universe);

    space_add_atom_id(&space, id_ab);
    space_add_atom_id(&space, id_bc);
    space_add_atom_id(&space, id_cd);
    space_add_atom_id(&space, id_de);
    assert(space_length64(&space) == 4);

    assert(space_pop(&space, &popped));
    assert(popped != NULL);
    popped_id = term_universe_lookup_atom_id(&universe, popped);
    assert(popped_id != CETTA_ATOM_ID_NONE);
    assert(space_length64(&space) == 3);
    assert(!space_contains_atom_id(&space, popped_id));

    kept0 = space_get_atom_id_at(&space, 0);
    kept1 = space_get_atom_id_at(&space, 1);
    assert(kept0 != CETTA_ATOM_ID_NONE);
    assert(kept1 != CETTA_ATOM_ID_NONE);

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_NATIVE_CANDIDATE_EXACT));
    assert(space_length64(&space) == 3);
    assert(!space_contains_atom_id(&space, popped_id));
    assert(space_contains_atom_id(&space, kept0));
    assert(space_contains_atom_id(&space, kept1));

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_PATHMAP));
    assert(space_truncate(&space, 1));
    assert(space_length64(&space) == 1);
    survivor = space_get_atom_id_at(&space, 0);
    assert(survivor != CETTA_ATOM_ID_NONE);

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_NATIVE_CANDIDATE_EXACT));
    assert(space_length64(&space) == 1);
    assert(space_get_atom_id_at(&space, 0) == survivor);

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_PATHMAP));
    assert(space_truncate(&space, 0));
    assert(space_length64(&space) == 0);

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_NATIVE_CANDIDATE_EXACT));
    assert(space_length64(&space) == 0);

    space_free(&space);
    arena_free(&arena);
    g_persistent_arena = NULL;
    term_universe_free(&universe);
    arena_free(&persistent);
    symbol_table_free(&symbols);
    printf("PASS: pathmap backend-primary destructive ABI\n");
    return 0;
}
