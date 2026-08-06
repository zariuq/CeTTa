#ifndef CETTA_EXECUTION_CONTRACTS_GENERATED_H
#define CETTA_EXECUTION_CONTRACTS_GENERATED_H

/* Generated from lib/gslt_execution_contracts.metta.
 * Source SHA-256: c7e0e132e4e8838aa56020e4479476055e9451d3a8a2794bbe2329a724f4753e
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CETTA_GSLT_QUERY_EFFECT_PURE = 0u,
    CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY = 1u << 0,
} CettaGsltQueryEffect;

#define CETTA_GSLT_QUERY_EFFECT_HEAD_ROWS(X) \
    X(match, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(get_atoms, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(mork_match_surface, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(get_type, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY) \
    X(get_type_space, CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY)

static inline CettaGsltQueryEffect cetta_gslt_query_effect_join(
    CettaGsltQueryEffect left, CettaGsltQueryEffect right) {
    return (CettaGsltQueryEffect)((unsigned)left | (unsigned)right);
}

/* The analysis policy is generated with the effect algebra.  Native code
 * supplies the revision-pinned equation graph; uncertainty is never allowed
 * to become accelerator authority. */
#define CETTA_GSLT_USER_HEAD_EFFECT_REVISION_KEYED 1
#define CETTA_GSLT_USER_HEAD_EFFECT_LEAST_FIXED_POINT 1
#define CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD     CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY
#define CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL     CETTA_GSLT_QUERY_EFFECT_PURE
#define CETTA_GSLT_INERT_EXPRESSION_CHILDREN_OPAQUE 1

#define CETTA_GSLT_TOTAL_INTEGER_HEAD_ROWS(X)     X(op_plus, 2u) \
    X(op_minus, 2u) \
    X(op_mul, 2u)

/* Default construction keeps these generated leaf classes arena-owned.
 * Explicit publication may still hash-cons them through hashcons_get. */
#define CETTA_GSLT_ARENA_OWNED_GROUNDED_KIND_ROWS(X)     X(GV_INT) \
    X(GV_FLOAT) \
    X(GV_BIGINT) \
    X(GV_RATIONAL)

/* Executable resource-leaf classifiers generated from resource-lifetime.
 * Atom constructors fold these judgments into compositional summary bits;
 * evaluators consume the summaries without recursively rescanning terms. */
#define CETTA_GSLT_IDENTITY_BEARING_GROUNDED_KIND_ROWS(X)     X(GV_SPACE) \
    X(GV_STATE) \
    X(GV_CAPTURE) \
    X(GV_FOREIGN)

static inline bool cetta_gslt_identity_bearing_grounded_kind(
    GroundedKind kind) {
    switch (kind) {
    case GV_SPACE:
        return true;
    case GV_STATE:
        return true;
    case GV_CAPTURE:
        return true;
    case GV_FOREIGN:
        return true;
    default:
        return false;
    }
}

#define CETTA_GSLT_THREAD_LOCAL_RESOURCE_SYMBOL_ROWS(X)     X(capture)

static inline bool cetta_gslt_thread_local_resource_symbol(
    SymbolId symbol, const BuiltinSyms *builtins) {
    return builtins &&
           (symbol == builtins->capture);
}

typedef enum {
    CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER = 0,
    CETTA_GSLT_REGISTER_RESULT_BOOLEAN = 1,
} CettaGsltRegisterResultKind;

typedef enum {
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD = 0,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT = 1,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY = 2,
    CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL = 3,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_EQUAL = 4,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS = 5,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER = 6,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS_EQUAL = 7,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER_EQUAL = 8,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER = 9,
    CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO = 10,
} CettaGsltRegisterInstruction;

typedef enum {
    CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS = 0,
    CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS = 1,
} CettaGsltRegisterOperandDiscipline;

