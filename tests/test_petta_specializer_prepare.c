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

    /* Query observations follow the live environment, including a variable
     * used as an expression head. An unknown/stale scope cannot certify
     * absence of higher-order suppliers. Rollback restores the inert case. */
    BindingsBuilder observation_bindings;
    CHECK(bindings_builder_init(&observation_bindings, NULL), "view builder");
    Atom *view_variable = atom_var(&result, "view-head");
    Atom *view_term = atom_expr2(&result, view_variable,
                               atom_symbol(&result, "inert-leaf"));
    CettaGsltTermCursorV1 view_argument = {.source = view_term};
    BindingsTermCursorContextV1 observation_context = {
        .bindings = bindings_builder_bindings(&observation_bindings)};
    CettaGsltTermCursorObserverV1 observer = {
        bindings_resolve_term_cursor_v1, &observation_context};
    SymbolId consumer = arity_call->expr.elems[0]->sym_id;
    BindingValue value_argument = binding_value_from_atom(view_term);
    CHECK(petta_specializer_query_value_execution_admission(
              &space, consumer, &observation_bindings.current,
              &value_argument, 1u) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "explicit inert binding value has no specializable supplier");
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &view_argument, 1u, observer) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "unbound nested head has no specializable supplier");
    uint32_t observation_mark = bindings_builder_save(&observation_bindings);
    CHECK(bindings_builder_add_var_fresh(&observation_bindings, view_variable,
              atom_symbol(&result, "late-arity")), "bind observed head");
    Atom *forced = bindings_apply(
        &observation_bindings.current, &result, view_term);
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &view_argument, 1u, observer) ==
              petta_specializer_query_execution_admission(
                  &space, consumer, &forced, 1u),
          "borrowed and forced admission agree on a bound nested head");
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &view_argument, 1u, observer) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "nested callable binding cannot be admitted as inert");
    CHECK(petta_specializer_query_value_execution_admission(
              &space, consumer, &observation_bindings.current,
              &value_argument, 1u) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "explicit binding value observes a nested callable binding");
    bindings_builder_rollback(&observation_bindings, observation_mark);
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &view_argument, 1u, observer) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "rollback changes the observed supplier");
    CHECK(petta_specializer_query_value_execution_admission(
              &space, consumer, &observation_bindings.current,
              &value_argument, 1u) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "explicit binding value observes rollback to an inert supplier");
    view_argument.scope = &observation_bindings;
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &view_argument, 1u, observer) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "unknown scope cannot certify absence of suppliers");
    /* The partial-constructor fact is cached with callability, while its
     * tuple remains a live observation that can change after rollback. */
    Atom *partial_tuple = atom_var(&result, "partial-tuple");
    Atom *partial_term = atom_expr3(
        &result, atom_symbol(&result, "partial"),
        atom_symbol(&result, "inert-base"), partial_tuple);
    CettaGsltTermCursorV1 partial_view = {.source = partial_term};
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &partial_view, 1u, observer) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "partial with an unbound tuple has no supplied application");
    observation_mark = bindings_builder_save(&observation_bindings);
    CHECK(bindings_builder_add_var_fresh(
              &observation_bindings, partial_tuple, atom_expr(&result, NULL, 0u)),
          "bind a partial argument tuple");
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &partial_view, 1u, observer) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "cached partial head still observes its bound tuple");
    bindings_builder_rollback(&observation_bindings, observation_mark);
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &partial_view, 1u, observer) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "rollback removes the partial tuple observation");
    bindings_builder_free(&observation_bindings);
    /* A shallow constructor stamp must not reuse a materialized-value fact
     * after the same skeleton acquires a different lexical interpretation. */
    BindingsBuilder contextual_builder;
    Atom *context_source = atom_var(&result, "context-source");
    Atom *context_key = atom_var_with_id(&result, "context-source",
        var_epoch_id(context_source->var_id, 421u));
    Atom *context_term = atom_expr2(&result, atom_symbol(&result, "context-inert"),
                                   atom_var(&result, "context-hole"));
    VarId context_ids[] = {context_source->var_id};
    Atom *context_variables[] = {context_source};
    CHECK(bindings_builder_init(&contextual_builder, NULL) &&
          bindings_builder_register_contextual_frame(
              &contextual_builder, context_ids, 1u, 421u) &&
          bindings_builder_add_var_fresh(&contextual_builder, context_key, context_term),
          "construct a materialized dense supplier");
    BindingsActivationView context_frame;
    bindings_activation_view_init(&context_frame);
    CHECK(bindings_activation_view_prepare(&context_frame, &contextual_builder,
              context_ids, context_variables, 1u, 421u, 0u), "prepare the supplier frame");
    BindingsTermCursorContextV1 typed_context = {
        .bindings = &contextual_builder.current, .frame = &context_frame};
    CettaGsltTermCursorObserverV1 typed_observer = {
        bindings_resolve_term_cursor_v1, &typed_context};
    CettaGsltTermCursorV1 typed_argument = {context_source, &context_frame};
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &typed_argument, 1u, typed_observer) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "materialized inert supplier establishes the initial forest fact");
    CHECK(bindings_rewrite_value_id(
              &contextual_builder.current, context_key->var_id,
              binding_value_from_context(context_term, 422u)),
          "change the supplier slot's lexical interpretation");
    CHECK(bindings_activation_view_prepare(&context_frame, &contextual_builder,
              context_ids, context_variables, 1u, 421u, 0u) &&
          petta_specializer_query_view_execution_admission(
              &space, consumer, &typed_argument, 1u, typed_observer) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "same constructor shape with contextual dependencies cannot reuse an inert forest fact");
    bindings_activation_view_free(&context_frame);
    bindings_builder_free(&contextual_builder);

    /* The supplier observation is independent of application saturation.
     * Exercise all named-arity authorities, including a type-only symbol
     * which becomes callable after a negative query has been cached. */
    Atom *typed_head = atom_symbol(&persistent, "type-only-supplier");
    Atom *typed_query = atom_expr(&result, &typed_head, 1u);
    CHECK(petta_specializer_query_execution_admission(
              &space, consumer, &typed_query, 1u) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "undeclared supplier head is inert");
    Atom *declaration = atom_expr3(
        &persistent, atom_symbol(&persistent, ":"), typed_head,
        atom_expr3(&persistent,
            atom_symbol_id(&persistent, g_builtin_syms.arrow),
            atom_symbol(&persistent, "Number"),
            atom_symbol(&persistent, "Number")));
    space_add(&space, declaration);
    petta_specializer_note_mutation(&space, declaration);
    Atom *supplier_heads[] = {
        typed_head, atom_symbol(&result, "late-arity"),
        atom_symbol_id(&result, g_builtin_syms.op_plus)};
    for (size_t kind = 0u; kind < 3u; kind++) {
        for (CettaExprLen supplied = 0u; supplied < 4u; supplied++) {
            Atom *parts[4] = {supplier_heads[kind]};
            for (CettaExprIndex i = 1u; i <= supplied; i++)
                parts[i] = atom_int(&result, i);
            Atom *argument = atom_expr(&result, parts, supplied + 1u);
            CHECK(petta_specializer_query_execution_admission(
                      &space, consumer, &argument, 1u) ==
                      PETTA_SPECIALIZER_RELATION_DEFER,
                  "callable head survives under, exact and over-application");
        }
    }
    Atom *nested_supplier = atom_expr2(
        &result, atom_symbol(&result, "inert-wrapper"), typed_head);
    CHECK(petta_specializer_query_execution_admission(
              &space, consumer, &nested_supplier, 1u) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "inert head does not erase a callable child");
    Atom *expression_head = atom_expr2(
        &result, typed_query, atom_int(&result, 0));
    CHECK(petta_specializer_query_execution_admission(
              &space, consumer, &expression_head, 1u) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "non-symbol head retains its recursive supplier observation");

    /* Open constructor forests are a revision-and-shape fact of the
     * authored roots and the live environment, not interned-closed
     * identity.  The same implication tree is read repeatedly. */
    Atom *open_phi = atom_var(&result, "open-phi");
    Atom *open_psi = atom_var(&result, "open-psi");
    Atom *open_impl = atom_expr3(
        &result, atom_symbol(&result, "→"), open_phi,
        atom_expr3(&result, atom_symbol(&result, "→"), open_psi, open_phi));
    CettaGsltTermCursorV1 open_view = {.source = open_impl};
    BindingsBuilder open_bindings;
    CHECK(bindings_builder_init(&open_bindings, NULL),
          "open-forest builder");
    BindingsTermCursorContextV1 open_context = {
        .bindings = bindings_builder_bindings(&open_bindings)};
    CettaGsltTermCursorObserverV1 open_observer = {
        bindings_resolve_term_cursor_v1, &open_context};
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &open_view, 1u, open_observer) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "open implication forest has no callable supplier");
    for (unsigned repeat = 0u; repeat < 8u; repeat++) {
        CHECK(petta_specializer_query_view_execution_admission(
                  &space, consumer, &open_view, 1u, open_observer) ==
                  PETTA_SPECIALIZER_RELATION_IRRELEVANT,
              "open-forest admission is a revision-and-shape read");
    }
    uint32_t open_mark = bindings_builder_save(&open_bindings);
    CHECK(bindings_builder_add_var_fresh(
              &open_bindings, open_phi,
              atom_symbol(&result, "late-arity")),
          "bind open-forest variable to a callable");
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &open_view, 1u, open_observer) ==
              PETTA_SPECIALIZER_RELATION_DEFER,
          "binding a callable into the forest is observed");
    bindings_builder_rollback(&open_bindings, open_mark);
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &open_view, 1u, open_observer) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "rollback restores the inert open forest");
    /* Distinct copies of the same constructor skeleton share the
     * revision-and-shape fact: variables are holes, not interned
     * identities. */
    Atom *iso_a = atom_var(&result, "iso-a");
    Atom *iso_b = atom_var(&result, "iso-b");
    Atom *iso_one = atom_expr3(
        &result, atom_symbol(&result, "→"), iso_a,
        atom_expr3(&result, atom_symbol(&result, "→"), iso_b, iso_a));
    Atom *iso_c = atom_var(&result, "iso-c");
    Atom *iso_d = atom_var(&result, "iso-d");
    Atom *iso_two = atom_expr3(
        &result, atom_symbol(&result, "→"), iso_c,
        atom_expr3(&result, atom_symbol(&result, "→"), iso_d, iso_c));
    CettaGsltTermCursorV1 iso_view_one = {.source = iso_one};
    CettaGsltTermCursorV1 iso_view_two = {.source = iso_two};
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &iso_view_one, 1u, open_observer) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "first isomorphic implication forest is inert");
    CHECK(petta_specializer_query_view_execution_admission(
              &space, consumer, &iso_view_two, 1u, open_observer) ==
              PETTA_SPECIALIZER_RELATION_IRRELEVANT,
          "isomorphic implication forest is a shape read");
    bindings_builder_free(&open_bindings);

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
