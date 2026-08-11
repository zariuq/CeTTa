#ifndef CETTA_GSLT2PARSE_PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_GUARDED_LEXICAL_EXEC_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_action_bytecode_v1.h"
#include "parser_atom_projection_action_v1.h"
#include "parser_pack_guarded_lexical_v1.h"
#include "parser_pack_guard_ref_v1.h"
#include "regular_span_nfa_v1.h"

typedef struct PPGuardedLexCursorV1SemanticMaskBinding
    PPGuardedLexCursorV1SemanticMaskBinding;

typedef struct {
    uint32_t dfa_state_limit;
    uint32_t dfa_transition_limit;
    uint64_t scan_work_limit;
    uint32_t scan_token_limit;
    uint32_t witness_work_limit;
    uint32_t parse_work_limit;
    uint32_t replay_depth;
    uint32_t result_limit;
} PPGuardedLexExecV1Limits;

typedef enum {
    PPGUARDED_LEX_CURSOR_V1_SLR_CONFLICT = UINT32_C(1) << 0u,
    PPGUARDED_LEX_CURSOR_V1_SLR_ACCEPT_SHAPE = UINT32_C(1) << 1u,
    PPGUARDED_LEX_CURSOR_V1_ZERO_INPUT_CYCLE = UINT32_C(1) << 2u,
    PPGUARDED_LEX_CURSOR_V1_UNMAPPED_TERMINAL = UINT32_C(1) << 3u,
    PPGUARDED_LEX_CURSOR_V1_DUPLICATE_TERMINAL_SOURCE = UINT32_C(1) << 4u,
    PPGUARDED_LEX_CURSOR_V1_EMPTY_TOKEN = UINT32_C(1) << 5u,
    PPGUARDED_LEX_CURSOR_V1_NULLABLE_TOKEN = UINT32_C(1) << 6u,
    PPGUARDED_LEX_CURSOR_V1_NON_PREFIX_FREE_TOKEN = UINT32_C(1) << 7u,
    PPGUARDED_LEX_CURSOR_V1_UNSUPPORTED_GUARD = UINT32_C(1) << 8u,
    PPGUARDED_LEX_CURSOR_V1_BOUNDARY_CROSSING = UINT32_C(1) << 9u,
    PPGUARDED_LEX_CURSOR_V1_TOKEN_PREFIX_OVERLAP = UINT32_C(1) << 10u
} PPGuardedLexCursorV1Failure;

/*
 * A proof-relevant promotion boundary for the one-cursor generated backend.
 * Eligibility is derived from the complete ParserPack execution plan; it is
 * never selected by timing or by a guest-language name.  Ineligible plans
 * remain executable by the complete GLL/GLR path.
 */
typedef struct {
    CettaLpNativeSlrSummary slr;
    uint32_t reachable_nonterminal_len;
    uint32_t reachable_production_len;
    uint32_t active_terminal_len;
    uint32_t scalar_terminal_len;
    uint32_t ordinary_span_tag_len;
    uint32_t guarded_span_tag_len;
    uint32_t guard_lookahead_tag_len;
    uint32_t zero_input_cycle_len;
    uint32_t unmapped_terminal_len;
    uint32_t duplicate_terminal_source_len;
    uint32_t empty_token_len;
    uint32_t nullable_token_len;
    uint32_t non_prefix_free_token_len;
    uint32_t unsupported_guard_len;
    uint32_t boundary_crossing_len;
    uint32_t token_prefix_overlap_len;
    uint32_t failure_mask;
    bool eligible;
    char digest[65];
} PPGuardedLexCursorV1Certificate;

typedef enum {
    PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR = 0,
    PPGUARDED_LEX_CURSOR_TERMINAL_ORDINARY_SPAN = 1,
    PPGUARDED_LEX_CURSOR_TERMINAL_GUARDED_SPAN = 2
} PPGuardedLexCursorV1TerminalKind;

/*
 * One exact source for each reachable final-parser terminal.  Scalar ranges
 * and guarded-lookahead ranges are slices of the owning program's range
 * array.  UINT32_MAX denotes an inapplicable DFA tag or semantic start.
 */
