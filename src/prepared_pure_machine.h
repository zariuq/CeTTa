#ifndef CETTA_PREPARED_PURE_MACHINE_H
#define CETTA_PREPARED_PURE_MACHINE_H

#include "owned_execution.h"

#include "match_decision.h"
#include "space.h"

/*
 * Revision-pinned machine for the deterministic, effect-free equation
 * fragment used by prepared folds.  The program compiler consumes the
 * generated control/register vocabulary and the space's equation graph;
 * execution uses positional slots and an explicit heap stack.  A program
 * outside the fragment is rejected and remains on the ordinary evaluator.
 */
typedef struct CettaPreparedPureProgram CettaPreparedPureProgram;
/* The caller holds the Program, arena and source/services exclusively until
 * close. These checks reject same-scope misuse; ordinary Program APIs and
 * reclamation are not independently thread-safe. Only step/readout reject
 * competing entry atomically on an already held execution. No collection is
 * performed in this initial bounded profile. */
typedef struct CettaPreparedPureClosedExecution CettaPreparedPureClosedExecution;
typedef enum {
    CETTA_PREPARED_CLOSED_SUSPENDED = 0,
    CETTA_PREPARED_CLOSED_ANSWER,
    CETTA_PREPARED_CLOSED_EXHAUSTED,
    CETTA_PREPARED_CLOSED_DECLINED,
    CETTA_PREPARED_CLOSED_INVALIDATED,
    CETTA_PREPARED_CLOSED_REFUSED,
    CETTA_PREPARED_CLOSED_SERVICE,
} CettaPreparedClosedStep;
typedef struct CettaPreparedServiceCut CettaPreparedServiceCut;
typedef struct {
    uint64_t ordinal;
    Atom *call;
    uint64_t ready_arguments;
    uint32_t operand_count, value_base;
    size_t frame_index;
    bool source_expression;
    bool normalize_result;
    /* Runtime sources and mixed-role applications retain operand origins. */
    bool export_source;
} CettaPreparedServiceRequest;

/* These operations borrow the same held execution, arena and source owner.
 * A reply's value is already evaluated in that interpretation. Unresolved
 * operands require a separate Need transport contract at the service. */
bool cetta_prepared_pure_closed_enable_service(
    CettaPreparedPureClosedExecution *, const void *owner);
bool cetta_prepared_pure_closed_service_request(
    CettaPreparedPureClosedExecution *, const void *owner,
    CettaPreparedServiceRequest *);
CettaPreparedServiceCut *cetta_prepared_pure_closed_service_capture(
    CettaPreparedPureClosedExecution *, const void *owner, uint64_t ordinal);
bool cetta_prepared_pure_closed_service_restore(
    CettaPreparedPureClosedExecution *, const void *owner, CettaPreparedServiceCut *);
bool cetta_prepared_pure_closed_service_reply(
    CettaPreparedPureClosedExecution *, Arena *, const void *owner,
    CettaPreparedServiceCut *, uint64_t ordinal, uint64_t reply_position, Atom *);
bool cetta_prepared_pure_closed_service_complete(
    CettaPreparedPureClosedExecution *, const void *owner,
    CettaPreparedServiceCut *, uint64_t ordinal, uint64_t reply_position);

/* The callback materializes one occurrence under the caller-held owner.
 * Equal syntax is not an origin. Refusal leaves the prepared cut unchanged;
 * the callback retains any partially materialized cells for an exact retry. */
typedef Atom *(*CettaPreparedServiceExportFn)(void *context,Atom *source,
    uint64_t source_occurrence_id,uint32_t source_argument_index,
    uint64_t reply_position,Atom *value,bool ready);
bool cetta_prepared_pure_closed_service_export(
    CettaPreparedPureClosedExecution *,Arena *,const void *owner,
    CettaPreparedServiceCut *,uint64_t ordinal,
    CettaPreparedServiceExportFn,void *context,Atom **result);

