#ifndef CETTA_GSLT2PARSE_PARSER_OCCURRENCE_FOLD_V1_H
#define CETTA_GSLT2PARSE_PARSER_OCCURRENCE_FOLD_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "native_sha256.h"
#include "parser_pack_abi_v1.h"
#include "parser_pack_guarded_lexical_exec_v1.h"
#include "parser_pack_lexical_v1.h"

typedef struct PPOccurrenceSpanMaskV1Plan PPOccurrenceSpanMaskV1Plan;

typedef enum {
    PPOCCURRENCE_FOLD_V1_TRANSITION_EPSILON = 0,
    PPOCCURRENCE_FOLD_V1_TRANSITION_ROLE = 1
} PPOccurrenceFoldV1TransitionKind;

typedef struct {
    PPOccurrenceFoldV1TransitionKind kind;
    uint32_t source;
    uint32_t target;
    uint32_t role_id;
} PPOccurrenceFoldV1Transition;

typedef struct {
    uint32_t state_len;
    uint32_t start_state;
    uint32_t accept_state;
    uint32_t transition_begin;
    uint32_t transition_len;
} PPOccurrenceFoldV1Contract;

typedef struct {
    uint32_t source_index;
    uint32_t role_id;
    uint32_t shift_operation_id;
    /*
     * UINT32_MAX selects the complete terminal span.  Otherwise this is the
     * exact normalized production label whose nested value occurrence
     * supplies the semantic payload span.  Plan construction verifies that
     * its transparent-inline source fiber contains exactly one selected
     * authored source occurrence.
     */
    uint32_t value_production_label;
} PPOccurrenceFoldV1TerminalBinding;

typedef struct {
    uint32_t production_index;
    uint32_t production_label;
    uint32_t node_id;
    uint32_t operation_id;
    uint32_t contract_index;
} PPOccurrenceFoldV1ProductionBinding;

/*
 * A dense occurrence-fold plan derived from compiler answers, a lexical plan,
 * and one validated cursor program.  Names are canonical authored identities;
 * execution uses only their dense IDs.
 */
typedef struct {
    char **roles;
    char **nodes;
    char **operations;
    PPOccurrenceFoldV1TerminalBinding *terminals;
    PPOccurrenceFoldV1ProductionBinding *productions;
    PPOccurrenceFoldV1Contract *contracts;
    PPOccurrenceFoldV1Transition *transitions;
    uint32_t *terminal_binding_by_source;
    uint32_t *production_binding_by_index;
    uint32_t role_len;
    uint32_t node_len;
    uint32_t operation_len;
    uint32_t terminal_len;
    uint32_t production_len;
    uint32_t contract_len;
    uint32_t transition_len;
    uint32_t cursor_terminal_len;
    uint32_t cursor_production_len;
    char base_pack_digest[65];
    char lexical_plan_digest[65];
    char cursor_program_digest[65];
    char compiler_answer_digest[65];
    char plan_digest[65];
} PPOccurrenceFoldV1Plan;

void ppoccurrence_fold_v1_plan_init(PPOccurrenceFoldV1Plan *plan);
void ppoccurrence_fold_v1_plan_free(PPOccurrenceFoldV1Plan *plan);

/*
 * Records have one of these compiler-normal forms:
 *
 *   (occurrence-fold-token-v1 TAG ROLE)
 *   (occurrence-fold-token-v3
 *      TAG ROLE NODE SOURCE-PRODUCTION-LABEL VALUE-PRODUCTION-LABEL)
 *   (occurrence-fold-shift-v1 ROLE OPERATION)
 *   (occurrence-fold-production-v1 LABEL NODE OPERATION CONTRACT)
 *
 * LABEL is matched to the label of an exact authored ParserPack production;
 * TAG is matched through the independently compiled lexical plan.  The
 * semantic token record additionally proves that VALUE-PRODUCTION-LABEL is
 * the unique normalized production whose certified source fiber contains
 * SOURCE-PRODUCTION-LABEL.  The supplied answer-set digest is recomputed
 * before any dense IDs are assigned.
 * `out` must have been initialized with ppoccurrence_fold_v1_plan_init.
 */
