#ifndef CETTA_PREPARED_PURE_MACHINE_H
#define CETTA_PREPARED_PURE_MACHINE_H

#include "owned_execution.h"
#include "lang.h"

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

typedef enum {
    CETTA_PREPARED_PURE_COMPILE_UNCERTIFIED,
    CETTA_PREPARED_PURE_COMPILE_READY,
    CETTA_PREPARED_PURE_COMPILE_INPUT_REFUSED,
    CETTA_PREPARED_PURE_COMPILE_RESOURCE_REFUSED,
    CETTA_PREPARED_PURE_COMPILE_INVALIDATED,
    CETTA_PREPARED_PURE_COMPILE_STABLE_REFUSED,
} CettaPreparedPureCompileStatus;

enum {
    CETTA_PREPARED_PURE_REFUSE_SCALAR = 1u,
    CETTA_PREPARED_PURE_REFUSE_ANSWERS = 2u,
};

/* Preparation metadata, never an evaluation outcome. A failure can certify
 * the other demand without certifying its own: a prior answer gate survives
 * a later uncertified scalar failure. Unclassified failures remain retryable.
 * The source is borrowed for the duration of its owner's preparation scope. */
typedef struct {
    SpaceProgramToken source;
    CettaPreparedPureCompileStatus status;
    uint8_t refusals;
} CettaPreparedPureCompileReport;

static inline bool cetta_prepared_pure_compile_refused(
        const CettaPreparedPureCompileReport *report, Space *space, bool answers) {
    return report &&
        (report->refusals & (answers ? CETTA_PREPARED_PURE_REFUSE_ANSWERS
                                   : CETTA_PREPARED_PURE_REFUSE_SCALAR)) != 0u &&
        space_program_token_matches_live_space(report->source, space);
}
/* Whether the prepared machine can call `head` with `arity` arguments: the
 * dialect leaves the head untyped, or it is an HE head whose one signature
 * has that arity and value-class parameters and range (Number, String, Bool
 * or %Undefined%), which the machine checks where HE casts them. */
bool cetta_prepared_pure_call_policy_supported(
    Space *space, SymbolId head, CettaExprLen arity);
/* Authored dialect restrictions are shared by source admission, lowering
 * and execution. Unscoped intrinsics retain their existing policy. */
bool cetta_prepared_pure_intrinsic_scope_supported(SymbolId head, CettaExprLen arity);
/* Shared interpretation of the authored strict primitive fragment. This is
 * operation metadata, not source authority or permission to speculate. */
bool cetta_prepared_pure_intrinsic_program(
    SymbolId head, CettaExprLen arity,
    CettaGsltPreparedPureIntrinsicInstruction *instruction_out);
/* A pure constructor, called during preparation as well as execution. Its
 * false/true images must be distinct and preserve canonical Boolean polarity.
 * Plain symbol/Bool images are cached by the program and decoded at Boolean
 * operation/branch boundaries. Data projections do not acquire that role. */
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
    CETTA_PREPARED_PURE_OBSERVE_IS_VARIABLE,
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
    /* A committed first-match selection: operand 1's value is tested
     * against the patterns of operand 2's (pattern body) arms in order, and
     * the first that matches answers its body under the pattern's bindings.
     * No matching arm answers nothing.  A pattern any part of which
     * computes is left to the canonical evaluator. */
    CETTA_PREPARED_PURE_EXPRESSION_SELECT,
    /* The complete expression is syntax data. Substitute lexical slots and
     * build its fields without interpreting a head or demanding an operand. */
    CETTA_PREPARED_PURE_EXPRESSION_DATA,
} CettaPreparedPureExpressionViewState;
typedef enum {
    CETTA_PREPARED_PURE_PROJECT_SOURCE = 0,
    /* Dispatch the outer application; retain its children's source roles. */
    CETTA_PREPARED_PURE_PROJECT_APPLICATION,
    /* Interpret the entire projected term in the current program. */
    CETTA_PREPARED_PURE_PROJECT_CODE,
    /* The same, where each variable of the term holds a value in place of
     * which the term is read: a value that is an expression would run as
     * code, so the machine declines it where it runs. */
    CETTA_PREPARED_PURE_PROJECT_CODE_OVER_VALUES,
} CettaPreparedPureProjectionMode;
typedef struct {
    Atom *projected;
    CettaPreparedPureProjectionMode projection_mode;
    CettaPreparedPureValueObservation observation;
} CettaPreparedPureExpressionView;
/* Classify dialect-owned expression forms before the generic constructor
 * fallback, and the symbols a dialect reserves for its own evaluator (only
 * canonical-only is read for a symbol).  A projection names the source child which is the form's value;
 * an observation names a pure outer-value discriminator whose operand is
 * data rather than an evaluation position.  Decline prevents an unhandled
 * form from falling through as inert syntax after shared prepared
 * instructions have had their chance.  Canonical-only rejects the form at
 * both compilation and dynamic execution boundaries.  Zero identifies a
 * dialect-owned no-result form; it is usable only as an explicitly guarded
 * equation branch and never becomes an ordinary prepared value. The optional
 * computation is the borrowed word of this occurrence, valid for this call
 * only. NULL requests fresh syntax interpretation; a retained operation
 * governs the stage even when the operand carrier has a different head. */