typedef enum {
    CETTA_PREPARED_INSTRUCTION_OTHER = 0,
    CETTA_PREPARED_INSTRUCTION_CALL,
    CETTA_PREPARED_INSTRUCTION_TAIL_CALL,
    CETTA_PREPARED_INSTRUCTION_RETURN,
    CETTA_PREPARED_INSTRUCTION_NONE,
    CETTA_PREPARED_NODE_CALL,
    CETTA_PREPARED_NODE_CALL_RETURN,
} CettaPreparedInstruction;
typedef struct {
    uint64_t performed, terminal_dispatches;
    size_t frames, slots, memo_entries;
    uint32_t pc, base, poll_countdown;
    uint32_t frame_state, node_kind;
    size_t values, contexts;
    bool register_code, runtime_frame;
    CettaPreparedInstruction next;
    CettaPreparedClosedStep status;
    const void *frame_storage, *slot_storage;
    const char *decline;
    bool optional_result;
    Atom *pending_answer;
} CettaPreparedClosedState;
CettaPreparedPureClosedExecution *cetta_prepared_pure_closed_open(
    CettaPreparedPureProgram *program, Arena *arena, const void *owner);
/* The optional register worker retains its exact instruction prefix. A zero
 * instruction completes empty; a value stays pending until explicit owner
 * acknowledgement. Repeated observation, including zero allowance, returns
 * the same borrowed value and performs no instruction or terminal probe. */
CettaPreparedPureClosedExecution *cetta_prepared_pure_closed_open_optional(
    CettaPreparedPureProgram *program, Arena *arena, const void *owner);
bool cetta_prepared_pure_closed_accept_optional(
    CettaPreparedPureClosedExecution *execution, const void *owner, Atom *answer);
CettaPreparedClosedStep cetta_prepared_pure_closed_step(
    CettaPreparedPureClosedExecution *execution, Arena *arena, const void *owner,
    uint64_t allowance, uint64_t *spent, Atom **answer);
bool cetta_prepared_pure_closed_read(
    CettaPreparedPureClosedExecution *execution, CettaPreparedClosedState *state);
/* Read-only validation of an externally held exclusive invocation. This
 * checks the existing reservation; it neither acquires lifetime nor resumes.
 * Earned optional outcomes retain that reservation after source edits;
 * unfinished work additionally requires its current source view. */
bool cetta_prepared_pure_closed_owned_by(
    CettaPreparedPureClosedExecution *execution,
    const CettaPreparedPureProgram *program, const Arena *arena, const void *owner);
bool cetta_prepared_pure_closed_close(
    CettaPreparedPureClosedExecution *execution, const void *owner);

typedef Atom *(*CettaPreparedPureBooleanValue)(Arena *arena, bool value);
typedef Atom *(*CettaPreparedPureConstructValue)(
    Arena *arena, Atom **elements, CettaExprLen length);
/* Whether the constructor builds, for every value of `length` elements
 * headed by `head`, exactly the expression atom_expr builds.  The machine
 * then builds such a value with atom_expr itself.  NULL: no head is known
 * to, and every value goes through the constructor. */
typedef bool (*CettaPreparedPureConstructsExpression)(
    const Atom *head, CettaExprLen length);
/* The classifier of atom_expr as a constructor: every head. */
bool cetta_prepared_pure_constructs_expression_always(
    const Atom *head, CettaExprLen length);
typedef bool (*CettaPreparedPureOpaqueValue)(const Atom *value);
typedef bool (*CettaPreparedPureInterruptPollFn)(void *context);
/* Map a dialect-owned syntax head to a semantic register instruction.
 * The generated common-head table is consulted first; this hook exists for
 * spellings whose meaning differs between compositions. */
typedef bool (*CettaPreparedPureRegisterViewFn)(
    SymbolId head, CettaExprLen arity,
    CettaGsltRegisterResultKind *result_kind,
    CettaGsltRegisterInstruction *instruction);
