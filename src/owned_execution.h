#ifndef CETTA_OWNED_EXECUTION_H
#define CETTA_OWNED_EXECUTION_H

#include "call_outcome.h"
#include "eval_completion.h"
#include "match.h"
#include <assert.h>

/* Borrowed operands of one invocation. A span contains either ordinary
 * syntax with complete variable identities, or contextual BindingValues.
 * Its storage and lexical owners must remain live through synchronous use.
 * Readiness and source authority are obligations of the calling frame;
 * neither follows from the storage representation. */
typedef struct {
    Atom *const *atoms;
    const BindingValue *values;
    CettaExprLen count;
} CettaArgumentSpan;

typedef struct CettaInvocation {
    BindingValue head;
    CettaArgumentSpan captured;
    CettaArgumentSpan supplied;
} CettaInvocation;

static inline bool cetta_argument_span_valid(const CettaArgumentSpan *span) {
    return span && (!span->count || (!!span->atoms != !!span->values));
}

/* Reads a single admitted operand without changing its lexical coordinate. */
static inline BindingValue cetta_argument_span_value(
        const CettaArgumentSpan *span, CettaExprIndex index) {
    assert(index < span->count);
    return span->values ? span->values[index]
        : binding_value_from_atom(span->atoms[index]);
}

/* Arity admission observes no operand and performs no allocation. */
static inline bool cetta_invocation_arity(
        const CettaInvocation *call, CettaExprLen *arity) {
    if (!call || !arity || !call->head.skeleton ||
        !cetta_argument_span_valid(&call->captured) ||
        !cetta_argument_span_valid(&call->supplied) ||
        call->captured.count > UINT64_MAX - call->supplied.count)
        return false;
    *arity = call->captured.count + call->supplied.count;
    return true;
}

/* The caller has admitted the complete arity and index. This read retains
 * lexical identity; it does not substitute, evaluate or memoize an operand. */
static inline BindingValue cetta_invocation_argument(
        const CettaInvocation *call, CettaExprIndex index) {
    const CettaArgumentSpan *span = &call->captured;
    if (index >= span->count) {
        index -= span->count;
        span = &call->supplied;
    }
    return cetta_argument_span_value(span, index);
}

/* A contiguous unqualified vector can be loaned to an existing native ABI.
 * A split or contextual vector requires its consumer to read the operands. */
static inline Atom *const *cetta_invocation_borrow_arguments(
        const CettaInvocation *call) {
    if (call->captured.count == 0u && !call->supplied.values)
        return call->supplied.atoms;
    if (call->supplied.count == 0u && !call->captured.values)
        return call->captured.atoms;
    return NULL;
}

/* Explicit syntax observation. Reify lexical identities once, without
 * applying branch substitutions or interpreting any argument as code.
 * The resulting syntax may share subterms; independent escape still needs
 * the caller's publication/ownership service. */
Atom *cetta_invocation_materialize(Arena *arena, const CettaInvocation *call);
/* Reify only the argument vector at a native ABI boundary. A contiguous
 * unqualified span is borrowed unchanged; other inputs allocate one vector.
 * This does not apply a logical world or grant evaluation permission. */
Atom **cetta_invocation_materialize_arguments(
    Arena *arena, const CettaInvocation *call);
/* Resolve the current logical world before entry, without evaluating code.
 * Closed-only admission observes every result before registry/native work.
 * On refusal the output is untouched. The resolved vector is valid only
 * for this entrance; it is not evidence reusable after mutation or rollback. */
bool cetta_invocation_resolve_arguments(
    Arena *arena, const Bindings *environment, const CettaInvocation *call,
    bool closed_only, CettaInvocation *resolved);

/* Intrusive owned continuation links keep dialect frames at stable addresses.
 * Push takes one frame; pop transfers it back to the adapter for publication
 * or release. The adapter owns roots and demand, so scalar values need no
 * universal heap wrapper and the link does not confer source-read authority. */
typedef struct CettaOwnedFrameLink {
    struct CettaOwnedFrameLink *next;
} CettaOwnedFrameLink;

typedef struct {
    CettaOwnedFrameLink *top;
    size_t depth;
} CettaOwnedFrameStack;

static inline void cetta_owned_frame_push(
    CettaOwnedFrameStack *stack, CettaOwnedFrameLink *frame) {
    assert(stack && frame && stack->depth < SIZE_MAX);
    frame->next = stack->top;
    stack->top = frame;
    stack->depth++;
}

