#include "eval.h"
#include "generated/petta_typecheck_v2_source_binding_v1.generated.h"
#include "library.h"
#include "parser.h"
#include "match.h"
#include "petta_program.h"
#include "petta_search_machine.h"
#include "petta_semantics.h"
#include "petta_typecheck.h"
#include "symbol.h"
#include "variant_shape.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static Atom *parse_one(Arena *arena, const char *source) {
    Atom **forms = NULL;
    int count = parse_metta_text(source, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static void add_clause(
    Space *space, Arena *arena, const char *source);

static Atom *test_nest_unary(
    Arena *arena, Atom *head, Atom *leaf, size_t depth) {
    Atom *nested = leaf;
    for (size_t index = 0u; index < depth; index++) {
        nested = atom_expr2(arena, head, nested);
        assert(nested);
    }
    return nested;
}

static AtomId test_nest_unary_id(
    TermUniverse *universe, AtomId head,
    AtomId leaf, size_t depth) {
    AtomId nested = leaf;
    for (size_t index = 0u; index < depth; index++) {
        AtomId elems[2] = {head, nested};
        nested = tu_expr_from_ids(universe, elems, 2u);
        assert(nested != CETTA_ATOM_ID_NONE);
    }
    return nested;
}

static AtomId test_descend_unary_id(
    const TermUniverse *universe, AtomId root,
    AtomId head, size_t depth) {
    AtomId cursor = root;
    for (size_t index = 0u; index < depth; index++) {
        assert(tu_kind(universe, cursor) == ATOM_EXPR);
        assert(tu_arity(universe, cursor) == 2u);
        assert(tu_child(universe, cursor, 0u) == head);
        cursor = tu_child(universe, cursor, 1u);
    }
    return cursor;
}

static void test_deep_typecheck_source_rewrites(
    TermUniverse *universe) {
    enum { DEEP_FINITE_DEPTH = 4096 };
    SymbolId box_symbol = symbol_intern_cstr(
        g_symbols, "deep-source-box");
    SymbolId type_symbol = symbol_intern_cstr(
        g_symbols, "DeepSourceType");
    SymbolId leaf_symbol = symbol_intern_cstr(
        g_symbols, "deep-source-leaf");
    SymbolId body_symbol = symbol_intern_cstr(
        g_symbols, "deep-source-body");
    SymbolId space_symbol = symbol_intern_cstr(
        g_symbols, "deep-source-space");
    SymbolId variable_symbol = symbol_intern_cstr(
        g_symbols, "$deep-source-whole");
    SymbolId at_symbol = symbol_intern_cstr(g_symbols, "@");
    assert(box_symbol != SYMBOL_ID_NONE);
    assert(type_symbol != SYMBOL_ID_NONE);
    assert(leaf_symbol != SYMBOL_ID_NONE);
    assert(body_symbol != SYMBOL_ID_NONE);
    assert(space_symbol != SYMBOL_ID_NONE);
    assert(variable_symbol != SYMBOL_ID_NONE);
    assert(at_symbol != SYMBOL_ID_NONE);

    AtomId box = tu_intern_symbol(universe, box_symbol);
    AtomId type = tu_intern_symbol(universe, type_symbol);
    AtomId leaf = tu_intern_symbol(universe, leaf_symbol);
    AtomId body = tu_intern_symbol(universe, body_symbol);
    AtomId space = tu_intern_symbol(universe, space_symbol);
    AtomId brand = tu_intern_symbol(
        universe, symbol_intern_cstr(g_symbols, "brand"));
    AtomId quote = tu_intern_symbol(
        universe, g_builtin_syms.quote);
    AtomId match = tu_intern_symbol(
        universe, g_builtin_syms.match);
    AtomId case_head = tu_intern_symbol(
        universe, g_builtin_syms.case_text);
    AtomId at = tu_intern_symbol(universe, at_symbol);
    AtomId variable = tu_intern_var(
        universe, variable_symbol, fresh_var_id());
    assert(box != CETTA_ATOM_ID_NONE);
    assert(type != CETTA_ATOM_ID_NONE);
    assert(leaf != CETTA_ATOM_ID_NONE);
    assert(body != CETTA_ATOM_ID_NONE);
    assert(space != CETTA_ATOM_ID_NONE);
    assert(brand != CETTA_ATOM_ID_NONE);
    assert(quote != CETTA_ATOM_ID_NONE);
    assert(match != CETTA_ATOM_ID_NONE);
    assert(case_head != CETTA_ATOM_ID_NONE);
    assert(at != CETTA_ATOM_ID_NONE);
    assert(variable != CETTA_ATOM_ID_NONE);

    CettaLibraryContext context = {0};
    cetta_eval_session_init(
        &context.session, CETTA_LANGUAGE_PETTA,
        cetta_profile_petta_extended());
    CettaLibraryContext *previous =
        eval_current_library_context();
    eval_set_library_context(&context);

    AtomId marker_elems[3] = {brand, type, leaf};
    AtomId marker = tu_expr_from_ids(
        universe, marker_elems, 3u);
    AtomId deep_marker = test_nest_unary_id(
        universe, box, marker, DEEP_FINITE_DEPTH);
    AtomId document[1] = {deep_marker};
    cetta_petta_erase_typecheck_marks_document(
        universe, document, 1);
    assert(test_descend_unary_id(
               universe, document[0], box,
               DEEP_FINITE_DEPTH) == leaf);

    /* Quotation is an explicit semantic boundary: even at the same depth,
     * its marker payload remains literal. */
    AtomId quote_elems[2] = {quote, marker};
    AtomId quoted_marker = tu_expr_from_ids(
        universe, quote_elems, 2u);
    AtomId deep_quoted_marker = test_nest_unary_id(
        universe, box, quoted_marker, DEEP_FINITE_DEPTH);
    document[0] = deep_quoted_marker;
    cetta_petta_erase_typecheck_marks_document(
        universe, document, 1);
    assert(document[0] == deep_quoted_marker);

    AtomId target_elems[2] = {box, leaf};
    AtomId target = tu_expr_from_ids(
        universe, target_elems, 2u);
    AtomId as_elems[3] = {at, variable, target};
    AtomId as_pattern = tu_expr_from_ids(
        universe, as_elems, 3u);
    AtomId deep_as_pattern = test_nest_unary_id(
        universe, box, as_pattern, DEEP_FINITE_DEPTH);
    AtomId match_elems[4] = {
        match, space, deep_as_pattern, body,
    };
    AtomId match_form = tu_expr_from_ids(
        universe, match_elems, 4u);
    document[0] = match_form;
    cetta_petta_erase_typecheck_marks_document(
        universe, document, 1);
    AtomId normalized_match = document[0];
    assert(tu_kind(universe, normalized_match) == ATOM_EXPR);
    assert(tu_arity(universe, normalized_match) == 4u);
    assert(test_descend_unary_id(
               universe,
               tu_child(universe, normalized_match, 2u),
               box, DEEP_FINITE_DEPTH) == target);
    AtomId match_body = tu_child(
        universe, normalized_match, 3u);
    assert(tu_kind(universe, match_body) == ATOM_EXPR);
    assert(tu_arity(universe, match_body) == 4u);
    assert(tu_head_sym(universe, match_body) ==
           g_builtin_syms.let);
    assert(tu_child(universe, match_body, 1u) == variable);
    assert(tu_child(universe, match_body, 2u) == target);
    assert(tu_child(universe, match_body, 3u) == body);

    /* The case branch grammar is a distinct traversal mode.  Exercise the
     * same deep alias through it, plus an ordinary branch that must remain
     * free of synthetic lets. */
    AtomId branch_elems[2] = {deep_as_pattern, body};
    AtomId branch = tu_expr_from_ids(
        universe, branch_elems, 2u);
    AtomId plain_branch_elems[2] = {leaf, body};
    AtomId plain_branch = tu_expr_from_ids(
        universe, plain_branch_elems, 2u);
    AtomId branches_elems[2] = {branch, plain_branch};
    AtomId branches = tu_expr_from_ids(
        universe, branches_elems, 2u);
    AtomId case_elems[3] = {case_head, leaf, branches};
    AtomId case_form = tu_expr_from_ids(
        universe, case_elems, 3u);
    document[0] = case_form;
    cetta_petta_erase_typecheck_marks_document(
        universe, document, 1);
    AtomId normalized_branches = tu_child(
        universe, document[0], 2u);
    AtomId normalized_branch = tu_child(
        universe, normalized_branches, 0u);
    assert(test_descend_unary_id(
               universe,
               tu_child(universe, normalized_branch, 0u),
               box, DEEP_FINITE_DEPTH) == target);
    AtomId branch_body = tu_child(
        universe, normalized_branch, 1u);
    assert(tu_head_sym(universe, branch_body) ==
           g_builtin_syms.let);
    assert(tu_child(
               universe, normalized_branches, 1u) ==
           plain_branch);

    eval_set_library_context(previous);
}

static void test_deep_cons_semantics(Arena *arena) {
    enum { DEEP_FINITE_DEPTH = 4096 };
    Atom *box = atom_symbol(arena, "deep-cons-box");
    Atom *item = atom_symbol(arena, "deep-cons-item");
    Atom *tail = atom_unit(arena);
    Atom *cons = petta_semantics_open_cons_value(
        arena, item, tail);
    Atom *scalar = atom_symbol(arena, "deep-cons-scalar");
    assert(box && item && tail && cons && scalar);

    Atom *deep_cons = test_nest_unary(
        arena, box, cons, DEEP_FINITE_DEPTH);
    Atom *deep_cons_peer = test_nest_unary(
        arena, box, cons, DEEP_FINITE_DEPTH);
    Atom *deep_scalar = test_nest_unary(
        arena, box, scalar, DEEP_FINITE_DEPTH);

    assert(petta_semantics_value_contains_observable_open_cons(
        deep_cons));
    assert(!petta_semantics_value_contains_observable_open_cons(
        deep_scalar));

    Atom *materialized = petta_semantics_materialize_value(
        arena, deep_cons);
    assert(materialized);
    assert(!petta_semantics_value_contains_observable_open_cons(
        materialized));
    Atom *cursor = materialized;
    for (size_t depth = 0u;
         depth < DEEP_FINITE_DEPTH; depth++) {
        assert(cursor->kind == ATOM_EXPR);
        assert(cursor->expr.len == 2u);
        assert(atom_alpha_eq(cursor->expr.elems[0], box));
        cursor = cursor->expr.elems[1];
    }
    assert(cursor->kind == ATOM_EXPR);
    assert(cursor->expr.len == 1u);
    assert(atom_alpha_eq(cursor->expr.elems[0], item));

    Atom *open_tail = atom_var_with_id(
        arena, "deep-open-tail", fresh_var_id());
    Atom *open_cons = petta_semantics_open_cons_value(
        arena, item, open_tail);
    Atom *observable_open = petta_semantics_materialize_value(
        arena, open_cons);
    assert(open_tail && open_cons && observable_open);
    assert(!petta_semantics_is_open_cons_value(observable_open));
    assert(observable_open->kind == ATOM_EXPR);
    assert(observable_open->expr.len == 3u);
    assert(petta_semantics_form(
               atom_head_symbol_id(observable_open)) ==
           PETTA_FORM_CONS);
    assert(atom_alpha_eq(observable_open->expr.elems[1], item));
    assert(observable_open->expr.elems[2]->kind == ATOM_VAR);
    assert(observable_open->expr.elems[2]->var_id ==
           open_tail->var_id);

    assert(petta_semantics_contains_cons_constraint(deep_cons));
    assert(!petta_semantics_contains_cons_constraint(deep_scalar));
    assert(petta_semantics_cons_pattern_may_match(
        deep_cons, deep_cons_peer));
    assert(!petta_semantics_cons_pattern_may_match(
        deep_cons, deep_scalar));

    BindingsBuilder builder;
    assert(bindings_builder_init(&builder, NULL));
    assert(petta_semantics_match_cons_constraint(
        arena, deep_cons, deep_cons_peer, &builder));
    assert(bindings_builder_bindings(&builder)->len == 0u);
    assert(!petta_semantics_match_cons_constraint(
        arena, deep_cons, deep_scalar, &builder));
    assert(bindings_builder_bindings(&builder)->len == 0u);
    bindings_builder_free(&builder);
}

static void test_answer_materialization_boundaries(
    Space *space, Arena *persistent, Arena *answers) {
    enum { DEEP_FINITE_DEPTH = 4096 };
    add_clause(
        space, persistent,
        "(= (answer-materialization-open) (cons a $tail))");

    Atom *query = parse_one(
        answers, "(answer-materialization-open)");
    assert(query);
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_EXPR);
    assert(answer->expr.len == 3u);
    assert(!petta_semantics_is_open_cons_value(answer));
    assert(petta_semantics_form(atom_head_symbol_id(answer)) ==
           PETTA_FORM_CONS);
    assert(atom_is_symbol_id(
        answer->expr.elems[1],
        symbol_intern_cstr(g_symbols, "a")));
    assert(answer->expr.elems[2]->kind == ATOM_VAR);
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);

    Atom *box = atom_symbol(answers, "deep-answer-box");
    Atom *leaf = atom_symbol(answers, "deep-answer-leaf");
    Atom *deep = test_nest_unary(
        answers, box, leaf, DEEP_FINITE_DEPTH);
    Atom *quote = atom_symbol_id(
        answers, g_builtin_syms.quote);
    Atom *quoted = atom_expr2(answers, quote, deep);
    assert(box && leaf && deep && quote && quoted);
    assert(petta_machine_init(
        &machine, space, answers, quoted, NULL, NULL));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    Atom *cursor = answer;
    for (size_t depth = 0u;
         depth < DEEP_FINITE_DEPTH; depth++) {
        assert(cursor && cursor->kind == ATOM_EXPR);
        assert(cursor->expr.len == 2u);
        assert(atom_alpha_eq(cursor->expr.elems[0], box));
        cursor = cursor->expr.elems[1];
    }
    assert(atom_alpha_eq(cursor, leaf));
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_logical_list_cursor_boundaries(Arena *arena) {
    enum { ITEM_COUNT = 129 };
    Atom *items[ITEM_COUNT];
    Atom *tail = atom_unit(arena);
    assert(tail);
    for (size_t index = ITEM_COUNT; index > 0u; index--) {
        items[index - 1u] = atom_int(arena, (int64_t)(index - 1u));
        assert(items[index - 1u]);
        tail = petta_semantics_open_cons_value(
            arena, items[index - 1u], tail);
        assert(tail);
    }

    CettaExprLen length = 0u;
    assert(petta_semantics_logical_list_length(tail, &length));
    assert(length == ITEM_COUNT);

    PeTTaLogicalListCursor cursor;
    petta_semantics_logical_list_cursor_init(&cursor, tail);
    for (size_t index = 0u; index < ITEM_COUNT; index++) {
        Atom *item = NULL;
        assert(petta_semantics_logical_list_cursor_next(
                   &cursor, &item) == PETTA_LOGICAL_LIST_ITEM);
        assert(item == items[index]);
    }
    Atom *extra = NULL;
    assert(petta_semantics_logical_list_cursor_next(
               &cursor, &extra) == PETTA_LOGICAL_LIST_END);

    Atom *flat = petta_semantics_materialize_closed_logical_list(
        arena, tail);
    assert(flat && flat->kind == ATOM_EXPR);
    assert(flat->expr.len == ITEM_COUNT);
    for (size_t index = 0u; index < ITEM_COUNT; index++)
        assert(flat->expr.elems[index] == items[index]);

    Atom *open_tail = atom_var_with_id(
        arena, "open-tail", fresh_var_id());
    Atom *partial = petta_semantics_open_cons_value(
        arena, items[0], open_tail);
    assert(open_tail && partial);
    petta_semantics_logical_list_cursor_init(&cursor, partial);
    Atom *head = NULL;
    assert(petta_semantics_logical_list_cursor_next(
               &cursor, &head) == PETTA_LOGICAL_LIST_ITEM);
    assert(head == items[0]);
    assert(petta_semantics_logical_list_cursor_next(
               &cursor, &extra) == PETTA_LOGICAL_LIST_INVALID);
    length = ITEM_COUNT;
    assert(!petta_semantics_logical_list_length(partial, &length));
    assert(length == 0u);
    assert(!petta_semantics_materialize_closed_logical_list(
        arena, partial));

    Arena carrier_arena;
    arena_init(&carrier_arena);
    arena_set_hashcons(&carrier_arena, NULL);
    ArenaMark carrier_origin = arena_mark(&carrier_arena);
    Atom *carrier_tail = atom_unit(&carrier_arena);
    Atom *carrier_head = atom_int(&carrier_arena, 1);
    Atom *first = petta_semantics_open_cons_value(
        &carrier_arena, carrier_head, carrier_tail);
    Atom *second = petta_semantics_open_cons_value(
        &carrier_arena, carrier_head, first);
    assert(first && second);
    assert(first->expr.elems[0] == second->expr.elems[0]);

    /* Reset invalidates the shared carrier tag along with every other arena
     * allocation.  Allocate unrelated atoms before rebuilding so a stale
     * cached pointer cannot accidentally retain the old tag contents. */
    arena_reset(&carrier_arena, carrier_origin);
    carrier_head = atom_int(&carrier_arena, 2);
    carrier_tail = atom_unit(&carrier_arena);
    Atom *after_reset = petta_semantics_open_cons_value(
        &carrier_arena, carrier_head, carrier_tail);
    assert(after_reset);
    assert(petta_semantics_is_open_cons_value(after_reset));
    assert(after_reset->expr.elems[1] == carrier_head);
    assert(after_reset->expr.elems[2] == carrier_tail);
    arena_free(&carrier_arena);
}

