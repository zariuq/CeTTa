#include "gslt_peano_add_specialization_v1.h"

#include <string.h>

static bool cetta_gslt_peano_add_application_v1(
    const Atom *term, uint32_t arity, SymbolId *head_out) {
    const Atom *head;

    if (!term || term->kind != ATOM_EXPR || !term->expr.elems ||
        term->expr.len != (CettaExprLen)arity + 1u ||
        !(head = term->expr.elems[0]) || head->kind != ATOM_SYMBOL)
        return false;
    if (head_out)
        *head_out = head->sym_id;
    return true;
}

static bool cetta_gslt_peano_add_unary_v1(
    const Atom *term, SymbolId *constructor_out, const Atom **argument_out) {
    SymbolId constructor;

    if (!cetta_gslt_peano_add_application_v1(term, 1u, &constructor))
        return false;
    if (constructor_out)
        *constructor_out = constructor;
    if (argument_out)
        *argument_out = term->expr.elems[1];
    return true;
}

static bool cetta_gslt_peano_add_same_variable_v1(
    const Atom *left, const Atom *right) {
    return left && right && left->kind == ATOM_VAR &&
           right->kind == ATOM_VAR && left->var_id == right->var_id;
}

typedef struct {
    SymbolId relation;
    SymbolId zero;
    uint32_t step_index;
} CettaGsltPeanoAddZeroRuleV1;

typedef struct {
    SymbolId relation;
    SymbolId successor;
    uint32_t step_index;
} CettaGsltPeanoAddSuccessorRuleV1;

static bool cetta_gslt_peano_add_zero_rule_v1(
    const CettaGsltHornRuleViewV1 *rule,
    CettaGsltPeanoAddZeroRuleV1 *zero_out) {
    SymbolId relation;
    const Atom *zero;
    const Atom *right;
    const Atom *result;

    if (!rule || !zero_out || rule->body_len != 0u ||
        !cetta_gslt_peano_add_application_v1(rule->head, 3u, &relation))
        return false;
    zero = rule->head->expr.elems[1];
    right = rule->head->expr.elems[2];
    result = rule->head->expr.elems[3];
    if (!zero || zero->kind != ATOM_SYMBOL ||
        !cetta_gslt_peano_add_same_variable_v1(right, result))
        return false;
    *zero_out = (CettaGsltPeanoAddZeroRuleV1){
        .relation = relation,
        .zero = zero->sym_id,
        .step_index = rule->step_index,
    };
    return true;
}

static bool cetta_gslt_peano_add_successor_rule_v1(
    const CettaGsltHornRuleViewV1 *rule,
    CettaGsltPeanoAddSuccessorRuleV1 *successor_out) {
    SymbolId relation;
    SymbolId left_successor;
    SymbolId result_successor;
    SymbolId body_relation;
    const Atom *left;
    const Atom *right;
    const Atom *result;
    const Atom *body;
    const Atom *body_left;
    const Atom *body_right;
    const Atom *body_result;

    if (!rule || !successor_out || rule->body_len != 1u || !rule->body ||
        !cetta_gslt_peano_add_application_v1(rule->head, 3u, &relation) ||
        !cetta_gslt_peano_add_unary_v1(
            rule->head->expr.elems[1], &left_successor, &left) ||
        !cetta_gslt_peano_add_unary_v1(
            rule->head->expr.elems[3], &result_successor, &result) ||
        left_successor != result_successor ||
        !(right = rule->head->expr.elems[2]) || right->kind != ATOM_VAR ||
        !(body = rule->body[0]) ||
        !cetta_gslt_peano_add_application_v1(body, 3u, &body_relation) ||
        relation != body_relation)
        return false;
    body_left = body->expr.elems[1];
    body_right = body->expr.elems[2];
    body_result = body->expr.elems[3];
    if (!cetta_gslt_peano_add_same_variable_v1(left, body_left) ||
        !cetta_gslt_peano_add_same_variable_v1(right, body_right) ||
        !cetta_gslt_peano_add_same_variable_v1(result, body_result))
        return false;
    *successor_out = (CettaGsltPeanoAddSuccessorRuleV1){
        .relation = relation,
        .successor = left_successor,
        .step_index = rule->step_index,
    };
    return true;
}