static inline CettaOwnedFrameLink *cetta_owned_frame_pop(
    CettaOwnedFrameStack *stack) {
    assert(stack && stack->top && stack->depth > 0u);
    CettaOwnedFrameLink *frame = stack->top;
    stack->top = frame->next;
    frame->next = NULL;
    stack->depth--;
    return frame;
}

/* A return loans its payload and environment from the computation owner.
 * Source expressions, completed values and conditional answers retain their
 * distinct provenance. Sequencing does not interpret syntax or memoize calls. */
typedef struct {
    CettaCallOutcome outcome;
    const Bindings *environment;
    /* This producer's retained stream has no return after this occurrence.
     * It does not certify a suspended payload or confer force authority. */
    bool final;
} CettaOwnedReturn;

typedef enum {
    CETTA_OWNED_EXECUTION_RETURN = 0,
    CETTA_OWNED_EXECUTION_COMPLETE,
    CETTA_OWNED_EXECUTION_PAUSED,
    CETTA_OWNED_EXECUTION_HANDOFF,
    CETTA_OWNED_EXECUTION_INTERRUPTED,
} CettaOwnedExecutionStep;

/* An entered call or qualified choice lends its returned occurrences;
 * advancing this cursor neither invokes a producer nor interprets a payload.
 * The caller retains the payloads and logical environment through delivery.
 * Completed frontiers lend data occurrences. An expression frontier stays
 * one pending computation unless an admitted choice view selects its
 * individual expressions for interpretation. */
typedef struct {
    CettaCallOutcome outcome;
    const Bindings *environment;
    CettaExprIndex position;
    /* An admitted choice view selects expression occurrences. Otherwise an
     * expression frontier remains one computation for native completion. */
    bool select_expressions;
} CettaCallReturnCursor;