typedef enum {
    CETTA_PREPARED_PURE_OBSERVE_IS_EXPRESSION = 0,
} CettaPreparedPureValueObservation;
typedef enum {
    CETTA_PREPARED_PURE_EXPRESSION_DEFAULT = 0,
    CETTA_PREPARED_PURE_EXPRESSION_PROJECT,
    CETTA_PREPARED_PURE_EXPRESSION_OBSERVE,
    CETTA_PREPARED_PURE_EXPRESSION_DECLINE,
    CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY,
    CETTA_PREPARED_PURE_EXPRESSION_ZERO,
    /* Project only inside a closed single-result attempt. If evaluation
     * declines, the caller must discard the attempt and run the original
     * expression. Never valid for a producer that publishes answers. */
    CETTA_PREPARED_PURE_EXPRESSION_PROJECT_SINGLE_RESULT,
    /* A dialect-owned value. Inspection must not enter its payload. */
    CETTA_PREPARED_PURE_EXPRESSION_OPAQUE_VALUE,
} CettaPreparedPureExpressionViewState;
typedef enum {
    CETTA_PREPARED_PURE_PROJECT_SOURCE = 0,
    /* Dispatch the outer application; retain its children's source roles. */
    CETTA_PREPARED_PURE_PROJECT_APPLICATION,
    /* Interpret the entire projected term in the current program. */
    CETTA_PREPARED_PURE_PROJECT_CODE,
} CettaPreparedPureProjectionMode;
typedef struct {
    Atom *projected;
    CettaPreparedPureProjectionMode projection_mode;
    CettaPreparedPureValueObservation observation;
} CettaPreparedPureExpressionView;
/* Classify dialect-owned atoms before the generic value/constructor
 * fallback.  A projection names the source child which is the form's value;
 * an observation names a pure outer-value discriminator whose operand is
 * data rather than an evaluation position.  Decline prevents an unhandled
 * form from falling through as inert syntax after shared prepared
 * instructions have had their chance.  Canonical-only rejects the form at
 * both compilation and dynamic execution boundaries.  Zero identifies a
 * dialect-owned no-result form; it is usable only as an explicitly guarded
 * equation branch and never becomes an ordinary prepared value. The hook
 * also receives atomic syntax, such as HE's no-result symbol Empty, and the
 * actual source Space whose revision the prepared program retains. */
typedef CettaPreparedPureExpressionViewState
(*CettaPreparedPureExpressionViewFn)(
    Space *space, const Atom *expression, CettaPreparedPureExpressionView *view);

/* A closed single-result program cannot represent a branch whose selected
 * arm has no answer.  Detect that direct answer-effect boundary before
 * constructing a program; false means only "not established" and never
 * licenses execution. */
bool cetta_prepared_pure_single_result_requires_answer_effect(
    Space *space, const Atom *expression,
    CettaPreparedPureExpressionViewFn expression_view);
typedef enum {
    CETTA_PREPARED_PURE_PATTERN_VIEW_NOT_APPLICABLE = 0,
    CETTA_PREPARED_PURE_PATTERN_VIEW_MISMATCH,
    CETTA_PREPARED_PURE_PATTERN_VIEW_DECOMPOSE,
    /* The form applies, but only the dialect's own matcher can decide this
     * value; prepared matching declines rather than guess. */
    CETTA_PREPARED_PURE_PATTERN_VIEW_UNDECIDED,
} CettaPreparedPurePatternViewState;
typedef struct {
    Atom *const *pattern_children;
    Atom *const *value_children;
    CettaExprLen child_count;
} CettaPreparedPurePatternView;
/* Whether a view applies is a property of the pattern: NOT_APPLICABLE for
 * one value means NOT_APPLICABLE for every value.  The prepared compiler
 * relies on this to compile the matching of patterns no view form reaches. */
typedef CettaPreparedPurePatternViewState
(*CettaPreparedPurePatternViewFn)(
    const Atom *pattern, const Atom *value,
    CettaPreparedPurePatternView *view);

/*
 * Optional language-owned evidence for the authored occurrence paired with
 * an Atom.  The prepared machine sees only a small role algebra and the
 * occurrence tree; the language keeps its concrete planning representation
 * private.  A missing view preserves the ordinary live-space classifier.
 *
 * The equation callback supplies the authored RHS view for an exact live
 * equation occurrence.  Refusal is an accelerator decline, leaving the
 * canonical evaluator authoritative.
 */
typedef enum {
    CETTA_PREPARED_PURE_SOURCE_UNSPECIFIED = 0,
    CETTA_PREPARED_PURE_SOURCE_VALUE,
    CETTA_PREPARED_PURE_SOURCE_DATA,
    CETTA_PREPARED_PURE_SOURCE_CALL,
    CETTA_PREPARED_PURE_SOURCE_DYNAMIC_CALL,
    CETTA_PREPARED_PURE_SOURCE_DECLINE,
} CettaPreparedPureSourceRole;
typedef CettaPreparedPureSourceRole
(*CettaPreparedPureSourceRoleFn)(
    void *context, const void *source_view);
