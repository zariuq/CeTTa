#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "atom.h"
#include "symbol.h"

/* Interning identifies a variable by its id, its spelling and its
 * structural name.  Two variables that share an id and nothing else stay
 * two atoms, each keeping its own name, and so do the expressions over
 * them; a variable interned twice with the same identity is one atom. */

/* A full single-bucket table forces equality to decide the collision. The
 * two owning tables outlive this borrowed probe; it never inserts or frees. */
static void collision_identity(Atom *stored, Atom *candidate, bool equal) {
    Atom *bucket = stored;
    HashConsTable probe = {.table = &bucket, .size = 1u, .used = 1u};
    assert(hashcons_get(&probe, candidate) == (equal ? stored : candidate));
    assert(probe.lookup_count == 1u && probe.lookup_probes == 1u);
    assert(bucket == stored && probe.used == 1u);
}

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
    assert(atom_symbol_current_id(first) == x);
    assert(atom_symbol_current_id(other) == y);
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
    assert(atom_symbol_current_id(fx->expr.elems[1]) == x);
    assert(atom_symbol_current_id(fy->expr.elems[1]) == y);
    assert(atom_expr(&arena, with_x, 2u) == fx);

    /* Symbols intern as before. */
    assert(atom_symbol(&arena, "f") == head);

    /* Float constructors are arena-owned. Explicit interning retains their
     * exact representation and value kind, including inside expressions. */
    Atom *positive_zero = hashcons_get(&table, atom_float(&arena, 0.0));
    Atom *negative_zero = hashcons_get(&table, atom_float(&arena, -0.0));
    assert(positive_zero != negative_zero);
    assert(hashcons_get(&table, atom_float(&arena, 0.0)) == positive_zero);
    assert(hashcons_get(&table, atom_float(&arena, -0.0)) == negative_zero);
    assert(atom_expr2(&arena, head, positive_zero) !=
           atom_expr2(&arena, head, negative_zero));
    assert(atom_expr2(&arena, head, hashcons_get(&table, atom_int(&arena, 1))) !=
           atom_expr2(&arena, head, hashcons_get(&table, atom_float(&arena, 1.0))));
    uint64_t nan_bits_one = UINT64_C(0x7ff8000000000001);
    uint64_t nan_bits_two = UINT64_C(0x7ff8000000000002);
    double nan_one, nan_two;
    _Static_assert(sizeof(nan_one) == sizeof(nan_bits_one),
                   "the atom float representation is binary64");
    memcpy(&nan_one, &nan_bits_one, sizeof(nan_one));
    memcpy(&nan_two, &nan_bits_two, sizeof(nan_two));
    Atom *nan_value_one = hashcons_get(&table, atom_float(&arena, nan_one));
    Atom *nan_value_two = hashcons_get(&table, atom_float(&arena, nan_two));
    assert(nan_value_one != nan_value_two);
    assert(hashcons_get(&table, atom_float(&arena, nan_one)) == nan_value_one);
    assert(atom_expr2(&arena, head, nan_value_one) !=
           atom_expr2(&arena, head, nan_value_two));
    assert(atom_expr2(&arena, head, nan_value_one) ==
           atom_expr2(&arena, head, nan_value_one));

    collision_identity(first, other, false);
    collision_identity(named_one, named_two, false);
    collision_identity(fx, fy, false);
    collision_identity(positive_zero, negative_zero, false);
    collision_identity(nan_value_one, nan_value_two, false);
    collision_identity(hashcons_get(&table, atom_int(&arena, 1)),
                       hashcons_get(&table, atom_float(&arena, 1.0)), false);

    /* Independent owners have different child pointers. Equal flat terms
     * still coalesce; deep terms must reach the owned comparison machine. */
    HashConsTable other_table;
    Arena other_arena;
    hashcons_init_compact(&other_table);
    arena_init(&other_arena);
    arena_set_hashcons(&other_arena, &other_table);
    Atom *other_head = atom_symbol(&other_arena, "f");
    Atom *other_x = atom_var_with_spelling(&other_arena, x, id);
    collision_identity(head, other_head, true);
    collision_identity(fx, atom_expr2(&other_arena, other_head, other_x), true);
    Atom *left = first, *right = other_x;
    Atom *different = atom_var_with_spelling(&other_arena, y, id);
    for (unsigned depth = 0u; depth < 32768u; depth++) {
        left = atom_expr2(&arena, head, left);
        right = atom_expr2(&other_arena, other_head, right);
        different = atom_expr2(&other_arena, other_head, different);
    }
    collision_identity(left, right, true);
    collision_identity(left, different, false);
    arena_free(&other_arena);
    hashcons_free(&other_table);

    arena_free(&arena);
    hashcons_free(&table);
    symbol_table_free(&symbols);
    printf("test_atom_intern_vars: ok\n");
    return 0;
}
