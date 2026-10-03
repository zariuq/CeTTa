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
} CettaEvalCompletion;

#endif