static inline bool cetta_gslt_register_operand_discipline(
    CettaGsltRegisterInstruction instruction,
    CettaGsltRegisterOperandDiscipline *discipline_out) {
    if (!discipline_out)
        return false;
    switch (instruction) {
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_TYPED_STRUCTURAL_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_EQUAL:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS_EQUAL:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER_EQUAL:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO:
        *discipline_out = CETTA_GSLT_REGISTER_OPERANDS_EXACT_INTEGER_OPERANDS;
        return true;
    default:
        return false;
    }
}

#define CETTA_GSLT_REGISTER_HEAD_ROWS(X)     X(op_plus, 2u, CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD) \
    X(op_minus, 2u, CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT) \
    X(op_mul, 2u, CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY) \
    X(op_eq, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL) \
    X(op_lt, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS) \
    X(op_gt, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER) \
    X(op_le, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS_EQUAL) \
    X(op_ge, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER_EQUAL) \
    X(numeric_eq, 2u, CETTA_GSLT_REGISTER_RESULT_BOOLEAN, CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_EQUAL)

typedef enum {
    CETTA_GSLT_FOLD_CONTROL_BIND = 0,
    CETTA_GSLT_FOLD_CONTROL_BRANCH = 1,
    CETTA_GSLT_FOLD_CONTROL_EVALUATE = 2,
} CettaGsltFoldControl;

typedef enum {
    CETTA_GSLT_PURE_CALL_EAGER = 0,
    CETTA_GSLT_PURE_CALL_CALL_BY_NEED = 1,
} CettaGsltPureCallMode;

typedef enum {
    CETTA_GSLT_PATTERN_VARIABLE = 0,
    CETTA_GSLT_PATTERN_ATOM = 1,
    CETTA_GSLT_PATTERN_EXPRESSION = 2,
} CettaGsltPatternKind;

/* Executable unique-match evidence generated from the pure-call component.
 * The runtime projects representation facts into this judgment; uncertainty
 * (including differences below a shared constructor) is deliberately not
 * evidence of determinacy. */
static inline bool cetta_gslt_pure_call_whnf_disjoint(
    CettaGsltPatternKind left_kind,
    CettaGsltPatternKind right_kind,
    bool atoms_equal,
    uint64_t left_arity,
    uint64_t right_arity,
    bool left_head_rigid,
    bool right_head_rigid,
    bool heads_equal) {
    if (left_kind == CETTA_GSLT_PATTERN_VARIABLE ||
        right_kind == CETTA_GSLT_PATTERN_VARIABLE)
        return false;
    if (left_kind != right_kind)
        return true;
    if (left_kind == CETTA_GSLT_PATTERN_ATOM)
        return !atoms_equal;
    if (left_kind != CETTA_GSLT_PATTERN_EXPRESSION)
        return false;
    if (left_arity != right_arity)
        return true;
    return left_arity > 0u && left_head_rigid &&
           right_head_rigid && !heads_equal;
}

/* Executable control arms for the generated determinate-fold program. */
#define CETTA_GSLT_FOLD_CONTROL_HEAD_ROWS(X)     X(let, 3u, CETTA_GSLT_FOLD_CONTROL_BIND) \
    X(if_text, 3u, CETTA_GSLT_FOLD_CONTROL_BRANCH) \
    X(eval, 1u, CETTA_GSLT_FOLD_CONTROL_EVALUATE) \
    X(function, 1u, CETTA_GSLT_FOLD_CONTROL_EVALUATE)

/* ── Evaluator root frames: generated evacuation arms ─────────────────────
 * Frame kinds and their field traversal sequences come from the
 * presentation.  The collector core defines ONE visitor macro per root
 * field kind and stays language-ignorant:
 *   CETTA_GC_VISIT_STRONG_ATOM_SLOT(SESSION, SLOT_PTR, FAIL)
 *   CETTA_GC_VISIT_STRONG_ATOM_SPAN(SESSION, SPAN_PTR, FAIL)
 *   CETTA_GC_VISIT_LOGICAL_BINDINGS(SESSION, ENV_PTR, FAIL)
 *   CETTA_GC_VISIT_OUTCOME_SET(SESSION, OS_PTR, FAIL)
 *   CETTA_GC_VISIT_EPHEMERON_ATOM_MAP(SESSION, MAP, FAIL)
 * A spec field kind without a core visitor macro fails to compile; an
 * omitted arm is exercised by the skip-arm negative gate. */