typedef struct {
    uint32_t terminal_id;
    uint32_t slr_terminal_index;
    PPGuardedLexCursorV1TerminalKind kind;
    CettaLpNativeUtf8TerminalKind scalar_kind;
    uint32_t scalar;
    uint32_t scalar_range_begin;
    uint32_t scalar_range_len;
    uint32_t dfa_tag;
    uint32_t guard_dfa_tag;
    uint32_t guard_range_begin;
    uint32_t guard_range_len;
    bool guard_accepts_eof;
    bool semantic_value_required;
    uint32_t semantic_start_state_id;
    uint32_t semantic_program_index;
} PPGuardedLexCursorV1Terminal;

typedef enum {
    PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_SCALAR = 0,
    PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_GUARD = 1
} PPGuardedLexCursorV1ValueTerminalKind;

/*
 * Exact input source for one shifting terminal in a token-value SLR program.
 * Scalar ranges are slices of the owning value program.  A guard terminal
 * consumes no source and obtains its value from another (base-grammar)
 * value program at the current global cursor.  The state-candidate slices are
 * derived from the immutable SLR action table and validated against every
 * non-error terminal column; they are execution indexes, not semantic input.
 */
typedef struct {
    uint32_t terminal_id;
    uint32_t slr_terminal_index;
    PPGuardedLexCursorV1ValueTerminalKind kind;
    CettaLpNativeUtf8TerminalKind scalar_kind;
    uint32_t scalar;
    uint32_t range_begin;
    uint32_t range_len;
    uint32_t guard_value_program_index;
} PPGuardedLexCursorV1ValueTerminal;

/*
 * A build-time-exported deterministic semantic program for one lexical or
 * guard-body start state.  It contains only generic SLR tables, terminal
 * sources, and provenance; all production meanings remain in the separately
 * bound action bytecode program.
 */
typedef struct {
    CettaLpNativeSlrProgram slr;
    uint32_t *production_lhs_indices;
    PPGuardedLexCursorV1ValueTerminal *terminals;
    CettaLpNativeUnicodeRange *ranges;
    uint32_t *state_candidate_offsets;
    uint32_t *state_candidate_terminal_indices;
    CettaLpNativeUnicodeRange *scalar_dispatch_ranges;
    CettaLpNativeSlrProgramAction *scalar_dispatch_actions;
    uint32_t *scalar_dispatch_shift_sources;
    uint32_t terminal_len;
    uint32_t range_len;
    uint32_t state_candidate_len;
    uint32_t scalar_dispatch_range_len;
    uint32_t start_state_id;
    bool guarded;
    char program_digest[65];
} PPGuardedLexCursorV1ValueProgram;

/*
 * Recognition/dispatch IR shared by the cursor interpreter and C emitter.
 * A recognition-only program may be built first.  Its optional action
 * program is bound later from the independently certified GSLT compiler;
 * actions are never inferred from recognition tables.  Token-value SLR
 * programs are an optional specialization: every conflict-free independent
 * start is retained.  Once actions are bound, a fixed-point dependency
 * analysis marks the terminal values observable from the start result;
 * value_programs_complete is true exactly when every such lexical/guard start
 * has a program.  A nonzero value_program_conflict_len records conflicts in
 * the full family, including semantically dead starts, rather than a build
 * failure; malformed inputs and resource failures still fail the build.
 */
typedef struct {
    RSDFAV1Program dfa;
    CettaLpNativeSlrProgram final_slr;
    uint32_t *final_production_lhs_indices;
    PPActionBytecodeV1Program actions;
    PPGuardedLexCursorV1Terminal *terminals;
    PPGuardedLexCursorV1ValueProgram *value_programs;
    CettaLpNativeUnicodeRange *ranges;
    uint32_t terminal_len;
    uint32_t value_program_len;
    uint32_t value_program_conflict_len;
    uint32_t range_len;
    bool value_programs_complete;
    bool actions_bound;
    char base_pack_digest[65];
    char lexical_plan_digest[65];
    char guard_plan_digest[65];
    char guarded_plan_digest[65];
    char execution_plan_digest[65];
    char certificate_digest[65];
    char program_digest[65];
} PPGuardedLexCursorV1Program;