static void test_private_variant_summary(Arena *arena) {
    Atom *ordinary = atom_var_with_id(
        arena, "ordinary", fresh_var_id());
    Atom *private_slot = atom_var_with_id(
        arena, "$_slot", variant_shape_slot_id(7u));
    Atom *plain_items[] = {
        atom_symbol(arena, "plain"),
        ordinary,
    };
    Atom *private_items[] = {
        atom_symbol(arena, "nested"),
        private_slot,
    };
    Atom *plain = atom_expr(arena, plain_items, 2u);
    Atom *nested_private =
        atom_expr(arena, private_items, 2u);
    assert(ordinary && private_slot && plain && nested_private);
    assert(!atom_has_private_variant_vars(ordinary));
    assert(!atom_has_private_variant_vars(plain));
    assert(atom_has_private_variant_vars(private_slot));
    assert(atom_has_private_variant_vars(nested_private));

    Bindings clean;
    Bindings private_value;
    bindings_init(&clean);
    bindings_init(&private_value);
    assert(bindings_add_id(
        &clean, fresh_var_id(), SYMBOL_ID_NONE, plain));
    assert(bindings_add_id(
        &private_value, fresh_var_id(), SYMBOL_ID_NONE,
        nested_private));
    assert(!bindings_contains_private_variant_slots(&clean));
    assert(bindings_contains_private_variant_slots(&private_value));
    bindings_free(&clean);
    bindings_free(&private_value);
}

static void test_binding_prefix_factoring(Arena *arena) {
    Atom *x = atom_var_with_id(arena, "prefix-x", fresh_var_id());
    Atom *y = atom_var_with_id(arena, "prefix-y", fresh_var_id());
    Atom *z = atom_var_with_id(arena, "prefix-z", fresh_var_id());
    Atom *one = atom_symbol(arena, "prefix-one");
    Atom *two = atom_symbol(arena, "prefix-two");
    Atom *three = atom_symbol(arena, "prefix-three");
    assert(x && y && z && one && two && three);

    Bindings base;
    Bindings extended;
    bindings_init(&base);
    bindings_init(&extended);
    assert(bindings_add_var(&base, x, one));
    assert(bindings_add_var(&base, y, two));
    assert(bindings_clone(&extended, &base));
    assert(bindings_add_var(&extended, z, three));

    bool factored = false;
    uint64_t elided = 0u;
    assert(bindings_factor_prefix(
        &extended, &base, &factored, &elided));
    assert(factored);
    assert(elided == 2u);
    assert(extended.len == 1u);
    assert(!bindings_lookup_id(&extended, x->var_id));
    assert(bindings_lookup_id(&extended, z->var_id) == three);
    bindings_free(&extended);

    /* A reordered environment is not certified as an inherited prefix.
     * The operation must leave it intact for the canonical merge path. */
    bindings_init(&extended);
    assert(bindings_add_var(&extended, y, two));
    assert(bindings_add_var(&extended, x, one));
    assert(bindings_add_var(&extended, z, three));
    factored = true;
    elided = UINT64_MAX;
    assert(bindings_factor_prefix(
        &extended, &base, &factored, &elided));
    assert(!factored);
    assert(elided == 0u);
    assert(extended.len == 3u);
    assert(bindings_lookup_id(&extended, x->var_id) == one);
    assert(bindings_lookup_id(&extended, z->var_id) == three);

    bindings_free(&extended);
    bindings_free(&base);
}

static void test_activation_epoch_suffix_application(Arena *arena) {
    Atom *outer = atom_var_with_id(
        arena, "outer-slot", fresh_var_id());
    Atom *outer_link = atom_var_with_id(
        arena, "outer-link", fresh_var_id());
    Atom *rule = atom_var_with_id(
        arena, "rule-slot", fresh_var_id());
    Atom *outer_value = atom_symbol(
        arena, "outer-value");
    assert(outer && outer_link && rule && outer_value);

    Bindings bindings;
    bindings_init(&bindings);
    assert(bindings_add_var(&bindings, outer_link, outer_value));
    assert(bindings_add_var(&bindings, outer, outer_link));
    uint32_t activation_first = bindings.len;
    uint32_t epoch = fresh_var_suffix();
    Atom *rule_slot = atom_var_like(
        arena, rule, var_epoch_id(rule->var_id, epoch));
    assert(rule_slot);
    assert(bindings_add_var(&bindings, rule_slot, outer));

    /* The activation view substitutes its own slot but deliberately leaves
     * an outer reference for the live machine trail. */
    Atom *local = bindings_apply_epoch_since(
        &bindings, arena, rule, epoch, activation_first);
    assert(local && local->kind == ATOM_VAR);
    assert(local->var_id == outer->var_id);

    /* The ordinary full-environment operation remains the materialization
     * boundary and therefore resolves the outer slot as well. */
    Atom *materialized = bindings_apply_epoch(
        &bindings, arena, rule, epoch);
    assert(materialized == outer_value);
    Atom *sequential = bindings_apply(
        &bindings, arena, local);
    assert(sequential == materialized);
    Atom *fused = bindings_apply_epoch_then_all(
        &bindings, arena, rule, epoch, activation_first);
    assert(fused == sequential);

    /* An unbound activation slot remains fresh rather than aliasing either the
     * source variable or an outer variable with the same spelling. */
    Atom *unbound = atom_var_with_id(
        arena, "rule-unbound", fresh_var_id());
    Atom *fresh_unbound = bindings_apply_epoch(
        &bindings, arena, unbound, epoch);
    assert(fresh_unbound && fresh_unbound->kind == ATOM_VAR);
    assert(fresh_unbound->var_id ==
           var_epoch_id(unbound->var_id, epoch));
    assert(fresh_unbound->var_id != unbound->var_id);

    Atom *pair = atom_symbol(arena, "activation-pair");
    Atom *source_children[] = {pair, rule, unbound};
    Atom *source = atom_expr(arena, source_children, 3u);
    Atom *local_pair = bindings_apply_epoch_since(
        &bindings, arena, source, epoch, activation_first);
    Atom *sequential_pair = bindings_apply(
        &bindings, arena, local_pair);
    Atom *fused_pair = bindings_apply_epoch_then_all(
        &bindings, arena, source, epoch, activation_first);
    assert(source && local_pair && sequential_pair && fused_pair);
    assert(atom_eq(fused_pair, sequential_pair));
    assert(fused_pair->kind == ATOM_EXPR &&
           fused_pair->expr.len == 3u);
    assert(fused_pair->expr.elems[1] == outer_value);
    assert(fused_pair->expr.elems[2]->kind == ATOM_VAR);
    assert(fused_pair->expr.elems[2]->var_id ==
           var_epoch_id(unbound->var_id, epoch));

    /* A malformed suffix boundary must fail closed. */
    assert(!bindings_apply_epoch_since(
        &bindings, arena, rule, epoch, bindings.len + 1u));
    bindings_free(&bindings);
}

static void add_clause(Space *space, Arena *arena, const char *source) {
    Atom *clause = parse_one(arena, source);
    assert(clause);
    space_add(space, clause);
}

typedef struct {
    size_t calls;
} CapacitySpecializerProbe;

static PettaSpecializeResult decline_specialization_for_capacity(
    void *context, Space *space, Arena *arena,
    Atom *call, Atom **prepared_call) {
    (void)space;
    (void)call;
    CapacitySpecializerProbe *probe = context;
    assert(probe && arena && prepared_call);
    probe->calls++;
    /* A declining accelerator has no authority to substitute even a
     * well-formed non-NULL result.  The machine must retain the source call. */
    *prepared_call = atom_symbol(
        arena, "capacity-specializer-poison");
    assert(*prepared_call);
    return PETTA_SPECIALIZE_CAPACITY;
}

static void test_specializer_capacity_fallback(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (capacity-specializer-fallback $value) $value)");
    Atom *query = parse_one(
        answers,
        "(capacity-specializer-fallback capacity-source-value)");
    Atom *expected = atom_symbol(
        answers, "capacity-source-value");
    assert(query && expected);

    CapacitySpecializerProbe probe = {0};
    PettaMachineHost host = {
        .context = &probe,
        .prepare_call = decline_specialization_for_capacity,
    };
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(answer, expected));
    bindings_free(&environment);
    assert(probe.calls == 1u);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.specializer_prepare_calls == 1u);
    assert(stats.specializer_prepare_capacity_declines == 1u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_deep_callable_detection(
    Space *space, Arena *persistent, Arena *answers) {
    enum { DEEP_FINITE_DEPTH = 4096 };
    Atom *box = atom_symbol(persistent, "deep-callable-box");
    Atom *empty_head = atom_symbol(persistent, "empty");
    Atom *empty_items[] = {empty_head};
    Atom *empty_call = atom_expr(persistent, empty_items, 1u);
    Atom *body = test_nest_unary(
        persistent, box, empty_call, DEEP_FINITE_DEPTH);
    Atom *relation_head = atom_symbol(
        persistent, "deep-callable-regression");
    Atom *lhs_items[] = {relation_head};
    Atom *lhs = atom_expr(persistent, lhs_items, 1u);
    Atom *equation = atom_expr3(
        persistent,
        atom_symbol_id(persistent, g_builtin_syms.equals),
        lhs, body);
    assert(box && empty_head && empty_call && body &&
           relation_head && lhs && equation);
    space_add(space, equation);

    Atom *query_head = atom_symbol(
        answers, "deep-callable-regression");
    Atom *query_items[] = {query_head};
    Atom *query = atom_expr(answers, query_items, 1u);
    assert(query_head && query);
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    PettaMachineStep step = petta_machine_next(
        &machine, &answer, &environment);
    assert(step == PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_deep_functional_match_pattern(
    Space *space, Arena *persistent, Arena *answers) {
    enum { DEEP_FINITE_DEPTH = 4096 };
    add_clause(
        space, persistent,
        "(: &self (SpaceOf Atom))");
    add_clause(
        space, persistent,
        "(: deep-pattern-id (-[det]-> Atom Atom))");
    add_clause(
        space, persistent,
        "(= (deep-pattern-id $value) $value)");

    Atom *stored_box = atom_symbol(
        persistent, "deep-functional-pattern-box");
    Atom *stored_leaf = atom_symbol(
        persistent, "deep-functional-pattern-leaf");
    Atom *stored = test_nest_unary(
        persistent, stored_box, stored_leaf,
        DEEP_FINITE_DEPTH);
    assert(stored_box && stored_leaf && stored);
    space_add(space, stored);

    Atom *box = atom_symbol(
        answers, "deep-functional-pattern-box");
    Atom *leaf = atom_symbol(
        answers, "deep-functional-pattern-leaf");
    Atom *det_head = atom_symbol(
        answers, "deep-pattern-id");
    Atom *det_call = atom_expr2(answers, det_head, leaf);
    Atom *pattern = test_nest_unary(
        answers, box, det_call, DEEP_FINITE_DEPTH);
    Atom *match_head = atom_symbol_id(
        answers, g_builtin_syms.match);
    Atom *self = atom_symbol(answers, "&self");
    Atom *truth = atom_symbol(answers, "true");
    Atom *match_items[] = {
        match_head, self, pattern, truth,
    };
    Atom *query = atom_expr(answers, match_items, 4u);
    assert(box && leaf && det_head && det_call && pattern);
    assert(match_head && self && truth && query);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(answer, truth));
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void format_nary_application(
    char *buffer, size_t capacity, const char *head,
    const char *argument_prefix, size_t argument_count) {
    assert(buffer && capacity > 0u && head && argument_prefix);
    size_t used = 0u;
    int written = snprintf(buffer, capacity, "(%s", head);
    assert(written >= 0 && (size_t)written < capacity);
    used = (size_t)written;
    for (size_t index = 0u; index < argument_count; index++) {
        written = snprintf(
            buffer + used, capacity - used,
            " %s%zu", argument_prefix, index);
        assert(written >= 0 && (size_t)written < capacity - used);
        used += (size_t)written;
    }
    written = snprintf(buffer + used, capacity - used, ")");
    assert(written == 1);
}

static void expect_value_type(
    Space *space, Arena *arena, const char *value_source,
    const char *type_source, PettaTypecheckVerdict verdict) {
    Atom *value = parse_one(arena, value_source);
    Atom *required = parse_one(arena, type_source);
    PettaTypecheckResult result;
    assert(value && required);
    assert(petta_typecheck_value(
        space, arena, value, required, NULL, &result));
    assert(result.fault == PETTA_TYPECHECK_FAULT_NONE);
    if (result.verdict != verdict) {
        fprintf(stderr,
                "value-type mismatch in test: %s : %s expected=%s actual=%s reason=%s\n",
                value_source, type_source,
                petta_typecheck_verdict_name(verdict),
                petta_typecheck_verdict_name(result.verdict),
                petta_typecheck_reason_name(result.reason));
    }
    assert(result.verdict == verdict);
}

static void expect_type_compatibility(
    Space *space, Arena *arena, const char *actual_source,
    const char *required_source, PettaTypecheckVerdict verdict) {
    Atom *actual = parse_one(arena, actual_source);
    Atom *required = parse_one(arena, required_source);
    PettaTypecheckResult result;
    assert(actual && required);
    assert(petta_typecheck_types(
        space, arena, actual, required, &result));
    assert(result.fault == PETTA_TYPECHECK_FAULT_NONE);
    if (result.verdict != verdict) {
        fprintf(stderr,
                "type-compat mismatch in test: %s <= %s expected=%s actual=%s reason=%s\n",
                actual_source, required_source,
                petta_typecheck_verdict_name(verdict),
                petta_typecheck_verdict_name(result.verdict),
                petta_typecheck_reason_name(result.reason));
    }
    assert(result.verdict == verdict);
}

static PettaTypecheckCallable test_typecheck_callable(
    void *context, Atom *value, CettaExprLen arity) {
    (void)value;
    bool callable = context && *(const bool *)context;
    return callable && arity == 1u
        ? PETTA_TYPECHECK_CALLABLE_YES
        : PETTA_TYPECHECK_CALLABLE_NO;
}

static void test_native_residual_typecheck(
    Space *space, Arena *persistent, Arena *scratch) {
    add_clause(space, persistent, "(: ScoreAlias (Alias Number))");
    add_clause(space, persistent,
               "(: NominalA (Newtype %Undefined%))");
    add_clause(space, persistent,
               "(: NominalB (Newtype %Undefined%))");
    add_clause(space, persistent,
               "(: ScoreBrand (Newtype Number))");
    add_clause(space, persistent,
               "(: NominalCycle (Newtype NominalCycle))");
    add_clause(space, persistent, "(: AliasCycleA (Alias AliasCycleB))");
    add_clause(space, persistent, "(: AliasCycleB (Alias AliasCycleA))");
    add_clause(space, persistent, "(: mixed-value (| Number String))");
    add_clause(space, persistent, "(: numeric-value (| Number Number))");
    add_clause(space, persistent,
               "(: callable-value (-> Number Number))");

    expect_value_type(space, scratch, "1", "Number",
                      PETTA_TYPECHECK_ESTABLISHED);
    expect_value_type(space, scratch, "1", "String",
                      PETTA_TYPECHECK_REFUTED);
    expect_value_type(space, scratch, "$open", "Number",
                      PETTA_TYPECHECK_UNDETERMINED);
    expect_value_type(space, scratch, "(1 2 3)", "(List Number)",
                      PETTA_TYPECHECK_ESTABLISHED);
    expect_value_type(space, scratch, "(1 \"bad\" 3)", "(List Number)",
                      PETTA_TYPECHECK_REFUTED);
    expect_value_type(space, scratch, "(Pair 1 \"ok\")",
                      "(Pair Number String)",
                      PETTA_TYPECHECK_ESTABLISHED);
    expect_value_type(space, scratch, "(Pair 1 False)",
                      "(Pair Number String)",
                      PETTA_TYPECHECK_REFUTED);
    expect_value_type(space, scratch, "(2 \"first\")",
                      "(Number String)",
                      PETTA_TYPECHECK_ESTABLISHED);
    expect_value_type(space, scratch, "((8 9) \"nested\")",
                      "((Number Number) String)",
                      PETTA_TYPECHECK_ESTABLISHED);
    expect_value_type(space, scratch, "1", "(| String Number)",
                      PETTA_TYPECHECK_ESTABLISHED);
    expect_value_type(space, scratch, "1", "(| String Bool)",
                      PETTA_TYPECHECK_REFUTED);
    expect_value_type(space, scratch, "mixed-value", "Number",
                      PETTA_TYPECHECK_REFUTED);
    expect_value_type(space, scratch, "numeric-value", "Number",
                      PETTA_TYPECHECK_ESTABLISHED);
    expect_value_type(space, scratch, "1", "ScoreAlias",
                      PETTA_TYPECHECK_ESTABLISHED);
    expect_value_type(space, scratch, "\"bad\"", "ScoreAlias",
                      PETTA_TYPECHECK_REFUTED);
    expect_value_type(space, scratch, "1", "ScoreBrand",
                      PETTA_TYPECHECK_ESTABLISHED);
    expect_value_type(space, scratch, "\"bad\"", "ScoreBrand",
                      PETTA_TYPECHECK_REFUTED);
    expect_value_type(space, scratch, "1", "NominalCycle",
                      PETTA_TYPECHECK_UNDETERMINED);

    bool callable = true;
    PettaTypecheckHooks callable_hooks = {
        .context = &callable,
        .callable = test_typecheck_callable,
    };
    PettaTypecheckResult arrow_result;
    assert(petta_typecheck_value(
        space, scratch, parse_one(scratch, "callable-value"),
        parse_one(scratch, "(-> Number Number)"),
        &callable_hooks, &arrow_result));
    assert(arrow_result.verdict == PETTA_TYPECHECK_ESTABLISHED);
    callable = false;
    assert(petta_typecheck_value(
        space, scratch, parse_one(scratch, "callable-value"),
        parse_one(scratch, "(-> Number Number)"),
        &callable_hooks, &arrow_result));
    assert(arrow_result.verdict == PETTA_TYPECHECK_REFUTED);
    assert(arrow_result.reason == PETTA_TYPECHECK_REASON_NONCALLABLE);

    Atom *indexed_value = parse_one(scratch, "1");
    Atom *indexed_type = parse_one(scratch, "ScoreAlias");
    PettaTypecheckResult indexed_result;
    assert(petta_typecheck_value(
        space, scratch, indexed_value, indexed_type,
        NULL, &indexed_result));
    assert(indexed_result.verdict == PETTA_TYPECHECK_ESTABLISHED);
    assert(indexed_result.declaration_lookup_cost.indexed_lookups > 0u);
    assert(indexed_result.declaration_lookup_cost.full_space_rows_examined ==
           0u);
    assert(indexed_result.declaration_lookup_cost.indexed_rows_examined <
           space_length64(space));

    expect_type_compatibility(space, scratch, "NominalA", "NominalA",
                              PETTA_TYPECHECK_ESTABLISHED);
    expect_type_compatibility(space, scratch, "NominalA", "NominalB",
                              PETTA_TYPECHECK_REFUTED);
    expect_type_compatibility(space, scratch, "%Undefined%", "NominalA",
                              PETTA_TYPECHECK_REFUTED);
    expect_type_compatibility(space, scratch, "AliasCycleA", "Number",
                              PETTA_TYPECHECK_UNDETERMINED);

    PettaTypecheckResult malformed;
    assert(!petta_typecheck_value(
        space, scratch, parse_one(scratch, "1"),
        parse_one(scratch, "()"), NULL, &malformed));
    assert(malformed.fault == PETTA_TYPECHECK_FAULT_MALFORMED_TYPE);
    assert(malformed.verdict == PETTA_TYPECHECK_UNDETERMINED);
}

static Atom *add_indexed_program_clause(
    PettaProgram *program, Space *space,
    Arena *arena, const char *source) {
    Atom *clause = parse_one(arena, source);
    assert(clause);
    CettaCount before = space_length64(space);
    space_add(space, clause);
    assert(space_length64(space) == before + 1u);
    Atom *stored = space_get_at64(space, before);
    assert(stored);
    assert(petta_program_note_add(
        program, space, stored, NULL));
    return stored;
}

static Atom *add_compiled_program_clause(
    PettaProgram *program, Space *space,
    Arena *arena, const char *source) {
    Atom *clause = parse_one(arena, source);
    assert(clause);
    const PettaPlanNode *plan =
        petta_program_plan_dynamic_add(program, clause);
    assert(plan);
    CettaCount before = space_length64(space);
    space_add(space, clause);
    assert(space_length64(space) == before + 1u);
    Atom *stored = space_get_at64(space, before);
    assert(stored);
    assert(petta_program_note_add(
        program, space, stored, plan));
    return stored;
}

static void test_constructor_slot_frame_plans(
    TermUniverse *universe, Arena *persistent, Arena *answers) {
    PettaProgram *program = petta_program_new();
    assert(program);

    Atom *ascription = parse_one(
        persistent, "(: $value Number)");
    const PettaPlanNode *ascription_plan =
        petta_program_plan_current(program, ascription);
    assert(ascription_plan);
    assert(ascription_plan->role == PETTA_PLAN_DATA);
    assert(
        ascription_plan->execution ==
        PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS);
    assert(!ascription_plan->contains_call);

    Atom *dependent_arrow = parse_one(
        persistent, "(-> (: $value Number) Result)");
    const PettaPlanNode *arrow_plan =
        petta_program_plan_current(program, dependent_arrow);
    assert(arrow_plan);
    assert(arrow_plan->role == PETTA_PLAN_DATA);
    assert(
        arrow_plan->execution ==
        PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS);
    assert(!arrow_plan->contains_call);

    Atom *active_field = parse_one(
        persistent, "(: $value (+ 1 2))");
    const PettaPlanNode *active_plan =
        petta_program_plan_current(program, active_field);
    assert(active_plan);
    assert(active_plan->role == PETTA_PLAN_DATA);
    assert(
        active_plan->execution ==
        PETTA_PLAN_EXEC_CONSTRUCTOR_SLOTS);
    assert(active_plan->contains_call);
    assert(active_plan->children[2u].role == PETTA_PLAN_STATIC_CALL);

    Atom *ordinary_data = parse_one(
        persistent, "(Pair $value (+ 1 2))");
    const PettaPlanNode *ordinary_plan =
        petta_program_plan_current(program, ordinary_data);
    assert(ordinary_plan);
    assert(ordinary_plan->role == PETTA_PLAN_DATA);
    assert(ordinary_plan->execution == PETTA_PLAN_EXEC_GENERIC);
    assert(ordinary_plan->contains_call);

    Atom *deep_plan_head = parse_one(
        persistent, "deep-plan-box");
    Atom *deep_plan_leaf = parse_one(
        persistent, "deep-plan-leaf");
    Atom *deep_data = test_nest_unary(
        persistent, deep_plan_head, deep_plan_leaf, 4096u);
    const PettaPlanNode *deep_plan =
        petta_program_plan_current(program, deep_data);
    assert(deep_plan);
    assert(!deep_plan->contains_call);
    const PettaPlanNode *deep_cursor = deep_plan;
    for (size_t depth = 0u; depth < 4096u; depth++) {
        assert(deep_cursor->role == PETTA_PLAN_DATA);
        assert(deep_cursor->child_count == 2u);
        assert(deep_cursor->children);
        assert(deep_cursor->children[0u].role == PETTA_PLAN_VALUE);
        deep_cursor = &deep_cursor->children[1u];
    }
    assert(deep_cursor->role == PETTA_PLAN_VALUE);
    assert(deep_cursor->child_count == 0u);

    Atom *pure_grounded = parse_one(
        persistent, "(+ 1 2)");
    const PettaPlanNode *pure_grounded_plan =
        petta_program_plan_current(program, pure_grounded);
    assert(pure_grounded_plan);
    assert(pure_grounded_plan->role == PETTA_PLAN_STATIC_CALL);
    assert(
        pure_grounded_plan->execution ==
        PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS);

    Atom *partial_grounded = parse_one(
        persistent, "(+ 1)");
    const PettaPlanNode *partial_grounded_plan =
        petta_program_plan_current(program, partial_grounded);
    assert(partial_grounded_plan);
    assert(
        partial_grounded_plan->execution ==
        PETTA_PLAN_EXEC_PURE_GROUNDED_SLOTS);

    Space execution_space;
    space_init_with_universe(&execution_space, universe);
    add_indexed_program_clause(
        program, &execution_space, persistent,
        "(= (slot-relation $value) $value)");
    Atom *relation_call = parse_one(
        persistent, "(slot-relation ready-value)");
    const PettaPlanNode *relation_plan =
        petta_program_plan_current(program, relation_call);
    assert(relation_plan);
    assert(relation_plan->role == PETTA_PLAN_STATIC_CALL);
    assert(
        relation_plan->execution ==
        PETTA_PLAN_EXEC_RELATION_SLOTS);

    Atom *active_relation_call = parse_one(
        persistent, "(slot-relation (+ 1 2))");
    const PettaPlanNode *active_relation_plan =
        petta_program_plan_current(program, active_relation_call);
    assert(active_relation_plan);
    assert(active_relation_plan->role == PETTA_PLAN_STATIC_CALL);
    assert(active_relation_plan->contains_call);
    assert(
        active_relation_plan->execution ==
        PETTA_PLAN_EXEC_GENERIC);

    PettaMachine machine;
    assert(petta_machine_init_with_plan(
        &machine, &execution_space, answers,
        ascription, ascription_plan, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    bindings_init(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(answer, ascription));
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.constructor_slot_frame_entries == 1u);
    assert(
        stats.constructor_slot_frame_direct_unifications == 1u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);

    assert(petta_machine_init_with_plan(
        &machine, &execution_space, answers,
        pure_grounded, pure_grounded_plan, NULL, NULL));
    bindings_init(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(answer, parse_one(answers, "3")));
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.pure_grounded_slot_frame_entries == 1u);
    assert(
        stats.pure_grounded_slot_frame_direct_dispatches == 1u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);

    assert(petta_machine_init_with_plan(
        &machine, &execution_space, answers,
        partial_grounded, partial_grounded_plan, NULL, NULL));
    bindings_init(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, parse_one(answers, "(partial + (1))")));
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.pure_grounded_slot_frame_entries == 0u);
    assert(
        stats.pure_grounded_slot_frame_direct_dispatches == 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);

    assert(petta_machine_init_with_plan(
        &machine, &execution_space, answers,
        relation_call, relation_plan, NULL, NULL));
    bindings_init(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, parse_one(answers, "ready-value")));
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.relation_slot_frame_entries == 1u);
    assert(stats.relation_slot_operands_reused == 1u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);

    assert(petta_machine_init_with_plan(
        &machine, &execution_space, answers,
        active_relation_call, active_relation_plan, NULL, NULL));
    bindings_init(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(answer, parse_one(answers, "3")));
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.relation_slot_frame_entries == 0u);
    assert(stats.relation_slot_operands_reused == 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);

    assert(petta_machine_init_with_plan(
        &machine, &execution_space, answers,
        active_field, active_plan, NULL, NULL));
    bindings_init(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, parse_one(answers, "(: $value 3)")));
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.constructor_slot_frame_entries == 1u);
    assert(
        stats.constructor_slot_frame_direct_unifications == 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);

    space_free(&execution_space);

    petta_program_free(program);
}

