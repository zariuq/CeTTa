#ifndef CETTA_OWNED_EXECUTION_H
#define CETTA_OWNED_EXECUTION_H

#include "call_outcome.h"
#include "eval_completion.h"
#include "match.h"
#include "ready_queue.h"
#include <assert.h>
#include <string.h>

/* Optional additive controller-work credit. Hosts serialize every operation
 * on a bank and its leases. A retained reference aliases one balance; only
 * reserve creates a separate purse, by transferring existing credit. These
 * cells grant neither source access nor permission to run foreign effects.
 * All uint64_t amounts, including zero and UINT64_MAX, are finite. An adapter
 * selects its unmetered provider separately, without constructing a cell. */
typedef struct CettaWorkCredit CettaWorkCredit;
typedef struct {
    uint64_t balance, spent;
    uint64_t root_initial, root_remaining, root_spent;
    size_t children;
    bool closed;
} CettaWorkCreditView;

CettaWorkCredit *cetta_work_credit_new(uint64_t amount);
bool cetta_work_credit_retain(CettaWorkCredit *);
void cetta_work_credit_release(CettaWorkCredit *);
/* Refusal leaves the parent unchanged. A child holds its parent alive. */
CettaWorkCredit *cetta_work_credit_reserve(CettaWorkCredit *, uint64_t amount);
/* Move existing credit between distinct live cells in the same bank. This
 * does not change either lease's parent or its eventual refund destination.
 * A caller supplies both references; retaining a parent is not a grant. */
bool cetta_work_credit_transfer(CettaWorkCredit *source, CettaWorkCredit *target,
    uint64_t amount);
uint64_t cetta_work_credit_available(const CettaWorkCredit *);
bool cetta_work_credit_spend(CettaWorkCredit *, uint64_t amount);
bool cetta_work_credit_inspect(const CettaWorkCredit *, CettaWorkCreditView *);
/* Close refuses while a child is open. First close returns only the unused
 * child balance; a repeated close does nothing and returns false. Final
 * reference release closes automatically. Root close retires its remaining
 * balance. This is lifetime/accounting, not asynchronous revocation. */
bool cetta_work_credit_close(CettaWorkCredit *);

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
/* A cleanup provider consumes its own retained disposal state. COMPLETE
 * leaves only the enclosing owner for `release`; it never frees that owner.
 * ATOMIC requests permission for an indivisible service still owned here.
 * With no such permission, that service is not invoked. Neither a request
 * count nor atomic permission establishes a physical work bound. Permission
 * here selects a work-placement profile; it grants no source/effect authority
 * and does not weaken the provider's lifetime requirements. */
typedef enum {
    CETTA_OWNED_RELEASE_PENDING,
    CETTA_OWNED_RELEASE_COMPLETE,
    CETTA_OWNED_RELEASE_ATOMIC,
} CettaOwnedReleaseStep;
typedef CettaOwnedReleaseStep (*CettaOwnedReleasePoll)(void *computation,
    uint64_t allowance, bool allow_atomic, uint64_t *used);
/* A read-only check of a final certificate after the consumer accepts.
 * It cannot step the producer. False preserves ordinary continuation, so a
 * changed source is reported without replaying the accepted occurrence. */
typedef bool (*CettaOwnedExecutionFinalIsCurrent)(void *computation);
/* False retains the pending return and complete computation at this boundary.
 * A consumer publishes the occurrence only on acceptance. Refusal may update
 * private retry state; the same consumer state must survive resumption. */
typedef bool (*CettaOwnedReturnConsumer)(
    void *consumer, const CettaOwnedReturn *returned);

typedef enum {
    CETTA_OWNED_TASK_PHASE_ROTATION,
    CETTA_OWNED_TASK_PHASE_ADMISSION,
    CETTA_OWNED_TASK_PHASE_PRODUCER,
    CETTA_OWNED_TASK_PHASE_CONSUMER,
    CETTA_OWNED_TASK_PHASE_FINAL_CURRENT,
    CETTA_OWNED_TASK_PHASE_RETIREMENT,
    CETTA_OWNED_TASK_PHASE_CLEANUP,
} CettaOwnedTaskPhase;
#define CETTA_OWNED_TASK_PHASE_CAPACITY 8u

/* Optional synchronous observation of a completed service action. `detail`
 * is the returned admission or execution verdict, the consumer/final Boolean,
 * cleanup service units, or zero for rotation and retirement. The observer may update only its own
 * state; it must not reenter or mutate the task, execution or queue. */
typedef void (*CettaOwnedTaskAccountObserver)(void *context,
    CettaOwnedTaskPhase phase, uint64_t detail);

