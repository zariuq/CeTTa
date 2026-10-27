#ifndef CETTA_EVAL_COMPLETION_H
#define CETTA_EVAL_COMPLETION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Whether an evaluation exhausted its complete answer frontier.  Services
 * used by the evaluator return the same vocabulary so resource failure can
 * never be mistaken for ordinary relational exhaustion. */
typedef enum {
    CETTA_EVAL_COMPLETE = 0,
    CETTA_EVAL_INCOMPLETE_FUEL,
    CETTA_EVAL_INCOMPLETE_CANCELLED,
    CETTA_EVAL_INCOMPLETE_STACK,
    CETTA_EVAL_INCOMPLETE_CAPACITY,
    /* The program changed under pending work that could not follow it. */
    CETTA_EVAL_INCOMPLETE_INVALIDATED,
    /* A host service the machine depends on failed. */
    CETTA_EVAL_INCOMPLETE_HOST_FAILURE,
} CettaEvalCompletion;

typedef enum {
    CETTA_SOURCE_COMPUTE,
    CETTA_SOURCE_ADVANCE,
    CETTA_SOURCE_PUBLISH,
} CettaSourcePhase;

/* The declaration/query root is distinct from a shared executable plan.
 * Position zero denotes derived work without an exact source-node receipt. */
typedef struct {
    uint64_t root;
    uint32_t position;
    uint32_t debit;
} CettaSourceOccurrence;

uint64_t cetta_source_root_new(void);

/* Borrowed admission receipt. Operational instances name the executing
 * machine and retained goal; source names the declaration/query occurrence.
 * Frontier boundaries have no goal instance and use source position zero.
 * Adapters without source-node metadata retain explicit unit accounting. */
typedef struct {
    uint64_t machine_instance;
    uint64_t occurrence_instance;
    uint64_t debit;
    CettaSourcePhase phase;
    CettaSourceOccurrence source;
} CettaSourceStep;

/* Completion is an identity; the first incomplete cause survives nested
 * delivery. A later successful or interrupted child cannot replace it. */
static inline void cetta_eval_completion_record(
        CettaEvalCompletion *current, CettaEvalCompletion observed) {
    if (*current == CETTA_EVAL_COMPLETE)
        *current = observed;
}

/* One nested evaluation's completion and borrowed semantic-step purse.
 * Physical worker allowances and recursion-depth fuel remain separate. */
typedef struct CettaEvalEpisode {
    CettaEvalCompletion completion;
    bool interrupted;
    bool budget_limited;
    uint64_t budget_initial;
    uint64_t budget_remaining;
    uint64_t steps_spent;
    struct CettaEvalEpisode *parent;
} CettaEvalEpisode;

/* Source-fuel admission for a worker without its own debit refinement.
 * Cancellation, callee authority and physical allowances are separate. */
static inline bool cetta_eval_episode_allows_unmetered(
        const CettaEvalEpisode *episode, int requested_fuel) {
    return requested_fuel < 0 && !(episode && episode->budget_limited);
}

static inline CettaEvalEpisode cetta_eval_episode_begin(
        CettaEvalEpisode *parent, int requested_fuel) {
    bool requested_limited = requested_fuel >= 0;
    bool inherited = parent && parent->budget_limited;
    uint64_t granted = requested_limited ? (uint64_t)requested_fuel : 0u;
    if (inherited && (!requested_limited || parent->budget_remaining < granted))
        granted = parent->budget_remaining;
    return (CettaEvalEpisode){
        .completion = parent && parent->interrupted
            ? parent->completion : CETTA_EVAL_COMPLETE,
        .interrupted = parent && parent->interrupted,
        .budget_limited = requested_limited || inherited,
        .budget_initial = granted, .budget_remaining = granted,
        .parent = parent,
    };
}

/* Refusal leaves the pending computation and its purse unspent. */
static inline bool cetta_eval_episode_step(CettaEvalEpisode *episode, uint64_t debit) {
    if (!episode)
        return true;
    if (episode->interrupted)
        return false;
    if (episode->budget_limited) {
        if (debit > episode->budget_remaining) {
            cetta_eval_completion_record(&episode->completion,
                CETTA_EVAL_INCOMPLETE_FUEL);
            return false;
        }
        episode->budget_remaining -= debit;
        episode->steps_spent = episode->steps_spent > UINT64_MAX - debit
            ? UINT64_MAX : episode->steps_spent + debit;
    }
    return true;
}

/* Settle actual child work once. Unused credit is never spent or refunded;
 * the parent has been paused while the child owns the borrowed purse. */
static inline CettaEvalCompletion cetta_eval_episode_end(CettaEvalEpisode *episode) {
    CettaEvalEpisode *parent = episode->parent;
    if (parent) {
        episode->parent = NULL;
        if (episode->completion != CETTA_EVAL_COMPLETE)
            cetta_eval_completion_record(&parent->completion, episode->completion);
        if (episode->interrupted)
            parent->interrupted = true;
        if (parent->budget_limited) {
            uint64_t spent = episode->steps_spent;
            parent->steps_spent = parent->steps_spent > UINT64_MAX - spent
                ? UINT64_MAX : parent->steps_spent + spent;
            parent->budget_remaining = spent >= parent->budget_remaining
                ? 0u : parent->budget_remaining - spent;
        }
    }
    return episode->completion;
}

#endif
