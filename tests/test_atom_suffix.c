#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "atom.h"
#include "symbol.h"

/* The summary a suffix carries is the one its own children fold to. */
static void assert_same_summary(const Atom *suffix, const Atom *folded) {
    assert(atom_eq((Atom *)suffix, (Atom *)folded));
    assert((suffix->flags & ~ATOM_FLAG_HASH_VALID) ==
           (folded->flags & ~ATOM_FLAG_HASH_VALID));
    assert(suffix->structural_facts == folded->structural_facts);
    assert(suffix->var_id == folded->var_id);
}

/* Every suffix of `expression`, and every suffix of those, taken in `arena`:
 * each carries the summary of a copy of its children, and shares the
 * expression's storage exactly when `shares` says the storage outlives it. */
static void check_suffixes(Arena *arena, Atom *expression, bool shares) {
    CettaExprLen length = expression->expr.len;
    for (CettaExprLen offset = 0u; offset <= length; offset++) {
        Atom *suffix = atom_expr_suffix(arena, expression, offset);
        Atom *folded = atom_expr(
            arena, offset < length ? expression->expr.elems + offset : NULL,
            length - offset);
        assert(suffix != NULL && folded != NULL);
        assert_same_summary(suffix, folded);
        if (offset < length) {
            assert((suffix->expr.elems == expression->expr.elems + offset) ==
                   shares);
            Atom *next = atom_expr_suffix(arena, suffix, 1u);
            Atom *next_folded = atom_expr(
                arena, suffix->expr.len > 1u ? suffix->expr.elems + 1u : NULL,
                suffix->expr.len - 1u);
            assert(next != NULL && next_folded != NULL);
            assert_same_summary(next, next_folded);
        }
    }
}

static Atom *list(Arena *arena, Atom **items, CettaExprLen length) {
    return atom_expr(arena, items, length);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    Arena other;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    arena_init(&other);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
    arena_set_runtime_kind(&other, CETTA_ARENA_RUNTIME_KIND_EVAL);

    Atom *a = atom_symbol(&arena, "a");
    Atom *b = atom_symbol(&arena, "b");
    Atom *f = atom_symbol(&arena, "f");
    Atom *x = atom_var_with_id(&arena, "x", fresh_var_id());
    Atom *y = atom_var_with_id(&arena, "y", fresh_var_id());
    Atom *resolve = atom_symbol(&arena, "resolve-name");
    Atom *handle = atom_symbol_id(&arena, g_builtin_syms.native_handle);

    /* Ground data: every departed child is neutral, so each suffix inherits
     * the summary and shares the storage. */
    Atom *fa = atom_expr2(&arena, f, a);
    Atom *ground[] = {
        atom_int(&arena, 1), a, fa, atom_string(&arena, "s"), b,
        atom_float(&arena, 2.5),
    };
    check_suffixes(&arena, list(&arena, ground, 6u), true);

    /* One variable throughout, then two: a departed child with variables
     * leaves the suffix's variables to its own fold. */
    Atom *fx = atom_expr2(&arena, f, x);
    Atom *one_variable[] = { x, fx, a, x };
    check_suffixes(&arena, list(&arena, one_variable, 4u), true);
    Atom *two_variables[] = { x, a, y, fx, b };
    check_suffixes(&arena, list(&arena, two_variables, 5u), true);
    Atom *late_variable[] = { a, b, fa, y };
    check_suffixes(&arena, list(&arena, late_variable, 4u), true);

    /* Adjusting heads: a departed one, and one a suffix begins with. */
    Atom *departed_resolve[] = { resolve, a, b };
    check_suffixes(&arena, list(&arena, departed_resolve, 3u), true);
    Atom *arriving_resolve[] = { a, resolve, b };
    check_suffixes(&arena, list(&arena, arriving_resolve, 3u), true);
    Atom *departed_handle[] = { handle, atom_int(&arena, 7), a };
    check_suffixes(&arena, list(&arena, departed_handle, 3u), true);
    Atom *arriving_handle[] = { a, handle, atom_int(&arena, 7) };
    check_suffixes(&arena, list(&arena, arriving_handle, 3u), true);

    /* Children allocated in another arena are not closed for this one. */
    Atom *foreign = atom_expr2(&other, atom_symbol(&other, "g"),
                               atom_symbol(&other, "c"));
    Atom *mixed[] = { a, foreign, b, foreign };
    check_suffixes(&arena, list(&arena, mixed, 4u), true);

    /* An expression in another arena could be released first: its suffix
     * copies the children. */
    Atom *elsewhere_items[] = {
        atom_symbol(&other, "p"), atom_symbol(&other, "q"),
        atom_symbol(&other, "r"),
    };
    check_suffixes(&arena, list(&other, elsewhere_items, 3u), false);

    /* A deep copy of a suffix is an ordinary expression equal to it. */
    Atom *source = list(&arena, ground, 6u);
    Atom *suffix = atom_expr_suffix(&arena, source, 2u);
    Atom *copied = atom_deep_copy(&other, suffix);
    assert(copied != NULL && atom_eq(copied, suffix));
    assert(copied->expr.elems != suffix->expr.elems);

    /* A suffix of a long list is taken in constant work per step: walking it
     * by repeated suffixes never copies the children. */
    enum { LONG_LENGTH = 50000 };
    Atom **items = arena_alloc(&arena, sizeof(Atom *) * LONG_LENGTH);
    for (uint32_t index = 0u; index < LONG_LENGTH; index++)
        items[index] = atom_int(&arena, (int64_t)index);
    Atom *walk = list(&arena, items, LONG_LENGTH);
    Atom **storage = walk->expr.elems;
    for (uint32_t step = 1u; step < LONG_LENGTH; step++) {
        walk = atom_expr_suffix(&arena, walk, 1u);
        assert(walk->expr.elems == storage + step);
        assert(walk->expr.elems[0] == items[step]);
    }
    assert(walk->expr.len == 1u);

    arena_free(&other);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: expression suffixes share storage and carry the folded summary");
    return 0;
}