static void test_program_head_occurrence_index(
    TermUniverse *universe, Arena *persistent) {
    Space indexed_space;
    space_init_with_universe(&indexed_space, universe);
    PettaProgram *program = petta_program_new();
    assert(program);

    Atom *first = add_indexed_program_clause(
        program, &indexed_space, persistent,
        "(= (indexed-hot first) first-result)");
    enum { UNRELATED_HEADS = 128 };
    for (size_t index = 0u; index < UNRELATED_HEADS; index++) {
        char source[160];
        int written = snprintf(
            source, sizeof(source),
            "(= (indexed-cold-%zu value) cold-result-%zu)",
            index, index);
        assert(written > 0 && (size_t)written < sizeof(source));
        add_indexed_program_clause(
            program, &indexed_space, persistent, source);
    }
    Atom *wildcard = add_indexed_program_clause(
        program, &indexed_space, persistent,
        "(= ($indexed-head wildcard) wildcard-result)");
    Atom *duplicate = add_indexed_program_clause(
        program, &indexed_space, persistent,
        "(= (indexed-hot duplicate) duplicate-result)");
    add_indexed_program_clause(
        program, &indexed_space, persistent,
        "(= (indexed-hot duplicate) duplicate-result)");

    SymbolId hot = symbol_intern_cstr(g_symbols, "indexed-hot");
    SymbolId absent = symbol_intern_cstr(g_symbols, "indexed-absent");
    assert(hot != SYMBOL_ID_NONE && absent != SYMBOL_ID_NONE);
    PettaClauseCandidate *candidates = NULL;
    size_t candidate_count = 0u;
    PettaClauseSnapshotStats stats;
    assert(petta_program_clause_snapshot_profiled(
        program, &indexed_space, hot,
        &candidates, &candidate_count, &stats));
    assert(candidate_count == 4u);
    assert(candidates[0].equation == first);
    assert(candidates[1].equation == wildcard);
    assert(candidates[2].equation == duplicate);
    assert(atom_eq(candidates[3].equation, duplicate));
    assert(stats.live_occurrences_scanned == 4u);
    assert(stats.declaration_records_examined == 4u);
    assert(stats.pointer_identity_hits == 4u);
    assert(stats.structural_equality_checks == 0u);
    assert(stats.alpha_equality_checks == 0u);
    assert(stats.candidates_emitted == 4u);
    assert(stats.cache_hits == 0u);
    candidates[0].equation = wildcard;
    free(candidates);

    /* The machine-facing lease borrows one exact-revision cache identity;
     * repeated selection neither copies nor reconstructs the N-candidate
     * catalog.  Callers must release the lease before any writer runs. */
    PettaClauseSnapshotLease first_lease = {0};
    PettaClauseSnapshotLease second_lease = {0};
    assert(petta_program_clause_snapshot_lease_profiled(
        program, &indexed_space, hot, &first_lease, &stats));
    assert(first_lease.len == 4u);
    assert(first_lease.items && !first_lease.owned_items);
    assert(stats.cache_hits == 1u);
    assert(petta_program_clause_snapshot_lease_profiled(
        program, &indexed_space, hot, &second_lease, &stats));
    assert(second_lease.items == first_lease.items);
    assert(second_lease.len == first_lease.len);
    assert(!second_lease.owned_items);
    PettaClauseCandidate retained = first_lease.items[0];
    petta_program_clause_snapshot_lease_release(&second_lease);
    petta_program_clause_snapshot_lease_release(&first_lease);
    assert(retained.equation == first);

    /*
     * The same immutable revision reuses the reconciled candidate program,
     * while each caller retains an independently writable choice array.
     */
    candidates = NULL;
    candidate_count = 0u;
    assert(petta_program_clause_snapshot_profiled(
        program, &indexed_space, hot,
        &candidates, &candidate_count, &stats));
    assert(candidate_count == 4u);
    assert(candidates[0].equation == first);
    assert(candidates[1].equation == wildcard);
    assert(candidates[2].equation == duplicate);
    assert(atom_eq(candidates[3].equation, duplicate));
    assert(stats.cache_hits == 1u);
    assert(stats.live_occurrences_scanned == 0u);
    assert(stats.declaration_records_examined == 0u);
    assert(stats.structural_equality_checks == 0u);
    free(candidates);

    /*
     * Space revision remains semantic authority even when a writer bypasses
     * the private plan catalog.  The unplanned occurrence must invalidate the
     * cache, appear once, and disappear after the next direct mutation.
     */
    Atom *unregistered_source = parse_one(
        persistent,
        "(= (indexed-hot external) external-result)");
    assert(unregistered_source);
    CettaCount before_external = space_length64(&indexed_space);
    space_add(&indexed_space, unregistered_source);
    assert(space_length64(&indexed_space) == before_external + 1u);
    Atom *unregistered = space_get_at64(
        &indexed_space, before_external);
    assert(unregistered);
    candidates = NULL;
    candidate_count = 0u;
    assert(petta_program_clause_snapshot_profiled(
        program, &indexed_space, hot,
        &candidates, &candidate_count, &stats));
    assert(candidate_count == 5u);
    assert(candidates[4].equation == unregistered);
    assert(stats.cache_hits == 0u);
    assert(stats.live_occurrences_scanned == 5u);
    free(candidates);

    assert(space_remove(&indexed_space, unregistered));
    candidates = NULL;
    candidate_count = 0u;
    assert(petta_program_clause_snapshot_profiled(
        program, &indexed_space, hot,
        &candidates, &candidate_count, &stats));
    assert(candidate_count == 4u);
    assert(stats.cache_hits == 0u);
    assert(stats.live_occurrences_scanned == 4u);
    free(candidates);

    candidates = NULL;
    candidate_count = 0u;
    assert(petta_program_clause_snapshot_profiled(
        program, &indexed_space, absent,
        &candidates, &candidate_count, &stats));
    assert(candidate_count == 1u);
    assert(candidates[0].equation == wildcard);
    assert(stats.live_occurrences_scanned == 1u);
    assert(stats.declaration_records_examined == 1u);
    free(candidates);

    assert(space_remove(&indexed_space, first));
    petta_program_note_remove_one(
        program, &indexed_space, first);
    candidates = NULL;
    candidate_count = 0u;
    assert(petta_program_clause_snapshot_profiled(
        program, &indexed_space, hot,
        &candidates, &candidate_count, &stats));
    assert(candidate_count == 3u);
    assert(candidates[0].equation == wildcard);
    assert(candidates[1].equation == duplicate);
    assert(atom_eq(candidates[2].equation, duplicate));
    assert(stats.live_occurrences_scanned == 3u);
    assert(stats.declaration_records_examined == 3u);
    free(candidates);

    /*
     * A read-only snapshot must not make PettaProgram the lifetime owner of
     * an evaluator-owned temporary Space.  In particular, caching that Space
     * would leave a stale pointer after its owner releases it.
     */
    Space *transient = malloc(sizeof(*transient));
    assert(transient);
    space_init_with_universe(transient, universe);
    add_clause(
        transient, persistent,
        "(= (indexed-hot transient) transient-result)");
    candidates = NULL;
    candidate_count = 0u;
    assert(petta_program_clause_snapshot_profiled(
        program, transient, hot,
        &candidates, &candidate_count, &stats));
    assert(candidate_count == 1u);
    assert(stats.cache_hits == 0u);
    free(candidates);
    candidates = NULL;
    candidate_count = 0u;
    assert(petta_program_clause_snapshot_profiled(
        program, transient, hot,
        &candidates, &candidate_count, &stats));
    assert(candidate_count == 1u);
    assert(stats.cache_hits == 0u);
    free(candidates);
    PettaClauseSnapshotLease transient_lease = {0};
    assert(petta_program_clause_snapshot_lease_profiled(
        program, transient, hot, &transient_lease, &stats));
    assert(transient_lease.len == 1u);
    assert(transient_lease.items == transient_lease.owned_items);
    petta_program_clause_snapshot_lease_release(&transient_lease);
    space_free(transient);
    free(transient);

    Atom *plan_probe = parse_one(
        persistent, "(indexed-hot first)");
    assert(plan_probe);
    assert(petta_program_plan_current(program, plan_probe));

    petta_program_free(program);
    space_free(&indexed_space);
}