typedef const void *(*CettaPreparedPureSourceChildFn)(
    void *context, const void *source_view,
    CettaExprIndex child_index);
typedef bool (*CettaPreparedPureEquationSourceFn)(
    void *context, Space *space, SymbolId head,
    size_t occurrence_ordinal,
    SpaceEquationOccurrenceId occurrence,
    const Atom *equation, const void **rhs_view_out);
typedef struct {
    void *context;
    const void *root;
    CettaPreparedPureSourceRoleFn role;
    CettaPreparedPureSourceChildFn child;
    CettaPreparedPureEquationSourceFn equation_rhs;
    /* Decline a named executable occurrence whose admitted rule semantics
     * this compiler cannot realize. Checked during compilation, including
     * callees, and at runtime-created application boundaries. */
    bool (*head_admitted)(void *context, Space *space, SymbolId head);
} CettaPreparedPureSourceView;

CettaPreparedPureProgram *cetta_prepared_pure_program_compile(
    Space *space, Atom *expression,
    VarId accumulator_var, VarId item_var,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics);

/* Compile a closed, deterministic, effect-free expression into the same
 * explicit-stack machine used by prepared folds.  Callable arguments are
 * retained as machine suspensions and forced according to call_mode. */
CettaPreparedPureProgram *cetta_prepared_pure_program_compile_closed(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics);

/* Compile an eager, effect-free at-most-one worker from actual equations.
 * Arguments must have the supplied value interpretation; unsupported code
 * and potentially overlapping answers refuse admission. No annotation is
 * used as evidence, and no commitment is introduced. */
CettaPreparedPureProgram *cetta_prepared_pure_program_compile_closed_optional(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics);

/* Retained call-by-need service profile. Closed canonical source nodes issue
 * occurrence-bound requests; the supplied owner must enable/drive services
 * and acknowledge every reply through the retained execution API. Open
 * lexical source requests still decline. Ordinary execute APIs refuse this
 * Program; source/call authority remains the embedding owner's obligation. */
CettaPreparedPureProgram *cetta_prepared_pure_program_compile_closed_service(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics);

/* Compile the finite answer-producing fragment of a closed pure relation.
 * An equation whose body is a fully determined value, or a pure tail call on
 * fully determined arguments, answers directly.  Any other body is lowered to
 * steps: deterministic regions evaluate on the machine, while calls that may
 * choose, let patterns, conditional branches and choice zero become steps that
 * resume after each answer.  Unsupported answer effects decline compilation;
 * they are never interpreted as an empty answer set. */
CettaPreparedPureProgram *cetta_prepared_pure_program_compile_closed_answers(
    Space *space, Atom *expression,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
    CettaPreparedPureConstructsExpression constructs_expression,
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics);

/* Every reachable authored head has disjoint WHNF alternatives. This is
 * classification only; it executes no call and grants no service authority. */
bool cetta_prepared_pure_program_all_heads_determinate(
    const CettaPreparedPureProgram *program);

bool cetta_prepared_pure_program_is_current(
    const CettaPreparedPureProgram *program);

/* The compiler-selected result representation, without executing a body.
 * This inspection grants neither source freshness nor execution authority. */
bool cetta_prepared_pure_program_returns_optional(
    const CettaPreparedPureProgram *program);

/* Rebind the runtime arguments of a previously compiled closed entry call.
 * The head, arity and argument role promised at compilation must still hold;
 * this low-level operation cannot recheck a dialect's value predicate. */
bool cetta_prepared_pure_program_rebind_closed_entry_call(
    CettaPreparedPureProgram *program, Atom *expression);

/* Release every invocation-owned argument reference before a reusable entry
 * program is parked.  Compiled code and its revision pin remain intact. */
void cetta_prepared_pure_program_clear_closed_entry_call(
    CettaPreparedPureProgram *program);

bool cetta_prepared_pure_program_execute(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item, Atom **result_out);