typedef enum {
    CETTA_EVAL_GC_FRAME_LEXICAL = 0,
    CETTA_EVAL_GC_FRAME_OUTCOME_SET = 1,
    CETTA_EVAL_GC_FRAME_FUNCTION_ARGS = 2,
    CETTA_EVAL_GC_FRAME_FUNCTION_ARGS_MACHINE = 3,
    CETTA_EVAL_GC_FRAME_PRIME_TASK = 4,
    CETTA_EVAL_GC_FRAME_PRIME_FRAME = 5,
    CETTA_EVAL_GC_FRAME_PRIME_DRIVER = 6,
    CETTA_EVAL_GC_FRAME_PREPARED_PURE_MACHINE = 7,
    CETTA_EVAL_GC_FRAME_KIND_COUNT
} CettaEvalGcFrameKind;

#define CETTA_EVAL_GC_FRAME_KIND_ROWS(X)     X(lexical, LEXICAL) \
    X(outcome_set, OUTCOME_SET) \
    X(function_args, FUNCTION_ARGS) \
    X(function_args_machine, FUNCTION_ARGS_MACHINE) \
    X(prime_task, PRIME_TASK) \
    X(prime_frame, PRIME_FRAME) \
    X(prime_driver, PRIME_DRIVER) \
    X(prepared_pure_machine, PREPARED_PURE_MACHINE)

/* Expanding these rows with C type emitters produces the root payload
 * structures themselves.  The presentation therefore owns both layout and
 * traversal order; the collector supplies only the representation
 * types and their language-ignorant visitors. */
#define CETTA_EVAL_GC_FRAME_FIELDS_lexical(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(atom_io) \
    LOGICAL_BINDINGS(env) \
    STRONG_ATOM_SLOT(etype_io)

#define CETTA_EVAL_GC_FRAME_FIELDS_outcome_set(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, EPHEMERON_ATOM_MAP) \
    OUTCOME_SET(outcomes)

#define CETTA_EVAL_GC_FRAME_FIELDS_function_args(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(orig_arg) \
    STRONG_ATOM_SLOT(arg_type) \
    LOGICAL_BINDINGS(env) \
    OUTCOME_SET(arg_os) \
    LOGICAL_BINDINGS(merged) \
    LOGICAL_BINDINGS(active) \
    LOGICAL_BINDINGS(child)

#define CETTA_EVAL_GC_FRAME_FIELDS_function_args_machine(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(op) \
    STRONG_ATOM_SPAN(orig_args) \
    STRONG_ATOM_SPAN(arg_types) \
    STRONG_ATOM_SPAN(prefix) \
    OUTCOME_SET(outcomes)

#define CETTA_EVAL_GC_FRAME_FIELDS_prime_task(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(atom) \
    STRONG_ATOM_SLOT(etype) \
    LOGICAL_BINDINGS(dynamic_env) \
    LOGICAL_BINDINGS(seed_env) \
    OUTCOME_SET(target)

#define CETTA_EVAL_GC_FRAME_FIELDS_prime_frame(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SLOT(atom) \
    STRONG_ATOM_SLOT(etype) \
    STRONG_ATOM_SLOT(strict_source) \
    STRONG_ATOM_SLOT(then_branch) \
    STRONG_ATOM_SLOT(else_branch) \
    STRONG_ATOM_SLOT(ref) \
    STRONG_ATOM_SPAN(normalization_children) \
    LOGICAL_BINDINGS(env) \
    OUTCOME_SET(child) \
    OUTCOME_SET(target)