static void test_program_wide_occurrence_reconciliation(
    TermUniverse *universe, Arena *persistent) {
    enum { WIDE_CLAUSES = 2048 };
    Space wide_space;
    space_init_with_universe(&wide_space, universe);
    PettaProgram *program = petta_program_new();
    assert(program);
    Atom *first = NULL;
    Atom *middle = NULL;
    Atom *last = NULL;
    for (size_t index = 0u; index < WIDE_CLAUSES; index++) {
        char source[160];
        int written = snprintf(
            source, sizeof(source),
            "(= (wide-reconcile key-%zu) value-%zu)",
            index, index);
        assert(written > 0 && (size_t)written < sizeof(source));
        Atom *clause = add_indexed_program_clause(
            program, &wide_space, persistent, source);
        if (index == 0u)
            first = clause;
        if (index == WIDE_CLAUSES / 2u)
            middle = clause;
        if (index + 1u == WIDE_CLAUSES)
            last = clause;
    }
    Atom *duplicate = add_indexed_program_clause(
        program, &wide_space, persistent,
        "(= (wide-reconcile key-1024) value-1024)");
    assert(first && middle && last && duplicate);

    SymbolId head =
        symbol_intern_cstr(g_symbols, "wide-reconcile");
    PettaClauseCandidate *candidates = NULL;
    size_t candidate_count = 0u;
    PettaClauseSnapshotStats stats;
    assert(petta_program_clause_snapshot_profiled(
        program, &wide_space, head,
        &candidates, &candidate_count, &stats));
    assert(candidate_count == WIDE_CLAUSES + 1u);
    assert(candidates[0].equation == first);
    assert(candidates[WIDE_CLAUSES / 2u].equation == middle);
    assert(candidates[WIDE_CLAUSES - 1u].equation == last);
    assert(candidates[WIDE_CLAUSES].equation == duplicate);
    assert(stats.live_occurrences_scanned == WIDE_CLAUSES + 1u);
    assert(stats.declaration_records_examined == WIDE_CLAUSES + 1u);
    assert(stats.pointer_identity_hits == WIDE_CLAUSES + 1u);
    assert(stats.structural_equality_checks == 0u);
    assert(stats.alpha_equality_checks == 0u);
    free(candidates);

    PettaClauseSnapshotLease lease = {0};
    assert(petta_program_clause_snapshot_lease_profiled(
        program, &wide_space, head, &lease, &stats));
    assert(lease.len == WIDE_CLAUSES + 1u);
    assert(lease.items && !lease.owned_items);
    assert(stats.cache_hits == 1u);
    assert(stats.live_occurrences_scanned == 0u);
    petta_program_clause_snapshot_lease_release(&lease);

    petta_program_free(program);
    space_free(&wide_space);
}

static void test_program_analysis_sidecar_interop(
    TermUniverse *universe, Arena *persistent, Arena *scratch) {
    Space shared_space;
    space_init_with_universe(&shared_space, universe);
    PettaProgram *program = petta_program_new();
    assert(program);
    assert(!petta_program_analysis_enabled(program));

    Atom *ordinary_annotation = add_indexed_program_clause(
        program, &shared_space, persistent,
        "(: ordinary-shared-value OrdinarySharedType)");
    assert(space_length64(&shared_space) == 1u);
    assert(space_get_at64(&shared_space, 0u) == ordinary_annotation);

    Atom **types = NULL;
    assert(petta_program_declared_types(
               program, ordinary_annotation->expr.elems[1],
               scratch, &types) == 0u);
    free(types);

    assert(petta_program_enable_analysis(program));
    assert(petta_program_analysis_enabled(program));
    types = NULL;
    assert(petta_program_declared_types(
               program, ordinary_annotation->expr.elems[1],
               scratch, &types) == 1u);
    assert(atom_alpha_eq(
        types[0], parse_one(scratch, "OrdinarySharedType")));
    free(types);

    Atom *typed_annotation = add_indexed_program_clause(
        program, &shared_space, persistent,
        "(: typed-shared-value TypedSharedType)");
    assert(space_length64(&shared_space) == 2u);
    assert(space_get_at64(&shared_space, 0u) == ordinary_annotation);
    assert(space_get_at64(&shared_space, 1u) == typed_annotation);
    types = NULL;
    assert(petta_program_declared_types(
               program, typed_annotation->expr.elems[1],
               scratch, &types) == 1u);
    assert(atom_alpha_eq(
        types[0], parse_one(scratch, "TypedSharedType")));
    free(types);

    petta_program_free(program);
    space_free(&shared_space);
}

static void test_typed_data_purity_boundary(
    TermUniverse *universe, Arena *persistent) {
    Space typed_space;
    space_init_with_universe(&typed_space, universe);
    PettaProgram *program = petta_program_new();
    assert(program);

    add_compiled_program_clause(
        program, &typed_space, persistent,
        "(= (safe-typed-data $x) (: payload (id $x)))");
    add_compiled_program_clause(
        program, &typed_space, persistent,
        "(= (effectful-typed-data $state $x)"
        "   (: payload (change-state! $state $x)))");
    add_compiled_program_clause(
        program, &typed_space, persistent,
        "(= (effectful-arrow-data $state $x)"
        "   (-> (change-state! $state $x) result))");
    add_compiled_program_clause(
        program, &typed_space, persistent,
        "(= (safe-let-star $x)"
        "   (let* (((pair $left $right) (pair $x payload)))"
        "     (id $left)))");
    add_compiled_program_clause(
        program, &typed_space, persistent,
        "(= (let-star-pattern-is-data $x)"
        "   (let* (((change-state! $state $value)"
        "            (pair inert $x)))"
        "     ok))");
    add_compiled_program_clause(
        program, &typed_space, persistent,
        "(= (effectful-let-star $state $x)"
        "   (let* (($value (change-state! $state $x)))"
        "     $value))");

    assert(petta_program_relation_table_safe(
        program, &typed_space,
        symbol_intern_cstr(g_symbols, "safe-typed-data"), 1u));
    assert(!petta_program_relation_table_safe(
        program, &typed_space,
        symbol_intern_cstr(g_symbols, "effectful-typed-data"), 2u));
    assert(!petta_program_relation_table_safe(
        program, &typed_space,
        symbol_intern_cstr(g_symbols, "effectful-arrow-data"), 2u));
    assert(petta_program_relation_table_safe(
        program, &typed_space,
        symbol_intern_cstr(g_symbols, "safe-let-star"), 1u));
    assert(petta_program_relation_table_safe(
        program, &typed_space,
        symbol_intern_cstr(
            g_symbols, "let-star-pattern-is-data"), 1u));
    assert(!petta_program_relation_table_safe(
        program, &typed_space,
        symbol_intern_cstr(g_symbols, "effectful-let-star"), 2u));

    assert(petta_program_classify_resolved_call(
               program, &typed_space,
               parse_one(persistent, "(if True yes no)")) ==
           PETTA_RESOLVED_CALL_MACHINE_LOCAL);
    assert(petta_program_classify_resolved_call(
               program, &typed_space,
               parse_one(persistent, "(unknown-constructor payload)")) ==
           PETTA_RESOLVED_CALL_MACHINE_LOCAL);
    assert(petta_program_classify_resolved_call(
               program, &typed_space,
               parse_one(persistent, "(safe-typed-data payload)")) ==
           PETTA_RESOLVED_CALL_RELATION);
    assert(petta_program_classify_resolved_call(
               program, &typed_space,
               parse_one(
                   persistent,
                   "(effectful-typed-data state payload)")) ==
           PETTA_RESOLVED_CALL_UNSAFE);

    petta_program_free(program);
    space_free(&typed_space);
}

static void expect_answers(
    Space *space, Arena *arena, const char *query_source,
    const char *const *expected_sources, size_t expected_count) {
    Atom *query = parse_one(arena, query_source);
    assert(query);
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, arena, query, NULL, NULL));

    for (size_t index = 0u; index < expected_count; index++) {
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step =
            petta_machine_next(&machine, &answer, &environment);
        if (step != PETTA_MACHINE_STEP_ANSWER) {
            fprintf(stderr,
                    "PeTTa machine stopped at step %d while solving ",
                    (int)step);
            atom_print(query, stderr);
            fputc('\n', stderr);
            abort();
        }
        Atom *expected = parse_one(arena, expected_sources[index]);
        assert(expected);
        if (!atom_alpha_eq(answer, expected)) {
            fputs("unexpected PeTTa machine answer: ", stderr);
            atom_print(answer, stderr);
            fputs(" expected ", stderr);
            atom_print(expected, stderr);
            fputc('\n', stderr);
            abort();
        }
        bindings_free(&environment);
    }

    Atom *answer = NULL;
    Bindings environment;
    PettaMachineStep final_step = petta_machine_next(
        &machine, &answer, &environment);
    if (final_step != PETTA_MACHINE_STEP_EXHAUSTED) {
        fprintf(stderr,
                "PeTTa machine produced step %d after %zu expected answers for ",
                (int)final_step, expected_count);
        atom_print(query, stderr);
        if (answer) {
            fputs(": ", stderr);
            atom_print(answer, stderr);
        }
        fputc('\n', stderr);
        abort();
    }
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.answers == expected_count);
    assert(stats.transitions >= expected_count);
    petta_machine_destroy(&machine);
}

typedef struct {
    VarId ids[8];
    size_t len;
} FreeVariableProbe;

static bool free_variable_probe_visit(
    void *context, VarId var_id, SymbolId spelling,
    Atom *name_key) {
    (void)spelling;
    (void)name_key;
    FreeVariableProbe *probe = context;
    assert(probe && probe->len < 8u);
    probe->ids[probe->len++] = var_id;
    return true;
}

static bool free_variable_probe_contains(
    const FreeVariableProbe *probe, VarId var_id) {
    for (size_t index = 0u; index < probe->len; index++) {
        if (probe->ids[index] == var_id)
            return true;
    }
    return false;
}

static void test_lexical_free_variable_projection(Arena *arena) {
    Atom *let_query = parse_one(
        arena,
        "(let $local (visible-source $free) $local)");
    assert(let_query && let_query->kind == ATOM_EXPR &&
           let_query->expr.len == 4u);
    Atom *local = let_query->expr.elems[1];
    Atom *free = let_query->expr.elems[2]->expr.elems[1];
    assert(local && local->kind == ATOM_VAR &&
           free && free->kind == ATOM_VAR);
    FreeVariableProbe probe = {0};
    assert(eval_visit_lexical_free_variables(
        let_query, free_variable_probe_visit, &probe));
    assert(probe.len == 1u);
    assert(probe.ids[0] == free->var_id);
    assert(!free_variable_probe_contains(&probe, local->var_id));

    Atom *map_query = parse_one(
        arena,
        "(map-atom $items $item (+ $item $offset))");
    assert(map_query && map_query->kind == ATOM_EXPR &&
           map_query->expr.len == 4u);
    Atom *items = map_query->expr.elems[1];
    Atom *item = map_query->expr.elems[2];
    Atom *offset = map_query->expr.elems[3]->expr.elems[2];
    probe.len = 0u;
    assert(eval_visit_lexical_free_variables(
        map_query, free_variable_probe_visit, &probe));
    assert(probe.len == 2u);
    assert(probe.ids[0] == items->var_id);
    assert(probe.ids[1] == offset->var_id);
    assert(!free_variable_probe_contains(&probe, item->var_id));

    Atom *fold_query = parse_one(
        arena,
        "(foldl-atom $items $initial $acc $item"
        "  (+ $acc (+ $item $offset)))");
    assert(fold_query && fold_query->kind == ATOM_EXPR &&
           fold_query->expr.len == 6u);
    Atom *initial = fold_query->expr.elems[2];
    Atom *accumulator = fold_query->expr.elems[3];
    item = fold_query->expr.elems[4];
    offset = fold_query->expr.elems[5]->expr.elems[2]
                 ->expr.elems[2];
    probe.len = 0u;
    assert(eval_visit_lexical_free_variables(
        fold_query, free_variable_probe_visit, &probe));
    assert(probe.len == 3u);
    assert(probe.ids[0] == fold_query->expr.elems[1]->var_id);
    assert(probe.ids[1] == initial->var_id);
    assert(probe.ids[2] == offset->var_id);
    assert(!free_variable_probe_contains(
        &probe, accumulator->var_id));
    assert(!free_variable_probe_contains(&probe, item->var_id));

    Atom *lambda_query = parse_one(
        arena, "(|-> ($arg) (+ $arg $captured))");
    assert(lambda_query && lambda_query->kind == ATOM_EXPR &&
           lambda_query->expr.len == 3u);
    Atom *argument = lambda_query->expr.elems[1]->expr.elems[0];
    Atom *captured = lambda_query->expr.elems[2]->expr.elems[2];
    probe.len = 0u;
    assert(eval_visit_lexical_free_variables(
        lambda_query, free_variable_probe_visit, &probe));
    assert(probe.len == 1u);
    assert(probe.ids[0] == captured->var_id);
    assert(!free_variable_probe_contains(&probe, argument->var_id));
}

static void test_machine_query_visible_projection(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (visible-source bound) result)");
    Atom *query = parse_one(
        answers,
        "(let $local (visible-source $free) $local)");
    assert(query && query->kind == ATOM_EXPR &&
           query->expr.len == 4u);
    Atom *local = query->expr.elems[1];
    Atom *free = query->expr.elems[2]->expr.elems[1];
    assert(local && local->kind == ATOM_VAR &&
           free && free->kind == ATOM_VAR);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, parse_one(answers, "result")));
    Atom *free_value = bindings_lookup_id(
        &environment, free->var_id);
    assert(free_value && atom_alpha_eq(
        free_value, parse_one(answers, "bound")));
    assert(!bindings_lookup_id(&environment, local->var_id));
    assert(environment.len == 1u);
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);

    query = parse_one(
        answers,
        "(let $local (visible-source bound) $local)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, parse_one(answers, "result")));
    assert(environment.len == 0u);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

typedef struct {
    size_t remaining;
} TransitionPermit;

typedef struct {
    SymbolId head;
    size_t calls;
    uint32_t observed_entries;
} HostProjectionProbe;

static PettaMachineHostMode host_projection_classify(
    void *context, Space *space, Atom *expression) {
    (void)space;
    HostProjectionProbe *probe = context;
    if (!probe || !expression ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len != 2u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL ||
        expression->expr.elems[0]->sym_id != probe->head) {
        return PETTA_MACHINE_HOST_NONE;
    }
    return PETTA_MACHINE_HOST_READY_APPLICATION;
}

static bool host_projection_evaluate(
    void *context, Space *space, Arena *arena, Atom *expression,
    const Bindings *environment, OutcomeSet *outcomes) {
    (void)space;
    HostProjectionProbe *probe = context;
    if (!probe || !arena || !expression || !environment ||
        !outcomes || expression->kind != ATOM_EXPR ||
        expression->expr.len != 2u) {
        return false;
    }
    probe->calls++;
    probe->observed_entries = environment->len;
    Bindings empty;
    bindings_init(&empty);
    outcome_set_add(
        outcomes, expression->expr.elems[1], &empty);
    return true;
}

static void test_host_environment_projection(
    Space *space, Arena *arena) {
    HostProjectionProbe probe = {
        .head = symbol_intern_cstr(
            g_symbols, "host-projection-probe"),
    };
    PettaMachineHost host = {
        .context = &probe,
        .classify = host_projection_classify,
        .evaluate = host_projection_evaluate,
    };

    Atom *live = atom_var_with_id(
        arena, "live-host", fresh_var_id());
    Atom *kept = atom_symbol(arena, "kept-host-value");
    Atom *head = atom_symbol_id(arena, probe.head);
    Atom *query_items[] = {head, live};
    Atom *query = atom_expr(arena, query_items, 2u);
    assert(live && kept && head && query);

    Bindings base;
    bindings_init(&base);
    assert(bindings_add_var(&base, live, kept));
    enum { DEAD_BINDING_COUNT = 16 };
    for (size_t index = 0u;
         index < DEAD_BINDING_COUNT; index++) {
        Atom *dead = atom_var_with_id(
            arena, "dead-host", fresh_var_id());
        assert(dead);
        assert(bindings_add_var(&base, dead, kept));
    }

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, arena, query, &base, &host));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(answer, kept));
    bindings_free(&environment);
    assert(probe.calls == 1u);
    assert(probe.observed_entries == 0u);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.host_environment_entries_observed ==
           DEAD_BINDING_COUNT + 1u);
    assert(stats.host_environment_entries_forwarded == 0u);
    assert(stats.maximum_host_environment_entries_forwarded == 0u);
    assert(stats.singleton_outcome_choices_elided == 1u);
    assert(stats.choice_continuation_snapshots == 0u);
    assert(stats.choice_continuation_items_copied == 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    bindings_free(&base);
}

static bool permit_transition(void *context) {
    TransitionPermit *permit = context;
    if (!permit || permit->remaining == 0u)
        return false;
    permit->remaining--;
    return true;
}

typedef struct {
    SymbolId heads[8];
    CettaExprLen arities[8];
    size_t relation_len;
    size_t remaining;
    bool meter_transitions;
} TableProbe;

