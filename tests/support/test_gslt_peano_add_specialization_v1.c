#include "gslt_peano_add_specialization_v1.h"

#include "symbol.h"

#include <stdio.h>

static unsigned checks;
static unsigned failures;

static void expect(bool condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static Atom *application(
    Arena *arena, const char *head, Atom **arguments, uint32_t arity) {
    Atom **elements = arena_alloc(
        arena, ((size_t)arity + 1u) * sizeof(*elements));

    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, head);
    for (uint32_t index = 0u; index < arity; index++)
        elements[index + 1u] = arguments[index];
    return elements[0]
        ? atom_expr(arena, elements, (CettaExprLen)arity + 1u)
        : NULL;
}

static Atom *unary(Arena *arena, const char *head, Atom *argument) {
    Atom *arguments[] = {argument};
    return application(arena, head, arguments, 1u);
}

typedef struct {
    CettaGsltHornRuleViewV1 zero;
    CettaGsltHornRuleViewV1 successor;
} AddRulesV1;

static AddRulesV1 add_rules(
    Arena *arena, const char *relation, const char *zero,
    const char *successor, uint32_t first_step) {
    Atom *left = atom_var_with_id(arena, "left", 1u);
    Atom *right = atom_var_with_id(arena, "right", 2u);
    Atom *result = atom_var_with_id(arena, "result", 3u);
    Atom *zero_arguments[] = {
        atom_symbol(arena, zero), right, right,
    };
    Atom *successor_arguments[] = {
        unary(arena, successor, left), right,
        unary(arena, successor, result),
    };
    Atom *body_arguments[] = {left, right, result};
    Atom **body = arena_alloc(arena, sizeof(*body));

    if (body)
        body[0] = application(arena, relation, body_arguments, 3u);
    return (AddRulesV1){
        .zero = {
            .head = application(arena, relation, zero_arguments, 3u),
            .body = NULL,
            .body_len = 0u,
            .step_index = first_step,
        },
        .successor = {
            .head = application(
                arena, relation, successor_arguments, 3u),
            .body = body,
            .body_len = 1u,
            .step_index = first_step + 1u,
        },
    };
}

