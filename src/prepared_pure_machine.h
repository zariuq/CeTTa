#ifndef CETTA_PREPARED_PURE_MACHINE_H
#define CETTA_PREPARED_PURE_MACHINE_H

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
typedef Atom *(*CettaPreparedPureBooleanValue)(Arena *arena, bool value);
typedef Atom *(*CettaPreparedPureConstructValue)(
    Arena *arena, Atom **elements, CettaExprLen length);
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
} CettaPreparedPureExpressionViewState;
typedef struct {
    Atom *projected;
    CettaPreparedPureValueObservation observation;
} CettaPreparedPureExpressionView;
/* Classify dialect-owned expression forms before the generic constructor
 * fallback.  A projection names the source child which is the form's value;
 * an observation names a pure outer-value discriminator whose operand is
 * data rather than an evaluation position.  Decline prevents an unhandled
 * form from falling through as inert syntax after shared prepared
 * instructions have had their chance.  Canonical-only rejects the form at
 * both compilation and dynamic execution boundaries.  Zero identifies a
 * dialect-owned no-result form; it is usable only as an explicitly guarded
 * equation branch and never becomes an ordinary prepared value. */
typedef CettaPreparedPureExpressionViewState
(*CettaPreparedPureExpressionViewFn)(
    const Atom *expression, CettaPreparedPureExpressionView *view);

/* A closed single-result program cannot represent a branch whose selected
 * arm has no answer.  Detect that direct answer-effect boundary before
 * constructing a program; false means only "not established" and never
 * licenses execution. */
bool cetta_prepared_pure_single_result_requires_answer_effect(
    const Atom *expression,
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
} CettaPreparedPureSourceView;

CettaPreparedPureProgram *cetta_prepared_pure_program_compile(
    Space *space, Atom *expression,
    VarId accumulator_var, VarId item_var,
    CettaGsltPureCallMode call_mode,
    CettaPreparedPureBooleanValue boolean_value,
    CettaPreparedPureConstructValue construct_value,
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
    CettaPreparedPureOpaqueValue opaque_value,
    CettaPreparedPureRegisterViewFn register_view,
    CettaPreparedPureExpressionViewFn expression_view,
    CettaPreparedPurePatternViewFn pattern_view,
    const CettaPreparedPureSourceView *source_view,
    bool entry_arguments_are_values,
    bool total_structural_equality,
    CettaMatchDecisionSemanticIdentity match_decision_semantics);

bool cetta_prepared_pure_program_is_current(
    const CettaPreparedPureProgram *program);

/* Rebind the runtime arguments of a previously compiled closed entry call.
 * The head and arity must match the compiled entry descriptor. */
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
 * until the cursor is closed or detached.  NULL declines, including when the
 * entry relation is not admitted. */
CettaPreparedPureAnswerCursor *cetta_prepared_pure_answer_cursor_open(
    CettaPreparedPureProgram *program,
    const CettaPreparedPureAnswerCursorOptions *options);

/* Produce the next answer.  An answer is borrowed until the next call on
 * this cursor.  After HANDOFF the frontier describes the remaining
 * enumeration and the cursor must not be advanced again. */
CettaPreparedPureCursorStep cetta_prepared_pure_answer_cursor_next(
    CettaPreparedPureAnswerCursor *cursor, Atom **answer_out);

/* Copy every value the frontier holds into the cursor's own arena, so that
 * nothing it holds borrows storage its caller may move or free.  An answer
 * borrowed before the call is released.  A cursor opened on a caller arena
 * has no arena of its own and is left as it is. */
bool cetta_prepared_pure_answer_cursor_detach(
    CettaPreparedPureAnswerCursor *cursor);

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

uint64_t cetta_prepared_pure_answer_cursor_answer_count(
    const CettaPreparedPureAnswerCursor *cursor);
uint64_t cetta_prepared_pure_answer_cursor_tail_call_count(
    const CettaPreparedPureAnswerCursor *cursor);

void cetta_prepared_pure_answer_cursor_close(
    CettaPreparedPureAnswerCursor *cursor);

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
void cetta_prepared_pure_program_release_closed_execution(
    CettaPreparedPureProgram *program);

void cetta_prepared_pure_program_free(
    CettaPreparedPureProgram *program);

#endif /* CETTA_PREPARED_PURE_MACHINE_H */