static bool table_probe_contains(
    void *context, SymbolId head, CettaExprLen arity) {
    TableProbe *probe = context;
    if (!probe)
        return false;
    for (size_t index = 0u;
         index < probe->relation_len; index++) {
        if (probe->heads[index] == head &&
            probe->arities[index] == arity) {
            return true;
        }
    }
    return false;
}

static bool table_probe_permit(void *context) {
    TableProbe *probe = context;
    if (!probe || !probe->meter_transitions)
        return true;
    if (probe->remaining == 0u)
        return false;
    probe->remaining--;
    return true;
}

static void test_ground_slg_tables(
    Space *space, Arena *persistent, Arena *answers) {
    TableProbe probe = {
        .heads = {
            symbol_intern_cstr(g_symbols, "f"),
            symbol_intern_cstr(g_symbols, "table-loop"),
            symbol_intern_cstr(g_symbols, "table-left"),
            symbol_intern_cstr(g_symbols, "table-right"),
            symbol_intern_cstr(g_symbols, "table-dag"),
        },
        .arities = {1u, 1u, 1u, 1u, 1u},
        .relation_len = 5u,
    };
    PettaMachineHost host = {
        .context = &probe,
        .permit_transition = table_probe_permit,
        .tabled_relation_contains = table_probe_contains,
    };

    Atom *query = parse_one(answers, "(f 1)");
    assert(query);
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    const char *expected[] = {"one", "uno", "uno"};
    for (size_t index = 0u; index < 3u; index++) {
        Atom *answer = NULL;
        Bindings environment;
        assert(petta_machine_next(
                   &machine, &answer, &environment) ==
               PETTA_MACHINE_STEP_ANSWER);
        assert(atom_alpha_eq(
            answer, parse_one(answers, expected[index])));
        bindings_free(&environment);
    }
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.table_lookups == 1u);
    assert(stats.table_generator_rounds == 1u);
    assert(stats.table_scc_completions == 1u);
    assert(stats.table_answer_replays == 3u);
    petta_machine_destroy(&machine);

    add_clause(
        space, persistent,
        "(= (table-alpha-pair)"
        "   (superpose ((f $left) (f $right))))");
    query = parse_one(answers, "(table-alpha-pair)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    const char *alpha_expected[] = {
        "one", "uno", "uno", "one", "uno", "uno",
    };
    for (size_t index = 0u; index < 6u; index++) {
        assert(petta_machine_next(
                   &machine, &answer, &environment) ==
               PETTA_MACHINE_STEP_ANSWER);
        assert(atom_alpha_eq(
            answer,
            parse_one(answers, alpha_expected[index])));
        bindings_free(&environment);
    }
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.table_lookups == 2u);
    assert(stats.table_hits == 1u);
    assert(stats.table_generator_rounds == 1u);
    assert(stats.table_answer_replays == 6u);
    petta_machine_destroy(&machine);

    probe.meter_transitions = true;
    probe.remaining = 2u;
    query = parse_one(answers, "(f 1)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&environment);
    probe.remaining = 1024u;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, parse_one(answers, "one")));
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    probe.meter_transitions = false;

    add_clause(
        space, persistent,
        "(= (table-loop $x) (table-loop $x))");
    query = parse_one(answers, "(table-loop a)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.table_generator_rounds == 2u);
    assert(stats.table_scc_completions == 1u);
    petta_machine_destroy(&machine);

    add_clause(
        space, persistent,
        "(= (table-left $x) (table-right $x))");
    add_clause(
        space, persistent,
        "(= (table-right $x) (table-left $x))");
    query = parse_one(answers, "(table-left a)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.table_generator_rounds >= 4u);
    assert(stats.table_scc_completions == 1u);
    petta_machine_destroy(&machine);

    add_clause(
        space, persistent,
        "(= (table-dag root)"
        "   (superpose ((table-dag left)"
        "               (table-dag right))))");
    add_clause(
        space, persistent,
        "(= (table-dag left) (table-dag leaf))");
    add_clause(
        space, persistent,
        "(= (table-dag right) (table-dag leaf))");
    add_clause(
        space, persistent,
        "(= (table-dag leaf) done)");
    query = parse_one(answers, "(table-dag root)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    for (size_t index = 0u; index < 2u; index++) {
        assert(petta_machine_next(
                   &machine, &answer, &environment) ==
               PETTA_MACHINE_STEP_ANSWER);
        assert(atom_alpha_eq(
            answer, parse_one(answers, "done")));
        bindings_free(&environment);
    }
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.table_generator_rounds == 4u);
    assert(stats.table_hits >= 1u);
    assert(stats.table_answer_replays >= 5u);
    petta_machine_destroy(&machine);

    query = parse_one(answers, "(f 1)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    bindings_free(&environment);
    add_clause(
        space, persistent, "(= (unrelated) mutation)");
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_INVALIDATED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

typedef struct {
    size_t remaining;
    size_t begins;
    size_t commits;
    size_t rollbacks;
} TransactionProbe;

static bool transaction_probe_permit(void *context) {
    TransactionProbe *probe = context;
    if (!probe || probe->remaining == 0u)
        return false;
    probe->remaining--;
    return true;
}

static bool transaction_probe_begin(
    void *context, Space *space, Arena *arena,
    void **transaction, Space **transaction_space) {
    (void)arena;
    TransactionProbe *probe = context;
    if (!probe || !space || !transaction ||
        !transaction_space) {
        return false;
    }
    probe->begins++;
    *transaction = probe;
    *transaction_space = space;
    return true;
}

static bool transaction_probe_commit(
    void *context, void *transaction) {
    TransactionProbe *probe = context;
    if (!probe || transaction != probe)
        return false;
    probe->commits++;
    return true;
}

static void transaction_probe_rollback(
    void *context, void *transaction) {
    TransactionProbe *probe = context;
    assert(probe && transaction == probe);
    probe->rollbacks++;
}

static void test_reachable_binding_projection(Arena *arena) {
    Atom *live = atom_var_with_id(
        arena, "live", fresh_var_id());
    Atom *middle = atom_var_with_id(
        arena, "middle", fresh_var_id());
    Atom *dead = atom_var_with_id(
        arena, "dead", fresh_var_id());
    Atom *kept_value = atom_symbol(arena, "kept");
    Atom *dead_value = atom_symbol(arena, "discarded");
    Atom *wrapper_items[] = {
        atom_symbol(arena, "wrapper"), middle,
    };
    Atom *wrapper = atom_expr(arena, wrapper_items, 2u);
    assert(live && middle && dead && kept_value &&
           dead_value && wrapper);

    Bindings full;
    bindings_init(&full);
    assert(bindings_add_var(&full, live, wrapper));
    assert(bindings_add_var(&full, middle, kept_value));
    assert(bindings_add_var(&full, dead, dead_value));

    Atom *roots[] = {live};
    Bindings projected;
    assert(bindings_project_reachable(
        &full, roots, 1u, &projected));
    assert(projected.len == 2u);
    assert(bindings_lookup_id(&projected, live->var_id));
    assert(bindings_lookup_id(&projected, middle->var_id));
    assert(!bindings_lookup_id(&projected, dead->var_id));
    bindings_free(&projected);

    Atom *ground_roots[] = {kept_value};
    assert(bindings_project_reachable(
        &full, ground_roots, 1u, &projected));
    assert(projected.len == 0u);
    assert(projected.eq_len == 0u);
    bindings_free(&projected);
    bindings_free(&full);

    /*
     * A closure over two variables must not copy a large unrelated logical
     * history.  The projected entries also retain their authoritative
     * relative order even when reachability discovers the later entry
     * transitively.
     */
    Bindings large;
    bindings_init(&large);
    Atom *large_live = atom_var_with_id(
        arena, "large-live", fresh_var_id());
    Atom *large_middle = atom_var_with_id(
        arena, "large-middle", fresh_var_id());
    assert(large_live && large_middle);
    assert(bindings_add_var(
        &large, large_live, large_middle));
    enum { UNRELATED_BINDING_COUNT = 256 };
    for (size_t index = 0u;
         index < UNRELATED_BINDING_COUNT; index++) {
        Atom *unrelated = atom_var_with_id(
            arena, "large-unrelated", fresh_var_id());
        assert(unrelated);
        assert(bindings_add_var(
            &large, unrelated, dead_value));
    }
    assert(bindings_add_var(
        &large, large_middle, kept_value));
    Atom *large_roots[] = {large_live};
    assert(bindings_project_reachable(
        &large, large_roots, 1u, &projected));
    assert(projected.len == 2u);
    assert(projected.entries[0].var_id ==
           large_live->var_id);
    assert(projected.entries[1].var_id ==
           large_middle->var_id);
    assert(bindings_lookup_id(
        &projected, large_live->var_id) == large_middle);
    assert(bindings_lookup_id(
        &projected, large_middle->var_id) == kept_value);
    bindings_free(&projected);
    bindings_free(&large);

    Atom *left_var = atom_var_with_id(
        arena, "left", fresh_var_id());
    Atom *right_var = atom_var_with_id(
        arena, "right", fresh_var_id());
    Atom *left_items[] = {
        atom_symbol(arena, "left-shape"), left_var,
    };
    Atom *right_items[] = {
        atom_symbol(arena, "right-shape"), right_var,
    };
    Atom *left = atom_expr(arena, left_items, 2u);
    Atom *right = atom_expr(arena, right_items, 2u);
    assert(left_var && right_var && left && right);

    Bindings constrained;
    bindings_init(&constrained);
    assert(bindings_add_constraint(&constrained, left, right));
    assert(constrained.eq_len == 1u);
    assert(bindings_add_var(
        &constrained, right_var, kept_value));
    Atom *constraint_roots[] = {left_var};
    assert(bindings_project_reachable(
        &constrained, constraint_roots, 1u, &projected));
    assert(projected.eq_len == 1u);
    assert(bindings_lookup_id(
        &projected, right_var->var_id));
    bindings_free(&projected);

    assert(bindings_project_reachable(
        &constrained, ground_roots, 1u, &projected));
    assert(projected.eq_len == 0u);
    assert(projected.len == 0u);
    bindings_free(&projected);
    bindings_free(&constrained);
}

static void test_deterministic_clause_elision(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (only-clause $x) $x)");
    Atom *query = parse_one(answers, "(only-clause token)");
    assert(query);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "token")));
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.deterministic_clause_choices_elided == 1u);
    assert(stats.clause_snapshot_candidates == 1u);
    assert(stats.clause_snapshot_candidates_copied == 1u);
    assert(stats.choice_continuation_snapshots == 0u);
    assert(stats.choice_continuation_items_copied == 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_ground_boolean_choice_elision(
    Space *space, Arena *answers) {
    Atom *query = parse_one(
        answers,
        "(if (or false true)"
        "  (if (or false true)"
        "    (if (or false true) done unreachable)"
        "    unreachable)"
        "  unreachable)");
    assert(query);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "done")));
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.maximum_choice_depth == 1u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_cons_shape_clause_index(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (shape-class ()) shape-empty)");
    add_clause(
        space, persistent,
        "(= (shape-class (cons $head $tail)) shape-nonempty)");

    /*
     * A rigid empty input proves that the cons clause is impossible.  It is
     * removed before selection, so the base clause is a WAM `trust` case
     * and no continuation prefix is retained.
     */
    Atom *empty_query = parse_one(
        answers, "(shape-class ())");
    assert(empty_query);
    PettaMachine empty_machine;
    assert(petta_machine_init(
        &empty_machine, space, answers,
        empty_query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &empty_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "shape-empty")));
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&empty_machine, &stats));
    assert(stats.clause_candidates_shape_pruned == 1u);
    assert(stats.clause_snapshot_candidates == 2u);
    assert(stats.clause_snapshot_candidates_copied == 1u);
    assert(stats.choice_continuation_snapshots == 0u);
    assert(stats.maximum_choice_continuation_trail == 0u);
    assert(petta_machine_next(
               &empty_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&empty_machine);

    /*
     * Conversely, a closed non-empty flat list cannot match the rigid empty
     * argument.  Literal indexing removes the first clause, leaving the cons
     * clause as a WAM `trust` case.
     */
    Atom *nonempty_query = parse_one(
        answers, "(shape-class (item))");
    assert(nonempty_query);
    PettaMachine nonempty_machine;
    assert(petta_machine_init(
        &nonempty_machine, space, answers,
        nonempty_query, NULL, NULL));
    assert(petta_machine_next(
               &nonempty_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "shape-nonempty")));
    bindings_free(&environment);
    assert(petta_machine_stats(&nonempty_machine, &stats));
    assert(stats.clause_candidates_shape_pruned == 1u);
    assert(stats.clause_snapshot_candidates == 2u);
    assert(stats.clause_snapshot_candidates_copied == 1u);
    assert(stats.choice_continuation_snapshots == 0u);
    assert(stats.maximum_choice_continuation_trail == 0u);
    assert(petta_machine_next(
               &nonempty_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&nonempty_machine);

    /*
     * An unbound input proves nothing.  Both declaration-ordered clauses
     * must remain observable, demonstrating that the accelerator cannot
     * specialize a relational query from its first answer.
     */
    Atom *open_query = parse_one(
        answers, "(shape-class $items)");
    assert(open_query);
    PettaMachine open_machine;
    assert(petta_machine_init(
        &open_machine, space, answers,
        open_query, NULL, NULL));
    assert(petta_machine_next(
               &open_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "shape-empty")));
    bindings_free(&environment);
    assert(petta_machine_next(
               &open_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "shape-nonempty")));
    bindings_free(&environment);
    assert(petta_machine_stats(&open_machine, &stats));
    assert(stats.clause_candidates_shape_pruned == 0u);
    assert(stats.clause_snapshot_candidates == 2u);
    assert(stats.clause_snapshot_candidates_copied == 2u);
    assert(petta_machine_next(
               &open_machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&open_machine);
}