typedef CettaPreparedPureExpressionViewState
(*CettaPreparedPureExpressionViewFn)(
    const Atom *expression, const CettaComputationPlan *computation,
    CettaPreparedPureExpressionView *view);

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
 * an Atom. The prepared machine receives the common computation word, a
 * small source-authority algebra and the occurrence tree; the language keeps
 * its concrete planning representation private. A missing view preserves
 * the ordinary live-space classifier.
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
/* Immutable executable metadata for an admitted source occurrence. NONE
 * means this adapter supplies no word and needs fresh dialect elaboration.
 * An invocation without a specialised native body still enters its callee. */
typedef CettaComputationPlan (*CettaPreparedPureSourceComputationFn)(
    void *context, const void *source_view);
typedef const void *(*CettaPreparedPureSourceChildFn)(
    void *context, const void *source_view,
    CettaExprIndex child_index);
typedef bool (*CettaPreparedPureEquationSourceFn)(
    void *context, Space *space, SymbolId head,
    size_t occurrence_ordinal,
    SpaceEquationOccurrenceId occurrence,
    const Atom *equation, const void **rhs_view_out);

/* The normalized reading of an existing computation node. Captured slots
 * and completed literals become value places; surviving operations retain
 * their computation words. Projection consults immutable source metadata,
 * never the live Space or the shape of a returned value. The adapter owns
 * the resulting plan for at least as long as its execution host. */
typedef struct {
    const void *source;
    CettaPreparedPureSourceRole role;
    CettaComputationPlan computation;
    const void *const *children;
    CettaExprLen child_count;
    bool relation_head_admitted;
} CettaPreparedPureSourceProjection;
typedef const void *(*CettaPreparedPureProjectSourceFn)(
    void *context, const CettaPreparedPureSourceProjection *projection);
typedef struct {
    void *context;
    const void *root;
    CettaCallResultPolicy result_policy;
    CettaPreparedPureSourceRoleFn role;
    CettaPreparedPureSourceComputationFn computation;
    CettaPreparedPureSourceChildFn child;
    CettaPreparedPureEquationSourceFn equation_rhs;
    CettaPreparedPureProjectSourceFn project_source;
    /* Decline a named executable occurrence whose admitted rule semantics
     * this compiler cannot realize. Checked during compilation, including
     * callees, and at runtime-created application boundaries. */
    bool (*head_admitted)(void *context, Space *space, SymbolId head);
    /* Execute a native whose head/arity was positively admitted by the
     * generated intrinsic contract. False declines before native entry.
     * True transfers the tagged result, including zero or raised control.
     * The native context is separate because a language's source-plan owner
     * and its library registry have distinct lifetimes and representations. */
    void *native_context;
    bool (*native_call)(
        void *context, Space *space, Arena *arena,
        const CettaInvocation *invocation, CettaCallOutcome *outcome);
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

/* Admit native entry only at a terminal node of an open eager body. All
 * operands and every compiled callee are native-free. Controlled execution
 * then transfers the native packet to its caller, which owns completion and
 * the remaining continuation. Refusal has entered no native operation. */
bool cetta_prepared_pure_program_use_native_tail_returns(
    CettaPreparedPureProgram *program);

/* Compile a closed, deterministic, effect-free expression into the same
 * explicit-stack machine used by prepared folds.  Callable arguments are
 * retained as machine suspensions and forced according to call_mode. The
 * optional report separates this attempt's status from certified refusals.
 * A generic ready entry can retain a terminal dialect refusal for this
 * demand; crossing demands additionally requires a shared terminal refusal
 * before demand inspection, or an explicit answer-excluding gate. Failure
 * alone supplies no certificate. Remaining callback/authority inputs must
 * stay fixed before any caller publishes a refusal. */
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
    CettaMatchDecisionSemanticIdentity match_decision_semantics,
    CettaPreparedPureCompileReport *report);

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
    CettaMatchDecisionSemanticIdentity match_decision_semantics,
    CettaPreparedPureCompileReport *report);