bool cetta_prepared_pure_program_execute_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out);

/* Execute using a caller-owned scratch arena.  A nonzero nursery budget
 * enables copying collection at the machine's generated-root loop boundary;
 * zero preserves the same execution without collection.  A collected result
 * can reside in the program's invocation survivor arena, so publish it only
 * after copying or materializing it; see release_closed_execution below. */
bool cetta_prepared_pure_program_execute_closed(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    Atom **result_out);

bool cetta_prepared_pure_program_execute_closed_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out);

typedef enum {
    CETTA_PREPARED_PURE_OPTIONAL_VALUE = 0,
    CETTA_PREPARED_PURE_OPTIONAL_EMPTY,
    CETTA_PREPARED_PURE_OPTIONAL_DECLINED,
    CETTA_PREPARED_PURE_OPTIONAL_INTERRUPTED,
    CETTA_PREPARED_PURE_OPTIONAL_INVALIDATED,
} CettaPreparedPureOptionalResult;

/* Only the dialect's explicit zero operation produces EMPTY. An unmatched
 * call or unsupported operation declines without claiming emptiness. VALUE
 * is borrowed under the same scratch/survivor lifetime as closed execution.
 * This pure synchronous ABI supplies no retained resumption or effect rights. */
CettaPreparedPureOptionalResult cetta_prepared_pure_program_execute_closed_optional(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out);

typedef bool (*CettaPreparedPureAnswerVisitorFn)(
    Atom *answer, void *context);

typedef enum {
    CETTA_PREPARED_PURE_ANSWERS_DECLINED = 0,
    CETTA_PREPARED_PURE_ANSWERS_COMPLETE = 1,
    CETTA_PREPARED_PURE_ANSWERS_STOPPED = 2,
    CETTA_PREPARED_PURE_ANSWERS_LIMIT = 3,
} CettaPreparedPureAnswersResult;

typedef struct {
    uint64_t max_transitions;
    size_t max_scratch_bytes;
    /* Must bound one construct_value call's live arena allocation with
     * hash-consing disabled.  No callback means constructed values decline.
     * Native and dialect-owned constructors supply separate cost adapters. */
    bool (*construct_allocation_bound)(CettaExprLen length, size_t *bytes_out);
} CettaPreparedPureAnswerLimits;

/* What a call that matches no equation answers, by dialect. */
typedef enum {
    /* Nothing: the call fails. */
    CETTA_PREPARED_PURE_UNMATCHED_FAILS = 0,
    /* The cursor stops with the call on its frontier. */
    CETTA_PREPARED_PURE_UNMATCHED_DECLINES,
    /* One answer: the call itself, as data. */
    CETTA_PREPARED_PURE_UNMATCHED_REDUCES_TO_ITSELF,
} CettaPreparedPureUnmatchedCall;

/* Enumerate a compiled closed answer producer in authored occurrence order.
 * COMPLETE includes the legitimate zero-answer case.  DECLINED means the
 * canonical evaluator remains authoritative; STOPPED means the caller or
 * interrupt observer requested an early boundary.  LIMIT is physical budget
 * exhaustion, never logical zero or completion.  All earlier visitor calls
 * are provisional unless a consumer has a separate partial-stream contract.
 * Limits bound loop transitions and charged constructor allocations; input,
 * compiled metadata, matcher workspace, arena block overhead, and output
 * publication have separate ownership and resource obligations.  A NULL
 * arena gives the enumeration a private arena it reclaims as it goes; an
 * answer is then valid only during its visit. */
CettaPreparedPureAnswersResult
cetta_prepared_pure_program_visit_closed_answers(
    CettaPreparedPureProgram *program, Arena *arena,
    const CettaPreparedPureAnswerLimits *limits,
    CettaPreparedPureUnmatchedCall unmatched_call,
    CettaPreparedPureAnswerVisitorFn visitor, void *visitor_context,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, uint64_t *answer_count_out,
    uint64_t *tail_call_count_out);

/* A suspended enumeration of a closed answer producer.  Its state is the
 * depth-first frontier of the producer: a stack of calls, each with the
 * ordinal of its next untried equation occurrence.  Every step either
 * completes or leaves that frontier exactly as it was, so a consumer that
 * cannot continue here can resume the same enumeration elsewhere from the
 * frontier. */
