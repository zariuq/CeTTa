#include "atom.h"
#include "generated/cetta_execution_contracts.generated.h"
#include "petta_semantics.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* This descriptor-only executable supplies the builtin table read by the
 * dialect elaborator; it does not link the runtime's symbol owner. */
BuiltinSyms g_builtin_syms;

/* The head table is compiled from the spec; the test checks the compiled
 * algebra (join laws, classification) and that the heads the pull consumers
 * admit are present.  It deliberately does not pin the row count: adding a
 * relational head to the spec must regenerate cleanly, not fail here. */
static void test_operation_metadata(void) {
    const BuiltinSyms builtins = {
        .op_plus = 11u, .op_and = 12u, .intersection_atom = 13u,
        .let = 14u, .if_text = 15u, .car_atom = 16u,
    };
    CettaGsltRegisterResultKind kind = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
    CettaGsltRegisterInstruction register_op =
        CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL;
    assert(cetta_gslt_register_head_program(
        11u, 2u, &kind, &register_op, &builtins));
    assert(kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER);
    assert(register_op == CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD);
    assert(!cetta_gslt_register_head_program(
        11u, 1u, &kind, &register_op, &builtins));
    assert(register_op == CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD);
    assert(!cetta_gslt_register_head_program(
        SYMBOL_ID_NONE, 2u, NULL, NULL, &builtins));
    CettaGsltPreparedPureIntrinsicOperandDiscipline operands;
    CettaGsltPreparedPureIntrinsicInstruction intrinsic;
    assert(cetta_gslt_prepared_intrinsic_program(
        12u, 2u, &operands, &intrinsic, &builtins));
    assert(operands == CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL);
    assert(intrinsic == CETTA_GSLT_PREPARED_PURE_INTRINSIC_BOOLEAN_DISPATCH);
    assert(cetta_gslt_prepared_intrinsic_program(
        16u, 1u, NULL, &intrinsic, &builtins));
    assert(intrinsic == CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH);
    assert(!cetta_gslt_prepared_intrinsic_program(
        999u, 1u, NULL, NULL, &builtins));
    assert(cetta_gslt_prepared_intrinsic_scope(
        13u, 2u, CETTA_GSLT_INTRINSIC_DIALECT_PETTA, &builtins));
    assert(!cetta_gslt_prepared_intrinsic_scope(
        13u, 2u, CETTA_GSLT_INTRINSIC_DIALECT_HE, &builtins));
    assert(!cetta_gslt_prepared_intrinsic_scope(
        13u, 2u, CETTA_GSLT_INTRINSIC_DIALECT_PRIME, &builtins));
    /* Unrestricted scope is not evidence that an operation exists. */
    assert(cetta_gslt_prepared_intrinsic_scope(
        999u, 1u, CETTA_GSLT_INTRINSIC_DIALECT_HE, &builtins));
    assert(!cetta_gslt_prepared_intrinsic_program(
        999u, 1u, NULL, NULL, &builtins));
    CettaGsltFoldControl control;
    assert(cetta_gslt_fold_control_program(14u, 3u, &control, &builtins));
    assert(control == CETTA_GSLT_FOLD_CONTROL_BIND);
    assert(cetta_gslt_fold_control_program(15u, 3u, &control, &builtins));
    assert(control == CETTA_GSLT_FOLD_CONTROL_BRANCH);
    assert(!cetta_gslt_fold_control_program(15u, 2u, &control, &builtins));
    assert(control == CETTA_GSLT_FOLD_CONTROL_BRANCH);
    assert(!cetta_gslt_fold_control_program(15u, 3u, NULL, NULL));
}

static void test_invocation_plans(void) {
    const BuiltinSyms builtins = {.repr = 21u, .car_atom = 22u, .petta_member = 23u};
    const struct { SymbolId head; unsigned arity, operation; } cases[] = {
        {21u, 1u, CETTA_INVOCATION_HOST},
        {22u, 1u, CETTA_INVOCATION_LIST_UNARY},
        {23u, 2u, CETTA_INVOCATION_RELATIONAL_MEMBER},
    };
    for (unsigned row = 0u; row < sizeof(cases) / sizeof(*cases); row++)
    for (unsigned arity = 0u; arity <= 256u; arity++) {
        CettaInvocationPlan plan = {255u, 254u, 253u};
        uint32_t before = cetta_invocation_plan_key(plan);
        bool admitted = cetta_invocation_plan_decode(cases[row].head, arity, &plan, &builtins);
        assert(admitted == (arity == cases[row].arity));
        if (admitted) {
            assert(plan.operation == cases[row].operation && plan.first == 0u && plan.last == arity);
            assert(cetta_invocation_plan_key(plan) == (cases[row].operation | arity << 16u));
        } else {
            assert(cetta_invocation_plan_key(plan) == before);
        }
    }
    assert(!cetta_invocation_plan_decode(SYMBOL_ID_NONE, 1u, NULL, &builtins));
    assert(!cetta_invocation_plan_decode(21u, 1u, NULL, NULL));
    assert(!cetta_invocation_plan_decode(999u, 1u, NULL, &builtins));
}

static void test_call_preparation_plans(void) {
    const CettaCallPreparationStep source_order[] = {
        CETTA_CALL_PREP_INSTALL_TYPE, CETTA_CALL_PREP_COMPUTE,
        CETTA_CALL_PREP_CHECK_RETURN, CETTA_CALL_PREP_CHECK_TYPE,
        CETTA_CALL_PREP_BIND,
    };
    for (unsigned mask = 0u; mask < 256u; mask++) {
        assert(cetta_call_preparation_valid((uint8_t)mask) == (mask < 32u));
        if (mask >= 32u)
            continue;
        CettaCallPreparationStep pushed[5];
        unsigned count = 0u;
#define RECORD_STEP(step) if (mask & (step)) pushed[count++] = (step);
        CETTA_CALL_PREPARATION_REVERSE_ROWS(RECORD_STEP)
#undef RECORD_STEP
        for (unsigned index = 0u; index < 5u; index++) {
            assert(source_order[index] == (1u << index));
            if (mask & source_order[index]) {
                assert(count > 0u);
                assert(pushed[--count] == source_order[index]);
            }
        }
        assert(count == 0u);
    }
}