bool cetta_prepared_pure_program_is_current(
    const CettaPreparedPureProgram *program);

/* The compiled node graph only selects, constructs, or observes values.
 * It neither interprets argument syntax nor matches/binds caller variables.
 * A live unresolved branch condition can still decline at execution. */
/* Compile a body over distinct ready-value parameters into the ordinary
 * answer cursor's continuation steps. The root has no Space equation or
 * named-call admission. Source syntax remains owned by the caller. */
CettaPreparedPureProgram *cetta_prepared_pure_program_compile_body_answers(
    Space *space, Atom *expression, const VarId *parameters, uint32_t arity,
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

bool cetta_prepared_pure_program_accepts_open_values(
    const CettaPreparedPureProgram *program);
/* Immutable slot accounting for root admission and retained-storage bounds. */
size_t cetta_prepared_pure_program_root_local_count(
    const CettaPreparedPureProgram *program);

/* For a fresh open-input node program, select canonical or execution-arena
 * ownership for Boolean values that may outlive this private plan.
 * Refuses a plan whose literals already retain its inline Boolean cache. */
bool cetta_prepared_pure_program_use_exportable_booleans(
    CettaPreparedPureProgram *program);

/* Rebind closed unqualified values from captured and supplied spans. Arity,
 * every operand and lexical scope are checked before any slot changes.
 * The invocation and operands remain borrowed; this grants no source authority. */
bool cetta_prepared_pure_program_rebind_closed_invocation(
    CettaPreparedPureProgram *program, const CettaInvocation *invocation);

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

/* True transfers a completed value, logical failure or interruption from
 * an open-entry invocation. False declines this realization. Values borrow
 * the same storage as execute; payload syntax does not classify completion. */
bool cetta_prepared_pure_program_execute_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    Atom *accumulator, Atom *item,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, CettaCallOutcome *outcome);

/* Execute using a caller-owned scratch arena.  A nonzero nursery budget
 * enables copying collection at the machine's generated-root loop boundary;
 * zero preserves the same execution without collection.  A collected result
 * can reside in the program's invocation survivor arena, so publish it only
 * after copying or materializing it; see release_closed_execution below. */
bool cetta_prepared_pure_program_execute_closed(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    Atom **result_out);

/* True carries a completed value, logical failure or interruption. False
 * declines the pure realization. Failure never becomes an ordinary datum.
 * Value storage has the same borrowed lifetime as execute_closed above. */
bool cetta_prepared_pure_program_execute_closed_controlled(
    CettaPreparedPureProgram *program, Arena *arena,
    size_t nursery_budget_bytes,
    CettaPreparedPureInterruptPollFn interrupt_poll,
    void *interrupt_context, CettaCallOutcome *outcome);

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
    /* An actual native return, or a ready invocation declined before native
     * entry, is retained with its continuation. It is not an equation retry. */
    CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN = 6,
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
    /* Use caller storage synchronously, then copy the exact live frontier
     * into private storage before retaining or suspending work. */
    bool borrowed_until_escape;
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
    /* Optional host authority for saved continuations. Completion workers
     * may change declarations or foreign dispatch without changing Space. */
    bool (*continuation_current)(const void *context);
    const void *continuation_context;
} CettaPreparedPureAnswerCursorOptions;