typedef struct CettaPreparedPureAnswerCursor CettaPreparedPureAnswerCursor;

typedef enum {
    CETTA_PREPARED_PURE_CURSOR_ANSWER = 0,
    CETTA_PREPARED_PURE_CURSOR_EXHAUSTED = 1,
    /* The frontier is intact and the reason says why this cursor stopped. */
    CETTA_PREPARED_PURE_CURSOR_HANDOFF = 2,
} CettaPreparedPureCursorStep;

typedef enum {
    CETTA_PREPARED_PURE_HANDOFF_NONE = 0,
    /* The next step leaves the producer fragment, or enters a relation its
     * consumer did not admit. */
    CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED = 1,
    /* A call matched no equation and the cursor was opened to decline
     * rather than read that as zero answers. */
    CETTA_PREPARED_PURE_HANDOFF_NO_MATCH = 2,
    /* The transition or scratch purse is spent. */
    CETTA_PREPARED_PURE_HANDOFF_LIMIT = 3,
    CETTA_PREPARED_PURE_HANDOFF_INTERRUPT = 4,
    /* The Space program the producer was compiled from has changed. */
    CETTA_PREPARED_PURE_HANDOFF_STALE = 5,
} CettaPreparedPureHandoffReason;

/* Asked once per relation, when the cursor first enters it and before any of
 * its equations is tried.  False stops the cursor with that call on the
 * frontier. */
typedef bool (*CettaPreparedPureHeadAdmissionFn)(
    void *context, SymbolId head);

typedef struct {
    /* NULL gives the cursor a private scratch arena, from which it reclaims
     * the storage of consumed answers and finished calls.  A caller arena
     * keeps every answer valid until the caller resets it. */
    Arena *arena;
    CettaPreparedPureAnswerLimits limits;
    CettaPreparedPureUnmatchedCall unmatched_call;
    CettaPreparedPureInterruptPollFn interrupt_poll;
    void *interrupt_context;
    /* Steps between interrupt polls; zero means every step. */
    uint32_t interrupt_poll_interval;
    /* Optional; every relation is admitted without it. */
    CettaPreparedPureHeadAdmissionFn head_admission;
    void *head_admission_context;
    /* A program with a body that resumes after a call keeps continuations on
     * its frontier.  That frontier has no equation-choice reading.  A consumer
     * may open it when the cursor runs those continuations itself.  On handoff
     * the consumer must not rebuild a continuation frame as an equation
     * choice: that either replays the call or publishes the callee in place of
     * the continuation.  Cancellation and resource exhaustion are not logical
     * completion, and answers already delivered stay delivered. */
    bool allow_continuations;
} CettaPreparedPureAnswerCursorOptions;

/* Open a cursor at the program's bound closed entry call.  The cursor
 * retains the program; it borrows the entry arguments, which must stay valid
 * until the cursor is closed or detached. The source Space/read view remains
 * borrowed from the host and must outlive the cursor; detachment moves value
 * storage, not semantic authority. Revision changes cause a stale handoff.
 * NULL declines, including when the
 * entry relation is not admitted. */
CettaPreparedPureAnswerCursor *cetta_prepared_pure_answer_cursor_open(
    CettaPreparedPureProgram *program,
    const CettaPreparedPureAnswerCursorOptions *options);

/* Produce the next answer.  An answer is borrowed until the next call on
 * this cursor.  After HANDOFF the frontier describes the remaining
 * enumeration and the cursor must not be advanced again. */
CettaPreparedPureCursorStep cetta_prepared_pure_answer_cursor_next(
    CettaPreparedPureAnswerCursor *cursor, Atom **answer_out);

/* Read-only completion evidence for the retained frontier. True means every
 * remaining call has matched and has no untried equation, so only reclamation
 * remains. This executes no body, continuation, guard or interrupt observer;
 * it does not release the borrowed answer. An unmatched call, lost authority
 * or a handoff cannot establish completion. */
bool cetta_prepared_pure_answer_cursor_frontier_is_complete(
    const CettaPreparedPureAnswerCursor *cursor);

