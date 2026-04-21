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

static Atom *sym(Arena *a, const char *name) {
    return atom_symbol(a, name);
}

static Atom *var(Arena *a, const char *name, VarId id) {
    return atom_var_with_spelling(a, symbol_intern_cstr(g_symbols, name), id);
}

static Atom *expr3(Arena *a, Atom *x0, Atom *x1, Atom *x2) {
    Atom *items[3] = {x0, x1, x2};
    return atom_expr(a, items, 3);
}

static Atom *make_typed_fact(Arena *a) {
    Atom *type_a_0 = var(a, "a", 1001);
    Atom *type_a_1 = var(a, "a", 1001);
    Atom *type_b = var(a, "b", 1002);
    Atom *inner = expr3(a, sym(a, "->"), type_b, type_a_1);
    Atom *outer = expr3(a, sym(a, "->"), type_a_0, inner);
    return expr3(a, sym(a, ":"), sym(a, "ax-1"), outer);
}

static Atom *make_typed_query(Arena *a) {
    return expr3(a, sym(a, ":"), var(a, "x", 2001), var(a, "t", 2002));
}

static void assert_typed_match_shape(const Bindings *bindings, SymbolId arrow_sym) {
    Atom *x_val = bindings_lookup_id((Bindings *)bindings, 2001);
    Atom *t_val = bindings_lookup_id((Bindings *)bindings, 2002);
    assert(x_val != NULL);
    assert(t_val != NULL);
    assert(x_val->kind == ATOM_SYMBOL);
    assert(x_val->sym_id == symbol_intern_cstr(g_symbols, "ax-1"));
    assert(t_val->kind == ATOM_EXPR);
    assert(atom_has_vars(t_val));
    assert(t_val->expr.len == 3);
    assert(t_val->expr.elems[0]->kind == ATOM_SYMBOL);
    assert(t_val->expr.elems[0]->sym_id == arrow_sym);
    assert(t_val->expr.elems[1]->kind == ATOM_VAR);
    assert(t_val->expr.elems[2]->kind == ATOM_EXPR);
    assert(t_val->expr.elems[2]->expr.len == 3);
    assert(t_val->expr.elems[2]->expr.elems[0]->kind == ATOM_SYMBOL);
    assert(t_val->expr.elems[2]->expr.elems[0]->sym_id == arrow_sym);
    assert(t_val->expr.elems[2]->expr.elems[1]->kind == ATOM_VAR);
    assert(t_val->expr.elems[2]->expr.elems[2]->kind == ATOM_VAR);
    assert(t_val->expr.elems[1]->var_id == t_val->expr.elems[2]->expr.elems[2]->var_id);
}

int main(void) {
    SymbolTable symbols;
    Arena persistent;
    TermUniverse universe;
    Arena arena;
    Space space;
    AtomId fact_id;
    SubstMatchSet matches;
    Atom *fact;
    Atom *query;
    SymbolId arrow_sym;

    init_test_symbols(&symbols);
    term_universe_init(&universe);
    arena_init(&persistent);
    term_universe_set_persistent_arena(&universe, &persistent);
    g_persistent_arena = &persistent;
    arena_init(&arena);

    space_init_with_universe(&space, &universe);
    space.kind = SPACE_KIND_HASH;
    assert(space_match_backend_try_set(&space, SPACE_ENGINE_PATHMAP));

    fact = make_typed_fact(&arena);
    query = make_typed_query(&arena);
    arrow_sym = symbol_intern_cstr(g_symbols, "->");

    fact_id = term_universe_store_atom_id(&universe, NULL, fact);
    assert(fact_id != CETTA_ATOM_ID_NONE);
    space_add_atom_id(&space, fact_id);
    assert(space_length(&space) == 1);

    smset_init(&matches);
    space_subst_query(&space, &arena, query, &matches);
    assert(matches.len == 1);
    assert(matches.items[0].bindings.len == 2);
    assert_typed_match_shape(&matches.items[0].bindings, arrow_sym);
    smset_free(&matches);

    space_free(&space);
    arena_free(&arena);
    g_persistent_arena = NULL;
    term_universe_free(&universe);
    arena_free(&persistent);
    symbol_table_free(&symbols);
    puts("PASS: pathmap typed query ABI");
    return 0;
}