/* Conservative admission of an immutable possible result. Variables in
 * shape are unknown operands, not values to interpret or bind. False may
 * reject only when no instantiation can meet the consumer. The callback
 * neither retains the shape nor changes logical bindings. */
typedef bool (*CettaPreparedPureAnswerAdmissionFn)(void *context, const Atom *shape);
bool cetta_prepared_pure_answer_cursor_set_admission(
    CettaPreparedPureAnswerCursor *cursor, CettaPreparedPureAnswerAdmissionFn admit,
    void *context);

/* Open a cursor at the program's bound closed entry call.  The cursor
 * retains the program; it borrows the entry arguments, which must stay valid
 * until the cursor is closed or detached. The source Space/read view remains
 * borrowed from the host and must outlive the cursor; detachment moves value
 * storage, not semantic authority. Revision changes cause a stale handoff.
 * Completed native calls preserve this loan. A retained native producer
 * first owns its frontier, returned packet and live continuation, so the
 * host can reclaim entry storage while that producer remains suspended.
 * NULL declines, including when the
 * entry relation is not admitted. */
CettaPreparedPureAnswerCursor *cetta_prepared_pure_answer_cursor_open(
    CettaPreparedPureProgram *program,
    const CettaPreparedPureAnswerCursorOptions *options);

/* Closed, resource-free ready parameters enter the compiled root body
 * directly. They are borrowed until detachment or an entered native return
 * retains the frontier. The entry source belongs to the program's caller. */
CettaPreparedPureAnswerCursor *cetta_prepared_pure_answer_cursor_open_body(
    CettaPreparedPureProgram *program, Atom *const *arguments, uint32_t arity,
    const CettaPreparedPureAnswerCursorOptions *options);

/* Produce the next answer.  An answer is borrowed until the next call on
 * this cursor.  After HANDOFF the frontier describes the remaining
 * enumeration and the cursor must not be advanced again. */
CettaPreparedPureCursorStep cetta_prepared_pure_answer_cursor_next(
    CettaPreparedPureAnswerCursor *cursor, Atom **answer_out);

/* Copy every value the frontier holds into the cursor's own arena, so that
 * nothing it holds borrows storage its caller may move or free.  An answer
 * borrowed before the call is released.  A cursor opened on a caller arena
 * stays borrowed unless borrowed_until_escape requested promotion. */
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

/* Execution storage, separately from any borrowed input graph. A containing
 * owner must detach this cursor before relocating that storage. */
bool cetta_prepared_pure_answer_cursor_uses_arena(
    const CettaPreparedPureAnswerCursor *cursor, const Arena *arena);

/* Keep a result in destination storage before releasing its loan. Values
 * already allocated in that caller arena need no second materialization. */
Atom *cetta_prepared_pure_answer_cursor_copy_value(
    const CettaPreparedPureAnswerCursor *cursor, Arena *destination, Atom *value);

/* The consumer has taken the answer just produced for good and keeps the
 * cursor only for its remaining alternatives: the answer can no longer be
 * withdrawn, its storage is released now rather than at the next step, and
 * the blocks the frontier does not use go back to the allocator. */
void cetta_prepared_pure_answer_cursor_suspend(
    CettaPreparedPureAnswerCursor *cursor);

CettaPreparedPureHandoffReason cetta_prepared_pure_answer_cursor_handoff_reason(
    const CettaPreparedPureAnswerCursor *cursor);

/* Inspect the ready call or actual pending return at a native boundary.
 * The loan remains owned by the cursor. No source call or producer is run.
 * When entered is false, the caller may execute the ready invocation once;
 * otherwise it completes the returned packet without repeating that call.
 * Stale authority permits inspection of retained data. It does not grant
 * resumption of compiled work. */
bool cetta_prepared_pure_answer_cursor_native_boundary(
    CettaPreparedPureAnswerCursor *cursor, CettaInvocation *invocation,
    bool *entered, CettaOwnedReturn *returned);
/* Move a replacement producer for that boundary into the retained cursor.
 * An entered packet remains live until the replacement finishes. A stale or
 * interrupted cursor refuses the move without changing either owner. */