/* Whether detach can release entry-argument borrows into a private arena.
 * Caller-arena cursors and programs with unsupported continuation locals
 * are not admitted. This query does not mutate the cursor or allocate; a
 * subsequent detach can still fail on allocation. */
bool cetta_prepared_pure_answer_cursor_can_detach(
    const CettaPreparedPureAnswerCursor *cursor);

/* Copy every value the frontier holds into the cursor's own arena, so that
 * nothing it holds borrows storage its caller may move or free.  An answer
 * borrowed before the call is released.  A cursor opened on a caller arena
 * has no arena of its own and is left as it is. */
bool cetta_prepared_pure_answer_cursor_detach(
    CettaPreparedPureAnswerCursor *cursor);

/* Transfer the cursor to a resumable return/sequence owner. Completed answers
 * are loaned unchanged; a handoff preserves the cursor's complete residual. */
void cetta_prepared_pure_answer_cursor_own_execution(
    CettaOwnedExecution *execution, CettaPreparedPureAnswerCursor *cursor);
/* The cursor's native return adapter, also usable by an owner that adds
 * independent host-authority checks around the same stored computation. */
CettaOwnedExecutionStep cetta_prepared_pure_answer_cursor_return(
    void *cursor, CettaOwnedReturn *returned, CettaEvalCompletion *completion);

/* Read the retained equation frontier without interpreting or polling it.
 * Untried equations, no-rule observations, handoff and stale source authority
 * refuse completion. A withdrawn answer restores an unfinished equation. */
bool cetta_prepared_pure_answer_cursor_frontier_is_complete(
    const CettaPreparedPureAnswerCursor *cursor);

/* Withdraw the answer just produced: restore the frontier to the state
 * before the step that produced it.  A consumer calls this when the answer
 * needs a treatment the producer does not own. */
bool cetta_prepared_pure_answer_cursor_unyield(
    CettaPreparedPureAnswerCursor *cursor);

CettaPreparedPureHandoffReason cetta_prepared_pure_answer_cursor_handoff_reason(
    const CettaPreparedPureAnswerCursor *cursor);

/* The Space program the producer reads. */
SpaceProgramToken cetta_prepared_pure_answer_cursor_program_token(
    const CettaPreparedPureAnswerCursor *cursor);

/* Calls entered so far, by head, in first-entry order. */
size_t cetta_prepared_pure_answer_cursor_head_count(
    const CettaPreparedPureAnswerCursor *cursor);
SymbolId cetta_prepared_pure_answer_cursor_head(
    const CettaPreparedPureAnswerCursor *cursor, size_t index);

/* The frontier, outermost call first.  `next_ordinal` indexes the head's
 * equation occurrences in Space cursor order.  While it is below
 * `equation_count`, `next_equation` and `next_logical_index` name that
 * occurrence.  Arguments are owned by the cursor. */
typedef struct {
    SymbolId head;
    Atom *const *arguments;
    uint32_t arity;
    uint32_t next_ordinal;
    uint32_t equation_count;
    const Atom *next_equation;
    CettaIndex next_logical_index;
    /* This call's answers resume a caller's step program.  They are not
     * themselves the consumer's answers. */
    bool resumes_continuation;
} CettaPreparedPureAnswerFrame;

size_t cetta_prepared_pure_answer_cursor_frame_count(
    const CettaPreparedPureAnswerCursor *cursor);
bool cetta_prepared_pure_answer_cursor_frame(
    const CettaPreparedPureAnswerCursor *cursor, size_t index,
    CettaPreparedPureAnswerFrame *frame_out);

/* A remaining authored equation occurrence, under the cursor's current
 * source authority. This reads retained code; it does not restart its call. */
Atom *cetta_prepared_pure_answer_cursor_frame_equation(
    const CettaPreparedPureAnswerCursor *cursor, size_t index, uint32_t ordinal);

/* Copy a value the cursor holds into the caller's arena. */
typedef Atom *(*CettaPreparedPureImportValueFn)(void *context, Atom *value);

/* The source computation a frame's answers still owe: its chain of pending
 * callers decoded from their resume templates, innermost inside, with `hole`
 * standing for the frame's answer.  Solving the result once per answer of the
 * frame's call gives exactly the answers the cursor would have delivered for
 * that call, in the same order; operands the callers had already evaluated
 * appear as their values and are not evaluated again.  False when the frame
 * resumes no continuation or a pending caller has no source reading. */