/* Exact calls made by one explicitly observed task visit, including their
 * actual kernel order. These are not callback instructions or elapsed time.
 * Arrival processing, owner cleanup and enclosing host work have their own
 * accounts. No task stores a meter; unaccounted execution records nothing. */
typedef struct {
    uint64_t admission, producer, consumer, final_is_current;
    uint64_t rotations, retirements;
    uint64_t cleanup_polls, cleanup_work;
    uint8_t phases[CETTA_OWNED_TASK_PHASE_CAPACITY], phase_count;
    bool phase_overflow;
} CettaOwnedTaskAccount;

static inline void cetta_owned_task_account_record(
    CettaOwnedTaskAccount *account,CettaOwnedTaskAccountObserver observer,
    void *observer_context,CettaOwnedTaskPhase phase,uint64_t detail) {
    if (phase>CETTA_OWNED_TASK_PHASE_CLEANUP) {
        if (account) account->phase_overflow=true;
        return;
    }
    if (account) {
        if (account->phase_count>=CETTA_OWNED_TASK_PHASE_CAPACITY)
            account->phase_overflow=true;
        else account->phases[account->phase_count++]=(uint8_t)phase;
    }
    if (observer) observer(observer_context,phase,detail);
}

typedef struct {
    void *computation;
    CettaOwnedExecutionNext next;
    CettaOwnedExecutionRelease release;
    CettaOwnedReleasePoll release_poll;
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
    bool retiring, release_ready;
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

/* Install before advancing the owner. Pending cleanup retains the complete
 * computation and callback context. Providers must use no more than the
 * allowance, make progress on a positive permitted grant, and complete on
 * finite owned storage. Unknown foreign services require atomic permission.
 * Installation clears any prior relocation qualification: an adapter must
 * qualify the complete new callback/capture configuration before marking it
 * movable again. The caller holds all lifetime dependencies through release. */
bool cetta_owned_execution_set_release_poll(CettaOwnedExecution *execution,
    CettaOwnedReleasePoll poll);
/* Zero allowance is inert. A positive cancellation first ends answer loans
 * and suppresses further producer/consumer calls, retaining storage until
 * cleanup completes and the final release action runs. Each action consumes
 * one of the provider's declared units; their prices remain separate.
 * ATOMIC retains the owner for another grant or synchronous cancellation. */
CettaOwnedReleaseStep cetta_owned_execution_cancel_poll(
    CettaOwnedExecution *execution, uint64_t allowance, bool allow_atomic,
    uint64_t *used);

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
    void *consumer, CettaOwnedTaskAccount *account,
    CettaOwnedTaskAccountObserver observer,void *observer_context) {
    if (!execution || !execution->computation || !next || !consume)
        return CETTA_OWNED_EXECUTION_HANDOFF;
    if (execution->terminal)
        return execution->stop;
    while (allowance > 0u) {
        if (!execution->has_pending) {
            CettaOwnedReturn returned = {0};
            CettaOwnedExecutionStep step = next(
                execution->computation, &returned, &execution->completion);
            if (account) account->producer++;
            cetta_owned_task_account_record(account,observer,observer_context,
                CETTA_OWNED_TASK_PHASE_PRODUCER,(uint64_t)step);
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
            bool current=execution->final_is_current(execution->computation);
            if (account) account->final_is_current++;
            cetta_owned_task_account_record(account,observer,observer_context,
                CETTA_OWNED_TASK_PHASE_FINAL_CURRENT,current?1u:0u);
            final = current;
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
        execution, allowance, next, consume, consumer, NULL, NULL, NULL);
}

/* Synchronous release explicitly admits indivisible cleanup services and
 * drains the same retained provider. It neither undoes accepted effects nor
 * publishes a pending loan; retained borrowed returns must be published first. */
void cetta_owned_execution_cancel(CettaOwnedExecution *execution);

/* A selected host service connects the existing queue to actual owned calls.
 * Task addresses and callback context remain stable until cancellation. This
 * queue contains only CettaOwnedTask links; membership grants no capability.
 * Callbacks may update private retry state, but must not mutate this task,
 * its execution or queue, or recursively service it. The host serializes all
 * operations and supplies the relevant source and publication permissions. */
/* Admission may configure a quiescent provider's next work grant through its
 * adapter. It must not advance or dispose the provider or alter the
 * sequencer's terminal, loan, acceptance or membership fields. A non-ready
 * verdict retains the exact computation and queue membership. INVALIDATED is
 * reported to the host; it does not silently discard a residual that may need
 * inspection, migration or explicit cancellation. */
typedef enum {
    /* Visit observation only. A callback that returns this is invalidated. */
    CETTA_OWNED_TASK_ADMISSION_UNCHECKED = 0,
    CETTA_OWNED_TASK_ADMISSION_READY,
    CETTA_OWNED_TASK_ADMISSION_PAUSED,
    CETTA_OWNED_TASK_ADMISSION_REFUSED,
    CETTA_OWNED_TASK_ADMISSION_INVALIDATED,
} CettaOwnedTaskAdmission;
typedef CettaOwnedTaskAdmission (*CettaOwnedTaskAdmit)(void *context);
typedef struct {
    CettaReadyLink ready;
    CettaOwnedExecution execution;
    CettaOwnedTaskAdmit admit;
    CettaOwnedReturnConsumer consume;
    void *context;
} CettaOwnedTask;

typedef struct {
    CettaOwnedTask *task; /* NULL means no queued task. */
    CettaOwnedExecutionStep step;
    /* Last admission observed in this visit; UNCHECKED means none was made. */
    CettaOwnedTaskAdmission admission;
} CettaOwnedVisit;

/* Move one qualified computation into an empty, unqueued task. Failure leaves
 * both objects unchanged. Admission uses cetta_ready_admit(&task->ready).
 * Completed tasks leave the queue but retain their owner until cancellation. */
bool cetta_owned_task_init(CettaOwnedTask *task, CettaOwnedExecution *execution,
    CettaOwnedTaskAdmit admit, CettaOwnedReturnConsumer consume,
    void *context);

/* Rotate once, then attempt at most one producer poll or held-loan retry.
 * A false poll rotates without callbacks or work. Admission is checked before
 * a producer call and before delivery, including a held loan. PAUSED, REFUSED
 * and INVALIDATED retain the entire call and its queue membership; the host
 * decides whether to retry, replace policy, inspect or cancel. Only an actual
 * terminal execution retires. Each callback/provider must return: this bounds
 * service visits and accepted occurrences, not instructions, time or source
 * firings. A provider's finite work quantum is a separate adapter contract. */
CettaOwnedVisit cetta_owned_task_visit(CettaReadyQueue *queue, bool poll);
/* The same one-poll service for an initialized task whose enclosing host
 * already selected its candidate. It does not admit or rotate membership;
 * terminal membership, if present, is retired just as by task_visit. */
CettaOwnedVisit cetta_owned_task_poll(CettaOwnedTask *task, bool poll);

/* Explicit numerical observation of the same visit/poll. A non-NULL account
 * is reset before service; NULL selects ordinary unaccounted execution.
 * Each visit makes at most two admission calls and one call to each of
 * producer, consumer and final-current validator. Retrying a held return
 * does not poll its producer. Poll alone performs no queue rotation.
 * Empty visits still have host/queue bookkeeping outside these call counts.
 * The account must not alias the queue, task, execution or callback state.
 * Callbacks must not modify the account or reenter this task. */
CettaOwnedVisit cetta_owned_task_visit_accounted(
    CettaReadyQueue *queue, bool poll, CettaOwnedTaskAccount *account);
CettaOwnedVisit cetta_owned_task_poll_accounted(
    CettaOwnedTask *task, bool poll, CettaOwnedTaskAccount *account);
/* The same account plus callback-completion observations in exact service
 * order. This is the bridge for optional receipts; NULL preserves the plain
 * accounted path. */
CettaOwnedVisit cetta_owned_task_poll_observed(
    CettaOwnedTask *task, bool poll, CettaOwnedTaskAccount *account,
    CettaOwnedTaskAccountObserver observer,void *observer_context);

/* A positive cleanup grant removes producer membership once, then advances
 * the existing retained execution cleanup. The grant bounds cleanup service
 * units; queue removal and the poll itself have separate account entries.
 * Zero preserves membership and the complete execution. Pending/atomic replies
 * keep the task and callback context alive; ordinary admission, production and
 * delivery are never invoked. Atomic permission admits work placement only.
 * The host must retain every context until COMPLETE and serialize all calls.
 * The optional account is reset, and the observer has the same restrictions
 * as poll_observed. A completed empty task has no further cleanup poll. */
CettaOwnedReleaseStep cetta_owned_task_cancel_poll(CettaOwnedTask *,
    uint64_t allowance, bool allow_atomic, uint64_t *used,
    CettaOwnedTaskAccount *, CettaOwnedTaskAccountObserver, void *observer_context);

/* Remove this membership and release its computation. Independent captured
 * returns and other tasks retain their own shares. No effect is rolled back. */
void cetta_owned_task_cancel(CettaOwnedTask *task);

#endif
