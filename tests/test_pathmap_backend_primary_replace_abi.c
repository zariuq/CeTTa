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

int main(void) {
    static const uint8_t add_chunk[] = "(edge c d)\n(edge d e)\n";
    static const uint8_t remove_chunk[] = "(edge a b)\n";
    SymbolTable symbols;
    Arena persistent;
    TermUniverse universe;
    Arena arena;
    Space target;
    Space *work = NULL;
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
    uint64_t added = 0;
    uint64_t removed = 0;

    init_test_symbols(&symbols);
    term_universe_init(&universe);
    arena_init(&persistent);
    term_universe_set_persistent_arena(&universe, &persistent);
    g_persistent_arena = &persistent;
    arena_init(&arena);

    space_init_with_universe(&target, &universe);
    target.kind = SPACE_KIND_HASH;

    edge_sym = symbol_intern_cstr(g_symbols, "edge");
    a_sym = symbol_intern_cstr(g_symbols, "a");
    b_sym = symbol_intern_cstr(g_symbols, "b");
    c_sym = symbol_intern_cstr(g_symbols, "c");
    d_sym = symbol_intern_cstr(g_symbols, "d");
    e_sym = symbol_intern_cstr(g_symbols, "e");

    assert(space_match_backend_try_set(&target, SPACE_ENGINE_PATHMAP));
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

    space_add_atom_id(&target, id_ab);
    space_add_atom_id(&target, id_bc);
    assert(space_length(&target) == 2);
    assert(space_contains_atom_id(&target, id_ab));
    assert(space_contains_atom_id(&target, id_bc));

    work = space_heap_clone_shallow(&target);
    assert(work != NULL);
    assert(work->match_backend.kind == SPACE_ENGINE_PATHMAP);
    assert(space_length(work) == 2);

    assert(space_match_backend_load_sexpr_chunk(
        work, &persistent, add_chunk, sizeof(add_chunk) - 1u, &added));
    assert(added == 2);
    assert(space_length(work) == 4);
    assert(space_contains_atom_id(work, id_cd));
    assert(space_contains_atom_id(work, id_de));

    assert(space_match_backend_remove_sexpr_chunk(
        work, &persistent, remove_chunk, sizeof(remove_chunk) - 1u, &removed));
    assert(removed == 1);
    assert(space_length(work) == 3);
    assert(!space_contains_atom_id(work, id_ab));
    assert(space_contains_atom_id(work, id_bc));
    assert(space_contains_atom_id(work, id_cd));
    assert(space_contains_atom_id(work, id_de));

    assert(space_length(&target) == 2);
    assert(space_contains_atom_id(&target, id_ab));
    assert(space_contains_atom_id(&target, id_bc));

    space_replace_contents(&target, work);
    assert(target.match_backend.kind == SPACE_ENGINE_PATHMAP);
    assert(space_length(&target) == 3);
    assert(!space_contains_atom_id(&target, id_ab));
    assert(space_contains_atom_id(&target, id_bc));
    assert(space_contains_atom_id(&target, id_cd));
    assert(space_contains_atom_id(&target, id_de));

    assert(work->match_backend.kind == SPACE_ENGINE_NATIVE);
    assert(space_length(work) == 0);
    assert(work->kind == SPACE_KIND_ATOM);

    assert(space_match_backend_try_set(&target, SPACE_ENGINE_NATIVE_CANDIDATE_EXACT));
    assert(space_length(&target) == 3);
    assert(!space_contains_atom_id(&target, id_ab));
    assert(space_contains_atom_id(&target, id_bc));
    assert(space_contains_atom_id(&target, id_cd));
    assert(space_contains_atom_id(&target, id_de));

    assert(space_match_backend_try_set(&target, SPACE_ENGINE_PATHMAP));
    space_add_atom_id(&target, id_ab);
    assert(space_length(&target) == 4);
    assert(space_contains_atom_id(&target, id_ab));

    assert(space_match_backend_try_set(&target, SPACE_ENGINE_NATIVE_CANDIDATE_EXACT));
    assert(space_length(&target) == 4);
    assert(space_contains_atom_id(&target, id_ab));
    assert(space_contains_atom_id(&target, id_bc));
    assert(space_contains_atom_id(&target, id_cd));
    assert(space_contains_atom_id(&target, id_de));

    space_free(work);
    free(work);
    space_free(&target);
    arena_free(&arena);
    g_persistent_arena = NULL;
    term_universe_free(&universe);
    arena_free(&persistent);
    symbol_table_free(&symbols);
    puts("PASS: pathmap backend-primary replace ABI");
    return 0;
}