bool cetta_prepared_pure_answer_cursor_frame_resumption(
    const CettaPreparedPureAnswerCursor *cursor, size_t index, Arena *arena,
    Atom *hole, CettaPreparedPureImportValueFn import_value,
    void *import_context, Atom **term_out);

uint64_t cetta_prepared_pure_answer_cursor_answer_count(
    const CettaPreparedPureAnswerCursor *cursor);
uint64_t cetta_prepared_pure_answer_cursor_tail_call_count(
    const CettaPreparedPureAnswerCursor *cursor);

/* Private execution storage, including external bytes accounted by its arena.
 * Caller-arena cursors report zero arena bytes; their metadata remains owned.
 * The retained compiled program and temporary copy workspace are separate. Collection
 * moves live values only; it is not a producer firing or a semantic charge. */
typedef struct {
    size_t live_bytes;
    size_t reserved_bytes;
    size_t metadata_bytes;
    uint64_t collections;
    uint64_t copied_bytes;
} CettaPreparedPureCursorStorage;
CettaPreparedPureCursorStorage cetta_prepared_pure_answer_cursor_storage(
    const CettaPreparedPureAnswerCursor *cursor);

void cetta_prepared_pure_answer_cursor_close(
    CettaPreparedPureAnswerCursor *cursor);
void cetta_prepared_pure_answer_cursor_storage_census(
    const CettaPreparedPureAnswerCursor *cursor, CettaStorageCensus *census);

/* Programs are reference counted: the creator holds one reference and
 * cetta_prepared_pure_program_free releases one. */
CettaPreparedPureProgram *cetta_prepared_pure_program_retain(
    CettaPreparedPureProgram *program);

/* The head and arity of each equation occurrence the program compiled. */
size_t cetta_prepared_pure_program_equation_count(
    const CettaPreparedPureProgram *program);
bool cetta_prepared_pure_program_equation_signature(
    const CettaPreparedPureProgram *program, size_t index,
    SymbolId *head_out, uint32_t *arity_out);

/* A value the host attaches to a program, zero until set: for example the
 * revision at which the host last admitted it. */
uint64_t cetta_prepared_pure_program_host_stamp(
    const CettaPreparedPureProgram *program);
void cetta_prepared_pure_program_set_host_stamp(
    CettaPreparedPureProgram *program, uint64_t stamp);

/* Resume a closed expression through the revision-pinned code and semantic
 * views of an existing Need program.  This is the producer-side dual of the
 * prepared fold API: callers may retain a small continuation expression and
 * request only its next weak-head value, without rebuilding the original
 * entry result.  The expression remains caller-owned.  The result is borrowed
 * from either the scratch or invocation survivor arena and must be consumed
 * before the next execution, explicit release, or program destruction. */
bool cetta_prepared_pure_program_execute_closed_expression_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *expression, size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, Atom **result_out);

/* True when the program's dialect-aware callable classifier finds no
 * suspended computation anywhere in value.  Producer consumers use this to
 * preserve eager element evaluation while allowing the collection tail to
 * remain suspended. */
bool cetta_prepared_pure_program_value_is_fully_evaluated(
    CettaPreparedPureProgram *program, Atom *value);

/* A collected closed execution can return a result owned by the program's
 * invocation survivor arena.  The result remains valid until the next
 * execution, program destruction, or this explicit release.  A caller which
 * publishes the result into another arena must copy or materialize it first. */
/* The arena holding a closed execution's collection survivors, or NULL when
 * no collection ran or a held execution keeps them; it dies with
 * cetta_prepared_pure_program_release_closed_execution. */
Arena *cetta_prepared_pure_program_closed_survivor_arena(
    CettaPreparedPureProgram *program);
void cetta_prepared_pure_program_release_closed_execution(
    CettaPreparedPureProgram *program);

void cetta_prepared_pure_program_free(
    CettaPreparedPureProgram *program);

void cetta_prepared_pure_program_storage_census(const CettaPreparedPureProgram *program, CettaStorageCensus *census);

#endif /* CETTA_PREPARED_PURE_MACHINE_H */
