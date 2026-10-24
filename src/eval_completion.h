#ifndef CETTA_EVAL_COMPLETION_H
#define CETTA_EVAL_COMPLETION_H

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
    /* A denied effect remains observable even when an Error is caught. */
    CETTA_EVAL_INCOMPLETE_EFFECT_DENIED,
    /* A requested finite service slice retained its unfinished computation. */
    CETTA_EVAL_INCOMPLETE_PAUSED,
} CettaEvalCompletion;

/* Completion is an identity; the first incomplete cause survives nested
 * delivery. A later successful or interrupted child cannot replace it. */
static inline void cetta_eval_completion_record(
        CettaEvalCompletion *current, CettaEvalCompletion observed) {
    if (*current == CETTA_EVAL_COMPLETE)
        *current = observed;
}

#endif
