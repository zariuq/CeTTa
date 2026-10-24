#ifndef CETTA_OWNED_EXECUTION_H
#define CETTA_OWNED_EXECUTION_H

#include "call_outcome.h"
#include "eval_completion.h"
#include "match.h"
#include "ready_queue.h"
#include <assert.h>
#include <string.h>

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

typedef struct {
    BindingValue head;
    CettaArgumentSpan captured;
    CettaArgumentSpan supplied;
} CettaInvocation;

/* Arity admission observes no operand and performs no allocation. */
static inline bool cetta_invocation_arity(
        const CettaInvocation *call, CettaExprLen *arity) {
    if (!call || !arity || !call->head.skeleton ||
        (call->captured.count &&
         ((!call->captured.atoms && !call->captured.values) ||
          (call->captured.atoms && call->captured.values))) ||
        (call->supplied.count &&
         ((!call->supplied.atoms && !call->supplied.values) ||
          (call->supplied.atoms && call->supplied.values))) ||
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
    assert(index < span->count);
    return span->values ? span->values[index]
        : binding_value_from_atom(span->atoms[index]);
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
    /* The producer has established that no return remains after this one.
     * This certifies its own stream, not completion of a suspended payload,
     * permission to force it, or release of another owner's account. False
     * retains the ordinary continuation protocol. */
    bool final;
} CettaOwnedReturn;

typedef enum {
    CETTA_OWNED_EXECUTION_RETURN = 0,
    CETTA_OWNED_EXECUTION_COMPLETE,
    CETTA_OWNED_EXECUTION_PAUSED,
    CETTA_OWNED_EXECUTION_HANDOFF,
    CETTA_OWNED_EXECUTION_INTERRUPTED,
} CettaOwnedExecutionStep;

/* A native call already ran. This cursor lends its returned occurrences;
 * advancing it neither invokes the operation nor interprets any payload.
 * The caller retains the payloads and logical environment through delivery.
 * Only VALUE/COMPLETED_FRONTIER expands into several completed values. */
typedef struct {
    CettaCallOutcome outcome;
    const Bindings *environment;
    CettaExprIndex position;
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
        if (outcome.result_form == CETTA_CALL_RESULT_COMPLETED_FRONTIER) {
            Atom *frontier = outcome.term;
            if (cursor->position >= frontier->expr.len)
                return CETTA_OWNED_EXECUTION_COMPLETE;
            Atom *value = frontier->expr.elems[cursor->position];
            if (!value)
                break;
            cursor->position++;
            *returned = (CettaOwnedReturn){
                .outcome = cetta_call_completed_value(value),
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
 * A consumer publishes the occurrence only on acceptance. Refusal may update
 * private retry state; the same consumer state must survive resumption. */
typedef bool (*CettaOwnedReturnConsumer)(
    void *consumer, const CettaOwnedReturn *returned);

/* Exact calls made by one explicitly observed task visit. These are not
 * callback instructions or elapsed time. Arrival processing, owner cleanup
 * and enclosing host work have their own accounts. No task stores a meter. */
typedef struct {
    uint64_t compatibility, producer, consumer, final_is_current;
    uint64_t rotations, retirements;
} CettaOwnedTaskAccount;

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
    /* Set by an adapter whose captures and enclosing services remain valid
     * after this driver moves. Self-references to this driver are not rebased.
     * Ordinary initialization leaves such transport unqualified. */
    bool relocatable;
} CettaOwnedExecution;

/* Takes one computation owner, whose code, captures and read authority are
 * qualified by its adapter. The boundary itself allocates no value wrapper. */
void cetta_owned_execution_init(CettaOwnedExecution *execution,
    void *computation, CettaOwnedExecutionNext next,
    CettaOwnedExecutionRelease release);

/* Move an adapter-qualified owner, including its pending loan and paid prefix.
 * Destination must be zero-initialized or cancelled. Refusal changes neither
 * argument; success leaves the source inert and independently cancellable.
 * Reference operations and ownership transfer are serialized by the caller. */
bool cetta_owned_execution_move(CettaOwnedExecution *destination,
    CettaOwnedExecution *source);

/* An allowance counts accepted returns, not producer firings or public fuel.
 * A pause keeps a pending return, its owner and the consumer's accumulated
 * state; resumption neither recomputes nor redelivers an accepted occurrence. */
CettaOwnedExecutionStep cetta_owned_execution_sequence(
    CettaOwnedExecution *execution, uint64_t allowance,
    CettaOwnedReturnConsumer consume, void *consumer);

/* A constructor-selected provider may be passed as a literal so native
 * adapters inline the same sequencer. It must be the initialized `next`;
 * the public entry reads that provider from the execution instead. The
 * task-service adapter supplies a fresh account for its single poll; this
 * kernel fills producer/final columns and the task callbacks fill the rest.
 * General sequencing passes NULL. */
static inline __attribute__((always_inline))
CettaOwnedExecutionStep cetta_owned_execution_sequence_accounted_with(
    CettaOwnedExecution *execution, uint64_t allowance,
    CettaOwnedExecutionNext next, CettaOwnedReturnConsumer consume,
    void *consumer, CettaOwnedTaskAccount *account) {
    if (!execution || !execution->computation || !next || !consume)
        return CETTA_OWNED_EXECUTION_HANDOFF;
    if (execution->terminal)
        return execution->stop;
    while (allowance > 0u) {
        if (!execution->has_pending) {
            CettaOwnedReturn returned = {0};
            if (account) account->producer++;
            CettaOwnedExecutionStep step = next(
                execution->computation, &returned, &execution->completion);
            if (step != CETTA_OWNED_EXECUTION_RETURN) {
                execution->stop = step;
                execution->terminal = step != CETTA_OWNED_EXECUTION_PAUSED;
                return step;
            }
            execution->pending = returned;
            execution->has_pending = true;
        }
        if (!consume(consumer, &execution->pending)) {
            execution->stop = CETTA_OWNED_EXECUTION_PAUSED;
            return execution->stop;
        }
        bool final = execution->pending.final &&
            execution->completion == CETTA_EVAL_COMPLETE;
        if (final && execution->final_is_current) {
            if (account) account->final_is_current++;
            final = execution->final_is_current(execution->computation);
        }
        execution->has_pending = false;
        memset(&execution->pending, 0, sizeof(execution->pending));
        execution->accepted++;
        allowance--;
        if (final) {
            execution->stop = CETTA_OWNED_EXECUTION_COMPLETE;
            execution->terminal = true;
            return execution->stop;
        }
    }
    execution->stop = CETTA_OWNED_EXECUTION_PAUSED;
    return execution->stop;
}

static inline __attribute__((always_inline))
CettaOwnedExecutionStep cetta_owned_execution_sequence_with(
    CettaOwnedExecution *execution, uint64_t allowance,
    CettaOwnedExecutionNext next, CettaOwnedReturnConsumer consume,
    void *consumer) {
    return cetta_owned_execution_sequence_accounted_with(
        execution, allowance, next, consume, consumer, NULL);
}

/* Releases abandoned execution without undoing accepted returns or effects.
 * The caller must publish any retained borrowed return before this call. */
void cetta_owned_execution_cancel(CettaOwnedExecution *execution);

/* A selected host service connects the existing queue to actual owned calls.
 * Task addresses and callback context remain stable until cancellation. This
 * queue contains only CettaOwnedTask links; membership grants no capability.
 * Callbacks may update private retry state, but must not mutate this task,
 * its execution or queue, or recursively service it. The host serializes all
 * operations and supplies the relevant source and publication permissions. */
/* Compatibility may configure a quiescent provider's next work grant through
 * its adapter. It must not advance or dispose the provider or alter the
 * sequencer's terminal, loan, acceptance or membership fields. */
typedef bool (*CettaOwnedTaskCompatible)(void *context);
typedef struct {
    CettaReadyLink ready;
    CettaOwnedExecution execution;
    CettaOwnedTaskCompatible compatible;
    CettaOwnedReturnConsumer consume;
    void *context;
} CettaOwnedTask;

typedef struct {
    CettaOwnedTask *task; /* NULL means no queued task. */
    CettaOwnedExecutionStep step;
    bool context_refused;
} CettaOwnedVisit;

/* Move one qualified computation into an empty, unqueued task. Failure leaves
 * both objects unchanged. Admission uses cetta_ready_admit(&task->ready).
 * Completed tasks leave the queue but retain their owner until cancellation. */
bool cetta_owned_task_init(CettaOwnedTask *task, CettaOwnedExecution *execution,
    CettaOwnedTaskCompatible compatible, CettaOwnedReturnConsumer consume,
    void *context);

/* Rotate once, then attempt at most one producer poll or held-loan retry.
 * A false poll rotates without callbacks or work. Compatibility is checked
 * before a producer call and before delivery, including a held loan. Refusal
 * retains the entire call and its queue membership. Only an actual terminal
 * execution retires. Each callback/provider must return: this bounds service
 * visits and accepted occurrences, not instructions, time or source firings.
 * A provider's finite work quantum is a separate adapter contract. */
CettaOwnedVisit cetta_owned_task_visit(CettaReadyQueue *queue, bool poll);
/* The same one-poll service for an initialized task whose enclosing host
 * already selected its candidate. It does not admit or rotate membership;
 * terminal membership, if present, is retired just as by task_visit. */
CettaOwnedVisit cetta_owned_task_poll(CettaOwnedTask *task, bool poll);

/* Explicit numerical observation of the same visit/poll. A non-NULL account
 * is reset before service; NULL selects ordinary unaccounted execution.
 * Each visit makes at most two compatibility calls and one call to each of
 * producer, consumer and final-current validator. Retrying a held return
 * does not poll its producer. Poll alone performs no queue rotation.
 * Empty visits still have host/queue bookkeeping outside these call counts.
 * The account must not alias the queue, task, execution or callback state.
 * Callbacks must not modify the account or reenter this task. */
CettaOwnedVisit cetta_owned_task_visit_accounted(
    CettaReadyQueue *queue, bool poll, CettaOwnedTaskAccount *account);
CettaOwnedVisit cetta_owned_task_poll_accounted(
    CettaOwnedTask *task, bool poll, CettaOwnedTaskAccount *account);

/* Remove this membership and release its computation. Independent captured
 * returns and other tasks retain their own shares. No effect is rolled back. */
void cetta_owned_task_cancel(CettaOwnedTask *task);

#endif