static void test_control_operand_plans(void) {
    const BuiltinSyms builtins = {.let = 21u, .chain = 22u, .if_text = 23u,
        .let_star = 24u, .eval = 25u, .function = 26u,
        .superpose = 27u, .empty_form = 28u, .case_text = 29u, .once = 30u};
    const unsigned operands[] = {17u, 29u, 43u};
    unsigned observations = 0u;
    for (SymbolId head = 21u; head <= 30u; head++) {
        for (unsigned arity = 0u; arity <= 5u; arity++) {
            /* Independent source grammar: a let holds pattern, producer,
             * body; chain holds producer, pattern, body. No generated row
             * is used to compute this reference. */
            unsigned operation = CETTA_CONTROL_NONE;
            unsigned source = 0u, first = 0u, last = 0u;
            if ((head == 21u || head == 22u) && arity == 3u) {
                operation = CETTA_CONTROL_SEQUENCE;
                source = head == 21u ? 29u : 17u;
                first = head == 21u ? 17u : 29u;
                last = 43u;
            } else if (head == 23u && (arity == 2u || arity == 3u)) {
                operation = CETTA_CONTROL_BRANCH;
                source = 17u; first = 29u;
                last = arity == 3u ? 43u : 0u;
            } else if (head == 24u && arity == 2u) {
                operation = CETTA_CONTROL_SEQUENCE_MANY;
                source = 17u; last = 29u;
            } else if ((head == 25u || head == 26u) && arity == 1u) {
                operation = head == 25u ? CETTA_CONTROL_FORCE : CETTA_CONTROL_RETURN_SCOPE;
                source = 17u;
            } else if (head == 27u && arity == 1u) {
                operation = CETTA_CONTROL_ORDERED_CHOICE;
                source = 17u;
            } else if (head == 28u && arity == 0u) {
                operation = CETTA_CONTROL_FAILURE;
            } else if (head == 29u && arity == 2u) {
                operation = CETTA_CONTROL_MATCH_BRANCHES;
                source = 17u; first = 29u;
            } else if (head == 30u && arity == 1u) {
                operation = CETTA_CONTROL_FIRST_SUCCESS;
                source = 17u;
            }
            CettaControlOperationPlan plan = {255u, 77u, 88u, 99u};
            uint32_t before = cetta_control_operation_key(plan);
            assert(cetta_control_operation_decode(head, arity, &plan, &builtins) ==
                (operation != CETTA_CONTROL_NONE));
            if (!operation) {
                assert(cetta_control_operation_key(plan) == before);
                continue;
            }
            assert(plan.operation == operation);
            if (operation == CETTA_CONTROL_FAILURE)
                assert(plan.source == CETTA_CONTROL_OPERAND_ABSENT);
            else
                assert(plan.source < arity && operands[plan.source] == source);
            if (plan.first == CETTA_CONTROL_OPERAND_ABSENT)
                assert(!first);
            else
                assert(plan.first < arity && operands[plan.first] == first);
            if (plan.last == CETTA_CONTROL_OPERAND_ABSENT)
                assert(!last);
            else
                assert(plan.last < arity && operands[plan.last] == last);
            CettaGsltFoldControl lowered = CETTA_GSLT_FOLD_CONTROL_BRANCH;
            bool eligible = head <= 26u && head != 22u && !(head == 23u && arity == 2u);
            assert(cetta_control_operation_lower_fold(plan, &lowered) == eligible);
            if (!eligible)
                assert(lowered == CETTA_GSLT_FOLD_CONTROL_BRANCH);
            if (operation == CETTA_CONTROL_SEQUENCE) {
                assert(cetta_control_operation_binding_layout(plan, plan.source));
                CettaSequenceOrderPlan order;
                assert(cetta_control_sequence_plan(plan, CETTA_SEQUENCE_SOURCE_FIRST, &order));
                assert(order.children[0] == plan.source && order.children[1] == plan.first &&
                       order.children[2] == plan.last);
                assert(cetta_control_sequence_plan(plan, CETTA_SEQUENCE_BINDER_FIRST, &order));
                assert(order.children[0] == plan.first && order.children[1] == plan.source &&
                       order.children[2] == plan.last);
                CettaSequenceOrderPlan saved = order;
                assert(!cetta_control_sequence_plan(plan, (CettaSequenceOrder)99, &order));
                assert(memcmp(&order, &saved, sizeof(order)) == 0);
                plan.first = plan.source;
                assert(!cetta_control_operation_binding_layout(plan, plan.source));
                assert(!cetta_control_sequence_plan(plan, CETTA_SEQUENCE_SOURCE_FIRST, &order));
                assert(memcmp(&order, &saved, sizeof(order)) == 0);
                assert(!cetta_control_operation_lower_fold(plan, &lowered));
            }
            observations++;
            if (operation == CETTA_CONTROL_MATCH_BRANCHES) {
                assert(cetta_control_operation_match_branches_layout(plan, arity));
                CettaControlOperationPlan invalid = plan;
                invalid.first = invalid.source;
                assert(!cetta_control_operation_match_branches_layout(invalid, arity));
                invalid = plan;
                invalid.source = arity;
                assert(!cetta_control_operation_match_branches_layout(invalid, arity));
                invalid = plan;
                invalid.last = 0u;
                assert(!cetta_control_operation_match_branches_layout(invalid, arity));
            }
        }
    }
    CettaControlOperationPlan force, scope;
    assert(cetta_control_operation_decode(25u, 1u, &force, &builtins));
    assert(cetta_control_operation_decode(26u, 1u, &scope, &builtins));
    assert(cetta_control_operation_key(force) != cetta_control_operation_key(scope));
    assert(!cetta_control_operation_decode(25u, UINT64_MAX, &force, &builtins));
    assert(!cetta_control_operation_decode(999u, 1u, NULL, &builtins));
    assert(!cetta_control_operation_decode(25u, 1u, NULL, NULL));
    assert(observations == 11u);
    CettaControlOperationPlan basic[] = {CETTA_CONTROL_RETURN_VALUE_PLAN,
        CETTA_CONTROL_CONSTRUCT_PLAN, CETTA_CONTROL_INVOKE_PLAN};
    for (size_t index = 0u; index < sizeof(basic) / sizeof(*basic); index++) {
        CettaControlOperationPlan operation = basic[index];
        assert(!cetta_control_operation_is_control_form(operation));
        assert(operation.source == CETTA_CONTROL_OPERAND_ABSENT &&
            operation.first == CETTA_CONTROL_OPERAND_ABSENT &&
            operation.last == CETTA_CONTROL_OPERAND_ABSENT);
        CettaGsltFoldControl untouched = CETTA_GSLT_FOLD_CONTROL_BRANCH;
        assert(!cetta_control_operation_lower_fold(operation, &untouched));
        assert(untouched == CETTA_GSLT_FOLD_CONTROL_BRANCH);
    }
    for (unsigned arity = 0u; arity <= 5u; arity++) {
        CettaControlOperationPlan commit = petta_semantics_elaborate_control(
            SYMBOL_ID_NONE, PETTA_FORM_CUT, arity);
        CettaControlOperationPlan handler = petta_semantics_elaborate_control(
            SYMBOL_ID_NONE, PETTA_FORM_CATCH, arity);
        CettaControlOperationPlan dispatch = petta_semantics_elaborate_control(
            SYMBOL_ID_NONE, PETTA_FORM_REDUCE, arity);
        assert(commit.operation == (arity == 0u
            ? CETTA_CONTROL_SCOPED_COMMIT : CETTA_CONTROL_NONE));
        assert(handler.operation == (arity == 1u
            ? CETTA_CONTROL_HANDLE : CETTA_CONTROL_NONE));
        assert(dispatch.operation == (arity == 1u
            ? CETTA_CONTROL_DISPATCH : CETTA_CONTROL_NONE));
        CettaGsltFoldControl untouched = CETTA_GSLT_FOLD_CONTROL_BRANCH;
        assert(!cetta_control_operation_lower_fold(commit, &untouched));
        assert(!cetta_control_operation_lower_fold(handler, &untouched));
        assert(!cetta_control_operation_lower_fold(dispatch, &untouched));
        assert(untouched == CETTA_GSLT_FOLD_CONTROL_BRANCH);
        if (arity == 0u)
            assert(commit.source == CETTA_CONTROL_OPERAND_ABSENT);
        if (arity == 1u)
            assert(handler.source == 0u &&
                handler.first == CETTA_CONTROL_OPERAND_ABSENT &&
                handler.last == CETTA_CONTROL_OPERAND_ABSENT);
        if (arity == 1u) {
            assert(dispatch.source == 0u &&
                dispatch.first == CETTA_CONTROL_OPERAND_ABSENT &&
                dispatch.last == CETTA_CONTROL_OPERAND_ABSENT);
            assert(cetta_control_operation_is_control_form(dispatch));
            assert(cetta_control_operation_key(dispatch) !=
                cetta_control_operation_key(force));
        }
    }
}