bool ppoccurrence_fold_v1_plan_build(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardedLexCursorV1Program *program,
    Atom *const *records,
    size_t record_len,
    const char *compiler_answer_digest,
    PPOccurrenceFoldV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Validate the self-contained plan against the staged cursor program.  This
 * is the validation boundary used by generated C: pack and lexical identities
 * remain sealed in both artifacts, while no runtime grammar reconstruction is
 * required.
 */
bool ppoccurrence_fold_v1_plan_validate_program(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    char *error_buf,
    size_t error_buf_size);

bool ppoccurrence_fold_v1_plan_validate(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    char *error_buf,
    size_t error_buf_size);

/*
 * Append an owning C initializer for a validated occurrence-fold plan.
 * The emitted initializer rechecks every dense identity and plan digest
 * against the supplied generated cursor program before publishing the plan.
 */
bool ppoccurrence_fold_v1_emit_c(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size);

const char *ppoccurrence_fold_v1_role_name(
    const PPOccurrenceFoldV1Plan *plan,
    uint32_t role_id);

const char *ppoccurrence_fold_v1_node_name(
    const PPOccurrenceFoldV1Plan *plan,
    uint32_t node_id);

const char *ppoccurrence_fold_v1_operation_name(
    const PPOccurrenceFoldV1Plan *plan,
    uint32_t operation_id);

typedef struct {
    uint32_t role_id;
    uint32_t source_index;
    uint32_t left_scalar;
    uint32_t right_scalar;
    uint32_t left_byte;
    uint32_t right_byte;
    const uint8_t *bytes;
    size_t byte_len;
} PPOccurrenceFoldV1Value;

typedef enum {
    PPOCCURRENCE_FOLD_V1_STEP_SHIFT = 0,
    PPOCCURRENCE_FOLD_V1_STEP_REDUCE = 1
} PPOccurrenceFoldV1StepKind;

typedef struct {
    PPOccurrenceFoldV1StepKind kind;
    uint32_t operation_id;
    uint32_t node_id;
    uint32_t production_index;
    uint32_t production_label;
    uint32_t left_scalar;
    uint32_t right_scalar;
    uint32_t left_byte;
    uint32_t right_byte;
    const PPOccurrenceFoldV1Value *values;
    uint32_t value_len;
} PPOccurrenceFoldV1Step;

/*
 * apply mutates only backend-private staging state.  commit publishes that
 * state after a matching parser ACCEPT receipt; abort discards it on every
 * other path.
 */
typedef bool (*PPOccurrenceFoldV1ApplyFn)(
    void *context,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size);

typedef bool (*PPOccurrenceFoldV1CommitFn)(
    void *context,
    char *error_buf,
    size_t error_buf_size);

typedef void (*PPOccurrenceFoldV1AbortFn)(void *context);

typedef struct {
    void *context;
    PPOccurrenceFoldV1ApplyFn apply;
    PPOccurrenceFoldV1CommitFn commit;
    PPOccurrenceFoldV1AbortFn abort;
} PPOccurrenceFoldV1Backend;

/*
 * A canonical proof-occurrence observation backend.  It hashes authored
 * operation, node, role, source span, and exact source bytes, publishes its
 * digest only on commit, and publishes nothing after abort.
 */
typedef struct {
    const PPOccurrenceFoldV1Plan *plan;
    CettaNativeSha256 trace;
    uint32_t step_len;
    uint32_t value_len;
    bool active;
    bool committed;
    bool aborted;
    char digest[65];
} PPOccurrenceFoldV1Trace;

void ppoccurrence_fold_v1_trace_init(
    PPOccurrenceFoldV1Trace *trace,
    const PPOccurrenceFoldV1Plan *plan);

PPOccurrenceFoldV1Backend ppoccurrence_fold_v1_trace_backend(
    PPOccurrenceFoldV1Trace *trace);

typedef struct {
    PPGuardedLexCursorV1Receipt parser_receipt;
    uint32_t step_len;
    uint32_t shift_step_len;
    uint32_t reduce_step_len;
    uint32_t value_len;
    uint32_t max_pending_value_len;
    uint32_t semantic_occurrence_projection_len;
    uint64_t semantic_occurrence_projection_work_item_len;
    bool committed;
} PPOccurrenceFoldV1Receipt;

/*
 * Execute recognition and its fold as one transaction.  Rejection and work
 * exhaustion are ordinary parser outcomes and always abort the backend.
 * Infrastructure, plan, contract, or backend failures return false.
 */
bool ppoccurrence_fold_v1_run_bytes(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPOccurrenceFoldV1Backend *backend,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPOccurrenceFoldV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Execute an already admitted immutable program/plan pair.  The ordinary
 * entry point proves the admission by recomputing both generated digests;
 * loaders that retain that admission may use this entry point for every
 * source in the same run without revalidating immutable compiler output.
 */
bool ppoccurrence_fold_v1_run_bytes_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPOccurrenceFoldV1Backend *backend,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPOccurrenceFoldV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Execute the same fold while using a compiled occurrence-span quotient for
 * selected nested token values.  The quotient changes only the construction
 * of fold-observed semantic occurrences; parser and fold observations remain
 * those of the complete value-program execution.
 */
bool ppoccurrence_fold_v1_run_bytes_with_span_mask(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    const PPOccurrenceSpanMaskV1Plan *span_mask,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPOccurrenceFoldV1Backend *backend,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPOccurrenceFoldV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

bool ppoccurrence_fold_v1_run_bytes_with_span_mask_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *plan,
    const PPOccurrenceSpanMaskV1Plan *span_mask,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPOccurrenceFoldV1Backend *backend,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPOccurrenceFoldV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

#endif