static inline CettaOwnedExecutionStep cetta_call_return_next(
        void *computation, CettaOwnedReturn *returned,
        CettaEvalCompletion *completion) {
    CettaCallReturnCursor *cursor = computation;
    CettaCallOutcome outcome = cursor->outcome;
    if (!cetta_call_outcome_shape_valid(outcome))
        goto malformed;
    switch (outcome.kind) {
    case CETTA_CALL_FAILURE:
        return CETTA_OWNED_EXECUTION_COMPLETE;
    case CETTA_CALL_INTERRUPTED:
        cetta_eval_completion_record(completion, CETTA_EVAL_INCOMPLETE_CANCELLED);
        return CETTA_OWNED_EXECUTION_INTERRUPTED;
    case CETTA_CALL_VALUE:
        if (outcome.result_form == CETTA_CALL_RESULT_COMPLETED_FRONTIER ||
            (outcome.result_form == CETTA_CALL_RESULT_EXPRESSION_FRONTIER &&
             cursor->select_expressions)) {
            Atom *frontier = outcome.term;
            if (cursor->position >= frontier->expr.len)
                return CETTA_OWNED_EXECUTION_COMPLETE;
            Atom *value = frontier->expr.elems[cursor->position];
            if (!value)
                break;
            cursor->position++;
            *returned = (CettaOwnedReturn){
                .outcome = {.kind = CETTA_CALL_VALUE, .term = value,
                    .result_form = cetta_call_frontier_element_form(outcome.result_form)},
                .environment = cursor->environment,
                .final = cursor->position == frontier->expr.len,
            };
            return CETTA_OWNED_EXECUTION_RETURN;
        }
        /* fall through */
    case CETTA_CALL_RAISED:
    case CETTA_CALL_SUSPENDED:
        if (cursor->position)
            return CETTA_OWNED_EXECUTION_COMPLETE;
        cursor->position = 1u;
        *returned = (CettaOwnedReturn){
            .outcome = outcome,
            .environment = cursor->environment,
            .final = true,
        };
        return CETTA_OWNED_EXECUTION_RETURN;
    }
malformed:
    /* A malformed producer result cannot certify logical exhaustion. */
    cetta_eval_completion_record(completion, CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
    return CETTA_OWNED_EXECUTION_INTERRUPTED;
}

typedef CettaOwnedExecutionStep (*CettaOwnedExecutionNext)(
    void *computation, CettaOwnedReturn *returned,
    CettaEvalCompletion *completion);
typedef void (*CettaOwnedExecutionRelease)(void *computation);
/* A read-only check of a final certificate after the consumer accepts.
 * It cannot step the producer. False preserves ordinary continuation, so a
 * changed source is reported without replaying the accepted occurrence. */
typedef bool (*CettaOwnedExecutionFinalIsCurrent)(void *computation);
/* False retains the pending return and complete computation at this boundary.
 * The consumer must commit its state only when accepting the return. */
typedef bool (*CettaOwnedReturnConsumer)(
    void *consumer, const CettaOwnedReturn *returned);

typedef struct {
    void *computation;
    CettaOwnedExecutionNext next;
    CettaOwnedExecutionRelease release;
    /* NULL is for certificates independent of mutable external authority. */
    CettaOwnedExecutionFinalIsCurrent final_is_current;
    CettaOwnedReturn pending;
    /* Completion is authoritative at a terminal boundary. PAUSED never
     * certifies exhaustion, even if no incomplete reason has been recorded. */
    CettaEvalCompletion completion;
    CettaOwnedExecutionStep stop;
    uint64_t accepted;
    bool has_pending;
    bool terminal;
} CettaOwnedExecution;

/* Takes one computation owner, whose code, captures and read authority are
 * qualified by its adapter. The boundary itself allocates no value wrapper. */
void cetta_owned_execution_init(CettaOwnedExecution *execution,
    void *computation, CettaOwnedExecutionNext next,
    CettaOwnedExecutionRelease release);

/* An allowance counts accepted returns, not producer firings or public fuel.
 * A pause keeps a pending return, its owner and the consumer's accumulated
 * state; resumption neither recomputes nor redelivers an accepted occurrence. */
CettaOwnedExecutionStep cetta_owned_execution_sequence(
    CettaOwnedExecution *execution, uint64_t allowance,
    CettaOwnedReturnConsumer consume, void *consumer);

/* Borrow one pending occurrence without acknowledging it. A continuation
 * keeps this loan until its child retires; repeated loans do not step the
 * producer. Acceptance validates the final certificate at that later point. */
CettaOwnedExecutionStep cetta_owned_execution_loan(
    CettaOwnedExecution *execution, const CettaOwnedReturn **returned);
bool cetta_owned_execution_accept(CettaOwnedExecution *execution);

/* Releases abandoned execution without undoing accepted returns or effects.
 * The caller must publish any retained borrowed return before this call. */
void cetta_owned_execution_cancel(CettaOwnedExecution *execution);

/* Build the computation following one value occurrence. The producer keeps
 * that value and its environment live until this computation finishes.
 * False declines continuation admission before its execution; the sequence
 * retains the actual return for another worker, never the original call.
 * Body ownership transfers on true only. On false the factory retains its
 * own allocations and must not leave an owned body in the output. */
typedef bool (*CettaOwnedBindBody)(void *context,
    const CettaOwnedReturn *returned, CettaOwnedExecution *body);

typedef struct {
    CettaOwnedExecution producer;
    CettaOwnedExecution body;
    CettaOwnedBindBody continuation;
    void *context;
    bool has_body;
    bool body_returned;
    bool forwarded;
    bool awaiting_body;
} CettaOwnedBind;

/* Move producer ownership into fresh caller-supplied sequence storage.
 * Logical failure skips the continuation; raised control is forwarded.
 * Each next call pulls at most one producer occurrence. An empty body may
 * pause without publication, so infinite zero-producing work stays resumable.
 * Answers borrow body storage until the next call. Finality is checked after
 * the real consumer accepts, including changes made by the continuation. */
bool cetta_owned_bind_open(CettaOwnedExecution *execution,
    CettaOwnedBind *bind, CettaOwnedExecution *producer,
    CettaOwnedBindBody continuation, void *context);

/* A continuation-admission handoff retains the complete producer packet. */
const CettaOwnedReturn *cetta_owned_bind_pending(const CettaOwnedBind *bind);

/* Move an admitted replacement body into that handoff. The producer is not
 * stepped or reentered, and its pending return remains owned until body exit.
 * A handoff inside an entered body instead retains body itself; its adapter
 * must transfer that residual rather than restart continuation admission. */
bool cetta_owned_bind_resume(CettaOwnedExecution *execution,
    CettaOwnedBind *bind, CettaOwnedExecution *body);

#endif