bool cetta_prepared_pure_answer_cursor_resume_native(
    CettaOwnedExecution *execution, CettaOwnedExecution *replacement);
bool cetta_prepared_pure_answer_cursor_native_started(
    const CettaPreparedPureAnswerCursor *cursor);
/* Immutable compilation metadata for consumers that own only scalar or
 * equation frontiers and cannot yet complete a retained native return. */
bool cetta_prepared_pure_program_has_native_calls(
    const CettaPreparedPureProgram *program);
/* Structural control metadata; dialects still own completion permission. */
/* A transferred canonical caller owns every remaining computation; the
 * retired compiled frontier contains no future lookup or executable step. */
bool cetta_prepared_pure_answer_cursor_owns_native_frontier(
    const CettaPreparedPureAnswerCursor *cursor);
/* Read structural completeness before moving the native owner. This does
 * not grant authority to execute a saved caller or an untried equation. */
bool cetta_prepared_pure_answer_cursor_native_frontier_complete(
    const CettaPreparedPureAnswerCursor *cursor);
/* A parent continuation can request an entered native boundary instead of
 * constructing a second dialect machine. Readiness grants no ownership.
 * Taking moves the raw producer, including its pending return. The cursor
 * retains its untried equations; owns_native_frontier is true only when none
 * remain. The parent first captures untried frames and their shared callers. It
 * can also transfer an entered packet after declaration changes when saved
 * plans cover its callers and host continuation authority is still current.
 * Without saved plans, stale transfer requires exhausted ordinary frames
 * and no compiled caller left to resume. It
 * still owns the producer's backing storage: cancel the moved execution
 * before closing it. A parent that closes this cursor captures its
 * untried frames first. Refusal leaves destination intact. */
void cetta_prepared_pure_answer_cursor_defer_native_completion(
    CettaPreparedPureAnswerCursor *cursor);
bool cetta_prepared_pure_answer_cursor_native_transfer_ready(
    CettaPreparedPureAnswerCursor *cursor);
bool cetta_prepared_pure_answer_cursor_take_native(
    CettaPreparedPureAnswerCursor *cursor, CettaOwnedExecution *destination);
/* After the exact moved producer and its parent caller bodies have finished,
 * retire that native frame and retain the actual compiled alternatives.
 * Success consumes completed; refusal preserves it. Stale authority refuses
 * untried compiled work, allowing a frozen frontier to take over; retiring
 * an exhausted frontier requires no new source authority. */
bool cetta_prepared_pure_answer_cursor_resume_native_frontier(
    CettaPreparedPureAnswerCursor *cursor, CettaOwnedExecution *completed);
bool cetta_prepared_pure_program_has_branches(
    const CettaPreparedPureProgram *program);
/* Extend physical work only at an intact frontier transition boundary. This
 * neither grants semantic fuel nor changes pending values or continuations.
 * Scratch exhaustion inside an operation is not such a boundary. */
/* Borrowed cursor consumers use the same retained-boundary transitions as
 * owned execution. Refilling preserves the exact frontier and records stale
 * authority; installing completion takes the replacement only on success.
 * completes_callers certifies that the replacement consumes the entire saved
 * caller chain. Its results must not enter that chain again. When all other
 * frames are matched and exhausted, this owner needs no compiled authority;
 * its own dialect machine checks all of its remaining work. */
bool cetta_prepared_pure_answer_cursor_refill_transitions(
    CettaPreparedPureAnswerCursor *cursor, uint64_t allowance,
    CettaEvalCompletion *completion);
bool cetta_prepared_pure_answer_cursor_complete_native(
    CettaPreparedPureAnswerCursor *cursor, CettaOwnedExecution *replacement,
    bool completes_callers);

bool cetta_prepared_pure_answer_cursor_resume_transitions(
    CettaOwnedExecution *execution, uint64_t allowance);

/* The Space program the producer reads. */
SpaceProgramToken cetta_prepared_pure_answer_cursor_program_token(
    const CettaPreparedPureAnswerCursor *cursor);

/* Calls entered so far, by head, in first-entry order. */
size_t cetta_prepared_pure_answer_cursor_head_count(
    const CettaPreparedPureAnswerCursor *cursor);