static Atom *numeral(
    Arena *arena, const char *zero, const char *successor, uint32_t value) {
    Atom *result = atom_symbol(arena, zero);

    while (result && value > 0u) {
        result = unary(arena, successor, result);
        value--;
    }
    return result;
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    AddRulesV1 trace_rules;
    AddRulesV1 zero_abt_rules;
    AddRulesV1 lf_rules;
    CettaGsltPeanoAddPlanV1 trace_plan;
    CettaGsltPeanoAddPlanV1 zero_abt_plan;
    CettaGsltPeanoAddPlanV1 lf_plan;
    CettaGsltPeanoAddPlanV1 rejected;
    Atom *result = NULL;
    Atom *expected;
    uint64_t attempts = 0u;
    uint64_t matches = 0u;
    uint32_t maximum_successors = 0u;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    arena_init(&arena);

    trace_rules = add_rules(
        &arena, "TraceNatAdd", "TraceZero", "TraceSucc", 41u);
    expect(cetta_gslt_peano_add_plan_recognize_v1(
               &trace_rules.zero, &trace_rules.successor, &trace_plan),
           "trace-style recursive addition is admitted");
    expect(trace_plan.admitted && trace_plan.zero_step_index == 41u &&
               trace_plan.successor_step_index == 42u,
           "trace plan retains replayable source-step identities");
    expected = numeral(&arena, "TraceZero", "TraceSucc", 5u);
    expect(cetta_gslt_peano_add_evaluate_v1(
               &arena, &trace_plan,
               numeral(&arena, "TraceZero", "TraceSucc", 2u),
               numeral(&arena, "TraceZero", "TraceSucc", 3u),
               8u, NULL, &result) == CETTA_GSLT_PEANO_ADD_OK_V1 &&
               atom_eq_fast(result, expected),
           "trace-style addition computes the admitted fold");
    cetta_gslt_peano_add_source_cost_v1(
        &trace_plan, 2u, &attempts, &matches);
    expect(attempts == 5u && matches == 3u &&
               cetta_gslt_peano_add_successor_budget_v1(
                   &trace_plan, 5u, &maximum_successors) &&
               maximum_successors == 2u,
           "base-first source attempts are preserved exactly");

    zero_abt_rules = add_rules(
        &arena, "qabt-nat-add", "q-zero", "q-succ", 7u);
    zero_abt_rules.zero.step_index = 8u;
    zero_abt_rules.successor.step_index = 7u;
    expect(cetta_gslt_peano_add_plan_recognize_v1(
               &zero_abt_rules.successor, &zero_abt_rules.zero,
               &zero_abt_plan),
           "the MeTTa Zero support-indexed ABT fold is admitted");
    expected = numeral(&arena, "q-zero", "q-succ", 4u);
    expect(cetta_gslt_peano_add_evaluate_v1(
               &arena, &zero_abt_plan,
               numeral(&arena, "q-zero", "q-succ", 3u),
               numeral(&arena, "q-zero", "q-succ", 1u),
               3u, NULL, &result) == CETTA_GSLT_PEANO_ADD_OK_V1 &&
               atom_eq_fast(result, expected),
           "the Zero ABT vocabulary uses the same compiled mechanism");
    cetta_gslt_peano_add_source_cost_v1(
        &zero_abt_plan, 3u, &attempts, &matches);
    expect(attempts == 5u && matches == 4u &&
               cetta_gslt_peano_add_successor_budget_v1(
                   &zero_abt_plan, 5u, &maximum_successors) &&
               maximum_successors == 3u,
           "recursive-first source attempts are preserved exactly");

    lf_rules = add_rules(
        &arena, "NatAdd", "Zero", "Succ", 73u);
    expect(cetta_gslt_peano_add_plan_recognize_v1(
               &lf_rules.zero, &lf_rules.successor, &lf_plan),
           "the first-order LF conversion fold is admitted");
    expected = numeral(&arena, "Zero", "Succ", 5u);
    expect(cetta_gslt_peano_add_evaluate_v1(
               &arena, &lf_plan,
               numeral(&arena, "Zero", "Succ", 1u),
               numeral(&arena, "Zero", "Succ", 4u),
               1u, NULL, &result) == CETTA_GSLT_PEANO_ADD_OK_V1 &&
               atom_eq_fast(result, expected),
           "the LF vocabulary uses the same compiled mechanism");

    expect(cetta_gslt_peano_add_evaluate_v1(
               &arena, &zero_abt_plan,
               numeral(&arena, "q-zero", "q-succ", 3u),
               numeral(&arena, "q-zero", "q-succ", 1u),
               2u, NULL, &result) == CETTA_GSLT_PEANO_ADD_RESOURCE_V1,
           "the specialization refuses an insufficient resource bound");
    expect(cetta_gslt_peano_add_evaluate_v1(
               &arena, &zero_abt_plan, atom_symbol(&arena, "NotANumeral"),
               numeral(&arena, "q-zero", "q-succ", 1u),
               8u, NULL, &result) == CETTA_GSLT_PEANO_ADD_DEFER_V1,
           "an unrecognized input defers to the ordinary rule machine");

    {
        Atom *wrong = atom_var_with_id(&arena, "wrong", 99u);
        Atom *left = atom_var_with_id(&arena, "left", 1u);
        Atom *right = atom_var_with_id(&arena, "right", 2u);
        Atom *result_var = atom_var_with_id(&arena, "result", 3u);
        Atom *successor_arguments[] = {
            unary(&arena, "TraceSucc", left), right,
            unary(&arena, "TraceSucc", result_var),
        };
        Atom *body_arguments[] = {left, wrong, result_var};
        Atom *body_term = application(
            &arena, "TraceNatAdd", body_arguments, 3u);
        Atom *body[] = {body_term};
        CettaGsltHornRuleViewV1 mutated = {
            .head = application(
                &arena, "TraceNatAdd", successor_arguments, 3u),
            .body = body,
            .body_len = 1u,
            .step_index = 42u,
        };

        expect(!cetta_gslt_peano_add_plan_recognize_v1(
                   &trace_rules.zero, &mutated, &rejected) &&
                   !rejected.admitted,
               "a recursive-coordinate mutation fails closed");
    }

    {
        CettaGsltHornRuleViewV1 duplicate_index = trace_rules.successor;

        duplicate_index.step_index = trace_rules.zero.step_index;
        expect(!cetta_gslt_peano_add_plan_recognize_v1(
                   &trace_rules.zero, &duplicate_index, &rejected) &&
                   !rejected.admitted,
               "two clauses cannot alias one canonical receipt coordinate");
    }

    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(GsltPeanoAddSpecializationV1Summary %u %u %u)\n",
           checks, checks - failures, failures);
    return failures ? 1 : 0;
}
