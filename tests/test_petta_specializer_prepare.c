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

    CHECK(petta_specializer_relation_execution_admission(
              &space, plain_call->expr.elems[0]->sym_id) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "relation-wide admission preserves a possible higher-order route");
    CHECK(petta_specializer_query_execution_admission(
              &space, plain_call->expr.elems[0]->sym_id,
              plain_call->expr.elems + 1u,
              plain_call->expr.len - 1u) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "query-shaped admission rejects an inert argument forest");
    CHECK(petta_specializer_query_execution_admission(
              &space, deep_call->expr.elems[0]->sym_id,
              deep_call->expr.elems + 1u,
              deep_call->expr.len - 1u) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "relation proof admits a query beyond the local node budget");
    CHECK(petta_specializer_query_execution_admission(
              &space, plain_call->expr.elems[0]->sym_id,
              NULL, 1u) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "missing query arguments fail closed");

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

    /* Reaching the per-call inspection budget is an optimization decline,
     * even when the relation could specialize a different, smaller call. */
    Atom *var_bounded = atom_var(&persistent, "bounded");
    space_add(&space, atom_expr3(
        &persistent, atom_symbol_id(&persistent, g_builtin_syms.equals),
        atom_expr2(&persistent, atom_symbol(&persistent, "relevance-bounded"),
                   var_bounded),
        atom_expr2(&persistent, var_bounded, atom_int(&persistent, 0))));
    Atom *bounded_call = atom_expr2(
        &result, atom_symbol(&result, "relevance-bounded"),
        wide_first_order_value(&result));
    out = NULL;
    PettaSpecializeResult bounded = petta_specializer_prepare_call(
        &space, NULL, &persistent, &result, bounded_call, &out);
    CHECK(bounded == PETTA_SPECIALIZE_UNCHANGED_RELEVANCE_BOUNDED,
          "deep uncertain call declines specialization at its node budget");
    CHECK(out == bounded_call, "budgeted call stays authoritative");

    Atom *bounded_higher_call = atom_expr2(
        &result, atom_symbol(&result, "relevance-bounded"),
        atom_symbol(&result, "+"));
    CHECK(petta_specializer_query_execution_admission(
              &space, bounded_higher_call->expr.elems[0]->sym_id,
              bounded_higher_call->expr.elems + 1u,
              bounded_higher_call->expr.len - 1u) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "callable argument preserves a productive specialization route");
    out = NULL;
    PettaSpecializeResult bounded_higher = petta_specializer_prepare_call(
        &space, NULL, &persistent, &result, bounded_higher_call, &out);
    CHECK(bounded_higher == PETTA_SPECIALIZE_REWRITTEN,
          "later small higher-order call remains eligible for specialization");

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

    /* Negative callable classifications are valid only for the exact
     * admitted space state.  Making the same symbol callable later must
     * invalidate the cached negative answer. */
    Atom *var_cache = atom_var(&persistent, "cache-f");
    space_add(&space, atom_expr3(
        &persistent, atom_symbol_id(&persistent, g_builtin_syms.equals),
        atom_expr2(&persistent, atom_symbol(&persistent, "callable-cache"),
                   var_cache),
        atom_expr2(&persistent, var_cache, atom_int(&persistent, 0))));
    Atom *late_symbol = atom_symbol(&result, "late-callable");
    Atom *late_call = atom_expr2(
        &result, atom_symbol(&result, "callable-cache"), late_symbol);
    CHECK(petta_specializer_query_execution_admission(
              &space, late_call->expr.elems[0]->sym_id,
              late_call->expr.elems + 1u,
              late_call->expr.len - 1u) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "unknown argument symbol is inert at the admitted revision");
    out = NULL;
    PettaSpecializeResult late_before = petta_specializer_prepare_call(
        &space, NULL, &persistent, &result, late_call, &out);
    CHECK(late_before == PETTA_SPECIALIZE_UNCHANGED_FILTERED,
          "unknown symbol receives a cached negative callable result");

    Atom *var_late = atom_var(&persistent, "late-x");
    Atom *late_equation = atom_expr3(
        &persistent,
        atom_symbol_id(&persistent, g_builtin_syms.equals),
        atom_expr2(&persistent, atom_symbol(&persistent, "late-callable"),
                   var_late),
        var_late);
    space_add(&space, late_equation);
    petta_specializer_note_mutation(&space, late_equation);
    CHECK(petta_specializer_query_execution_admission(
              &space, late_call->expr.elems[0]->sym_id,
              late_call->expr.elems + 1u,
              late_call->expr.len - 1u) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "space mutation invalidates query-shaped negative admission");
    out = NULL;
    PettaSpecializeResult late_after = petta_specializer_prepare_call(
        &space, NULL, &persistent, &result, late_call, &out);
    CHECK(late_after == PETTA_SPECIALIZE_REWRITTEN,
          "space mutation invalidates a cached negative callable result");
    CHECK(out && out != late_call,
          "new callable symbol receives its specialized relation head");

    /* Exact named-arity judgments use the same admitted-space generation.
     * An expression that is not known to be under-applied can become a
     * specialization value after a larger-arity equation is admitted. */
    Atom *var_arity = atom_var(&persistent, "arity-f");
    space_add(&space, atom_expr3(
        &persistent, atom_symbol_id(&persistent, g_builtin_syms.equals),
        atom_expr2(&persistent, atom_symbol(&persistent, "arity-cache"),
                   var_arity),
        atom_expr2(&persistent, var_arity, atom_int(&persistent, 0))));
    Atom *nested_late = atom_expr2(
        &result, atom_symbol(&result, "late-arity"),
        atom_symbol(&result, "a"));
    Atom *arity_call = atom_expr2(
        &result, atom_symbol(&result, "arity-cache"), nested_late);
    out = NULL;
    PettaSpecializeResult arity_before = petta_specializer_prepare_call(
        &space, NULL, &persistent, &result, arity_call, &out);
    CHECK(arity_before == PETTA_SPECIALIZE_UNCHANGED_FILTERED,
          "unknown nested arity receives a cached negative judgment");

    Atom *var_arity_x = atom_var(&persistent, "arity-x");
    Atom *var_arity_y = atom_var(&persistent, "arity-y");
    Atom *arity_equation = atom_expr3(
        &persistent,
        atom_symbol_id(&persistent, g_builtin_syms.equals),
        atom_expr3(&persistent, atom_symbol(&persistent, "late-arity"),
                   var_arity_x, var_arity_y),
        var_arity_x);
    space_add(&space, arity_equation);
    petta_specializer_note_mutation(&space, arity_equation);
    out = NULL;
    PettaSpecializeResult arity_after = petta_specializer_prepare_call(
        &space, NULL, &persistent, &result, arity_call, &out);
    CHECK(arity_after == PETTA_SPECIALIZE_REWRITTEN,
          "space mutation invalidates a cached negative arity judgment");
    CHECK(out && out != arity_call,
          "new under-application receives its specialized relation head");

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