void cetta_gslt_peano_add_plan_init_v1(CettaGsltPeanoAddPlanV1 *plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

bool cetta_gslt_peano_add_plan_recognize_v1(
    const CettaGsltHornRuleViewV1 *first,
    const CettaGsltHornRuleViewV1 *second,
    CettaGsltPeanoAddPlanV1 *plan_out) {
    CettaGsltPeanoAddZeroRuleV1 zero = {0};
    CettaGsltPeanoAddSuccessorRuleV1 successor = {0};
    bool ordered;

    if (!plan_out)
        return false;
    cetta_gslt_peano_add_plan_init_v1(plan_out);
    ordered = cetta_gslt_peano_add_zero_rule_v1(first, &zero) &&
              cetta_gslt_peano_add_successor_rule_v1(second, &successor);
    if (!ordered &&
        !(cetta_gslt_peano_add_zero_rule_v1(second, &zero) &&
          cetta_gslt_peano_add_successor_rule_v1(first, &successor)))
        return false;
    if (zero.relation != successor.relation ||
        zero.step_index == successor.step_index ||
        zero.zero == successor.successor)
        return false;
    *plan_out = (CettaGsltPeanoAddPlanV1){
        .relation = zero.relation,
        .zero = zero.zero,
        .successor = successor.successor,
        .zero_step_index = zero.step_index,
        .successor_step_index = successor.step_index,
        .admitted = true,
    };
    return true;
}

void cetta_gslt_peano_add_source_cost_v1(
    const CettaGsltPeanoAddPlanV1 *plan,
    uint32_t successor_count,
    uint64_t *rule_attempts_out,
    uint64_t *rule_matches_out) {
    uint64_t count = successor_count;
    uint64_t attempts = 0u;

    if (plan && plan->admitted) {
        attempts = plan->zero_step_index < plan->successor_step_index
            ? count * 2u + 1u
            : count + 2u;
    }
    if (rule_attempts_out)
        *rule_attempts_out = attempts;
    if (rule_matches_out)
        *rule_matches_out = plan && plan->admitted ? count + 1u : 0u;
}

bool cetta_gslt_peano_add_successor_budget_v1(
    const CettaGsltPeanoAddPlanV1 *plan,
    uint64_t available_rule_attempts,
    uint32_t *maximum_successors_out) {
    uint64_t maximum;

    if (maximum_successors_out)
        *maximum_successors_out = 0u;
    if (!plan || !plan->admitted || !maximum_successors_out)
        return false;
    if (plan->zero_step_index < plan->successor_step_index) {
        if (available_rule_attempts < 1u)
            return false;
        maximum = (available_rule_attempts - 1u) / 2u;
    } else {
        if (available_rule_attempts < 2u)
            return false;
        maximum = available_rule_attempts - 2u;
    }
    *maximum_successors_out = maximum < UINT32_MAX
        ? (uint32_t)maximum : UINT32_MAX;
    return true;
}

CettaGsltPeanoAddResultV1 cetta_gslt_peano_add_evaluate_v1(
    Arena *arena,
    const CettaGsltPeanoAddPlanV1 *plan,
    const Atom *left,
    Atom *right,
    uint32_t maximum_successors,
    uint32_t *successor_count_out,
    Atom **result_out) {
    const Atom *cursor = left;
    uint32_t successor_count = 0u;
    Atom *successor;
    Atom *result;

    if (result_out)
        *result_out = NULL;
    if (successor_count_out)
        *successor_count_out = 0u;
    if (!arena || !plan || !plan->admitted || !left || !right ||
        !result_out)
        return CETTA_GSLT_PEANO_ADD_INVALID_V1;
    if (atom_has_vars((Atom *)right))
        return CETTA_GSLT_PEANO_ADD_DEFER_V1;
    for (;;) {
        SymbolId constructor;
        const Atom *argument;

        if (cursor->kind == ATOM_SYMBOL && cursor->sym_id == plan->zero)
            break;
        if (!cetta_gslt_peano_add_unary_v1(
                cursor, &constructor, &argument) ||
            constructor != plan->successor || !argument)
            return CETTA_GSLT_PEANO_ADD_DEFER_V1;
        if (successor_count == maximum_successors)
            return CETTA_GSLT_PEANO_ADD_RESOURCE_V1;
        successor_count++;
        cursor = argument;
    }
    result = right;
    if (successor_count == 0u) {
        if (successor_count_out)
            *successor_count_out = 0u;
        *result_out = result;
        return CETTA_GSLT_PEANO_ADD_OK_V1;
    }
    successor = atom_symbol_id(arena, plan->successor);
    if (!successor)
        return CETTA_GSLT_PEANO_ADD_RESOURCE_V1;
    if (successor_count_out)
        *successor_count_out = successor_count;
    while (successor_count > 0u) {
        result = atom_expr2(arena, successor, result);
        if (!result)
            return CETTA_GSLT_PEANO_ADD_RESOURCE_V1;
        successor_count--;
    }
    *result_out = result;
    return CETTA_GSLT_PEANO_ADD_OK_V1;
}