static void test_query_head_rows(void) {
    unsigned rows = 0u;
    bool saw_match = false, saw_get_atoms = false, saw_mork_match = false;
    bool saw_get_type = false, saw_get_type_space = false;
    bool saw_add_atom = false, saw_remove_atom = false;
#define COUNT_HEAD(head, effect) do { \
    assert((effect) != CETTA_GSLT_QUERY_EFFECT_PURE); \
    rows++; \
    if (strcmp(#head, "add_atom") == 0) \
        saw_add_atom = (effect) == CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE; \
    if (strcmp(#head, "remove_atom") == 0) \
        saw_remove_atom = (effect) == CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE; \
    if (strcmp(#head, "match") == 0) saw_match = true; \
    if (strcmp(#head, "get_atoms") == 0) saw_get_atoms = true; \
    if (strcmp(#head, "mork_match_syntax") == 0) saw_mork_match = true; \
    if (strcmp(#head, "get_type") == 0) saw_get_type = true; \
    if (strcmp(#head, "get_type_space") == 0) saw_get_type_space = true; \
} while (0);
    CETTA_GSLT_QUERY_EFFECT_HEAD_ROWS(COUNT_HEAD)
#undef COUNT_HEAD
    assert(rows >= 5u);
    assert(saw_match && saw_get_atoms && saw_mork_match);
    assert(saw_get_type && saw_get_type_space);
    assert(saw_add_atom && saw_remove_atom);

    /* Joining is union over the two flags. */
    assert(cetta_gslt_query_effect_join(
               CETTA_GSLT_QUERY_EFFECT_PURE,
               CETTA_GSLT_QUERY_EFFECT_PURE) ==
           CETTA_GSLT_QUERY_EFFECT_PURE);
    assert(cetta_gslt_query_effect_join(
               CETTA_GSLT_QUERY_EFFECT_PURE,
               CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) ==
           CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY);
    assert(cetta_gslt_query_effect_join(
               CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY,
               CETTA_GSLT_QUERY_EFFECT_PURE) ==
           CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY);
    assert(cetta_gslt_query_effect_join(
               CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY,
               CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) ==
           CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY);
    assert(cetta_gslt_query_effect_join(
               CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE,
               CETTA_GSLT_QUERY_EFFECT_PURE) ==
           CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE);
    assert(cetta_gslt_query_effect_join(
               CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY,
               CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE) ==
           (CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY |
            CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE));
    assert(cetta_gslt_query_effect_join(
               CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE,
               CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) ==
           cetta_gslt_query_effect_join(
               CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY,
               CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE));
    assert(CETTA_GSLT_USER_HEAD_EFFECT_REVISION_KEYED == 1);
    assert(CETTA_GSLT_USER_HEAD_EFFECT_LEAST_FIXED_POINT == 1);
    /* An uncertain head may do anything. */
    assert(CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD ==
           (CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY |
            CETTA_GSLT_QUERY_EFFECT_WORLD_WRITE));
    assert(CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL ==
           CETTA_GSLT_QUERY_EFFECT_PURE);
}

static void test_row_dispositions(void) {
    assert(cetta_gslt_classify_streamed_row(false, false) ==
           CETTA_GSLT_ROW_TRANSPORT_FAULT);
    assert(cetta_gslt_classify_streamed_row(false, true) ==
           CETTA_GSLT_ROW_TRANSPORT_FAULT);
    assert(cetta_gslt_classify_streamed_row(true, true) ==
           CETTA_GSLT_ROW_ADDITIVE_ZERO);
    assert(cetta_gslt_classify_streamed_row(true, false) ==
           CETTA_GSLT_ROW_CONTRIBUTE);
}

static void test_observation_and_lifetime(void) {
    assert(!cetta_gslt_observation_visible(
        CETTA_GSLT_OBSERVATION_MATCH_CHAIN_PROGRESS, false));
    assert(cetta_gslt_observation_visible(
        CETTA_GSLT_OBSERVATION_MATCH_CHAIN_PROGRESS, true));
    assert(!cetta_gslt_observation_visible(
        CETTA_GSLT_OBSERVATION_DETERMINATE_RULE_USE, false));
    assert(cetta_gslt_observation_visible(
        CETTA_GSLT_OBSERVATION_DETERMINATE_RULE_USE, true));
    assert(!cetta_gslt_lifetime_invalidates_allocations(
        CETTA_GSLT_LIFETIME_INITIALIZE));
    assert(!cetta_gslt_lifetime_invalidates_allocations(
        CETTA_GSLT_LIFETIME_CLEANUP));
    assert(!cetta_gslt_lifetime_invalidates_allocations(
        CETTA_GSLT_LIFETIME_ARENA_REUSE));
    assert(cetta_gslt_lifetime_invalidates_allocations(
        CETTA_GSLT_LIFETIME_ARENA_RESET));
    assert(cetta_gslt_lifetime_invalidates_allocations(
        CETTA_GSLT_LIFETIME_ARENA_FREE));
}

static void test_prepared_equation_admission(void) {
    const uint32_t plan =
        CETTA_GSLT_EVIDENCE_SINGLETON_HEAD |
        CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS |
        CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS |
        CETTA_GSLT_EVIDENCE_REVISION_CURRENT |
        CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED;
    assert(CETTA_GSLT_PREPARED_EQUATION_MAX_REGISTERS == 16u);
    assert(cetta_gslt_prepared_equation_plan_admitted(plan));
    assert(!cetta_gslt_prepared_equation_plan_admitted(
        plan & ~CETTA_GSLT_EVIDENCE_SINGLETON_HEAD));
    assert(!cetta_gslt_prepared_equation_plan_admitted(
        plan & ~CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS));
    assert(!cetta_gslt_prepared_equation_plan_admitted(
        plan & ~CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS));
    assert(!cetta_gslt_prepared_equation_plan_admitted(
        plan & ~CETTA_GSLT_EVIDENCE_REVISION_CURRENT));
    assert(!cetta_gslt_prepared_equation_plan_admitted(
        plan & ~CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED));
    assert(!cetta_gslt_prepared_equation_call_admitted(plan));
    assert(cetta_gslt_prepared_equation_call_admitted(
        plan | CETTA_GSLT_EVIDENCE_GROUND_CALL));
    const uint32_t register_step =
        plan | CETTA_GSLT_EVIDENCE_GROUND_CALL |
        CETTA_GSLT_EVIDENCE_REGISTER_GUARD |
        CETTA_GSLT_EVIDENCE_REGISTER_BASE |
        CETTA_GSLT_EVIDENCE_REGISTER_TAIL;
    assert(cetta_gslt_prepared_register_step_admitted(register_step));
    assert(!cetta_gslt_prepared_register_step_admitted(
        register_step & ~CETTA_GSLT_EVIDENCE_REGISTER_GUARD));
    assert(!cetta_gslt_prepared_register_step_admitted(
        register_step & ~CETTA_GSLT_EVIDENCE_REGISTER_BASE));
    assert(!cetta_gslt_prepared_register_step_admitted(
        register_step & ~CETTA_GSLT_EVIDENCE_REGISTER_TAIL));
}

static void test_total_integer_heads(void) {
    unsigned rows = 0u;
    bool saw_plus = false, saw_minus = false, saw_mul = false;
#define COUNT_TOTAL_INTEGER_HEAD(field, arity) do { \
    assert((arity) == 2u); \
    rows++; \
    if (strcmp(#field, "op_plus") == 0) saw_plus = true; \
    if (strcmp(#field, "op_minus") == 0) saw_minus = true; \
    if (strcmp(#field, "op_mul") == 0) saw_mul = true; \
} while (0);
    CETTA_GSLT_TOTAL_INTEGER_HEAD_ROWS(COUNT_TOTAL_INTEGER_HEAD)
#undef COUNT_TOTAL_INTEGER_HEAD
    assert(rows == 3u);
    assert(saw_plus && saw_minus && saw_mul);
}

static void test_generated_value_allocation(void) {
    unsigned rows = 0u;
    bool saw_int = false, saw_float = false;
    bool saw_bigint = false, saw_rational = false;
#define COUNT_ARENA_OWNED_GROUNDED(kind) do { \
    rows++; \
    if (strcmp(#kind, "GV_INT") == 0) saw_int = true; \
    if (strcmp(#kind, "GV_FLOAT") == 0) saw_float = true; \
    if (strcmp(#kind, "GV_BIGINT") == 0) saw_bigint = true; \
    if (strcmp(#kind, "GV_RATIONAL") == 0) saw_rational = true; \
} while (0);
    CETTA_GSLT_ARENA_OWNED_GROUNDED_KIND_ROWS(
        COUNT_ARENA_OWNED_GROUNDED)
#undef COUNT_ARENA_OWNED_GROUNDED
    assert(rows == 4u);
    assert(saw_int && saw_float && saw_bigint && saw_rational);
}

static void test_generated_resource_classification(void) {
    unsigned grounded_rows = 0u;
    bool saw_space = false, saw_state = false;
    bool saw_capture = false, saw_foreign = false, saw_bindings = false;
#define COUNT_IDENTITY_GROUNDED(kind) do { \
    grounded_rows++; \
    if (strcmp(#kind, "GV_SPACE") == 0) saw_space = true; \
    if (strcmp(#kind, "GV_STATE") == 0) saw_state = true; \
    if (strcmp(#kind, "GV_CAPTURE") == 0) saw_capture = true; \
    if (strcmp(#kind, "GV_FOREIGN") == 0) saw_foreign = true; \
    if (strcmp(#kind, "GV_BINDINGS") == 0) saw_bindings = true; \
} while (0);
    CETTA_GSLT_IDENTITY_BEARING_GROUNDED_KIND_ROWS(
        COUNT_IDENTITY_GROUNDED)
#undef COUNT_IDENTITY_GROUNDED
    assert(grounded_rows == 5u);
    assert(saw_space && saw_state && saw_capture && saw_foreign &&
           saw_bindings);
    assert(cetta_gslt_identity_bearing_grounded_kind(GV_SPACE));
    assert(cetta_gslt_identity_bearing_grounded_kind(GV_STATE));
    assert(cetta_gslt_identity_bearing_grounded_kind(GV_CAPTURE));
    assert(cetta_gslt_identity_bearing_grounded_kind(GV_FOREIGN));
    assert(cetta_gslt_identity_bearing_grounded_kind(GV_BINDINGS));
    assert(!cetta_gslt_identity_bearing_grounded_kind(GV_INT));
    assert(!cetta_gslt_identity_bearing_grounded_kind(GV_PRIME_CONTEXT));

    BuiltinSyms builtins = {0};
    builtins.capture = 17u;
    assert(cetta_gslt_thread_local_resource_symbol(17u, &builtins));
    assert(!cetta_gslt_thread_local_resource_symbol(18u, &builtins));
    assert(!cetta_gslt_thread_local_resource_symbol(17u, NULL));
}

static void test_register_heads(void) {
    unsigned rows = 0u;
    bool saw_plus = false, saw_minus = false;
    bool saw_mul = false, saw_eq = false;
#define COUNT_REGISTER_HEAD(field, arity, result_kind, instruction) do { \
    assert((arity) == 2u); \
    rows++; \
    if (strcmp(#field, "op_plus") == 0) { \
        saw_plus = true; \
        assert((result_kind) == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER); \
        assert((instruction) == CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD); \
    } \
    if (strcmp(#field, "op_minus") == 0) { \
        saw_minus = true; \
        assert((result_kind) == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER); \
        assert((instruction) == CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT); \
    } \
    if (strcmp(#field, "op_mul") == 0) { \
        saw_mul = true; \
        assert((result_kind) == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER); \
        assert((instruction) == CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY); \
    } \
    if (strcmp(#field, "op_eq") == 0) { \
        saw_eq = true; \
        assert((result_kind) == CETTA_GSLT_REGISTER_RESULT_BOOLEAN); \
        assert((instruction) == CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL); \
    } \
} while (0);
    CETTA_GSLT_REGISTER_HEAD_ROWS(COUNT_REGISTER_HEAD)
#undef COUNT_REGISTER_HEAD
    assert(rows == 9u);
    assert(saw_plus && saw_minus && saw_mul && saw_eq);
}

static void test_generated_register_program(void) {
    CettaGsltRegisterOperandDiscipline discipline;
    assert(cetta_gslt_register_operand_discipline(
        CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL, &discipline));
    assert(discipline ==
           CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS);
    assert(cetta_gslt_register_operand_discipline(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD, &discipline));
    assert(discipline ==
           CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS);
    assert(cetta_gslt_register_operand_discipline(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER, &discipline));
    assert(discipline ==
           CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS);
    assert(cetta_gslt_register_operand_discipline(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO, &discipline));
    assert(discipline ==
           CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS);
    int64_t small_integer = 0;
    bool small_boolean = false;
    bool promote = false;
    CettaGsltRegisterResultKind small_kind =
        CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
    assert(cetta_gslt_register_execute_small_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER,
        &small_integer, &small_boolean, -17, 5,
        &small_kind, &promote));
    assert(!promote);
    assert(small_kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER);
    assert(small_integer == -2);
    assert(cetta_gslt_register_execute_small_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO,
        &small_integer, &small_boolean, -17, 5,
        &small_kind, &promote));
    assert(small_integer == 3);
    assert(cetta_gslt_register_execute_small_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO,
        &small_integer, &small_boolean, 17, -5,
        &small_kind, &promote));
    assert(small_integer == -3);
    assert(cetta_gslt_register_execute_small_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER,
        &small_integer, &small_boolean, INT64_MIN, -1,
        &small_kind, &promote));
    assert(small_integer == 0);
    assert(!cetta_gslt_register_execute_small_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER,
        &small_integer, &small_boolean, 17, 0,
        &small_kind, &promote));
    assert(!cetta_gslt_register_execute_small_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO,
        &small_integer, &small_boolean, 17, 0,
        &small_kind, &promote));
#if CETTA_BUILD_WITH_GMP
    mpz_t left;
    mpz_t right;
    mpz_t output;
    mpz_init_set_si(left, 7);
    mpz_init_set_si(right, 5);
    mpz_init(output);
    bool boolean = false;
    CettaGsltRegisterResultKind kind =
        CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
    assert(cetta_gslt_register_execute_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD,
        output, &boolean, left, right, &kind));
    assert(kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER);
    assert(mpz_cmp_si(output, 12) == 0);
    assert(cetta_gslt_register_execute_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER,
        output, &boolean, left, right, &kind));
    assert(kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN);
    assert(boolean);
    mpz_set_si(left, -17);
    mpz_set_si(right, 5);
    assert(cetta_gslt_register_execute_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER,
        output, &boolean, left, right, &kind));
    assert(kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER);
    assert(mpz_cmp_si(output, -2) == 0);
    assert(cetta_gslt_register_execute_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO,
        output, &boolean, left, right, &kind));
    assert(mpz_cmp_si(output, 3) == 0);
    mpz_set_ui(right, 0u);
    assert(!cetta_gslt_register_execute_binary(
        CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER,
        output, &boolean, left, right, &kind));
    mpz_clear(output);
    mpz_clear(right);
    mpz_clear(left);
#endif
}

static void test_generated_fold_control_program(void) {
    unsigned rows = 0u;
    bool saw_bind = false;
    bool saw_branch = false;
    bool saw_eval = false;
    bool saw_bind_sequence = false;
#define COUNT_FOLD_CONTROL(field, arity, control) do { \
    rows++; \
    if (strcmp(#field, "let") == 0) { \
        assert((arity) == 3u); \
        assert((control) == CETTA_GSLT_FOLD_CONTROL_BIND); \
        saw_bind = true; \
    } \
    if (strcmp(#field, "if_text") == 0) { \
        assert((arity) == 3u); \
        assert((control) == CETTA_GSLT_FOLD_CONTROL_BRANCH); \
        saw_branch = true; \
    } \
    if (strcmp(#field, "eval") == 0) { \
        assert((arity) == 1u); \
        assert((control) == CETTA_GSLT_FOLD_CONTROL_EVALUATE); \
        saw_eval = true; \
    } \
    if (strcmp(#field, "let_star") == 0) { \
        assert((arity) == 2u); \
        assert((control) == CETTA_GSLT_FOLD_CONTROL_BIND_SEQUENCE); \
        saw_bind_sequence = true; \
    } \
} while (0);
    CETTA_GSLT_FOLD_CONTROL_HEAD_ROWS(COUNT_FOLD_CONTROL)
#undef COUNT_FOLD_CONTROL
    assert(rows == 5u);
    assert(saw_bind && saw_branch && saw_eval && saw_bind_sequence);
}

static void test_generated_sequence_representations(void) {
    unsigned rows = 0u;
    bool saw_clist = false;
#define CHECK_SEQUENCE_REPRESENTATION(key, materializer, empty, cons) do { \
    rows++;                                                               \
    if (strcmp((key), "clist") == 0) {                                  \
        assert(strcmp((materializer), "clist:to-list") == 0);            \
        assert(strcmp((empty), "ClistNil") == 0);                        \
        assert(strcmp((cons), "ClistCons") == 0);                        \
        assert(strcmp((empty), (cons)) != 0);                             \
        saw_clist = true;                                                  \
    }                                                                      \
} while (0);
    CETTA_GSLT_SEQUENCE_REPRESENTATION_ROWS(
        CHECK_SEQUENCE_REPRESENTATION)
#undef CHECK_SEQUENCE_REPRESENTATION
    assert(rows == 1u);
    assert(saw_clist);

    unsigned consumer_rows = 0u;
    bool saw_clist_consumer = false;
#define CHECK_SEQUENCE_FOLD_CONSUMER(                                  \
    key, consumer, arity, represented_operand) do {                    \
    consumer_rows++;                                                    \
    if (strcmp((key), "clist") == 0) {                                \
        assert(strcmp((consumer), "clist:foldl") == 0);               \
        assert((arity) == 5u);                                          \
        assert((represented_operand) == 0u);                            \
        saw_clist_consumer = true;                                      \
    }                                                                   \
} while (0);
    CETTA_GSLT_SEQUENCE_FOLD_CONSUMER_ROWS(
        CHECK_SEQUENCE_FOLD_CONSUMER)
#undef CHECK_SEQUENCE_FOLD_CONSUMER
    assert(consumer_rows == 1u);
    assert(saw_clist_consumer);

    unsigned fold_rows = 0u;
    bool saw_clist_fold = false;
#define CHECK_SEQUENCE_FOLD_TARGET(key, target) do {                    \
    fold_rows++;                                                        \
    if (strcmp((key), "clist") == 0) {                                \
        assert(strcmp((target), "list:foldl") == 0);                   \
        saw_clist_fold = true;                                          \
    }                                                                   \
} while (0);
    CETTA_GSLT_SEQUENCE_FOLD_TARGET_ROWS(CHECK_SEQUENCE_FOLD_TARGET)
#undef CHECK_SEQUENCE_FOLD_TARGET
    assert(fold_rows == 1u);
    assert(saw_clist_fold);

    unsigned erasure_rows = 0u;
    bool saw_identity = false, saw_append = false, saw_range = false;
#define CHECK_SEQUENCE_ERASURE(                                        \
    key, source, target, mode, minimum, maximum) do {                   \
    assert(strcmp((key), "clist") == 0);                              \
    assert((minimum) <= (maximum));                                     \
    erasure_rows++;                                                     \
    if (strcmp((source), "clist:from-list") == 0) {                   \
        assert(strcmp((target), "identity") == 0);                    \
        assert((mode) == CETTA_GSLT_SEQUENCE_ERASURE_UNWRAP);           \
        assert((minimum) == 1u && (maximum) == 1u);                     \
        saw_identity = true;                                            \
    }                                                                   \
    if (strcmp((source), "clist:append") == 0) {                      \
        assert(strcmp((target), "list:append") == 0);                 \
        assert((mode) == CETTA_GSLT_SEQUENCE_ERASURE_RECURSIVE_ALL);    \
        assert((minimum) == 2u && (maximum) == 2u);                     \
        saw_append = true;                                              \
    }                                                                   \
    if (strcmp((source), "clist:range") == 0) {                       \
        assert(strcmp((target), "list:range") == 0);                  \
        assert((mode) == CETTA_GSLT_SEQUENCE_ERASURE_SCALAR_ALL);       \
        assert((minimum) == 1u && (maximum) == 2u);                     \
        saw_range = true;                                               \
    }                                                                   \
} while (0);
    CETTA_GSLT_SEQUENCE_ERASURE_ROWS(CHECK_SEQUENCE_ERASURE)
#undef CHECK_SEQUENCE_ERASURE
    assert(erasure_rows == 4u);
    assert(saw_identity && saw_append && saw_range);
}

static void test_generated_prepared_intrinsic_program(void) {
    unsigned rows = 0u;
    bool saw_division = false, saw_sort_numbers = false;
    bool saw_and = false, saw_or = false, saw_not = false;
    bool saw_intersection = false;
    bool saw_remove_all = false, saw_unkey = false;
    bool saw_retain_top_k = false, saw_cons = false, saw_union = false;
    unsigned dual_numeric_heads = 0u;
#define COUNT_PREPARED_INTRINSIC_HEAD(                                  \
    field, arity, discipline, instruction) do {                         \
    assert((discipline) ==                                              \
           CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL);     \
    rows++;                                                             \
    if (strcmp(#field, "sort_numbers_atom") == 0) {                    \
        saw_sort_numbers = true;                                        \
        assert((arity) == 1u);                                          \
        assert((instruction) ==                                         \
               CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH);   \
    }                                                                   \
    if (strcmp(#field, "op_div") == 0) {                              \
        saw_division = true;                                            \
        assert((arity) == 2u);                                          \
        assert((instruction) ==                                         \
               CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH);   \
    }                                                                   \
    if (strcmp(#field, "op_and") == 0 ||                             \
        strcmp(#field, "op_or") == 0) {                               \
        assert((arity) == 2u);                                          \
        assert((instruction) ==                                         \
               CETTA_GSLT_PREPARED_PURE_INTRINSIC_BOOLEAN_DISPATCH);    \
        if (strcmp(#field, "op_and") == 0) saw_and = true;            \
        else saw_or = true;                                              \
    }                                                                   \
    if (strcmp(#field, "op_not") == 0) {                              \
        saw_not = true;                                                 \
        assert((arity) == 1u);                                          \
        assert((instruction) ==                                         \
               CETTA_GSLT_PREPARED_PURE_INTRINSIC_BOOLEAN_DISPATCH);    \
    }                                                                   \
    if (strcmp(#field, "intersection_atom") == 0) {                  \
        saw_intersection = true;                                       \
        assert((arity) == 2u);                                          \
        assert((instruction) ==                                         \
               CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH);   \
    }                                                                   \
    if (strcmp(#field, "op_plus") == 0 ||                             \
        strcmp(#field, "op_minus") == 0 ||                            \
        strcmp(#field, "op_mul") == 0 ||                              \
        strcmp(#field, "op_lt") == 0 ||                               \
        strcmp(#field, "op_gt") == 0 ||                               \
        strcmp(#field, "op_le") == 0 ||                               \
        strcmp(#field, "op_ge") == 0 ||                               \
        strcmp(#field, "numeric_eq") == 0) {                          \
        assert((arity) == 2u);                                          \
        assert((instruction) ==                                         \
               CETTA_GSLT_PREPARED_PURE_INTRINSIC_GROUNDED_DISPATCH);   \
        dual_numeric_heads++;                                           \
    }                                                                   \
    if (strcmp(#field, "remove_all_atom") == 0) saw_remove_all = true; \
    if (strcmp(#field, "retain_top_k_keyed_atom") == 0)                \
        saw_retain_top_k = true;                                        \
    if (strcmp(#field, "unkey_atom") == 0) saw_unkey = true;          \
    if (strcmp(#field, "cons_atom") == 0) {                            \
        saw_cons = true;                                                \
        assert((arity) == 2u);                                          \
        assert((instruction) ==                                         \
               CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONSTRUCT_EXPRESSION_CONS); \
    }                                                                   \
    if (strcmp(#field, "union_atom") == 0) {                           \
        saw_union = true;                                               \
        assert((arity) == 2u);                                          \
        assert((instruction) ==                                         \
               CETTA_GSLT_PREPARED_PURE_INTRINSIC_CONCATENATE_EXPRESSIONS); \
    }                                                                   \
} while (0);
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_HEAD_ROWS(
        COUNT_PREPARED_INTRINSIC_HEAD)
#undef COUNT_PREPARED_INTRINSIC_HEAD
    assert(rows >= 15u);
    assert(saw_division && saw_sort_numbers && saw_remove_all);
    assert(saw_retain_top_k && saw_unkey);
    assert(saw_cons && saw_union);
    assert(saw_and && saw_or && saw_not);
    assert(saw_intersection);
    assert(dual_numeric_heads == 8u);
    bool saw_intersection_scope = false;
#define CHECK_INTRINSIC_SCOPE(field, arity, dialects) do {               \
    assert((dialects) != 0u);                                           \
    assert(((dialects) & ~(CETTA_GSLT_INTRINSIC_DIALECT_HE |             \
        CETTA_GSLT_INTRINSIC_DIALECT_PETTA |                            \
        CETTA_GSLT_INTRINSIC_DIALECT_PRIME)) == 0u);                    \
    if (strcmp(#field, "intersection_atom") == 0) {                  \
        assert((arity) == 2u);                                          \
        assert((dialects) == CETTA_GSLT_INTRINSIC_DIALECT_PETTA);        \
        saw_intersection_scope = true;                                 \
    }                                                                  \
} while (0);
    CETTA_GSLT_PREPARED_PURE_INTRINSIC_SCOPE_ROWS(CHECK_INTRINSIC_SCOPE)
#undef CHECK_INTRINSIC_SCOPE
    assert(saw_intersection_scope);
}

static void test_generated_sequence_constructor_policy(void) {
    struct SequencePolicy {
        const char *field;
        unsigned arities;
        unsigned held_mask;
    };
#define SEQUENCE_POLICY(field, arities, mask) {#field, arities, mask},
    const struct SequencePolicy constructors[] = {
        CETTA_GSLT_SEQUENCE_VALUE_CONSTRUCTOR_ROWS(SEQUENCE_POLICY)
    };
#undef SEQUENCE_POLICY
    const size_t count = sizeof(constructors) / sizeof(constructors[0]);
    bool saw_range = false, saw_repeat = false;
    bool saw_library_range = false, saw_library_repeat = false;
    for (size_t index = 0u; index < count; index++) {
        const struct SequencePolicy *policy = &constructors[index];
        assert(policy->held_mask <= 0xffffu);
        assert((policy->arities & 1u) == 0u);
        assert((policy->arities & (1u << 2u)) != 0u);
        assert((policy->arities & (1u << 3u)) == 0u);
        for (size_t previous = 0u; previous < index; previous++)
            assert(strcmp(policy->field, constructors[previous].field) != 0);
        if (strcmp(policy->field, "range_atom") == 0) {
            saw_range = true;
            assert(policy->arities == 6u);
            assert(policy->held_mask == 0u);
        }
        if (strcmp(policy->field, "lib_list_range") == 0) {
            saw_library_range = true;
            assert(policy->arities == 6u);
            assert(policy->held_mask == 0u);
        }
        if (strcmp(policy->field, "repeat_atom") == 0) {
            saw_repeat = true;
            assert(policy->arities == 4u);
            assert(policy->held_mask == 2u);
        }
        if (strcmp(policy->field, "lib_list_repeat") == 0) {
            saw_library_repeat = true;
            assert(policy->arities == 4u);
            assert(policy->held_mask == 2u);
        }
        /* Strict-all code cannot erase a constructor's held operand. */
#define CHECK_PREPARED_CONSTRUCTOR(symbol_field, arity, discipline, instruction) \
        if (strcmp(policy->field, #symbol_field) == 0) { \
            assert(policy->held_mask == 0u); \
            assert((policy->arities & (1u << (arity))) != 0u); \
        }
        CETTA_GSLT_PREPARED_PURE_INTRINSIC_HEAD_ROWS(CHECK_PREPARED_CONSTRUCTOR)
#undef CHECK_PREPARED_CONSTRUCTOR
    }
    assert(saw_range && saw_repeat && saw_library_range && saw_library_repeat);
}

static void test_generated_pure_call_modes(void) {
    assert(CETTA_GSLT_PURE_CALL_EAGER !=
           CETTA_GSLT_PURE_CALL_CALL_BY_NEED);
    assert(CETTA_GSLT_PURE_CALL_EAGER == 0);
    assert(CETTA_GSLT_PURE_CALL_CALL_BY_NEED == 1);

    assert(!cetta_gslt_pure_call_whnf_disjoint(
        CETTA_GSLT_PATTERN_VARIABLE, CETTA_GSLT_PATTERN_ATOM,
        false, 0u, 0u, false, false, false));
    assert(cetta_gslt_pure_call_whnf_disjoint(
        CETTA_GSLT_PATTERN_ATOM, CETTA_GSLT_PATTERN_ATOM,
        false, 0u, 0u, false, false, false));
    assert(!cetta_gslt_pure_call_whnf_disjoint(
        CETTA_GSLT_PATTERN_ATOM, CETTA_GSLT_PATTERN_ATOM,
        true, 0u, 0u, false, false, true));
    assert(cetta_gslt_pure_call_whnf_disjoint(
        CETTA_GSLT_PATTERN_EXPRESSION, CETTA_GSLT_PATTERN_EXPRESSION,
        false, 2u, 3u, false, false, true));
    assert(cetta_gslt_pure_call_whnf_disjoint(
        CETTA_GSLT_PATTERN_EXPRESSION, CETTA_GSLT_PATTERN_EXPRESSION,
        false, 3u, 3u, true, true, false));
    assert(!cetta_gslt_pure_call_whnf_disjoint(
        CETTA_GSLT_PATTERN_EXPRESSION, CETTA_GSLT_PATTERN_EXPRESSION,
        false, 3u, 3u, true, true, true));
    /* Compound heads can overlap through nested variables; unequal syntax is
     * not rigid WHNF evidence. */
    assert(!cetta_gslt_pure_call_whnf_disjoint(
        CETTA_GSLT_PATTERN_EXPRESSION, CETTA_GSLT_PATTERN_EXPRESSION,
        false, 3u, 3u, false, false, false));
}

typedef struct {
    Atom **items;
    size_t len;
} TestGeneratedRootSpan;

typedef struct {
    size_t live_entries;
} TestGeneratedEphemeronAtomMap;

#define TEST_DECLARE_STRONG_ATOM_SLOT(name) Atom **name;
#define TEST_DECLARE_STRONG_ATOM_SPAN(name) TestGeneratedRootSpan name;
#define TEST_DECLARE_LOGICAL_BINDINGS(name) void *name;
#define TEST_DECLARE_OUTCOME_SET(name) void *name;
#define TEST_DECLARE_VARIANT_INSTANCE(name) void *name;
#define TEST_DECLARE_EPHEMERON_ATOM_MAP(name) \
    TestGeneratedEphemeronAtomMap name;
typedef struct {
    CETTA_EVAL_GC_FRAME_FIELDS_prepared_pure_machine(
        TEST_DECLARE_STRONG_ATOM_SLOT,
        TEST_DECLARE_STRONG_ATOM_SPAN,
        TEST_DECLARE_LOGICAL_BINDINGS,
        TEST_DECLARE_OUTCOME_SET,
        TEST_DECLARE_VARIANT_INSTANCE,
        TEST_DECLARE_EPHEMERON_ATOM_MAP)
} TestPreparedPureMachineRoots;
typedef struct {
    CETTA_EVAL_GC_FRAME_FIELDS_outcome_continuation(
        TEST_DECLARE_STRONG_ATOM_SLOT,
        TEST_DECLARE_STRONG_ATOM_SPAN,
        TEST_DECLARE_LOGICAL_BINDINGS,
        TEST_DECLARE_OUTCOME_SET,
        TEST_DECLARE_VARIANT_INSTANCE,
        TEST_DECLARE_EPHEMERON_ATOM_MAP)
} TestOutcomeContinuationRoots;
typedef struct {
    CETTA_EVAL_GC_FRAME_FIELDS_case_branch_continuation(
        TEST_DECLARE_STRONG_ATOM_SLOT,
        TEST_DECLARE_STRONG_ATOM_SPAN,
        TEST_DECLARE_LOGICAL_BINDINGS,
        TEST_DECLARE_OUTCOME_SET,
        TEST_DECLARE_VARIANT_INSTANCE,
        TEST_DECLARE_EPHEMERON_ATOM_MAP)
} TestCaseBranchContinuationRoots;
typedef struct {
    CETTA_EVAL_GC_FRAME_FIELDS_tuple_machine(
        TEST_DECLARE_STRONG_ATOM_SLOT,
        TEST_DECLARE_STRONG_ATOM_SPAN,
        TEST_DECLARE_LOGICAL_BINDINGS,
        TEST_DECLARE_OUTCOME_SET,
        TEST_DECLARE_VARIANT_INSTANCE,
        TEST_DECLARE_EPHEMERON_ATOM_MAP)
} TestTupleMachineRoots;
#undef TEST_DECLARE_EPHEMERON_ATOM_MAP
#undef TEST_DECLARE_VARIANT_INSTANCE
#undef TEST_DECLARE_OUTCOME_SET
#undef TEST_DECLARE_LOGICAL_BINDINGS
#undef TEST_DECLARE_STRONG_ATOM_SPAN
#undef TEST_DECLARE_STRONG_ATOM_SLOT

static size_t generated_root_atoms_visited(
    const TestPreparedPureMachineRoots *roots) {
    size_t visited = 0u;
#define CETTA_GC_VISIT_STRONG_ATOM_SPAN(session, span, fail) do { \
    (void)(session);                                                \
    if ((span).len == SIZE_MAX) { fail; }                           \
    visited += (span).len;                                          \
} while (0)
#define CETTA_GC_VISIT_EPHEMERON_ATOM_MAP(session, map, fail) do { \
    (void)(session);                                                \
    if ((map).live_entries == SIZE_MAX) { fail; }                   \
    visited += (map).live_entries;                                 \
} while (0)
    CETTA_EVAL_GC_ARM_prepared_pure_machine(
        NULL, roots, goto failed);
#undef CETTA_GC_VISIT_EPHEMERON_ATOM_MAP
#undef CETTA_GC_VISIT_STRONG_ATOM_SPAN
    return visited;
failed:
    return SIZE_MAX;
}

static void test_generated_prepared_machine_roots(void) {
    TestPreparedPureMachineRoots complete = {
        .values = {NULL, 2u},
        .live_slots = {NULL, 3u},
        .runtime_frames = {NULL, 5u},
        .thunk_updates = {7u},
    };
    assert(generated_root_atoms_visited(&complete) == 17u);

    TestPreparedPureMachineRoots omitted_runtime_frames = complete;
    omitted_runtime_frames.runtime_frames.len = 0u;
    assert(generated_root_atoms_visited(&omitted_runtime_frames) == 12u);
}

static size_t generated_outcome_continuation_roots_visited(
    const TestOutcomeContinuationRoots *roots) {
    size_t visited = 0u;
#define CETTA_GC_VISIT_STRONG_ATOM_SPAN(session, span, fail) do { \
    (void)(session);                                                \
    if ((span).len == SIZE_MAX) { fail; }                           \
    visited += (span).len;                                          \
} while (0)
#define CETTA_GC_VISIT_OUTCOME_SET(session, outcomes, fail) do { \
    (void)(session);                                             \
    if (!(outcomes)) { fail; }                                  \
    visited++;                                                   \
} while (0)
    CETTA_EVAL_GC_ARM_outcome_continuation(
        NULL, roots, goto failed);
#undef CETTA_GC_VISIT_OUTCOME_SET
#undef CETTA_GC_VISIT_STRONG_ATOM_SPAN
    return visited;
failed:
    return SIZE_MAX;
}

static void test_generated_outcome_continuation_roots(void) {
    int child = 0;
    int parent = 0;
    TestOutcomeContinuationRoots complete = {
        .live_atoms = {NULL, 3u},
        .child_outcomes = &child,
        .parent_outcomes = &parent,
    };
    assert(generated_outcome_continuation_roots_visited(&complete) == 5u);

    TestOutcomeContinuationRoots no_derived_atoms = complete;
    no_derived_atoms.live_atoms.len = 0u;
    assert(generated_outcome_continuation_roots_visited(
               &no_derived_atoms) == 2u);

    TestOutcomeContinuationRoots omitted_child = complete;
    omitted_child.child_outcomes = NULL;
    assert(generated_outcome_continuation_roots_visited(
               &omitted_child) == SIZE_MAX);

    TestOutcomeContinuationRoots omitted_parent = complete;
    omitted_parent.parent_outcomes = NULL;
    assert(generated_outcome_continuation_roots_visited(
               &omitted_parent) == SIZE_MAX);
}

static size_t generated_case_branch_continuation_roots_visited(
    const TestCaseBranchContinuationRoots *roots) {
    size_t visited = 0u;
#define CETTA_GC_VISIT_STRONG_ATOM_SPAN(session, span, fail) do { \
    (void)(session);                                                \
    if ((span).len == SIZE_MAX) { fail; }                           \
    visited += (span).len;                                          \
} while (0)
#define CETTA_GC_VISIT_LOGICAL_BINDINGS(session, bindings, fail) do { \
    (void)(session);                                                   \
    if (!(bindings)) { fail; }                                        \
    visited++;                                                         \
} while (0)
#define CETTA_GC_VISIT_OUTCOME_SET(session, outcomes, fail) do { \
    (void)(session);                                             \
    if (!(outcomes)) { fail; }                                  \
    visited++;                                                   \
} while (0)
    CETTA_EVAL_GC_ARM_case_branch_continuation(
        NULL, roots, goto failed);
#undef CETTA_GC_VISIT_OUTCOME_SET
#undef CETTA_GC_VISIT_LOGICAL_BINDINGS
#undef CETTA_GC_VISIT_STRONG_ATOM_SPAN
    return visited;
failed:
    return SIZE_MAX;
}

static void test_generated_case_branch_continuation_roots(void) {
    int lexical_env = 0;
    int branch_env = 0;
    int source_outcomes = 0;
    int parent_outcomes = 0;
    TestCaseBranchContinuationRoots complete = {
        .live_atoms = {NULL, 4u},
        .lexical_env = &lexical_env,
        .branch_env = &branch_env,
        .source_outcomes = &source_outcomes,
        .parent_outcomes = &parent_outcomes,
    };
    assert(generated_case_branch_continuation_roots_visited(
               &complete) == 8u);

    TestCaseBranchContinuationRoots omitted_atoms = complete;
    omitted_atoms.live_atoms.len = 0u;
    assert(generated_case_branch_continuation_roots_visited(
               &omitted_atoms) == 4u);

    TestCaseBranchContinuationRoots omitted_lexical = complete;
    omitted_lexical.lexical_env = NULL;
    assert(generated_case_branch_continuation_roots_visited(
               &omitted_lexical) == SIZE_MAX);

    TestCaseBranchContinuationRoots omitted_branch = complete;
    omitted_branch.branch_env = NULL;
    assert(generated_case_branch_continuation_roots_visited(
               &omitted_branch) == SIZE_MAX);

    TestCaseBranchContinuationRoots omitted_source = complete;
    omitted_source.source_outcomes = NULL;
    assert(generated_case_branch_continuation_roots_visited(
               &omitted_source) == SIZE_MAX);

    TestCaseBranchContinuationRoots omitted_parent = complete;
    omitted_parent.parent_outcomes = NULL;
    assert(generated_case_branch_continuation_roots_visited(
               &omitted_parent) == SIZE_MAX);
}

static size_t generated_tuple_machine_roots_visited(
    const TestTupleMachineRoots *roots) {
    size_t visited = 0u;
#define CETTA_GC_VISIT_STRONG_ATOM_SPAN(session, span, fail) do { \
    (void)(session);                                                \
    if ((span).len == SIZE_MAX) { fail; }                           \
    visited += (span).len;                                          \
} while (0)
#define CETTA_GC_VISIT_LOGICAL_BINDINGS(session, bindings, fail) do { \
    (void)(session);                                                   \
    if (!(bindings)) { fail; }                                        \
    visited++;                                                         \
} while (0)
#define CETTA_GC_VISIT_OUTCOME_SET(session, outcomes, fail) do { \
    (void)(session);                                             \
    if (!(outcomes)) { fail; }                                  \
    visited++;                                                   \
} while (0)
    CETTA_EVAL_GC_ARM_tuple_machine(NULL, roots, goto failed);
#undef CETTA_GC_VISIT_OUTCOME_SET
#undef CETTA_GC_VISIT_LOGICAL_BINDINGS
#undef CETTA_GC_VISIT_STRONG_ATOM_SPAN
    return visited;
failed:
    return SIZE_MAX;
}

static void test_generated_tuple_machine_roots(void) {
    int env = 0;
    int outcomes = 0;
    int parent_outcomes = 0;
    TestTupleMachineRoots complete = {
        .continuation_atoms = {NULL, 2u},
        .orig_elems = {NULL, 3u},
        .prefix = {NULL, 5u},
        .env = &env,
        .outcomes = &outcomes,
        .parent_outcomes = &parent_outcomes,
    };
    assert(generated_tuple_machine_roots_visited(&complete) == 13u);

    TestTupleMachineRoots omitted_continuation = complete;
    omitted_continuation.continuation_atoms.len = 0u;
    assert(generated_tuple_machine_roots_visited(
               &omitted_continuation) == 11u);

    TestTupleMachineRoots omitted_env = complete;
    omitted_env.env = NULL;
    assert(generated_tuple_machine_roots_visited(
               &omitted_env) == SIZE_MAX);
}

int main(void) {
    test_invocation_plans();
    test_call_preparation_plans();
    test_operation_metadata();
    test_control_operand_plans();
    test_query_head_rows();
    test_row_dispositions();
    test_observation_and_lifetime();
    test_prepared_equation_admission();
    test_total_integer_heads();
    test_generated_value_allocation();
    test_generated_resource_classification();
    test_register_heads();
    test_generated_register_program();
    test_generated_fold_control_program();
    test_generated_sequence_representations();
    test_generated_prepared_intrinsic_program();
    test_generated_sequence_constructor_policy();
    test_generated_pure_call_modes();
    test_generated_prepared_machine_roots();
    test_generated_outcome_continuation_roots();
    test_generated_case_branch_continuation_roots();
    test_generated_tuple_machine_roots();
    puts("PASS: generated execution contracts");
    return 0;
}
