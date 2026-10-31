#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "space_skeleton.h"
#include "stats.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"

/* Skeletons: a fact's spine with holes.  Cutting a fact and building it
 * back are inverse; the common shape of facts generalizes where they
 * differ; a fact's holes, cut from its atom, are the parts of its alpha
 * key; a fact is looked up from its holes; a pattern aligns with a
 * skeleton, fixing holes by ids where ids decide. */

static Arena g_arena;

static Atom *sym(const char *name) { return atom_symbol(&g_arena, name); }
static Atom *var(const char *name) { return atom_var(&g_arena, name); }
static Atom *num(int64_t value) { return atom_int(&g_arena, value); }
static Atom *ex(int n, ...) {
    Atom *elems[16];
    va_list args;
    va_start(args, n);
    for (int i = 0; i < n; i++)
        elems[i] = va_arg(args, Atom *);
    va_end(args);
    return atom_expr(&g_arena, elems, (CettaExprLen)n);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable var_intern;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&var_intern);
    g_var_intern = &var_intern;
    arena_init(&g_arena);
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);

    /* (sol 5 1 (C (/ (> p s) (> q r)) (1 (M I)))) and
     * (sol 4 2 (C (/ (> a b) c) (M (2 I)))) */
    Atom *f1 = ex(4, sym("sol"), num(5), num(1),
                  ex(3, sym("C"),
                     ex(3, sym("/"), ex(3, sym(">"), sym("p"), sym("s")),
                        ex(3, sym(">"), sym("q"), sym("r"))),
                     ex(2, num(1), ex(2, sym("M"), sym("I")))));
    Atom *f2 = ex(4, sym("sol"), num(4), num(2),
                  ex(3, sym("C"),
                     ex(3, sym("/"), ex(3, sym(">"), sym("a"), sym("b")),
                        sym("c")),
                     ex(2, sym("M"), ex(2, num(2), sym("I")))));
    AtomId id1 = term_universe_store_atom_id(&universe, NULL, f1);
    AtomId id2 = term_universe_store_atom_id(&universe, NULL, f2);
    assert(id1 != CETTA_ATOM_ID_NONE && id2 != CETTA_ATOM_ID_NONE);

    /* One fact's shape: every expression headed by a symbol is spine. */
    CettaSkeleton sk;
    assert(cetta_skeleton_of(&universe, id1, &sk));
    assert(cetta_skeleton_len(&sk) == 4u);
    AtomId holes[CETTA_SKELETON_HOLES * 2];
    assert(sk.holes <= CETTA_SKELETON_HOLES * 2);

    /* The common shape: (sol _ _ (C (/ (> _ _) _) _)), six holes. */
    assert(cetta_skeleton_generalize(&sk, &universe, id2));
    assert(sk.holes == 6u);
    assert(sk.count == 10u);
    /* A fact the shape fits leaves it as it is. */
    CettaSkeleton same = sk;
    assert(cetta_skeleton_generalize(&same, &universe, id1));
    assert(same.count == sk.count && same.holes == sk.holes &&
           memcmp(same.nodes, sk.nodes, sk.count * sizeof(*sk.nodes)) == 0);
    /* Both fit it; cut and build are inverse. */
    assert(cetta_skeleton_cut(&universe, &sk, id1, holes));
    assert(holes[0] == term_universe_lookup_atom_id(&universe, num(5)));
    assert(holes[5] == term_universe_lookup_atom_id(
                           &universe, ex(2, num(1), ex(2, sym("M"), sym("I")))));
    assert(cetta_skeleton_build(&universe, &sk, holes) == id1);
    assert(cetta_skeleton_cut(&universe, &sk, id2, holes));
    assert(cetta_skeleton_build(&universe, &sk, holes) == id2);
    /* Looked up from its holes, a fact is its record; holes of a fact the
     * universe does not hold are none, and nothing is stored. */
    assert(cetta_skeleton_lookup(&universe, &sk, holes) == id2);
    AtomId swapped[CETTA_SKELETON_HOLES * 2];
    memcpy(swapped, holes, sk.holes * sizeof(*holes));
    swapped[0] = term_universe_lookup_atom_id(&universe, num(5));
    assert(cetta_skeleton_lookup(&universe, &sk, swapped) == CETTA_ATOM_ID_NONE);
    Atom *never = ex(4, sym("sol"), num(5), num(2),
                     ex(3, sym("C"),
                        ex(3, sym("/"), ex(3, sym(">"), sym("a"), sym("b")),
                           sym("c")),
                        ex(2, sym("M"), ex(2, num(2), sym("I")))));
    assert(term_universe_lookup_atom_id(&universe, never) == CETTA_ATOM_ID_NONE);
    /* A fact of another spine does not fit. */
    Atom *f3 = ex(4, sym("sol"), num(4), num(2), ex(2, sym("D"), sym("x")));
    AtomId id3 = term_universe_store_atom_id(&universe, NULL, f3);
    assert(!cetta_skeleton_cut(&universe, &sk, id3, holes));
    /* Nor does one of another length. */
    CettaSkeleton other = sk;
    Atom *f4 = ex(3, sym("sol"), num(1), num(2));
    AtomId id4 = term_universe_store_atom_id(&universe, NULL, f4);
    assert(!cetta_skeleton_generalize(&other, &universe, id4));

    /* A fact holding variables, cut from its atom: its holes are the parts
     * of its alpha key, the variables numbered across the whole fact. */
    Atom *o1 = ex(4, sym("sol"), num(3), num(1),
                  ex(3, sym("C"),
                     ex(3, sym("/"), ex(3, sym(">"), var("x"), sym("s")),
                        ex(3, sym(">"), var("y"), var("x"))),
                     ex(2, sym("M"), sym("I"))));
    bool missing = false;
    assert(cetta_skeleton_cut_atom(&universe, &sk, o1, true, holes, &missing));
    assert(!missing);
    AtomId key = term_universe_store_alpha_key(&universe, o1);
    assert(key != CETTA_ATOM_ID_NONE);
    assert(cetta_skeleton_build(&universe, &sk, holes) == key);
    /* A variant of it is the same holes, looked up. */
    Atom *o2 = ex(4, sym("sol"), num(3), num(1),
                  ex(3, sym("C"),
                     ex(3, sym("/"), ex(3, sym(">"), var("u"), sym("s")),
                        ex(3, sym(">"), var("w"), var("u"))),
                     ex(2, sym("M"), sym("I"))));
    AtomId again[CETTA_SKELETON_HOLES * 2];
    assert(cetta_skeleton_cut_atom(&universe, &sk, o2, false, again, &missing));
    assert(!missing);
    assert(memcmp(again, holes, sk.holes * sizeof(*holes)) == 0);
    /* A part no term holds is missing. */
    Atom *o3 = ex(4, sym("sol"), num(3), num(77),
                  ex(3, sym("C"),
                     ex(3, sym("/"), ex(3, sym(">"), var("u"), sym("s")),
                        sym("never-seen")),
                     ex(2, sym("M"), sym("I"))));
    assert(cetta_skeleton_cut_atom(&universe, &sk, o3, false, again, &missing));
    assert(missing);

    /* A shape past the room keeps every element of each expression it
     * shapes, as holes where the room runs out, and still cuts and builds
     * the fact back. */
    Atom *branch[4];
    for (int b = 0; b < 4; b++)
        branch[b] = ex(3, sym("a"),
                       ex(3, sym("b"),
                          ex(3, sym("c"), ex(3, sym("d"), num(b), sym("y")),
                             sym("z")),
                          sym("w")),
                       sym("v"));
    Atom *wide = ex(5, sym("big"), branch[0], branch[1], branch[2], branch[3]);
    AtomId wide_id = term_universe_store_atom_id(&universe, NULL, wide);
    CettaSkeleton roomy;
    assert(cetta_skeleton_of(&universe, wide_id, &roomy));
    assert(roomy.count <= CETTA_SKELETON_NODES);
    uint32_t open = 0u;
    for (uint32_t i = 0u; i < roomy.count; i++)
        open = open + (roomy.nodes[i].len ? roomy.nodes[i].len - 1u : 0u) -
               (i != 0u);
    assert(open == 0u);
    AtomId wide_holes[CETTA_SKELETON_NODES];
    assert(cetta_skeleton_cut(&universe, &roomy, wide_id, wide_holes));
    assert(cetta_skeleton_build(&universe, &roomy, wide_holes) == wide_id);
    assert(cetta_skeleton_generalize(&roomy, &universe, wide_id));

    /* Holes fitted to four: the deepest expressions become holes. */
    CettaSkeleton four = sk;
    assert(cetta_skeleton_fit_holes(&four, 4u));
    assert(four.holes == 4u);
    assert(cetta_skeleton_cut(&universe, &four, id1, holes));
    assert(cetta_skeleton_build(&universe, &four, holes) == id1);

    /* Alignment. */
    CettaDenseId fixed[CETTA_SKELETON_HOLES * 2];
    bool exact = false;
    assert(cetta_skeleton_align(&universe, &sk,
                                ex(4, sym("sol"), var("a"), var("b"), var("c")),
                                0u, fixed, &exact));
    assert(exact);
    for (uint32_t h = 0u; h < sk.holes; h++)
        assert(fixed[h] == CETTA_DENSE_ANY);
    assert(cetta_skeleton_align(
        &universe, &sk,
        ex(4, sym("sol"), num(5), var("h"),
           ex(3, sym("C"), ex(3, sym("/"), var("x"), var("y")), var("f"))),
        0u, fixed, &exact));
    assert(exact);
    assert(fixed[0] == term_universe_lookup_atom_id(&universe, num(5)));
    assert(fixed[1] == CETTA_DENSE_ANY && fixed[5] == CETTA_DENSE_ANY);
    /* A hole some fact holds a variable in is left to unification. */
    assert(cetta_skeleton_align(
        &universe, &sk,
        ex(4, sym("sol"), num(5), var("h"), var("c")), 1u, fixed, &exact));
    assert(!exact && fixed[0] == CETTA_DENSE_ANY);
    /* The same variable twice: ids alone do not decide. */
    assert(cetta_skeleton_align(&universe, &sk,
                                ex(4, sym("sol"), var("k"), var("k"), var("c")),
                                0u, fixed, &exact));
    assert(!exact);
    /* A variable heading an expression the spine heads by a symbol meets
     * the symbol: every fact, and exact while the variable occurs once. */
    assert(cetta_skeleton_align(
        &universe, &sk,
        ex(4, sym("sol"), var("k"), var("h"),
           ex(3, var("c"), ex(3, var("s"), var("x"), var("y")), var("f"))),
        0u, fixed, &exact));
    assert(exact);
    for (uint32_t h = 0u; h < sk.holes; h++)
        assert(fixed[h] == CETTA_DENSE_ANY);
    assert(cetta_skeleton_align(
        &universe, &sk,
        ex(4, sym("sol"), var("k"), var("h"),
           ex(3, var("c"), ex(3, var("c"), var("x"), var("y")), var("f"))),
        0u, fixed, &exact));
    assert(!exact);
    /* An expression there meets no symbol. */
    assert(!cetta_skeleton_align(
        &universe, &sk,
        ex(4, sym("sol"), var("k"), var("h"),
           ex(3, ex(1, sym("C")), var("p"), var("f"))),
        0u, fixed, &exact));
    /* Another spine or another head: no fact matches. */
    assert(!cetta_skeleton_align(
        &universe, &sk,
        ex(4, sym("sol"), num(5), num(1), ex(2, sym("D"), var("x"))), 0u,
        fixed, &exact));
    assert(!cetta_skeleton_align(&universe, &sk,
                                 ex(4, sym("goal"), var("a"), var("b"),
                                    var("c")),
                                 0u, fixed, &exact));
    /* An atom the universe never made fixes no fact. */
    assert(!cetta_skeleton_align(&universe, &sk,
                                 ex(4, sym("sol"), num(123456), var("b"),
                                    var("c")),
                                 0u, fixed, &exact));
    /* By elements, as a count projects them. */
    Atom *elements[4] = {sym("sol"), var("a"), num(1), var("c")};
    assert(cetta_skeleton_align_elements(&universe, &sk, elements, 4u, 0u,
                                         fixed, &exact));
    assert(exact && fixed[1] == term_universe_lookup_atom_id(&universe, num(1)));

    term_universe_free(&universe);
    arena_free(&persistent);
    arena_free(&g_arena);
    var_intern_free(&var_intern);
    symbol_table_free(&symbols);
    printf("PASS: skeletons cut, build and look up facts, generalize, fit "
           "their holes, key open facts by parts and align patterns\n");
    return 0;
}