#define CETTA_EVAL_GC_FRAME_FIELDS_prime_driver(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, EPHEMERON_ATOM_MAP) \
    OUTCOME_SET(root_target)

#define CETTA_EVAL_GC_FRAME_FIELDS_prepared_pure_machine(\
    STRONG_ATOM_SLOT, STRONG_ATOM_SPAN, \
    LOGICAL_BINDINGS, OUTCOME_SET, EPHEMERON_ATOM_MAP) \
    STRONG_ATOM_SPAN(values) \
    STRONG_ATOM_SPAN(live_slots) \
    STRONG_ATOM_SPAN(runtime_frames) \
    EPHEMERON_ATOM_MAP(thunk_updates)

#define CETTA_EVAL_GC_ARM_lexical(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->atom_io, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->env, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->etype_io, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_outcome_set(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->outcomes, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_function_args(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->orig_arg, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->arg_type, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->env, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->arg_os, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->merged, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->active, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->child, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_function_args_machine(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->op, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->orig_args, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->arg_types, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->prefix, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->outcomes, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_prime_task(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->atom, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->etype, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->dynamic_env, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->seed_env, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->target, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_prime_frame(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->atom, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->etype, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->strict_source, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->then_branch, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->else_branch, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SLOT((SESSION), (PAYLOAD)->ref, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->normalization_children, FAIL); \
    CETTA_GC_VISIT_LOGICAL_BINDINGS((SESSION), (PAYLOAD)->env, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->child, FAIL); \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->target, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_prime_driver(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_OUTCOME_SET((SESSION), (PAYLOAD)->root_target, FAIL); \
} while (0)

#define CETTA_EVAL_GC_ARM_prepared_pure_machine(SESSION, PAYLOAD, FAIL) do { \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->values, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->live_slots, FAIL); \
    CETTA_GC_VISIT_STRONG_ATOM_SPAN((SESSION), (PAYLOAD)->runtime_frames, FAIL); \
    CETTA_GC_VISIT_EPHEMERON_ATOM_MAP((SESSION), (PAYLOAD)->thunk_updates, FAIL); \
} while (0)

/* Representation-polymorphic register arms generated from the presentation. */
static inline bool cetta_gslt_register_execute_atom_binary(
    CettaGsltRegisterInstruction instruction,
    Atom *left, Atom *right, bool *boolean_out,
    CettaGsltRegisterResultKind *kind_out) {
    if (!left || !right || !boolean_out || !kind_out)
        return false;
    switch (instruction) {
    case CETTA_GSLT_REGISTER_INSTRUCTION_ATOM_EQUAL:
        *boolean_out = atom_eq(left, right);
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    default:
        return false;
    }
}

/* Tagged-small-integer interpretation generated from the same register
 * program.  Exact arithmetic requests GMP promotion only on overflow. */
static inline bool cetta_gslt_register_execute_small_binary(
    CettaGsltRegisterInstruction instruction,
    int64_t *integer_out, bool *boolean_out,
    int64_t left, int64_t right,
    CettaGsltRegisterResultKind *kind_out, bool *promote_out) {
    if (!integer_out || !boolean_out || !kind_out || !promote_out)
        return false;
    *promote_out = false;
    switch (instruction) {
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD:
        *promote_out = __builtin_add_overflow(left, right, integer_out);
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT:
        *promote_out = __builtin_sub_overflow(left, right, integer_out);
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY:
        *promote_out = __builtin_mul_overflow(left, right, integer_out);
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_EQUAL:
        *boolean_out = left == right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS:
        *boolean_out = left < right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER:
        *boolean_out = left > right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS_EQUAL:
        *boolean_out = left <= right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER_EQUAL:
        *boolean_out = left >= right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER:
        if (right == 0) { return false; } *integer_out = (left == INT64_MIN && right == -1) ? 0 : left % right;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO:
        if (right == 0) { return false; } if (left == INT64_MIN && right == -1) { *integer_out = 0; } else { *integer_out = left % right; if (*integer_out != 0 && ((*integer_out < 0) != (right < 0))) *integer_out += right; }
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    default:
        return false;
    }
}

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>

