#include <assert.h>
#include <stdio.h>

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
    space_add_atom_id(&space, id_ab);
    space_add_atom_id(&space, id_bc);
    space_add_atom_id(&space, id_cd);
    space_add_atom_id(&space, id_de);
    assert(space_length(&space) == 4);

    assert(space_pop(&space, &popped));
    assert(popped != NULL);
    popped_id = term_universe_lookup_atom_id(&universe, popped);
    assert(popped_id != CETTA_ATOM_ID_NONE);
    assert(space_length(&space) == 3);
    assert(!space_contains_atom_id(&space, popped_id));

    kept0 = space_get_atom_id_at(&space, 0);
    kept1 = space_get_atom_id_at(&space, 1);
    assert(kept0 != CETTA_ATOM_ID_NONE);
    assert(kept1 != CETTA_ATOM_ID_NONE);

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_NATIVE_CANDIDATE_EXACT));
    assert(space_length(&space) == 3);
    assert(!space_contains_atom_id(&space, popped_id));
    assert(space_contains_atom_id(&space, kept0));
    assert(space_contains_atom_id(&space, kept1));

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_PATHMAP));
    assert(space_truncate(&space, 1));
    assert(space_length(&space) == 1);
    survivor = space_get_atom_id_at(&space, 0);
    assert(survivor != CETTA_ATOM_ID_NONE);

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_NATIVE_CANDIDATE_EXACT));
    assert(space_length(&space) == 1);
    assert(space_get_atom_id_at(&space, 0) == survivor);

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_PATHMAP));
    assert(space_truncate(&space, 0));
    assert(space_length(&space) == 0);

    assert(space_match_backend_try_set(&space, SPACE_ENGINE_NATIVE_CANDIDATE_EXACT));
    assert(space_length(&space) == 0);

    space_free(&space);
    arena_free(&arena);
    g_persistent_arena = NULL;
    term_universe_free(&universe);
    arena_free(&persistent);
    symbol_table_free(&symbols);
    printf("PASS: pathmap backend-primary destructive ABI\n");
    return 0;
}