SymbolId cetta_prepared_pure_answer_cursor_head(
    const CettaPreparedPureAnswerCursor *cursor, size_t index);

/* The named-call frontier, outermost call first. A parameterized root has no
 * authored equation: frame_count includes its execution frame, but frame
 * refuses that frame. Its owned cursor retains the body; physical refills
 * and native-boundary inspection do not fabricate an equation choice.
 * `next_ordinal` indexes the head's
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
 * appear as fresh ports in `captures`, so the ordinary binding owner reads
 * their values without evaluating them again. `captures` must be initialized;
 * success replaces it, and refusal leaves it unchanged. A native frame reads
 * its actual retained continuation; a native tail has the identity residual.
 * Reading retained code does not grant execution authority. False when an
 * ordinary frame resumes no continuation or a caller has no source reading. */
bool cetta_prepared_pure_answer_cursor_frame_resumption(
    const CettaPreparedPureAnswerCursor *cursor, size_t index, Arena *arena,
    Atom *hole, CettaPreparedPureImportValueFn import_value,
    void *import_context, Bindings *captures, Atom **term_out);

/* A caller is decoded once, with an input value port and a parent root.
 * Root zero denotes the consumer; node i has root i+1 and an older parent.
 * Captures are immutable input values, not a second branch binding state.
 * This readout neither enters code nor transfers the cursor's producer. */
typedef struct {
    size_t parent;
    Atom *input;
    Atom *term;
    Bindings captures;
    /* Immutable plan projected by the same source adapter during lowering.
     * NULL preserves the consumer's fresh, authority-checked elaboration. */
    const void *plan;
} CettaPreparedPureCaller;

typedef struct {
    CettaPreparedPureCaller *nodes;
    size_t length;
    size_t *roots;
    size_t root_count;
} CettaPreparedPureCallerGraph;

/* Select callers by frame index, sharing actual retained ancestor records.
 * The destination must be empty. Failure leaves it empty; imported atoms
 * belong to arena, and the graph's arrays/capture vectors are freed below.
 * For example sibling frames can select different roots with one ancestor.
 * An invalid index or a caller without a source reading refuses the readout. */
bool cetta_prepared_pure_answer_cursor_read_callers(
    const CettaPreparedPureAnswerCursor *cursor,
    const size_t *frames, size_t frame_count, Arena *arena,
    CettaPreparedPureImportValueFn import_value, void *import_context,
    CettaPreparedPureCallerGraph *destination);
void cetta_prepared_pure_caller_graph_free(CettaPreparedPureCallerGraph *graph);

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

/* Programs are reference counted: the creator holds one reference and
 * cetta_prepared_pure_program_free releases one. */
CettaPreparedPureProgram *cetta_prepared_pure_program_retain(
    CettaPreparedPureProgram *program);

/* The head and arity of each equation occurrence the program compiled. */
size_t cetta_prepared_pure_program_equation_count(
    const CettaPreparedPureProgram *program);
/* Number of bounded, already-ready scalar regions in the compiled code.
 * This reports admission, not an assertion that every region was executed. */
size_t cetta_prepared_pure_program_scalar_tree_count(
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
/* An incomplete inspection grants neither readiness nor permission to run
 * the value as code. COMPLETE is required. */
bool cetta_prepared_pure_program_value_is_fully_evaluated(
    CettaPreparedPureProgram *program, Atom *value, bool *complete);

/* A collected closed execution can return a result owned by the program's
 * invocation survivor arena.  The result remains valid until the next
 * execution, program destruction, or this explicit release.  A caller which
 * publishes the result into another arena must copy or materialize it first. */
/* The arena holding a closed execution's collection survivors, or NULL when
 * no collection ran; it dies with
 * cetta_prepared_pure_program_release_closed_execution. */
Arena *cetta_prepared_pure_program_closed_survivor_arena(
    CettaPreparedPureProgram *program);
void cetta_prepared_pure_program_release_closed_execution(
    CettaPreparedPureProgram *program);

void cetta_prepared_pure_program_free(
    CettaPreparedPureProgram *program);

#endif /* CETTA_PREPARED_PURE_MACHINE_H */
