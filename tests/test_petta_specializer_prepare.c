#include <stdio.h>
#include <stdlib.h>

#include "atom.h"
#include "space.h"
#include "symbol.h"
#include "petta_specializer.h"

/* Direct boundary test for the specializer's surviving API.  The prepared
 * evaluation lanes now absorb the whole-program shapes the old stats-based
 * gate used, so the filtered/bounded classifications are asserted here at
 * petta_specializer_prepare_call itself; the fixture halves of the gate keep
 * covering end-to-end answer stability and the higher-order route cache. */

static unsigned checks = 0u;
static unsigned failures = 0u;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", (label));                            \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static Atom *wide_first_order_value(Arena *a) {
    Atom *elems[67];
    elems[0] = atom_symbol(a, "d");
    for (unsigned i = 1u; i < 67u; i++)
        elems[i] = atom_symbol(a, "a");
    return atom_expr(a, elems, 67u);
}

int main(void) {
    SymbolTable symbols;
    Arena persistent;
    Arena result;
    Space space;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&persistent);
    arena_init(&result);
    space_init(&space);

    /* The same relations the fixture uses, so the classifications tested
     * here are the ones the end-to-end gate historically measured. */
    Atom *var_x = atom_var(&persistent, "x");
    space_add(&space, atom_expr3(
        &persistent, atom_symbol_id(&persistent, g_builtin_syms.equals),
        atom_expr2(&persistent, atom_symbol(&persistent, "relevance-plain"),
                   var_x),
        atom_expr3(&persistent, atom_symbol(&persistent, "+"), var_x,
                   atom_int(&persistent, 1))));
    space_add(&space, atom_expr3(
        &persistent, atom_symbol_id(&persistent, g_builtin_syms.equals),
        atom_expr2(&persistent, atom_symbol(&persistent, "relevance-deep"),
                   atom_var(&persistent, "y")),
        atom_symbol(&persistent, "deep-ok")));

    Atom *plain_call = atom_expr2(
        &result, atom_symbol(&result, "relevance-plain"),
        atom_int(&result, 41));
    Atom *deep_call = atom_expr2(
        &result, atom_symbol(&result, "relevance-deep"),
        wide_first_order_value(&result));

    Atom *out = NULL;
    PettaSpecializeResult plain = petta_specializer_prepare_call(
        &space, NULL, &persistent, &result, plain_call, &out);
    CHECK(plain == PETTA_SPECIALIZE_UNCHANGED_FILTERED,
          "first-order call with no possible higher-order value is filtered");
    CHECK(out == plain_call, "filtered call stays authoritative");

    out = NULL;
    PettaSpecializeResult deep = petta_specializer_prepare_call(
        &space, NULL, &persistent, &result, deep_call, &out);
    CHECK(deep == PETTA_SPECIALIZE_UNCHANGED_RELATION_FILTERED,
          "relation with no higher-order variable route is filtered once");
    CHECK(out == deep_call, "relation-filtered call stays authoritative");

    out = NULL;
    PettaSpecializeResult again = petta_specializer_prepare_call(
        &space, NULL, &persistent, &result, deep_call, &out);
    CHECK(again == PETTA_SPECIALIZE_UNCHANGED_RELATION_FILTERED,
          "repeated relation reuses its revision-pinned negative proof");

    /* A later equation creates a genuine higher-order route.  Revision
     * invalidation must prevent the prior negative proof from suppressing
     * specialization. */
    Atom *var_f = atom_var(&persistent, "f");
    Atom *higher_equation = atom_expr3(
        &persistent,
        atom_symbol_id(&persistent, g_builtin_syms.equals),
        atom_expr2(&persistent,
                   atom_symbol(&persistent, "relevance-deep"), var_f),
        atom_expr2(&persistent, var_f, atom_int(&persistent, 0)));
    space_add(&space, higher_equation);
    petta_specializer_note_mutation(&space, higher_equation);
    Atom *higher_call = atom_expr2(
        &result, atom_symbol(&result, "relevance-deep"),
        atom_symbol(&result, "+"));
    out = NULL;
    PettaSpecializeResult higher = petta_specializer_prepare_call(
        &space, NULL, &persistent, &result, higher_call, &out);
    CHECK(higher == PETTA_SPECIALIZE_REWRITTEN,
          "new higher-order route invalidates the negative relation proof");
    CHECK(out && out != higher_call,
          "higher-order call receives its specialized relation head");

    if (failures == 0u)
        printf("PASS: specializer prepare boundary (%u checks)\n", checks);
    else
        printf("FAIL: specializer prepare boundary (%u/%u failed)\n",
               failures, checks);

    petta_specializer_reset_thread();
    space_free(&space);
    arena_free(&result);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return failures == 0u ? 0 : 1;
}
