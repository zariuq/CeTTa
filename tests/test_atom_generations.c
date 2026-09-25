#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "atom.h"
#include "symbol.h"

/* A nursery that names a tenured arena as its older generation.  A nursery
 * expression over tenured structure is closed across the two generations
 * without being closed in one arena, a copy into the nursery shares that
 * structure, and structure anywhere else is copied. */

static bool generation_closed(const Atom *atom) {
    return (atom->structural_facts & ATOM_STRUCTURAL_GENERATION_CLOSED) != 0u;
}

/* A view carries exactly the summary its own children fold to. */
static void assert_view_summary(Arena *arena, Atom *view) {
    Atom *folded = atom_expr(arena, view->expr.elems, view->expr.len);
    assert(folded != NULL && atom_eq(view, folded));
    assert((view->flags & ~ATOM_FLAG_HASH_VALID) ==
           (folded->flags & ~ATOM_FLAG_HASH_VALID));
    assert(view->structural_facts == folded->structural_facts);
}

int main(void) {
    SymbolTable symbols;
    Arena nursery;
    Arena tenured;
    Arena scratch;
    Arena elsewhere;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&nursery);
    arena_init(&tenured);
    arena_init(&scratch);
    arena_init(&elsewhere);
    arena_set_hashcons(&nursery, NULL);
    arena_set_hashcons(&tenured, NULL);
    arena_set_hashcons(&scratch, NULL);
    arena_set_hashcons(&elsewhere, NULL);
    arena_set_older_generation(&nursery, &tenured);
    assert(nursery.older_identity == tenured.identity);

    Atom *old_items[] = {
        atom_symbol(&tenured, "queue"), atom_int(&tenured, 1),
        atom_int(&tenured, 2), atom_int(&tenured, 3),
    };
    Atom *old_list = atom_expr(&tenured, old_items, 4u);
    assert(atom_graph_is_closed_for_arena(&tenured, old_list));
    assert(!generation_closed(old_list));

    /* A nursery expression over tenured structure: closed across the
     * generations, while the one-arena bit keeps its meaning. */
    Atom *young_items[] = { atom_int(&nursery, 0), old_list };
    Atom *young = atom_expr(&nursery, young_items, 2u);
    assert(!atom_graph_is_closed_for_arena(&nursery, young));
    assert(generation_closed(young));
    assert(atom_settled_for_arena(&nursery, young));
    assert(atom_settled_for_arena(&nursery, old_list));
    assert(!atom_settled_for_arena(&tenured, young));
    assert(!atom_settled_for_arena(&elsewhere, old_list));

    /* A copy into the nursery shares what is settled there and copies the
     * rest; the source's arena can then be released. */
    Atom *result_items[] = { atom_int(&scratch, 7), old_list, young };
    Atom *result = atom_expr(&scratch, result_items, 3u);
    Atom *copy = atom_deep_copy(&nursery, result);
    assert(copy != NULL && copy->arena_id == nursery.identity);
    assert(copy->expr.elems[0] != result_items[0]);
    assert(copy->expr.elems[1] == old_list);
    assert(copy->expr.elems[2] == young);
    assert(atom_settled_for_arena(&nursery, copy));
    arena_free(&scratch);
    Atom *expected_items[] = { atom_int(&nursery, 7), old_list, young };
    assert(atom_eq(copy, atom_expr(&nursery, expected_items, 3u)));

    /* Without the link, the same structure is copied. */
    Atom *copy_elsewhere = atom_deep_copy(&elsewhere, copy);
    assert(copy_elsewhere != NULL && atom_eq(copy_elsewhere, copy));
    assert(copy_elsewhere->expr.elems[1] != old_list);

    /* A child from an arena the nursery does not reach leaves its parent
     * unsettled, and the copy takes that child. */
    Atom *mixed_items[] = { atom_int(&elsewhere, 3), old_list };
    Atom *mixed = atom_expr(&nursery, mixed_items, 2u);
    assert(!generation_closed(mixed));
    assert(!atom_settled_for_arena(&nursery, mixed));
    Atom *mixed_copy = atom_deep_copy(&nursery, mixed);
    assert(mixed_copy != mixed && mixed_copy->expr.elems[1] == old_list);
    assert(atom_settled_for_arena(&nursery, mixed_copy));

    /* One level: an arena that has an older generation cannot serve as
     * one. */
    Arena third;
    arena_init(&third);
    arena_set_older_generation(&third, &nursery);
    assert(third.older_identity == 0u);
    arena_free(&third);

    /* A view in the nursery over tenured storage shares it and is settled;
     * each view of a view too, with the summary of a fresh fold. */
    Atom *view = old_list;
    for (CettaExprLen step = 1u; step < old_list->expr.len; step++) {
        view = atom_expr_suffix(&nursery, view, 1u);
        assert(view->expr.elems == old_list->expr.elems + step);
        assert(generation_closed(view));
        assert(atom_settled_for_arena(&nursery, view));
        assert_view_summary(&nursery, view);
    }

    /* Views over a nursery list whose elements are closed across the
     * generations: those elements depart as neutral children. */
    Atom *rows[] = { young, young, atom_int(&nursery, 5), young };
    Atom *table = atom_expr(&nursery, rows, 4u);
    assert(generation_closed(table));
    for (CettaExprLen offset = 1u; offset < 4u; offset++) {
        Atom *suffix = atom_expr_suffix(&nursery, table, offset);
        assert(suffix->expr.elems == table->expr.elems + offset);
        assert_view_summary(&nursery, suffix);
    }

    /* A departed child the nursery does not admit: the view folds the bit
     * afresh, and admits what remains. */
    Atom *departing[] = { atom_int(&elsewhere, 9), old_list, young };
    Atom *head_foreign = atom_expr(&nursery, departing, 3u);
    assert(!generation_closed(head_foreign));
    Atom *rest = atom_expr_suffix(&nursery, head_foreign, 1u);
    assert(generation_closed(rest));
    assert_view_summary(&nursery, rest);

    /* A view over storage the nursery does not reach copies the children. */
    Atom *foreign_items[] = {
        atom_symbol(&elsewhere, "p"), atom_symbol(&elsewhere, "q"),
        atom_symbol(&elsewhere, "r"),
    };
    Atom *foreign = atom_expr(&elsewhere, foreign_items, 3u);
    Atom *foreign_view = atom_expr_suffix(&nursery, foreign, 1u);
    assert(foreign_view->expr.elems != foreign->expr.elems + 1u);
    assert_view_summary(&nursery, foreign_view);

    arena_free(&elsewhere);
    arena_free(&nursery);
    arena_free(&tenured);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: a nursery shares its older generation's closed structure");
    return 0;
}