/* Executable interpretation generated from register-expression-execution. */
static inline bool cetta_gslt_register_execute_binary(
    CettaGsltRegisterInstruction instruction,
    mpz_ptr integer_out, bool *boolean_out,
    mpz_srcptr left, mpz_srcptr right,
    CettaGsltRegisterResultKind *kind_out) {
    if (!integer_out || !boolean_out || !left || !right || !kind_out)
        return false;
    switch (instruction) {
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_ADD:
        mpz_add(integer_out, left, right);
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_SUBTRACT:
        mpz_sub(integer_out, left, right);
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_MULTIPLY:
        mpz_mul(integer_out, left, right);
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_EQUAL:
        *boolean_out = mpz_cmp(left, right) == 0;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS:
        *boolean_out = mpz_cmp(left, right) < 0;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER:
        *boolean_out = mpz_cmp(left, right) > 0;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_LESS_EQUAL:
        *boolean_out = mpz_cmp(left, right) <= 0;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_GREATER_EQUAL:
        *boolean_out = mpz_cmp(left, right) >= 0;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_BOOLEAN;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_REMAINDER:
        if (mpz_sgn(right) == 0) { return false; } mpz_tdiv_r(integer_out, left, right);
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    case CETTA_GSLT_REGISTER_INSTRUCTION_INTEGER_FLOOR_MODULO:
        if (mpz_sgn(right) == 0) { return false; } mpz_fdiv_r(integer_out, left, right);
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    default:
        return false;
    }
}
#endif

typedef enum {
    CETTA_GSLT_ROW_CONTRIBUTE = 0,
    CETTA_GSLT_ROW_ADDITIVE_ZERO = 1,
    CETTA_GSLT_ROW_TRANSPORT_FAULT = 2,
    CETTA_GSLT_ROW_CONSUMER_STOP = 3,
} CettaGsltRowDisposition;

static inline CettaGsltRowDisposition cetta_gslt_classify_streamed_row(
    bool decoded, bool cyclic) {
    if (!decoded)
        return CETTA_GSLT_ROW_TRANSPORT_FAULT;
    if (cyclic)
        return CETTA_GSLT_ROW_ADDITIVE_ZERO;
    return CETTA_GSLT_ROW_CONTRIBUTE;
}

typedef enum {
    CETTA_GSLT_OBSERVATION_MATCH_CHAIN_PROGRESS = 0,
    CETTA_GSLT_OBSERVATION_DETERMINATE_RULE_USE = 1,
} CettaGsltObservationEvent;

static inline bool cetta_gslt_observation_visible(
    CettaGsltObservationEvent event, bool diagnostic_profile) {
    return diagnostic_profile &&
           (event == CETTA_GSLT_OBSERVATION_MATCH_CHAIN_PROGRESS ||
           event == CETTA_GSLT_OBSERVATION_DETERMINATE_RULE_USE);
}

#define CETTA_GSLT_MATCH_CHAIN_TRACE_ENV "CETTA_MATCH_CHAIN_TRACE"
#define CETTA_GSLT_MATCH_CHAIN_TRACE_INTERVAL_ENV "CETTA_MATCH_CHAIN_TRACE_INTERVAL"

typedef enum {
    CETTA_GSLT_LIFETIME_INITIALIZE = 0,
    CETTA_GSLT_LIFETIME_CLEANUP = 1,
    CETTA_GSLT_LIFETIME_ARENA_RESET = 2,
    CETTA_GSLT_LIFETIME_ARENA_FREE = 3,
    CETTA_GSLT_LIFETIME_ARENA_REUSE = 4,
} CettaGsltLifetimeTransition;