static void test_nested_clause_shape_index(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (nested-shape (tuple marker-a $x)) wrong)");
    add_clause(
        space, persistent,
        "(= (nested-shape (tuple marker-b $x)) right)");

    Atom *query = parse_one(
        answers, "(nested-shape (tuple marker-b value))");
    assert(query);
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "right")));
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.clause_candidates_shape_pruned == 1u);
    assert(stats.clause_match_attempts == 1u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);

    /*
     * A nested relational pattern may compute a scalar.  Its expression
     * shape is therefore not comparable to the scalar before the relation
     * has run.
     */
    add_clause(
        space, persistent,
        "(= (nested-value $x) $x)");
    add_clause(
        space, persistent,
        "(= (nested-scalar-shape (nested-value $x)) $x)");
    query = parse_one(
        answers, "(nested-scalar-shape scalar-value)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "scalar-value")));
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.clause_candidates_shape_pruned == 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);

    /*
     * Candidate discovery precedes the first answer, but nested callability
     * is observed when each alternative is attempted.  A relation installed
     * between answers must therefore make the retained relational-head
     * alternative viable without rebuilding the outer clause snapshot.
     */
    add_clause(
        space, persistent,
        "(= (late-shape $anything) first)");
    add_clause(
        space, persistent,
        "(= (late-shape (late-call a)) late-ok)");
    query = parse_one(
        answers, "(late-shape (tuple marker-b value))");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "first")));
    bindings_free(&environment);

    add_clause(
        space, persistent,
        "(= (late-call a) (tuple marker-b value))");
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "late-ok")));
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.clause_candidates_shape_pruned == 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_choice_continuation_trail(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (trail-pick) (superpose (trail-a trail-b)))");
    add_clause(
        space, persistent,
        "(= (trail-accept trail-b) trail-ok)");
    Atom *query = parse_one(
        answers,
        "(chain (trail-pick) $x (trail-accept $x))");
    assert(query);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "trail-ok")));
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.choice_continuation_snapshots > 0u);
    assert(stats.choice_continuation_items_copied == 0u);
    assert(stats.choice_continuation_items_trailed > 0u);
    assert(stats.maximum_choice_continuation_trail > 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_deterministic_heap_collection(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (drain ()) done)");
    add_clause(
        space, persistent,
        "(= (drain (cons $head $tail))"
        "   (drain $tail))");

    enum {
        ITEM_COUNT = 8192,
        BOUNDED_BINDING_COLLECTION_WINDOW = 4096,
    };
    Atom **items = cetta_malloc(
        ITEM_COUNT * sizeof(*items));
    Atom *item = atom_symbol(answers, "item");
    assert(item);
    for (size_t i = 0u; i < ITEM_COUNT; i++)
        items[i] = item;
    Atom *list = atom_expr(answers, items, ITEM_COUNT);
    free(items);
    Atom *query_items[] = {
        atom_symbol(answers, "drain"), list,
    };
    Atom *query = atom_expr(answers, query_items, 2u);
    assert(list && query);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "done")));
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.deterministic_heap_collections > 0u);
    assert(stats.deterministic_minor_heap_collections > 0u);
    assert(
        stats.deterministic_heap_collection_elapsed_ns ==
        stats.deterministic_minor_heap_collection_elapsed_ns +
            stats.deterministic_major_heap_collection_elapsed_ns);
    assert(stats.deterministic_heap_bytes_promoted > 0u);
    assert(
        stats.deterministic_heap_bytes_promoted ==
        stats.deterministic_minor_heap_bytes_promoted +
            stats.deterministic_major_heap_bytes_promoted);
    assert(
        stats.deterministic_heap_bytes_promoted ==
        stats.deterministic_root_atom_bytes_promoted +
            stats.deterministic_binding_atom_bytes_promoted);
    assert(
        stats.deterministic_root_atom_bytes_promoted ==
        stats.deterministic_query_atom_bytes_promoted +
            stats.deterministic_visible_atom_bytes_promoted +
            stats.deterministic_type_atom_bytes_promoted +
            stats.deterministic_goal_atom_bytes_promoted);
    assert(
        stats.deterministic_goal_atom_bytes_promoted ==
        stats.deterministic_goal_first_bytes_promoted +
            stats.deterministic_goal_second_bytes_promoted +
            stats.deterministic_goal_third_bytes_promoted +
            stats.deterministic_goal_fourth_bytes_promoted);
    assert(stats.deterministic_heap_bytes_reclaimed > 0u);
    assert(stats.deterministic_binding_entries_discarded > 0u);
    assert(stats.maximum_nursery_live_bytes > 0u);
    assert(stats.maximum_tenured_live_bytes > 0u);
    /*
     * The two-clause relation retains one semantically live choice point per
     * input cell until its first answer.  Binding storage may therefore grow
     * linearly with ITEM_COUNT, plus at most one bounded collection window,
     * but not with the quadratic total size of all list suffixes.
     */
    assert(
        stats.maximum_binding_entries <=
        (size_t)ITEM_COUNT * 2u +
            BOUNDED_BINDING_COLLECTION_WINDOW);

    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_choice_binding_compaction(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (choose-drain $items) (drain $items))");
    add_clause(
        space, persistent,
        "(= (choose-drain $items) fallback)");

    enum {
        ITEM_COUNT = 4096,
        BOUNDED_BINDING_COLLECTION_WINDOW = 4096,
    };
    Atom **items = cetta_malloc(
        ITEM_COUNT * sizeof(*items));
    Atom *item = atom_symbol(answers, "choice-item");
    assert(item);
    for (size_t i = 0u; i < ITEM_COUNT; i++)
        items[i] = item;
    Atom *list = atom_expr(answers, items, ITEM_COUNT);
    free(items);
    Atom *query_items[] = {
        atom_symbol(answers, "choose-drain"), list,
    };
    Atom *query = atom_expr(answers, query_items, 2u);
    assert(list && query);

    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "done")));
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.choice_binding_collections > 0u);
    assert(stats.choice_binding_items_discarded > 0u);
    assert(stats.choice_trail_entries_discarded > 0u);
    assert(
        stats.maximum_binding_entries <=
        (size_t)ITEM_COUNT * 2u +
            BOUNDED_BINDING_COLLECTION_WINDOW);

    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        answer, atom_symbol(answers, "fallback")));
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_choice_heap_instant_reclaiming(
    Space *space, Arena *persistent, Arena *answers) {
    enum {
        ROW_COUNT = 512,
        VARIABLE_ROW_COUNT = 32,
        UNRELATED_COUNT = 1024,
    };
    Atom *head = atom_symbol(persistent, "heap-row");
    Atom *unrelated_head =
        atom_symbol(persistent, "unrelated-heap-row");
    assert(head);
    assert(unrelated_head);
    for (int64_t index = 0; index < ROW_COUNT; index++) {
        Atom *items[] = {
            head,
            atom_int(persistent, index),
        };
        Atom *row = atom_expr(persistent, items, 2u);
        assert(row);
        space_add(space, row);
    }
    for (int64_t index = 0; index < VARIABLE_ROW_COUNT; index++) {
        Atom *items[] = {
            head,
            atom_var(persistent, "stored-heap-value"),
        };
        Atom *row = atom_expr(persistent, items, 2u);
        assert(row);
        space_add(space, row);
    }
    for (int64_t index = 0; index < UNRELATED_COUNT; index++) {
        Atom *items[] = {
            unrelated_head,
            atom_int(persistent, index),
        };
        Atom *row = atom_expr(persistent, items, 2u);
        assert(row);
        space_add(space, row);
    }
    {
        Atom *duplicate_items[] = {
            head,
            atom_int(persistent, 7),
        };
        Atom *duplicate =
            atom_expr(persistent, duplicate_items, 2u);
        assert(duplicate);
        space_add(space, duplicate);
    }
    add_clause(
        space, persistent,
        "(not-heap-row ignored)");

    Atom *query = parse_one(
        answers,
        "(length"
        "  (collapse"
        "    (match &self (heap-row $x) $x)))");
    assert(query);
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED);
    assert(answer->ground.gkind == GV_INT);
    assert(
        answer->ground.ival ==
        ROW_COUNT + VARIABLE_ROW_COUNT + 1);
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);

    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(
        stats.count_aggregate_answers ==
        ROW_COUNT + VARIABLE_ROW_COUNT + 1u);
    assert(
        stats.match_candidates <
        (uint64_t)(ROW_COUNT + UNRELATED_COUNT));
    assert(
        stats.choice_heap_resets >=
        ROW_COUNT + VARIABLE_ROW_COUNT);
    assert(stats.choice_heap_bytes_reclaimed > 0u);
    petta_machine_destroy(&machine);

    /* A stored variable is an activation-local slot, not a request to copy
     * the whole stored row.  Every occurrence must still contribute one bag
     * answer while the epoch view standardizes only the variable it touches. */
    query = parse_one(
        answers,
        "(match &self (heap-row epoch-view-probe) epoch-view-hit)");
    assert(query);
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, NULL));
    uint64_t epoch_view_answers = 0u;
    for (;;) {
        PettaMachineStep step = petta_machine_next(
            &machine, &answer, &environment);
        if (step == PETTA_MACHINE_STEP_EXHAUSTED) {
            bindings_free(&environment);
            break;
        }
        assert(step == PETTA_MACHINE_STEP_ANSWER);
        assert(atom_alpha_eq(
            answer, atom_symbol(answers, "epoch-view-hit")));
        bindings_free(&environment);
        epoch_view_answers++;
    }
    assert(epoch_view_answers == VARIABLE_ROW_COUNT);
    assert(petta_machine_stats(&machine, &stats));
    assert(
        stats.match_candidate_epoch_views ==
        VARIABLE_ROW_COUNT);
    petta_machine_destroy(&machine);
}

static void test_terminal_match_count_fold(
    Space *space, Arena *answers) {
    enum {
        EXPECTED_ROWS = 512 + 32 + 1,
    };
    PettaPlanNode template_children[2] = {
        {.role = PETTA_PLAN_VALUE},
        {.role = PETTA_PLAN_VALUE},
    };
    PettaPlanNode template_plan = {
        .role = PETTA_PLAN_DATA,
        .child_count = 2u,
        .children = template_children,
    };
    PettaPlanNode match_children[4] = {
        {.role = PETTA_PLAN_VALUE},
        {.role = PETTA_PLAN_VALUE},
        {.role = PETTA_PLAN_VALUE},
        template_plan,
    };
    PettaPlanNode match_plan = {
        .role = PETTA_PLAN_STATIC_CALL,
        .child_count = 4u,
        .children = match_children,
    };
    PettaPlanNode collapse_children[2] = {
        {.role = PETTA_PLAN_VALUE},
        match_plan,
    };
    PettaPlanNode collapse_plan = {
        .role = PETTA_PLAN_STATIC_CALL,
        .child_count = 2u,
        .children = collapse_children,
    };
    PettaPlanNode length_children[2] = {
        {.role = PETTA_PLAN_VALUE},
        collapse_plan,
    };
    PettaPlanNode length_plan = {
        .role = PETTA_PLAN_STATIC_CALL,
        .child_count = 2u,
        .children = length_children,
    };

    Atom *query = parse_one(
        answers,
        "(length"
        "  (collapse"
        "    (match &self"
        "      (heap-row $x)"
        "      (heap-row $x))))");
    assert(query);
    PettaMachine machine;
    assert(petta_machine_init_with_plan(
        &machine, space, answers, query, &length_plan,
        NULL, NULL));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED);
    assert(answer->ground.gkind == GV_INT);
    assert(answer->ground.ival == EXPECTED_ROWS);
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.count_aggregate_match_folds == 1u);
    assert(
        stats.count_aggregate_match_answers ==
        EXPECTED_ROWS);
    assert(stats.count_aggregate_answers == EXPECTED_ROWS);
    petta_machine_destroy(&machine);

    add_clause(
        space, answers,
        "(dynamic-count-left unique-count-marker unique-count-tail)");
    add_clause(
        space, answers,
        "(dynamic-count-left unique-count-marker unique-count-tail)");
    add_clause(
        space, answers,
        "(dynamic-count-right unique-count-marker unique-count-tail)");
    add_clause(
        space, answers,
        "(dynamic-count-right other-count-marker unique-count-tail)");
    match_children[3].role = PETTA_PLAN_DYNAMIC_CALL;
    query = parse_one(
        answers,
        "(length"
        "  (collapse"
        "    (match &self"
        "      ($relation unique-count-marker unique-count-tail)"
        "      ($relation unique-count-marker unique-count-tail))))");
    assert(query);
    assert(petta_machine_init_with_plan(
        &machine, space, answers, query, &length_plan,
        NULL, NULL));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED);
    assert(answer->ground.gkind == GV_INT);
    assert(answer->ground.ival == 3);
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.count_aggregate_match_folds == 1u);
    assert(stats.count_aggregate_match_answers == 3u);
    assert(stats.count_aggregate_answers == 3u);
    petta_machine_destroy(&machine);

    PettaPlanNode empty_children[1] = {
        {.role = PETTA_PLAN_VALUE},
    };
    PettaPlanNode empty_plan = {
        .role = PETTA_PLAN_STATIC_CALL,
        .child_count = 1u,
        .children = empty_children,
    };
    match_children[3] = empty_plan;
    query = parse_one(
        answers,
        "(length"
        "  (collapse"
        "    (match &self (heap-row $x) (empty))))");
    assert(query);
    assert(petta_machine_init_with_plan(
        &machine, space, answers, query, &length_plan,
        NULL, NULL));
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED);
    assert(answer->ground.gkind == GV_INT);
    assert(answer->ground.ival == 0);
    bindings_free(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.count_aggregate_match_folds == 0u);
    assert(stats.count_aggregate_match_answers == 0u);
    petta_machine_destroy(&machine);
}

static PettaMachineBoundaryResult test_analysis_boundary_accept(
    void *context, Space *space, Atom *call,
    char *diagnostic, size_t diagnostic_size) {
    (void)context;
    (void)space;
    (void)call;
    if (diagnostic && diagnostic_size > 0u)
        diagnostic[0] = '\0';
    return PETTA_MACHINE_BOUNDARY_ACCEPTED;
}

typedef struct {
    uint64_t calls;
    bool mutate_during_first_judgment;
} TestAnalysisAuthority;

static bool test_analysis_authority_token(
    void *opaque, PettaMachineAuthorityToken *token) {
    TestAnalysisAuthority *authority = opaque;
    if (!authority || !token)
        return false;
    authority->calls++;
    uint64_t revision =
        authority->mutate_during_first_judgment &&
        authority->calls >= 3u ? 2u : 1u;
    *token = (PettaMachineAuthorityToken){
        .words = {UINT64_C(7), revision},
        .length = 2u,
    };
    return true;
}

static uint32_t test_analysis_policy_identity(void *opaque) {
    (void)opaque;
    return 0u;
}

static bool test_analysis_judge_value(
    void *opaque, Space *space, Arena *scratch,
    Atom *value, Atom *requirement,
    PettaAnalysisCallableFn callable, void *callable_context,
    PettaAnalysisResult *result) {
    (void)opaque;
    PettaTypecheckHooks hooks = {
        .context = callable_context,
        .callable = callable,
    };
    return petta_typecheck_value(
        space, scratch, value, requirement, &hooks, result);
}

static bool test_analysis_has_runtime_classifier(
    void *opaque, Space *space, Atom *requirement) {
    (void)opaque;
    return petta_typecheck_type_has_runtime_classifier(
        space, requirement);
}

static Atom *test_analysis_error_atom(
    void *opaque, Arena *arena, Atom *source,
    int exit_code, const char *diagnostic) {
    (void)opaque;
    return petta_typecheck_error_atom(
        arena, source, exit_code, diagnostic);
}

static const char *test_analysis_reason_name(
    void *opaque, PettaAnalysisReason reason) {
    (void)opaque;
    return petta_typecheck_reason_name(reason);
}

static const CettaNikDirectAuthorityV1 test_direct_authority = {
    .alias = "TEST-ANALYSIS",
    .system_id = "test.analysis",
    .authority_identity = UINT64_C(0x746573746a756467),
    .realization_identity = UINT64_C(0x74657374616e6c79),
    .authority_revision = 1u,
    .realization_abi = 1u,
};

static const PettaAnalysisService test_analysis_service = {
    .authority = &test_direct_authority,
    .capabilities = PETTA_MACHINE_ANALYSIS_TYPE_OBLIGATIONS,
    .policy_identity = test_analysis_policy_identity,
    .mutable_authority_token = test_analysis_authority_token,
    .judge_value = test_analysis_judge_value,
    .type_has_runtime_classifier =
        test_analysis_has_runtime_classifier,
    .error_atom = test_analysis_error_atom,
    .reason_name = test_analysis_reason_name,
    .validate_ready_call = test_analysis_boundary_accept,
};

static void test_direct_authority_identity_contract(void) {
    CettaNikDirectAuthorityV1 authority =
        *test_analysis_service.authority;
    assert(cetta_nik_direct_authority_v1_is_valid(&authority));

    CettaNikDirectAuthorityStampV1 stamp;
    assert(cetta_nik_direct_authority_v1_stamp(
        &authority, 3u, &stamp));
    CettaNikDirectAuthorityStampV1 same_stamp;
    assert(cetta_nik_direct_authority_v1_stamp(
        &authority, 3u, &same_stamp));
    assert(cetta_nik_direct_authority_stamp_v1_equal(
        &stamp, &same_stamp));
    same_stamp.policy_identity++;
    assert(!cetta_nik_direct_authority_stamp_v1_equal(
        &stamp, &same_stamp));

    CettaNikDirectAuthorityTokenV1 mutable = {
        .words = {11u, 12u, 13u, 14u},
        .length = 4u,
    };
    CettaNikDirectAuthorityTokenV1 token;
    assert(cetta_nik_direct_authority_v1_token(
        &authority, 3u, &mutable, &token));
    assert(token.length ==
        CETTA_NIK_DIRECT_AUTHORITY_TOKEN_BASE_WORDS + mutable.length);
    assert(token.words[0] == authority.authority_identity);
    assert(token.words[1] == authority.realization_identity);
    assert(token.words[2] ==
        (((uint64_t)authority.authority_revision << 48u) |
         ((uint64_t)authority.realization_abi << 32u) | 3u));
    assert(token.words[3] == 11u && token.words[6] == 14u);

    CettaNikDirectAuthorityTokenV1 maximal_mutable = {
        .words = {21u, 22u, 23u, 24u, 25u},
        .length = 5u,
    };
    CettaNikDirectAuthorityTokenV1 maximal_token;
    assert(cetta_nik_direct_authority_v1_token(
        &authority, 3u, &maximal_mutable, &maximal_token));
    assert(maximal_token.length == PETTA_MACHINE_AUTHORITY_WORD_CAPACITY);
    assert(maximal_token.words[7] == 25u);

    CettaNikDirectAuthorityTokenV1 same_token;
    assert(cetta_nik_direct_authority_v1_token(
        &authority, 3u, &mutable, &same_token));
    assert(cetta_nik_direct_authority_token_v1_equal(
        &token, &same_token));

    CettaNikDirectAuthorityV1 replacement = authority;
    replacement.realization_identity++;
    CettaNikDirectAuthorityTokenV1 replacement_token;
    assert(cetta_nik_direct_authority_v1_token(
        &replacement, 3u, &mutable, &replacement_token));
    assert(!cetta_nik_direct_authority_token_v1_equal(
        &token, &replacement_token));

    CettaNikDirectAuthorityV1 revised = authority;
    revised.authority_revision++;
    CettaNikDirectAuthorityTokenV1 revised_token;
    assert(cetta_nik_direct_authority_v1_token(
        &revised, 3u, &mutable, &revised_token));
    assert(!cetta_nik_direct_authority_token_v1_equal(
        &token, &revised_token));

    CettaNikDirectAuthorityTokenV1 oversized = {.length = 6u};
    CettaNikDirectAuthorityTokenV1 rejected = {
        .words = {UINT64_MAX},
        .length = 1u,
    };
    assert(!cetta_nik_direct_authority_v1_token(
        &authority, 3u, &oversized, &rejected));
    assert(rejected.length == 0u && rejected.words[0] == 0u);

    CettaNikDirectAuthorityV1 invalid = authority;
    invalid.authority_identity = 0u;
    assert(!cetta_nik_direct_authority_v1_is_valid(&invalid));
    invalid = authority;
    invalid.realization_identity = 0u;
    assert(!cetta_nik_direct_authority_v1_is_valid(&invalid));
    invalid = authority;
    invalid.authority_revision = 0u;
    assert(!cetta_nik_direct_authority_v1_is_valid(&invalid));
    invalid = authority;
    invalid.realization_abi = 0u;
    assert(!cetta_nik_direct_authority_v1_is_valid(&invalid));
    invalid = authority;
    invalid.authority_revision =
        (uint32_t)CETTA_NIK_DIRECT_AUTHORITY_REVISION_MAX + 1u;
    assert(!cetta_nik_direct_authority_v1_is_valid(&invalid));
    invalid = authority;
    invalid.realization_abi =
        (uint32_t)CETTA_NIK_DIRECT_AUTHORITY_REALIZATION_ABI_MAX + 1u;
    assert(!cetta_nik_direct_authority_v1_is_valid(&invalid));
    invalid = authority;
    invalid.alias = "";
    assert(!cetta_nik_direct_authority_v1_is_valid(&invalid));
    invalid = authority;
    invalid.system_id = NULL;
    assert(!cetta_nik_direct_authority_v1_is_valid(&invalid));
}

