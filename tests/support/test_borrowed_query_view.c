#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "atom.h"
#include "space.h"
#include "symbol.h"

static void compare(Space *space, Arena *arena, Atom **parts, CettaExprLen len) {
    Atom *query = atom_expr(arena, parts, len);
    assert(query);
    CettaIndex *ordinary = NULL, *borrowed = NULL;
    CettaIndex expected = space_match_candidates64(space, query, &ordinary), actual = 0;
    ArenaMark mark = arena_mark(arena);
    assert(space_match_candidates_expression_coordinates64(space, parts, len, &borrowed, &actual));
    assert(arena_at_mark(arena, mark));
    CettaIndex matched = 0u;
    for (CettaIndex position = 0u; position < space_length64(space); position++) {
        Bindings reference; bindings_init(&reference);
        bool succeeds = match_atoms_atom_id_epoch(query, space->native.universe,
            space_get_atom_id_at64(space, position), &reference, arena, 333u);
        bindings_free(&reference);
        if (!succeeds) continue;
        while (matched < actual) {
            Bindings trial; bindings_init(&trial);
            bool kept = match_atoms_atom_id_epoch(query, space->native.universe,
                space_get_atom_id_at64(space, borrowed[matched]), &trial, arena, 334u);
            bindings_free(&trial);
            if (kept) break;
            matched++;
        }
        assert(matched < actual && borrowed[matched++] == position);
    }
    while (matched < actual) {
        Bindings trial; bindings_init(&trial);
        assert(!match_atoms_atom_id_epoch(query, space->native.universe,
            space_get_atom_id_at64(space, borrowed[matched++]), &trial, arena, 335u));
        bindings_free(&trial);
    }
    assert(expected == actual);
    for (CettaIndex i = 0; i < expected; i++) assert(ordinary[i] == borrowed[i]);
    AtomExprBorrowedView view;
    assert(atom_expr_borrowed_view_init(&view, parts, len));
    assert(atom_eq(&view.root, query));
    assert(atom_has_vars(&view.root) == atom_has_vars(query));
    assert(!(view.root.flags & ATOM_FLAG_ARENA_CLOSED));
    assert(!(view.root.structural_facts & ATOM_STRUCTURAL_OWNER_CLOSED));
    free(ordinary); free(borrowed);
}
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    Arena arena; arena_init(&arena);
    TermUniverse universe; term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    Space storage; Space *space = &storage; space_init_with_universe(space, &universe);
    Atom *head = atom_symbol(&arena, "view-row");
    Atom *x = atom_var_with_id(&arena, "x", 1201), *y = atom_var_with_id(&arena, "y", 1202);
    for (int i = 0; i < 100; i++) {
        Atom *row = atom_expr3(&arena, head, atom_int(&arena, i % 17), atom_int(&arena, i % 17));
        AtomId id = term_universe_store_atom_id(&universe, NULL, row);
        assert(id != CETTA_ATOM_ID_NONE); space_add_atom_id(space, id);
    }
    Atom *open[] = {head, x, y}, *repeated[] = {head, x, x};
    Atom *rigid[] = {head, atom_int(&arena, 7), y};
    Atom *missing[] = {head, atom_int(&arena, 999), y};
    Atom *nested[] = {head, atom_expr2(&arena, x, atom_int(&arena, 4)), y};
    for (int i = 0; i < 5; i++) {
        compare(space, &arena, open, 3); compare(space, &arena, repeated, 3);
        compare(space, &arena, rigid, 3); compare(space, &arena, missing, 3);
        compare(space, &arena, nested, 3);
    }
    for (int i = 0; i < 20; i++) {
        rigid[1] = atom_int(&arena, i % 17);
        compare(space, &arena, rigid, 3);
    }
    Atom *deep = atom_int(&arena, 42);
    for (int i = 0; i < 128; i++) { Atom *one[] = {deep}; deep = atom_expr(&arena, one, 1u); }
    Atom *deep_parts[] = {head, deep, atom_symbol(&arena, "deep")};
    AtomId deep_id = term_universe_store_atom_id(&universe, NULL,
        atom_expr(&arena, deep_parts, 3));
    assert(deep_id != CETTA_ATOM_ID_NONE); space_add_atom_id(space, deep_id);
    compare(space, &arena, deep_parts, 3);
    /* Each query owner is released after the synchronous read. Subsequent
     * queries must not read coordinates retained from an earlier owner. */
    for (int iteration = 0; iteration < 1000; iteration++) {
        Arena temporary; arena_init(&temporary);
        Atom *parts[] = {
            atom_symbol(&temporary, "view-row"),
            atom_int(&temporary, iteration % 17),
            atom_var_with_id(&temporary, "temporary", 5000u + iteration),
        };
        compare(space, &temporary, parts, 3);
        arena_free(&temporary);
    }
    AtomExprBorrowedView invalid = {0};
    assert(!atom_expr_borrowed_view_init(NULL, open, 3));
    assert(!atom_expr_borrowed_view_init(&invalid, NULL, 3));
    Atom *null_part[] = {head, NULL}; assert(!atom_expr_borrowed_view_init(&invalid, null_part, 2));
    CettaIndex *unchanged = (CettaIndex *)&invalid, count = 123;
    assert(!space_match_candidates_expression_coordinates64(NULL, open, 3, &unchanged, &count));
    assert(unchanged == (CettaIndex *)&invalid && count == 123);
    space_free(space);term_universe_free(&universe);arena_free(&arena);symbol_table_free(&symbols);
    puts("PASS: borrowed candidate coordinates preserve ordered occurrences against a full scan, repeated variables, changed coordinates, deep/nested patterns and allocation marks");
}