static inline bool cetta_gslt_lifetime_invalidates_allocations(
    CettaGsltLifetimeTransition transition) {
    return transition == CETTA_GSLT_LIFETIME_ARENA_RESET ||
           transition == CETTA_GSLT_LIFETIME_ARENA_FREE;
}

typedef enum {
    CETTA_GSLT_EVIDENCE_SINGLETON_HEAD = 1u << 0,
    CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS = 1u << 1,
    CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS = 1u << 2,
    CETTA_GSLT_EVIDENCE_REVISION_CURRENT = 1u << 3,
    CETTA_GSLT_EVIDENCE_GROUND_CALL = 1u << 4,
    CETTA_GSLT_EVIDENCE_REGISTER_GUARD = 1u << 5,
    CETTA_GSLT_EVIDENCE_REGISTER_BASE = 1u << 6,
    CETTA_GSLT_EVIDENCE_REGISTER_TAIL = 1u << 7,
    CETTA_GSLT_EVIDENCE_REGISTER_RECURSIVE = 1u << 8,
    CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED = 1u << 9,
} CettaGsltPreparedEquationEvidence;

#define CETTA_GSLT_PREPARED_EQUATION_MAX_REGISTERS     16u

static inline bool cetta_gslt_prepared_equation_plan_admitted(
    uint32_t evidence) {
    const uint32_t required =
        CETTA_GSLT_EVIDENCE_SINGLETON_HEAD |
        CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS |
        CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS |
        CETTA_GSLT_EVIDENCE_REVISION_CURRENT |
        CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED;
    return (evidence & required) == required;
}

/* Shared accelerator admission for call-policy preservation.  The current
 * implementation is conservative: declared arrow signatures fall back to
 * canonical dispatch until an accelerator implements their policy. */
#define CETTA_GSLT_ACCELERATOR_CALL_POLICY_SUPPORTED(space, head, arity)     (!space_head_has_arrow_signature((space), (head), (arity)))

static inline bool cetta_gslt_prepared_equation_call_admitted(
    uint32_t evidence) {
    const uint32_t required =
        CETTA_GSLT_EVIDENCE_SINGLETON_HEAD |
        CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS |
        CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS |
        CETTA_GSLT_EVIDENCE_REVISION_CURRENT |
        CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED |
        CETTA_GSLT_EVIDENCE_GROUND_CALL;
    return (evidence & required) == required;
}

static inline bool cetta_gslt_prepared_register_step_admitted(
    uint32_t evidence) {
    const uint32_t required =
        CETTA_GSLT_EVIDENCE_SINGLETON_HEAD |
        CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS |
        CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS |
        CETTA_GSLT_EVIDENCE_REVISION_CURRENT |
        CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED |
        CETTA_GSLT_EVIDENCE_GROUND_CALL |
        CETTA_GSLT_EVIDENCE_REGISTER_GUARD |
        CETTA_GSLT_EVIDENCE_REGISTER_BASE |
        CETTA_GSLT_EVIDENCE_REGISTER_TAIL;
    return (evidence & required) == required;
}

static inline bool cetta_gslt_prepared_register_recursion_admitted(
    uint32_t evidence) {
    const uint32_t required =
        CETTA_GSLT_EVIDENCE_SINGLETON_HEAD |
        CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS |
        CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS |
        CETTA_GSLT_EVIDENCE_REVISION_CURRENT |
        CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED |
        CETTA_GSLT_EVIDENCE_GROUND_CALL |
        CETTA_GSLT_EVIDENCE_REGISTER_RECURSIVE;
    return (evidence & required) == required;
}

/* This interpretation makes the uninitialized -> live transition explicit
 * at every cleanup-owned local.  The owning header must be visible at
 * expansion. */
#define CETTA_GSLT_OWNED_BINDINGS(name) \
    __attribute__((cleanup(bindings_free))) Bindings name; \
    bindings_init(&(name))

#endif /* CETTA_EXECUTION_CONTRACTS_GENERATED_H */
