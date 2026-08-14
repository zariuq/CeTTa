#include "atom.h"
#include "generated/cetta_execution_contracts.generated.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The head table is compiled from the spec; the test checks the compiled
 * algebra (join laws, classification) and that the heads the pull consumers
 * admit are present.  It deliberately does not pin the row count: adding a
 * relational head to the spec must regenerate cleanly, not fail here. */
static void test_query_head_rows(void) {
    unsigned rows = 0u;
    bool saw_match = false, saw_get_atoms = false, saw_mork_match = false;
    bool saw_get_type = false, saw_get_type_space = false;
#define COUNT_HEAD(head, effect) do { \
    assert((effect) != CETTA_GSLT_QUERY_EFFECT_PURE); \
    rows++; \
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

    /* All four join laws of the two-point semilattice. */
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
    assert(CETTA_GSLT_USER_HEAD_EFFECT_REVISION_KEYED == 1);
    assert(CETTA_GSLT_USER_HEAD_EFFECT_LEAST_FIXED_POINT == 1);
    assert(CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD ==
           CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY);
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
    bool saw_capture = false, saw_foreign = false;
#define COUNT_IDENTITY_GROUNDED(kind) do { \
    grounded_rows++; \
    if (strcmp(#kind, "GV_SPACE") == 0) saw_space = true; \
    if (strcmp(#kind, "GV_STATE") == 0) saw_state = true; \
    if (strcmp(#kind, "GV_CAPTURE") == 0) saw_capture = true; \
    if (strcmp(#kind, "GV_FOREIGN") == 0) saw_foreign = true; \
} while (0);
    CETTA_GSLT_IDENTITY_BEARING_GROUNDED_KIND_ROWS(
        COUNT_IDENTITY_GROUNDED)
#undef COUNT_IDENTITY_GROUNDED
    assert(grounded_rows == 4u);
    assert(saw_space && saw_state && saw_capture && saw_foreign);
    assert(cetta_gslt_identity_bearing_grounded_kind(GV_SPACE));
    assert(cetta_gslt_identity_bearing_grounded_kind(GV_STATE));
    assert(cetta_gslt_identity_bearing_grounded_kind(GV_CAPTURE));
    assert(cetta_gslt_identity_bearing_grounded_kind(GV_FOREIGN));
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
} while (0);
    CETTA_GSLT_FOLD_CONTROL_HEAD_ROWS(COUNT_FOLD_CONTROL)
#undef COUNT_FOLD_CONTROL
    assert(rows == 4u);
    assert(saw_bind && saw_branch && saw_eval);
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
#define TEST_DECLARE_EPHEMERON_ATOM_MAP(name) \
    TestGeneratedEphemeronAtomMap name;
typedef struct {
    CETTA_EVAL_GC_FRAME_FIELDS_prepared_pure_machine(
        TEST_DECLARE_STRONG_ATOM_SLOT,
        TEST_DECLARE_STRONG_ATOM_SPAN,
        TEST_DECLARE_LOGICAL_BINDINGS,
        TEST_DECLARE_OUTCOME_SET,
        TEST_DECLARE_EPHEMERON_ATOM_MAP)
} TestPreparedPureMachineRoots;
#undef TEST_DECLARE_EPHEMERON_ATOM_MAP
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

int main(void) {
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
    test_generated_pure_call_modes();
    test_generated_prepared_machine_roots();
    puts("PASS: generated execution contracts");
    return 0;
}