static void test_direct_source_binding_contract(void) {
    const CettaNikDirectSourceBindingV1 *binding =
        &petta_typecheck_v2_source_binding_v1;
    assert(cetta_nik_direct_source_binding_v1_is_valid(binding));
    assert(binding->authority ==
        &petta_typecheck_v2_direct_authority_v1);
    assert(strcmp(binding->schema_id, "finite-horn-gslt-v1") == 0);
    assert(strcmp(
        binding->presentation_id, "petta-typecheck-v2-guard") == 0);
    assert(strcmp(
        binding->semantic_scope,
        "petta.typecheck-v2.guard-core") == 0);
    assert(binding->coverage ==
        CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT);
    assert(binding->coverage !=
        CETTA_NIK_DIRECT_SOURCE_COMPLETE_PRESENTATION);

    CettaNikDirectSourceBindingV1 invalid = *binding;
    invalid.semantic_scope = "";
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid));
    invalid = *binding;
    invalid.source_sha256 = "short";
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid));
    invalid = *binding;
    invalid.package_sha256 =
        "EF8FBACD54E350DC0A25A965A457137714F06AEBBD1FD4756FFBB0838A8F84E2";
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid));
    invalid = *binding;
    invalid.coverage = (CettaNikDirectSourceCoverageV1)0;
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid));
    invalid = *binding;
    invalid.authority = NULL;
    assert(!cetta_nik_direct_source_binding_v1_is_valid(&invalid));
}

static void test_inferred_signature_authority_provenance(
    TermUniverse *universe, Arena *arena) {
    PettaProgram *program = petta_program_new();
    assert(program);
    assert(petta_program_enable_analysis(program));
    Space space;
    space_init_with_universe(&space, universe);

    CettaNikDirectAuthorityStampV1 first;
    assert(cetta_nik_direct_authority_v1_stamp(
        &test_direct_authority, 1u, &first));
    CettaNikDirectAuthorityV1 replacement_authority =
        test_direct_authority;
    replacement_authority.realization_identity++;
    CettaNikDirectAuthorityStampV1 replacement;
    assert(cetta_nik_direct_authority_v1_stamp(
        &replacement_authority, 1u, &replacement));
    CettaNikDirectAuthorityStampV1 other_policy;
    assert(cetta_nik_direct_authority_v1_stamp(
        &test_direct_authority, 2u, &other_policy));
    CettaNikDirectAuthorityV1 revised_authority =
        test_direct_authority;
    revised_authority.authority_revision++;
    CettaNikDirectAuthorityStampV1 revised;
    assert(cetta_nik_direct_authority_v1_stamp(
        &revised_authority, 1u, &revised));

    Atom *signature = parse_one(arena, "(-> Number Number)");
    Atom *head = parse_one(arena, "typed-head");
    assert(signature && head && head->kind == ATOM_SYMBOL);
    petta_program_inferred_signatures_reset_under_authority(
        program, &space, &first);
    assert(petta_program_inferred_signature_put_under_authority(
        program, &space, &first, head->sym_id, 1u, signature));
    assert(petta_program_inferred_signatures_current_under_authority(
        program, &space, &first));
    assert(!petta_program_inferred_signatures_current_under_authority(
        program, &space, &replacement));
    assert(!petta_program_inferred_signatures_current_under_authority(
        program, &space, &other_policy));
    assert(!petta_program_inferred_signatures_current_under_authority(
        program, &space, &revised));
    assert(!petta_program_inferred_signatures_current(
        program, &space));

    Atom *copy = NULL;
    assert(petta_program_inferred_signature_lookup_under_authority(
        program, &space, &first, head->sym_id, 1u, arena, &copy));
    assert(copy && atom_alpha_eq(copy, signature));
    copy = NULL;
    assert(!petta_program_inferred_signature_lookup_under_authority(
        program, &space, &replacement,
        head->sym_id, 1u, arena, &copy));
    assert(!copy);

    space_add(&space, parse_one(arena, "authority-mutation"));
    assert(!petta_program_inferred_signatures_current_under_authority(
        program, &space, &first));
    petta_program_inferred_signatures_rebase_under_authority(
        program, &space, &replacement);
    assert(!petta_program_inferred_signatures_current_under_authority(
        program, &space, &first));
    petta_program_inferred_signatures_rebase_under_authority(
        program, &space, &first);
    assert(petta_program_inferred_signatures_current_under_authority(
        program, &space, &first));

    petta_program_inferred_signatures_reset(program, &space);
    assert(petta_program_inferred_signature_put(
        program, &space, head->sym_id, 1u, signature));
    assert(petta_program_inferred_signatures_current(program, &space));
    assert(!petta_program_inferred_signatures_current_under_authority(
        program, &space, &first));

    space_free(&space);
    petta_program_free(program);
}

static void assert_typecheck_block_results_equal(
    const PettaTypecheckBlockResult *left,
    const PettaTypecheckBlockResult *right) {
    assert(left && right);
    assert(left->verdict == right->verdict);
    assert(left->fault == right->fault);
    assert(left->declarations_seen == right->declarations_seen);
    assert(left->equations_checked == right->equations_checked);
    assert(strcmp(left->diagnostic, right->diagnostic) == 0);
}

static void test_declaration_authority_parity(
    TermUniverse *universe, Arena *arena) {
    PettaProgram *legacy_program = petta_program_new();
    PettaProgram *authority_program = petta_program_new();
    PettaProgram *selected_program = petta_program_new();
    assert(legacy_program && authority_program && selected_program);
    Space legacy_space;
    Space authority_space;
    Space selected_space;
    space_init_with_universe(&legacy_space, universe);
    space_init_with_universe(&authority_space, universe);
    space_init_with_universe(&selected_space, universe);

    Atom *positive[] = {
        parse_one(arena, "(: typed-id (-> Number Number))"),
        parse_one(arena, "(= (typed-id $x) $x)"),
    };
    assert(positive[0] && positive[1]);
    PettaTypecheckBlockResult legacy_positive;
    PettaTypecheckBlockResult authority_positive;
    bool legacy_judged = petta_typecheck_declaration_block(
        legacy_program, &legacy_space, NULL,
        positive, 2u, PETTA_TYPECHECK_POLICY_DEFAULT,
        &legacy_positive);
    bool authority_judged =
        petta_typecheck_declaration_block_under_authority(
            &petta_typecheck_v2_direct_authority_v1,
            authority_program, &authority_space, NULL,
            positive, 2u, PETTA_TYPECHECK_POLICY_DEFAULT,
            &authority_positive);
    assert(legacy_judged == authority_judged);
    assert_typecheck_block_results_equal(
        &legacy_positive, &authority_positive);
    assert(authority_positive.verdict == PETTA_TYPECHECK_ESTABLISHED);

    Atom *negative[] = {
        parse_one(arena, "(: typed-bad (-> Number Number))"),
        parse_one(arena, "(= (typed-bad $x) True)"),
    };
    assert(negative[0] && negative[1]);
    PettaTypecheckBlockResult legacy_negative;
    PettaTypecheckBlockResult authority_negative;
    legacy_judged = petta_typecheck_declaration_block(
        legacy_program, &legacy_space, NULL,
        negative, 2u, PETTA_TYPECHECK_POLICY_DEFAULT,
        &legacy_negative);
    authority_judged =
        petta_typecheck_declaration_block_under_authority(
            &petta_typecheck_v2_direct_authority_v1,
            authority_program, &authority_space, NULL,
            negative, 2u, PETTA_TYPECHECK_POLICY_DEFAULT,
            &authority_negative);
    assert(legacy_judged == authority_judged);
    assert_typecheck_block_results_equal(
        &legacy_negative, &authority_negative);
    assert(authority_negative.verdict == PETTA_TYPECHECK_REFUTED);

    CettaNikDirectAuthorityV1 invalid =
        petta_typecheck_v2_direct_authority_v1;
    invalid.realization_identity = 0u;
    PettaTypecheckBlockResult invalid_result;
    assert(!petta_typecheck_declaration_block_under_authority(
        &invalid, authority_program, &authority_space, NULL,
        positive, 2u, PETTA_TYPECHECK_POLICY_DEFAULT,
        &invalid_result));
    assert(invalid_result.fault == PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT);

    /* The qualified alternative is now the production selection.  An
     * inferred fact must therefore be visible only under its exact direct
     * authority and policy, never through the unqualified legacy index. */
    Atom *selected_forms[] = {
        parse_one(arena, "(= (selected-id 1) 2)"),
    };
    assert(selected_forms[0]);
    PettaTypecheckBlockResult selected_result;
    assert(petta_typecheck_declaration_block_selected(
        selected_program, &selected_space, NULL,
        selected_forms, 1u, PETTA_TYPECHECK_POLICY_DEFAULT,
        &selected_result));
    assert(selected_result.verdict == PETTA_TYPECHECK_ESTABLISHED);
    CettaNikDirectAuthorityStampV1 selected_stamp;
    assert(cetta_nik_direct_authority_v1_stamp(
        &petta_typecheck_v2_direct_authority_v1,
        (uint32_t)PETTA_TYPECHECK_POLICY_DEFAULT,
        &selected_stamp));
    assert(petta_program_inferred_signatures_current_under_authority(
        selected_program, &selected_space, &selected_stamp));
    assert(!petta_program_inferred_signatures_current(
        selected_program, &selected_space));
    Atom *selected_head = parse_one(arena, "selected-id");
    Atom *selected_signature = NULL;
    assert(selected_head && selected_head->kind == ATOM_SYMBOL);
    assert(petta_program_inferred_signature_lookup_under_authority(
        selected_program, &selected_space, &selected_stamp,
        selected_head->sym_id, 1u, arena, &selected_signature));
    assert(selected_signature);

    space_free(&selected_space);
    space_free(&authority_space);
    space_free(&legacy_space);
    petta_program_free(selected_program);
    petta_program_free(authority_program);
    petta_program_free(legacy_program);
}

static void test_analysis_capability_contract(
    Space *space, Arena *answers) {
    Atom *query = parse_one(answers, "1");
    assert(query);
    PettaMachine machine;
    TestAnalysisAuthority authority = {0};

    PettaMachineHost callback_without_capability = {
        .context = &authority,
        .analysis = &(const PettaAnalysisService){
            .authority = &test_direct_authority,
            .capabilities = PETTA_MACHINE_ANALYSIS_NONE,
            .policy_identity = test_analysis_policy_identity,
            .mutable_authority_token = test_analysis_authority_token,
            .validate_ready_call = test_analysis_boundary_accept,
        },
    };
    assert(!petta_machine_init(
        &machine, space, answers, query, NULL,
        &callback_without_capability));

    PettaMachineHost capability_without_boundary = {
        .context = &authority,
        .analysis = &(const PettaAnalysisService){
            .authority = &test_direct_authority,
            .capabilities = PETTA_MACHINE_ANALYSIS_TYPE_OBLIGATIONS,
            .policy_identity = test_analysis_policy_identity,
            .mutable_authority_token = test_analysis_authority_token,
            .judge_value = test_analysis_judge_value,
            .type_has_runtime_classifier =
                test_analysis_has_runtime_classifier,
            .error_atom = test_analysis_error_atom,
            .reason_name = test_analysis_reason_name,
        },
    };
    assert(!petta_machine_init(
        &machine, space, answers, query, NULL,
        &capability_without_boundary));

    PettaMachineHost capability_without_authority = {
        .analysis = &(const PettaAnalysisService){
            .capabilities = PETTA_MACHINE_ANALYSIS_TYPE_OBLIGATIONS,
            .policy_identity = test_analysis_policy_identity,
            .judge_value = test_analysis_judge_value,
            .type_has_runtime_classifier =
                test_analysis_has_runtime_classifier,
            .error_atom = test_analysis_error_atom,
            .reason_name = test_analysis_reason_name,
            .validate_ready_call = test_analysis_boundary_accept,
        },
    };
    assert(!petta_machine_init(
        &machine, space, answers, query, NULL,
        &capability_without_authority));

    PettaMachineHost complete = {
        .context = &authority,
        .analysis = &test_analysis_service,
    };
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &complete));
    petta_machine_destroy(&machine);
}