/*
 * A checked sparse realization of the final SLR terminal columns.  Each
 * state owns two source-index slices, preserving the canonical terminal
 * order: scalar sources first participate in zero-copy scalar selection;
 * span sources participate after the shared DFA scan.  Membership is
 * derived solely from non-error cells in the owning SLR action table.
 * This object is execution data and deliberately remains outside the
 * generated cursor-program ABI.
 */
typedef struct {
    uint32_t *scalar_offsets;
    uint32_t *scalar_source_indices;
    uint32_t *span_offsets;
    uint32_t *span_source_indices;
    uint32_t state_len;
    uint32_t scalar_candidate_len;
    uint32_t span_candidate_len;
} PPGuardedLexCursorV1DispatchIndex;

void ppguarded_lex_cursor_v1_dispatch_index_init(
    PPGuardedLexCursorV1DispatchIndex *index);

void ppguarded_lex_cursor_v1_dispatch_index_free(
    PPGuardedLexCursorV1DispatchIndex *index);

bool ppguarded_lex_cursor_v1_dispatch_index_build(
    const PPGuardedLexCursorV1Program *program,
    PPGuardedLexCursorV1DispatchIndex *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_cursor_v1_dispatch_index_validate(
    const PPGuardedLexCursorV1Program *program,
    const PPGuardedLexCursorV1DispatchIndex *index,
    char *error_buf,
    size_t error_buf_size);

void ppguarded_lex_cursor_v1_program_init(
    PPGuardedLexCursorV1Program *program);

void ppguarded_lex_cursor_v1_program_free(
    PPGuardedLexCursorV1Program *program);

bool ppguarded_lex_cursor_v1_program_validate(
    const PPGuardedLexCursorV1Program *program,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_cursor_v1_program_bind_actions(
    PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    Atom *const *compiler_answers,
    size_t compiler_answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_cursor_v1_program_validate_bound(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    char *error_buf,
    size_t error_buf_size);

/*
 * One DFA contains the ordinary lexical NFAs, guarded prefix NFAs, and
 * positive-guard body NFAs.  Their tag ranges are contiguous in that order.
 * A v1 execution plan requires every positive guard to have exactly one
 * guarded-prefix owner; richer mixed scannerless/guarded plans fail closed.
 * NFA tag atoms and terminal identities are borrowed from the source plans,
 * which must outlive this plan.
 */
typedef struct {
    RSDFAV1Plan dfa;
    PPNativeV1TerminalPlan scalar_terminals;
    CettaLpNativeSlrPrepared final_slr;
    CettaLpNativeGllPrepared final_gll;
    CettaLpNativeGlrPrepared final_glr;
    PPNativeV1PreparedStartFamily ordinary_witness_parsers;
    PPNativeV1PreparedStartFamily guard_witness_parsers;
    PPNativeV1PreparedStartFamily guarded_witness_parsers;
    Atom *start_state;
    uint32_t start_state_id;
    uint32_t final_parser_table_build_count;
    uint32_t ordinary_witness_parser_family_build_count;
    uint32_t ordinary_witness_prepared_start_len;
    uint32_t guard_witness_parser_family_build_count;
    uint32_t guard_witness_prepared_start_len;
    uint32_t guarded_witness_parser_family_build_count;
    uint32_t guarded_witness_prepared_start_len;
    PPNativeV1TerminalExtension *terminals;
    uint32_t *projected_tag_terminal_ids;
    uint32_t *guard_local_for_guarded;
    Atom *const *guard_tags;
    uint32_t lexical_tag_len;
    uint32_t guarded_tag_len;
    uint32_t guard_tag_len;
    uint32_t combined_tag_len;
    uint32_t terminal_len;
    PPGuardedLexCursorV1Certificate cursor_certificate;
    char base_pack_digest[65];
    char lexical_plan_digest[65];
    char guard_plan_digest[65];
    char guarded_plan_digest[65];
    char execution_plan_digest[65];
} PPGuardedLexExecV1Plan;

bool ppguarded_lex_cursor_v1_program_build(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    PPGuardedLexCursorV1Program *out,
    char *error_buf,
    size_t error_buf_size);

typedef enum {
    PPGUARDED_LEX_CURSOR_V1_ACCEPTED = 0,
    PPGUARDED_LEX_CURSOR_V1_REJECTED = 1,
    PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT = 2
} PPGuardedLexCursorV1Outcome;

typedef struct {
    PPGuardedLexCursorV1Outcome outcome;
    uint32_t input_byte_len;
    uint32_t input_scalar_len;
    uint32_t farthest_scalar;
    uint32_t farthest_byte;
    uint32_t token_len;
    uint32_t scalar_token_len;
    uint32_t ordinary_span_token_len;
    uint32_t guarded_span_token_len;
    uint32_t shift_len;
    uint32_t reduce_len;
    uint32_t epsilon_reduce_len;
    uint32_t unit_reduce_len;
    uint32_t reselect_reduce_len;
    uint32_t unit_reselect_reduce_len;
    uint32_t cursor_scan_len;
    uint32_t max_stack_len;
    uint32_t source_pass_count;
    uint64_t dfa_work_item_len;
    uint64_t parser_work_item_len;
    char trace_digest[65];
} PPGuardedLexCursorV1Receipt;

typedef enum {
    PPGUARDED_LEX_CURSOR_V1_COUNTERS_ONLY = 0,
    PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE = 1
} PPGuardedLexCursorV1Observation;

/*
 * Proof-relevant occurrence stream emitted by the deterministic cursor.
 * Symbol and production identities are copied from the generated SLR/terminal
 * program; the generic runtime neither names nor interprets guest-language
 * constructs.  REDUCE spans are the exact covered source interval, including
 * zero-width epsilon reductions.  ACCEPT is emitted only after the complete
 * start derivation has been recognized.
 *
 * Consumers should update an isolated transaction and commit it only on
 * ACCEPT.  A later parse failure therefore cannot publish a partial fold.
 */
typedef enum {
    PPGUARDED_LEX_CURSOR_V1_EVENT_SHIFT = 0,
    PPGUARDED_LEX_CURSOR_V1_EVENT_REDUCE = 1,
    PPGUARDED_LEX_CURSOR_V1_EVENT_ACCEPT = 2,
    /*
     * An authored reduction inside the deterministic value program of one
     * shifted terminal.  source_index names that enclosing terminal while
     * production_label retains the exact normalized production occurrence.
     * Its certified transparent-inline source fiber remains available to
     * consumers that bind authored source occurrences.
     */
    PPGUARDED_LEX_CURSOR_V1_EVENT_SEMANTIC_REDUCE = 3
} PPGuardedLexCursorV1EventKind;

typedef struct {
    PPGuardedLexCursorV1EventKind kind;
    uint32_t symbol_id;
    uint32_t source_index;
    uint32_t production_index;
    uint32_t production_label;
    uint32_t rhs_len;
    bool authored;
    uint32_t left_scalar;
    uint32_t right_scalar;
    uint32_t left_byte;
    uint32_t right_byte;
} PPGuardedLexCursorV1Event;

typedef bool (*PPGuardedLexCursorV1EventEmitFn)(
    void *context,
    const PPGuardedLexCursorV1Event *event,
    char *error_buf,
    size_t error_buf_size);

typedef bool (*PPGuardedLexCursorV1SemanticOccurrenceRequiredFn)(
    void *context,
    uint32_t source_index);

typedef enum {
    PPGUARDED_LEX_CURSOR_V1_OCCURRENCE_PROJECTION_UNHANDLED = 0,
    PPGUARDED_LEX_CURSOR_V1_OCCURRENCE_PROJECTION_ACCEPTED = 1,
    PPGUARDED_LEX_CURSOR_V1_OCCURRENCE_PROJECTION_WORK_LIMIT = 2
} PPGuardedLexCursorV1SemanticOccurrenceProjectionOutcome;

typedef struct {
    PPGuardedLexCursorV1SemanticOccurrenceProjectionOutcome outcome;
    uint32_t production_label;
    uint32_t left_scalar;
    uint32_t right_scalar;
    uint64_t work_item_len;
} PPGuardedLexCursorV1SemanticOccurrenceProjection;

typedef bool (*PPGuardedLexCursorV1SemanticOccurrenceProjectFn)(
    void *context,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t source_index,
    uint32_t left_scalar,
    uint32_t right_scalar,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticOccurrenceProjection *out,
    char *error_buf,
    size_t error_buf_size);

typedef struct {
    void *context;
    PPGuardedLexCursorV1EventEmitFn emit;
    /*
     * Optional observation-liveness masks over canonical cursor source and
     * production indices.  A null mask requests the complete corresponding
     * event stream.  A present mask must cover the complete owning domain;
     * zero cells suppress only the callback, never parser execution, spans,
     * work accounting, or trace construction.
     */
    const uint8_t *shift_event_live;
    uint32_t shift_event_live_len;
    const uint8_t *reduction_event_live;
    uint32_t reduction_event_live_len;
    /*
     * Ask the cursor to interpret selected terminal value programs in the
     * source-span algebra and emit their nested authored reductions.
     * `semantic_occurrence_required`, when present, is the independent
     * observation-liveness predicate.  Otherwise final-result semantic
     * liveness is used for backward-compatible event audits.  Ordinary event
     * consumers leave `semantic_occurrences` false.
     */
    bool semantic_occurrences;
    PPGuardedLexCursorV1SemanticOccurrenceRequiredFn
        semantic_occurrence_required;
    /*
     * An optional compiler-certified quotient of nested semantic reductions.
     * ACCEPTED supplies the one reduction observed by this consumer;
     * UNHANDLED preserves the complete value-program fallback.
     */
    PPGuardedLexCursorV1SemanticOccurrenceProjectFn
        semantic_occurrence_project;
} PPGuardedLexCursorV1EventSink;

/*
 * Run recognition while emitting the requested occurrence observation.  A
 * sink without liveness masks receives the complete stream.  Observation is
 * semantically transparent: the ordinary receipt is unchanged, and a
 * consumer failure fails the run rather than silently dropping a live event.
 */
bool ppguarded_lex_cursor_v1_program_run_events_scalar_view_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    const PPGuardedLexCursorV1EventSink *sink,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_cursor_v1_program_run_events_scalar_view(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    const PPGuardedLexCursorV1EventSink *sink,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_cursor_v1_program_run_events_bytes_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPGuardedLexCursorV1EventSink *sink,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_cursor_v1_program_run_events_bytes(
    const PPGuardedLexCursorV1Program *program,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPGuardedLexCursorV1EventSink *sink,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Semantic execution of the deterministic cursor owns every constructed
 * Atom in arena.  semantic_result is the ordinary ParserPack
 * (result <start-value> nil) value on acceptance and NULL on rejection.
 * The witness lattice supplies only exact span-terminal values; recognition
 * and production selection remain properties of the cursor program.
 */
typedef struct {
    Arena arena;
    Atom *semantic_result;
    uint32_t terminal_value_len;
    uint32_t action_execution_len;
    uint64_t action_instruction_len;
    uint32_t value_program_run_len;
    uint32_t value_terminal_value_len;
    uint32_t value_action_execution_len;
    uint64_t value_action_instruction_len;
    uint64_t value_parser_work_item_len;
    uint32_t prepared_fallback_run_len;
    uint64_t prepared_fallback_work_item_len;
    PPGuardedLexCursorV1Receipt receipt;
} PPGuardedLexCursorV1SemanticResult;

typedef struct {
    Atom **atoms;
    uint32_t atom_len;
    uint32_t terminal_value_len;
    uint32_t action_execution_len;
    uint64_t action_instruction_len;
    uint32_t value_program_run_len;
    uint32_t value_terminal_value_len;
    uint32_t value_action_execution_len;
    uint64_t value_action_instruction_len;
    uint64_t value_parser_work_item_len;
    uint32_t semantic_mask_run_len;
    uint32_t semantic_mask_input_scalar_len;
    uint32_t semantic_mask_retained_scalar_len;
    uint64_t semantic_mask_work_item_len;
    PPGuardedLexCursorV1Receipt receipt;
} PPGuardedLexCursorV1ProjectedResult;

void ppguarded_lex_cursor_v1_projected_result_init(
    PPGuardedLexCursorV1ProjectedResult *result);

typedef enum {
    PPGUARDED_LEX_CURSOR_V1_PREPARED_GLL = 0,
    PPGUARDED_LEX_CURSOR_V1_PREPARED_GLR = 1
} PPGuardedLexCursorV1PreparedBackend;

void ppguarded_lex_cursor_v1_semantic_result_init(
    PPGuardedLexCursorV1SemanticResult *result);

void ppguarded_lex_cursor_v1_semantic_result_free(
    PPGuardedLexCursorV1SemanticResult *result);

bool ppguarded_lex_cursor_v1_program_run_scalar_view_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_cursor_v1_program_run_scalar_view(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Production byte-source recognition entry point.  UTF-8 is decoded once
 * before the validated deterministic cursor runs.
 */
bool ppguarded_lex_cursor_v1_program_run_bytes(
    const PPGuardedLexCursorV1Program *program,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Execute the bound action bytecode along the actual deterministic cursor
 * reduction trace.  The lattice must describe the same scalar view and
 * carry exactly one witness value for every selected span terminal.  This is
 * the reference adapter against which direct value programs are checked.
 */
bool ppguarded_lex_cursor_v1_program_run_semantic_lattice_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    const CettaLpNativeUtf8Lattice *semantic_lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_cursor_v1_program_run_semantic_lattice(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    const CettaLpNativeUtf8Lattice *semantic_lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Execute the same cursor/action trace while deriving every selected span
 * value from the build-time-exported value programs.  No forest, token
 * lattice, or source re-decode is permitted on this path.
 */
bool ppguarded_lex_cursor_v1_program_run_semantic_direct_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_cursor_v1_program_run_semantic_direct(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Production byte-source entry point for a complete deterministic value
 * program.  UTF-8 is decoded exactly once and no prepared parser fallback is
 * available on this path.
 */
bool ppguarded_lex_cursor_v1_program_run_semantic_direct_bytes(
    const PPGuardedLexCursorV1Program *program,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Prepared byte-source entry point.  The program was validated when it was
 * bound, and the decoder constructs the trusted scalar view.  COUNTERS_ONLY
 * omits per-transition hashing; EXACT_TRACE retains the audit digest.
 */
bool ppguarded_lex_cursor_v1_program_run_semantic_direct_bytes_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Compact production path: the same deterministic recognition trace executes
 * projection-specialized actions and feeds the shared dense event consumer.
 * The returned Atoms belong to output_arena; no neutral semantic Atom tree is
 * constructed.
 */
bool ppguarded_lex_cursor_v1_program_run_projected_direct_bytes_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const PPAtomProjectionActionV1Program *projection_actions,
    const PPAtomProjectionV1Plan *projection,
    const PPGuardedLexCursorV1SemanticMaskBinding *semantic_masks,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    Arena *output_arena,
    uint32_t projection_depth_limit,
    PPGuardedLexCursorV1ProjectedResult *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Execute the generated value programs where their deterministic certificate
 * is complete.  A selected ordinary span whose value program is structurally
 * ineligible is parsed only over that exact already-decoded span with the
 * corresponding prepared ParserPack start state, then replayed to one value.
 * This path constructs neither an all-span token lattice nor a second source
 * decode.  Guarded-span fallbacks remain outside v1 and fail closed.
 */
bool ppguarded_lex_cursor_v1_program_run_semantic_hybrid_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const CettaLpNativeUtf8ScalarView *view,
    PPGuardedLexCursorV1PreparedBackend backend,
    uint32_t replay_depth,
    uint32_t result_limit,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_cursor_v1_program_run_semantic_hybrid(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const CettaLpNativeUtf8ScalarView *view,
    PPGuardedLexCursorV1PreparedBackend backend,
    uint32_t replay_depth,
    uint32_t result_limit,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Production byte-source entry point.  UTF-8 is decoded exactly once into an
 * owned scalar view, then the same validated hybrid program is executed.
 */
bool ppguarded_lex_cursor_v1_program_run_semantic_hybrid_bytes(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    PPGuardedLexCursorV1PreparedBackend backend,
    uint32_t replay_depth,
    uint32_t result_limit,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size);

void ppguarded_lex_exec_v1_plan_init(PPGuardedLexExecV1Plan *plan);
void ppguarded_lex_exec_v1_plan_free(PPGuardedLexExecV1Plan *plan);

bool ppguarded_lex_exec_v1_plan_build(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const RSNFAV1Plan *lexical_nfa,
    const RSNFAV1Plan *guard_nfa,
    const PPGuardedLexExecV1Limits *limits,
    PPGuardedLexExecV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

typedef struct {
    Arena arena;
    Atom **values;
    uint32_t len;
    PPLexV1WitnessOutcome outcome;
    uint32_t parser_runs;
    uint64_t work_item_len;
    uint32_t source_pass_count;
    char detail[512];
} PPGuardedLexWitnessV1;

typedef struct {
    char execution_plan_digest[65];
    char guard_relation_digest[65];
    char projected_relation_digest[65];
    char forest_digest[65];
    uint32_t source_pass_count;
    uint32_t dfa_scan_count;
    uint32_t input_byte_len;
    uint32_t input_scalar_len;
    uint32_t combined_token_len;
    uint32_t ordinary_token_len;
    uint32_t guard_body_token_len;
    uint32_t guarded_candidate_len;
    uint32_t guarded_token_len;
    uint32_t projected_token_len;
    uint32_t ordinary_witness_runs;
    uint32_t guard_witness_runs;
    uint32_t guarded_witness_runs;
    uint32_t ordinary_witness_parser_family_build_count;
    uint32_t ordinary_witness_prepared_start_len;
    uint32_t guard_witness_parser_family_build_count;
    uint32_t guard_witness_prepared_start_len;
    uint32_t guarded_witness_parser_family_build_count;
    uint32_t guarded_witness_prepared_start_len;
    uint32_t final_parser_table_build_count;
    CettaLpNativeSlrLatticeOutcome slr_outcome;
    uint32_t slr_parse_work_item_len;
    uint64_t final_parse_work_item_len;
    PPNativeV1Outcome outcome;
    bool accepted;
} PPGuardedLexExecV1Receipt;

typedef struct {
    RSDFAV1ScanResult scan;
    RSDFAV1Token *ordinary_tokens;
    RSDFAV1Token *guard_tokens;
    RSDFAV1Token *guarded_tokens;
    RSDFAV1Token *projected_tokens;
    uint32_t ordinary_token_len;
    uint32_t guard_token_len;
    uint32_t guarded_candidate_len;
    uint32_t guarded_token_len;
    uint32_t projected_token_len;
    RSDFAV1ParserLattice ordinary_lattice;
    RSDFAV1ParserLattice projected_lattice;
    PPLexV1WitnessTable ordinary_gll_witness;
    PPLexV1WitnessTable ordinary_glr_witness;
    PPGuardRelationV1 gll_relation;
    PPGuardRelationV1 glr_relation;
    /* Projected relations borrow Atom graphs from the ordinary/guarded
     * witness arenas and the corresponding relation arena.  They must be
     * freed before any of those three owners. */
    PPGuardRelationV1 gll_projected_relation;
    PPGuardRelationV1 glr_projected_relation;
    PPGuardRefV1Receipt gll_guard_witness;
    PPGuardRefV1Receipt glr_guard_witness;
    PPGuardedLexWitnessV1 guarded_gll_witness;
    PPGuardedLexWitnessV1 guarded_glr_witness;
    Atom **gll_witness_values;
    Atom **glr_witness_values;
    uint32_t witness_len;
    PPNativeV1Result slr;
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    PPGuardedLexExecV1Receipt receipt;
} PPGuardedLexExecV1Result;

void ppguarded_lex_exec_v1_result_init(PPGuardedLexExecV1Result *result);
void ppguarded_lex_exec_v1_result_free(PPGuardedLexExecV1Result *result);

/* Decode and traverse raw UTF-8 exactly once with the combined DFA. */
bool ppguarded_lex_exec_v1_run_bytes(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const uint8_t *input,
    size_t input_len,
    const PPGuardedLexExecV1Limits *limits,
    PPGuardedLexExecV1Result *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Traverse the supplied scalar view once with the combined DFA.  All later
 * recognizers consume lattices over that retained view and never decode or
 * rescan source bytes.
 */
bool ppguarded_lex_exec_v1_run(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const CettaLpNativeUtf8ScalarView *view,
    const PPGuardedLexExecV1Limits *limits,
    PPGuardedLexExecV1Result *out,
    char *error_buf,
    size_t error_buf_size);

#endif
