#include <assert.h>
#include <stdio.h>

#include "atom.h"
#include "symbol.h"

/* Interning identifies a variable by its id, its spelling and its
 * structural name.  Two variables that share an id and nothing else stay
 * two atoms, each keeping its own name, and so do the expressions over
 * them; a variable interned twice with the same identity is one atom. */

int main(void) {
    SymbolTable symbols;
    HashConsTable table;
    Arena arena;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    hashcons_init(&table);
    arena_init(&arena);
    arena_set_hashcons(&arena, &table);

    SymbolId x = symbol_intern_cstr(&symbols, "x");
    SymbolId y = symbol_intern_cstr(&symbols, "y");

    /* The same identity interns to one atom. */
    Atom *first = atom_var_with_spelling(&arena, x, 0u);
    assert(first != NULL && first->kind == ATOM_VAR);
    VarId id = first->var_id;
    Atom *again = atom_var_with_spelling(&arena, x, id);
    assert(again == first);

    /* The same id with another spelling is another atom, and each keeps
     * its own spelling. */
    Atom *other = atom_var_with_spelling(&arena, y, id);
    assert(other != NULL && other != first);
    assert(first->sym_id == x);
    assert(other->sym_id == y);
    assert(atom_var_with_spelling(&arena, y, id) == other);

    /* The same id and spelling with another structural name is another
     * atom too. */
    Atom *key_one = atom_symbol(&arena, "k1");
    Atom *key_two = atom_symbol(&arena, "k2");
    Atom *named_one = atom_var_with_presentation(&arena, x, key_one, id);
    Atom *named_two = atom_var_with_presentation(&arena, x, key_two, id);
    assert(named_one != NULL && named_two != NULL);
    assert(named_one != named_two);
    assert(named_one != first && named_two != first);
    assert(atom_var_with_presentation(&arena, x, key_one, id) == named_one);

    /* Expressions over them follow their children. */
    Atom *head = atom_symbol(&arena, "f");
    Atom *with_x[] = { head, first };
    Atom *with_y[] = { head, other };
    Atom *fx = atom_expr(&arena, with_x, 2u);
    Atom *fy = atom_expr(&arena, with_y, 2u);
    assert(fx != NULL && fy != NULL && fx != fy);
    assert(fx->expr.elems[1]->sym_id == x);
    assert(fy->expr.elems[1]->sym_id == y);
    assert(atom_expr(&arena, with_x, 2u) == fx);

    /* Symbols intern as before. */
    assert(atom_symbol(&arena, "f") == head);

    arena_free(&arena);
    hashcons_free(&table);
    symbol_table_free(&symbols);
    printf("test_atom_intern_vars: ok\n");
    return 0;
}