static void test_analysis_authority_retry(
    Space *space, Arena *answers) {
    Atom *query = parse_one(
        answers,
        "(let $typed (the Number $x)"
        "     (let 1 $x $typed))");
    assert(query);
    TestAnalysisAuthority authority = {
        .mutate_during_first_judgment = true,
    };
    PettaMachineHost host = {
        .context = &authority,
        .analysis = &test_analysis_service,
    };
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    Atom *answer = NULL;
    Bindings environment;
    bindings_init(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(answer && answer->kind == ATOM_GROUNDED &&
           answer->ground.gkind == GV_INT &&
           answer->ground.ival == 1);
    assert(authority.calls >= 5u);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

static void test_relational_obligation_guard_gc(
    Space *space, Arena *persistent, Arena *answers) {
    add_clause(
        space, persistent,
        "(= (get-type foo) RuntimeOnly)");
    add_clause(
        space, persistent,
        "(= (guard-drain ()) done)");
    add_clause(
        space, persistent,
        "(= (guard-drain (cons $head $tail))"
        "   (guard-drain $tail))");

    enum { ITEM_COUNT = 8192 };
    Atom **items = cetta_malloc(
        ITEM_COUNT * sizeof(*items));
    Atom *item = atom_symbol(answers, "guard-item");
    assert(item);
    for (size_t index = 0u; index < ITEM_COUNT; index++)
        items[index] = item;
    Atom *list = atom_expr(answers, items, ITEM_COUNT);
    free(items);
    assert(list);

    Atom *let_head = atom_symbol(answers, "let");
    Atom *the_head = atom_symbol(answers, "the");
    Atom *drain_head = atom_symbol(answers, "guard-drain");
    Atom *runtime_only = atom_symbol(answers, "RuntimeOnly");
    Atom *foo = atom_symbol(answers, "foo");
    Atom *value = atom_var_with_id(
        answers, "guard-value", fresh_var_id());
    Atom *typed = atom_var_with_id(
        answers, "guard-typed", fresh_var_id());
    Atom *ignored = atom_var_with_id(
        answers, "guard-ignored", fresh_var_id());
    assert(let_head && the_head && drain_head && runtime_only && foo &&
           value && typed && ignored);

    Atom *ascription_items[] = {the_head, runtime_only, value};
    Atom *ascription = atom_expr(answers, ascription_items, 3u);
    Atom *drain_items[] = {drain_head, list};
    Atom *drain = atom_expr(answers, drain_items, 2u);
    Atom *after_drain_items[] = {let_head, ignored, drain, typed};
    Atom *after_drain = atom_expr(answers, after_drain_items, 4u);
    Atom *bind_items[] = {let_head, foo, value, after_drain};
    Atom *bind = atom_expr(answers, bind_items, 4u);
    Atom *query_items[] = {let_head, typed, ascription, bind};
    Atom *query = atom_expr(answers, query_items, 4u);
    assert(ascription && drain && after_drain && bind && query);

    TestAnalysisAuthority authority = {0};
    PettaMachineHost host = {
        .context = &authority,
        .analysis = &test_analysis_service,
    };
    PettaMachine machine;
    assert(petta_machine_init(
        &machine, space, answers, query, NULL, &host));
    Atom *answer = NULL;
    Bindings environment;
    bindings_init(&environment);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(answer, foo));
    bindings_free(&environment);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    assert(stats.deterministic_heap_collections > 0u);
    assert(stats.deterministic_heap_bytes_promoted > 0u);
    assert(petta_machine_next(
               &machine, &answer, &environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
}

int main(void) {
    Arena persistent;
    Arena answers;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable variables;

    arena_init(&persistent);
    arena_set_runtime_kind(
        &persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&answers);
    arena_set_runtime_kind(
        &answers, CETTA_ARENA_RUNTIME_KIND_EVAL);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    space_init_with_universe(&space, &universe);

    test_analysis_capability_contract(&space, &answers);
    test_direct_authority_identity_contract();
    test_direct_source_binding_contract();
    test_inferred_signature_authority_provenance(
        &universe, &answers);
    test_declaration_authority_parity(&universe, &answers);
    test_analysis_authority_retry(&space, &answers);
    test_relational_obligation_guard_gc(
        &space, &persistent, &answers);

    test_native_residual_typecheck(
        &space, &persistent, &answers);

    test_constructor_slot_frame_plans(
        &universe, &persistent, &answers);
    test_program_head_occurrence_index(
        &universe, &persistent);
    test_program_wide_occurrence_reconciliation(
        &universe, &persistent);
    test_program_analysis_sidecar_interop(
        &universe, &persistent, &answers);
    test_typed_data_purity_boundary(
        &universe, &persistent);
    test_deep_typecheck_source_rewrites(&universe);
    test_deep_cons_semantics(&answers);
    test_answer_materialization_boundaries(
        &space, &persistent, &answers);
    test_logical_list_cursor_boundaries(&answers);
    test_private_variant_summary(&answers);
    test_binding_prefix_factoring(&answers);
    test_activation_epoch_suffix_application(&answers);
    test_lexical_free_variable_projection(&answers);
    test_reachable_binding_projection(&answers);
    test_host_environment_projection(&space, &answers);
    test_deep_callable_detection(
        &space, &persistent, &answers);
    test_specializer_capacity_fallback(
        &space, &persistent, &answers);
    test_deep_functional_match_pattern(
        &space, &persistent, &answers);
    test_deterministic_clause_elision(
        &space, &persistent, &answers);
    test_ground_boolean_choice_elision(
        &space, &answers);
    test_cons_shape_clause_index(
        &space, &persistent, &answers);
    test_nested_clause_shape_index(
        &space, &persistent, &answers);
    test_choice_continuation_trail(
        &space, &persistent, &answers);
    test_deterministic_heap_collection(
        &space, &persistent, &answers);
    test_choice_binding_compaction(
        &space, &persistent, &answers);

    add_clause(&space, &persistent, "(= (f 1) one)");
    add_clause(&space, &persistent, "(= (f 1) uno)");
    add_clause(&space, &persistent, "(= (f 1) uno)");
    test_machine_query_visible_projection(
        &space, &persistent, &answers);
    test_ground_slg_tables(
        &space, &persistent, &answers);
    const char *ordered[] = {"one", "uno", "uno"};
    expect_answers(&space, &answers, "(f 1)", ordered, 3u);

    add_clause(
        &space, &persistent,
        "(= (second ($first $second $third)) $second)");
    const char *patterned[] = {"b"};
    expect_answers(
        &space, &answers, "(second (a b c))", patterned, 1u);

    add_clause(
        &space, &persistent,
        "(= (cons-second (cons $first (cons $second $rest)))"
        "   $second)");
    expect_answers(
        &space, &answers, "(cons-second (a b c))",
        patterned, 1u);
    expect_answers(
        &space, &answers, "(cons-second ())", NULL, 0u);

    add_clause(&space, &persistent, "(= (identity $x) $x)");
    const char *inverse[] = {"a"};
    expect_answers(
        &space, &answers,
        "(let (identity $x) a $x)", inverse, 1u);

    const char *appended[] = {"(a b c)"};
    expect_answers(
        &space, &answers, "(append (a) (b c))",
        appended, 1u);
    const char *inverse_append[] = {"(a (b c))"};
    expect_answers(
        &space, &answers,
        "(let (append ($head) $tail)"
        "     (a b c)"
        "     ($head $tail))",
        inverse_append, 1u);
    expect_answers(
        &space, &answers,
        "(let (append (a) $tail)"
        "     (b c)"
        "     $tail)",
        NULL, 0u);

    const char *sum_forward[] = {"5"};
    expect_answers(
        &space, &answers, "(#+ 2 3)", sum_forward, 1u);
    const char *sum_inverse_left[] = {"7"};
    expect_answers(
        &space, &answers,
        "(let (#+ $left 35) 42 $left)",
        sum_inverse_left, 1u);
    const char *sum_inverse_right[] = {"35"};
    expect_answers(
        &space, &answers,
        "(let (#+ 7 $right) 42 $right)",
        sum_inverse_right, 1u);
    expect_answers(
        &space, &answers,
        "(let (#+ 7 35) 41 impossible)",
        NULL, 0u);
    expect_answers(
        &space, &answers,
        "(#+ 9223372036854775807 1)",
        NULL, 0u);

    const char *equal_true_false[] = {"true", "false"};
    expect_answers(
        &space, &answers,
        "(= 1 (superpose (1 2)))",
        equal_true_false, 2u);
    const char *equal_open[] = {"true", "true"};
    expect_answers(
        &space, &answers,
        "(= $item (superpose (1 2)))",
        equal_open, 2u);
    /* The machine unifier consumes the live trail lazily.  A transitive
     * alias inside a compound must resolve identically to eager whole-term
     * substitution, while a structurally different leaf remains a negative
     * witness. */
    const char *nested_alias_true[] = {"true"};
    expect_answers(
        &space, &answers,
        "(let $x a"
        "  (let $y $x"
        "    (= (pair $x $y) (pair a a))))",
        nested_alias_true, 1u);
    const char *nested_alias_false[] = {"false"};
    expect_answers(
        &space, &answers,
        "(let $x a"
        "  (let $y $x"
        "    (= (pair $x $y) (pair a b))))",
        nested_alias_false, 1u);
    /* PeTTa's typed-data constructors evaluate authored child computations
     * while preserving fan-out and completed zero. */
    const char *arrow_value[] = {"(-> a Result)"};
    expect_answers(
        &space, &answers,
        "(-> (identity a) Result)",
        arrow_value, 1u);
    const char *arrow_fanout[] = {
        "(-> A Result)", "(-> B Result)",
    };
    expect_answers(
        &space, &answers,
        "(-> (superpose (A B)) Result)",
        arrow_fanout, 2u);
    expect_answers(
        &space, &answers,
        "(-> (empty) Result)", NULL, 0u);
    add_clause(
        &space, &persistent,
        "(= (list-product $tail)"
        "   (append (append (42) (10)) $tail))");
    const char *equal_inverse[] = {"(true (40))"};
    expect_answers(
        &space, &answers,
        "((= (42 10 40) (list-product $tail)) $tail)",
        equal_inverse, 1u);

    add_clause(
        &space, &persistent,
        "(= (pair2 $left $right) ($left $right))");
    const char *partial_pair[] = {"(partial pair2 (a))"};
    expect_answers(
        &space, &answers,
        "(pair2 a)", partial_pair, 1u);
    const char *applied_pair[] = {"(a b)"};
    expect_answers(
        &space, &answers,
        "((pair2 a) b)", applied_pair, 1u);
    const char *partial_intrinsic[] = {"(partial + (1))"};
    expect_answers(
        &space, &answers,
        "(+ 1)", partial_intrinsic, 1u);
    const char *overapplied_intrinsic[] = {
        "(Error (domain_error (function_input_arities + (2)) 3) none)",
    };
    expect_answers(
        &space, &answers,
        "(+ 1 2 3)", overapplied_intrinsic, 1u);
    add_clause(
        &space, &persistent,
        "(= (overloaded one) exact)");
    add_clause(
        &space, &persistent,
        "(= (overloaded one two) larger)");
    const char *exact_precedes_partial[] = {"exact"};
    expect_answers(
        &space, &answers,
        "(overloaded one)", exact_precedes_partial, 1u);
    add_clause(
        &space, &persistent,
        "(= (returns-add) (#+))");
    const char *extended_callable[] = {"5"};
    expect_answers(
        &space, &answers,
        "(returns-add 2 3)", extended_callable, 1u);
    add_clause(
        &space, &persistent,
        "(= (returns-data) 1)");
    const char *overapplied_data[] = {
        "(Error (domain_error "
        "(function_input_arities returns-data (0)) 1) none)",
    };
    expect_answers(
        &space, &answers,
        "(returns-data extra)", overapplied_data, 1u);
    add_clause(
        &space, &persistent,
        "(= (mixed-arity $x) one)");
    add_clause(
        &space, &persistent,
        "(= (mixed-arity $x $y $z) three)");
    const char *gap_stays_partial[] = {
        "(partial mixed-arity (a b))",
    };
    expect_answers(
        &space, &answers,
        "(mixed-arity a b)", gap_stays_partial, 1u);
    const char *mixed_overapplication[] = {
        "(Error (domain_error "
        "(function_input_arities mixed-arity (1 3)) 4) none)",
    };
    expect_answers(
        &space, &answers,
        "(mixed-arity a b c d)", mixed_overapplication, 1u);

    /* Arity is semantic data, not a machine-word bitmap.  Keep both exact
     * application and the complete overapplication diagnostic correct above
     * the usual 64- and 128-bit shortcut boundaries. */
    enum { WIDE_ARITY = 130 };
    char wide_lhs[4096];
    char wide_equation[8192];
    char wide_exact_call[4096];
    char wide_overapplied_call[4096];
    format_nary_application(
        wide_lhs, sizeof wide_lhs, "wide-arity", "$x", WIDE_ARITY);
    int wide_equation_length = snprintf(
        wide_equation, sizeof wide_equation, "(= %s wide)", wide_lhs);
    assert(wide_equation_length >= 0 &&
           (size_t)wide_equation_length < sizeof wide_equation);
    add_clause(&space, &persistent, wide_equation);
    format_nary_application(
        wide_exact_call, sizeof wide_exact_call,
        "wide-arity", "a", WIDE_ARITY);
    const char *wide_exact[] = {"wide"};
    expect_answers(
        &space, &answers, wide_exact_call, wide_exact, 1u);
    format_nary_application(
        wide_overapplied_call, sizeof wide_overapplied_call,
        "wide-arity", "a", WIDE_ARITY + 1u);
    const char *wide_overapplication[] = {
        "(Error (domain_error "
        "(function_input_arities wide-arity (130)) 131) none)",
    };
    expect_answers(
        &space, &answers, wide_overapplied_call,
        wide_overapplication, 1u);
    add_clause(
        &space, &persistent,
        "(= (select-callable special) (#+ 1))");
    const char *selected_extension[] = {"3"};
    expect_answers(
        &space, &answers,
        "(select-callable special 2)",
        selected_extension, 1u);
    expect_answers(
        &space, &answers,
        "(select-callable other 2)", NULL, 0u);
    add_clause(
        &space, &persistent,
        "(: typed-map (-> Atom %Undefined%))");
    add_clause(
        &space, &persistent,
        "(= (typed-map ($f ())) ())");
    add_clause(
        &space, &persistent,
        "(= (typed-map ($f (cons $x $xs)))"
        "   (cons ($f $x) (typed-map ($f $xs))))");
    add_clause(
        &space, &persistent,
        "(= (increment $x) (#+ $x 1))");
    const char *typed_relational_head[] = {"(2 3)"};
    expect_answers(
        &space, &answers,
        "(typed-map (increment (1 2)))",
        typed_relational_head, 1u);
    expect_answers(
        &space, &answers,
        "(typed-map (increment not-a-list))", NULL, 0u);
    const char *computed_head_data[] = {"((1 2) 3)"};
    expect_answers(
        &space, &answers,
        "((1 2) 3)", computed_head_data, 1u);

    add_clause(&space, &persistent, "(= (successor b a) True)");
    add_clause(&space, &persistent, "(= (successor c b) True)");
    add_clause(
        &space, &persistent,
        "(= (later $x $y) (successor $x $y))");
    add_clause(
        &space, &persistent,
        "(= (later $x $y)"
        "   (and (successor $x $middle)"
        "        (later $middle $y)))");
    const char *successor_truth[] = {"True"};
    expect_answers(
        &space, &answers,
        "(successor c $answer)", successor_truth, 1u);
    const char *recursive_truths[] = {"True", "true"};
    expect_answers(
        &space, &answers,
        "(later c $answer)", recursive_truths, 2u);
    const char *recursive[] = {"(True b)", "(true a)"};
    expect_answers(
        &space, &answers,
        "((later c $answer) $answer)", recursive, 2u);

    add_clause(
        &space, &persistent,
        "(= (choose) (superpose (first second)))");
    const char *committed[] = {"first"};
    expect_answers(
        &space, &answers,
        "(let $answer (choose)"
        "  (let $_ (cut) $answer))",
        committed, 1u);

    add_clause(&space, &persistent, "(a b)");
    add_clause(&space, &persistent, "(a c)");
    const char *matched[] = {"(a b)", "(a c)"};
    expect_answers(
        &space, &answers,
        "(match &self (a $x) (a $x))",
        matched, 2u);
    add_clause(
        &space, &persistent,
        "(= (first-a)"
        "   (let* (($answer (match &self (a $x) (a $x)))"
        "           ($_ (cut)))"
        "          $answer))");
    const char *matched_committed[] = {"(a b)"};
    expect_answers(
        &space, &answers, "(first-a)",
        matched_committed, 1u);

    Atom *suspended_query = parse_one(&answers, "(f 1)");
    assert(suspended_query);
    TransitionPermit permit = {.remaining = 1u};
    PettaMachineHost suspended_host = {
        .context = &permit,
        .permit_transition = permit_transition,
    };
    PettaMachine suspended;
    assert(petta_machine_init(
        &suspended, &space, &answers, suspended_query, NULL,
        &suspended_host));
    Atom *suspended_answer = NULL;
    Bindings suspended_environment;
    assert(petta_machine_next(
               &suspended, &suspended_answer,
               &suspended_environment) ==
           PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&suspended_environment);
    PettaMachineStats suspended_stats;
    assert(petta_machine_stats(&suspended, &suspended_stats));
    assert(suspended_stats.transitions == 1u);
    permit.remaining = 64u;
    assert(petta_machine_next(
               &suspended, &suspended_answer,
               &suspended_environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        suspended_answer, parse_one(&answers, "one")));
    bindings_free(&suspended_environment);
    petta_machine_destroy(&suspended);

    TransitionPermit collapse_permit = {.remaining = 2u};
    PettaMachineHost collapse_host = {
        .context = &collapse_permit,
        .permit_transition = permit_transition,
    };
    Atom *collapse_query =
        parse_one(&answers, "(collapse (f 1))");
    assert(collapse_query);
    PettaMachine collapse_machine;
    assert(petta_machine_init(
        &collapse_machine, &space, &answers,
        collapse_query, NULL, &collapse_host));
    Atom *collapse_answer = NULL;
    Bindings collapse_environment;
    assert(petta_machine_next(
               &collapse_machine, &collapse_answer,
               &collapse_environment) ==
           PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&collapse_environment);
    collapse_permit.remaining = 128u;
    assert(petta_machine_next(
               &collapse_machine, &collapse_answer,
               &collapse_environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        collapse_answer,
        parse_one(&answers, "(one uno uno)")));
    bindings_free(&collapse_environment);
    assert(petta_machine_next(
               &collapse_machine, &collapse_answer,
               &collapse_environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&collapse_environment);
    PettaMachineStats collapse_stats;
    assert(petta_machine_stats(
        &collapse_machine, &collapse_stats));
    /* The public machine owns one defensive query copy.  Its synchronous
     * collapse child borrows the immutable body from the paused parent, so a
     * suspend/resume cycle must not introduce a second query copy. */
    assert(collapse_stats.atom_copy_query_calls == 1u);
    petta_machine_destroy(&collapse_machine);

    TransactionProbe transaction_probe = {
        .remaining = 1u,
    };
    PettaMachineHost transaction_host = {
        .context = &transaction_probe,
        .permit_transition = transaction_probe_permit,
        .transaction_begin = transaction_probe_begin,
        .transaction_commit = transaction_probe_commit,
        .transaction_rollback = transaction_probe_rollback,
    };
    Atom *transaction_query =
        parse_one(&answers, "(transaction (f 1))");
    assert(transaction_query);
    PettaMachine transaction_machine;
    assert(petta_machine_init(
        &transaction_machine, &space, &answers,
        transaction_query, NULL, &transaction_host));
    Atom *transaction_answer = NULL;
    Bindings transaction_environment;
    assert(petta_machine_next(
               &transaction_machine, &transaction_answer,
               &transaction_environment) ==
           PETTA_MACHINE_STEP_SUSPENDED);
    bindings_free(&transaction_environment);
    assert(transaction_probe.begins == 1u);
    assert(transaction_probe.commits == 0u);
    assert(transaction_probe.rollbacks == 0u);
    transaction_probe.remaining = 64u;
    assert(petta_machine_next(
               &transaction_machine, &transaction_answer,
               &transaction_environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        transaction_answer, parse_one(&answers, "one")));
    bindings_free(&transaction_environment);
    assert(transaction_probe.commits == 1u);
    assert(transaction_probe.rollbacks == 0u);
    petta_machine_destroy(&transaction_machine);

    transaction_probe.remaining = 64u;
    Atom *failed_transaction_query =
        parse_one(&answers, "(transaction (empty))");
    assert(failed_transaction_query);
    assert(petta_machine_init(
        &transaction_machine, &space, &answers,
        failed_transaction_query, NULL, &transaction_host));
    assert(petta_machine_next(
               &transaction_machine, &transaction_answer,
               &transaction_environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&transaction_environment);
    assert(transaction_probe.begins == 2u);
    assert(transaction_probe.commits == 1u);
    assert(transaction_probe.rollbacks == 1u);
    petta_machine_destroy(&transaction_machine);

    Atom *mutable_query = parse_one(&answers, "(f 1)");
    assert(mutable_query);
    PettaMachine logical_view;
    assert(petta_machine_init(
        &logical_view, &space, &answers, mutable_query, NULL, NULL));
    Atom *first_answer = NULL;
    Bindings first_environment;
    assert(petta_machine_next(
               &logical_view, &first_answer, &first_environment) ==
           PETTA_MACHINE_STEP_ANSWER);
    assert(atom_alpha_eq(
        first_answer, parse_one(&answers, "one")));
    bindings_free(&first_environment);
    add_clause(&space, &persistent, "(= (f 1) after)");
    const char *remaining[] = {"uno", "uno"};
    for (size_t index = 0u; index < 2u; index++) {
        Bindings remaining_environment;
        assert(petta_machine_next(
                   &logical_view, &first_answer,
                   &remaining_environment) ==
               PETTA_MACHINE_STEP_ANSWER);
        assert(atom_alpha_eq(
            first_answer,
            parse_one(&answers, remaining[index])));
        bindings_free(&remaining_environment);
    }
    Bindings exhausted_environment;
    assert(petta_machine_next(
               &logical_view, &first_answer,
               &exhausted_environment) ==
           PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&exhausted_environment);
    petta_machine_destroy(&logical_view);

    const char *next_call[] = {"one", "uno", "uno", "after"};
    expect_answers(
        &space, &answers, "(f 1)", next_call, 4u);
    test_choice_heap_instant_reclaiming(
        &space, &persistent, &answers);
    test_terminal_match_count_fold(&space, &answers);

    space_free(&space);
    term_universe_free(&universe);
    arena_free(&answers);
    arena_free(&persistent);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    puts("PASS: explicit PeTTa search machine");
    return 0;
}
