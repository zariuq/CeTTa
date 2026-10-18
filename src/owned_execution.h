#ifndef CETTA_OWNED_EXECUTION_H
#define CETTA_OWNED_EXECUTION_H

#include "call_outcome.h"
#include "eval_completion.h"
#include "match.h"
#include "ready_queue.h"
#include <assert.h>
#include <string.h>

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
 * the public entry reads that provider from the execution instead. */
static inline __attribute__((always_inline))
CettaOwnedExecutionStep cetta_owned_execution_sequence_with(
    CettaOwnedExecution *execution, uint64_t allowance,
    CettaOwnedExecutionNext next, CettaOwnedReturnConsumer consume,
    void *consumer) {
    if (!execution || !execution->computation || !next || !consume)
        return CETTA_OWNED_EXECUTION_HANDOFF;
    if (execution->terminal)
        return execution->stop;
    while (allowance > 0u) {
        if (!execution->has_pending) {
            CettaOwnedReturn returned = {0};
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
            execution->completion == CETTA_EVAL_COMPLETE &&
            (!execution->final_is_current ||
             execution->final_is_current(execution->computation));
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

/* Remove this membership and release its computation. Independent captured
 * returns and other tasks retain their own shares. No effect is rolled back. */
void cetta_owned_task_cancel(CettaOwnedTask *task);

#endif
