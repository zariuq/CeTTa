#ifndef CETTA_CALL_OUTCOME_H
#define CETTA_CALL_OUTCOME_H

#include "atom.h"

/*
 * What one step of calling an operation did.  Every route that runs an
 * operation reports it in this one type -- a grounded operation, a Prolog
 * predicate, a Python callable, a host evaluation -- and every evaluator
 * reads the same states:
 *
 *   VALUE        `term` is an answer; a call with several answers reports
 *                each, then ends in one of the other states.
 *   FAILURE      no (further) answer: the branch fails.
 *   RAISED       `term` is the error.  It ends the call and propagates to
 *                the nearest handler: PeTTa's catch makes it a value, a
 *                dispatch decided at run time fails its own branch, and a
 *                static call's uncaught error stops the run.
 *   INTERRUPTED  cancellation or a requested exit stopped the call; its
 *                answers so far are not a finished answer bag.
 *   SUSPENDED    `term` is an answer that holds while the goals `delayed`
 *                on its variables hold: the caller suspends them in its
 *                delay service (delay_service.h), which wakes them when one
 *                of their variables is bound.  SWI-Prolog's constraints,
 *                attributes and frozen goals answer this way.
 *
 * A raised error is known where it is raised, never recovered from the
 * shape of a value: an Error term an operation returns as data, or a value
 * PeTTa's catch made, is a VALUE.
 */
typedef enum {
    CETTA_CALL_VALUE = 0,
    CETTA_CALL_FAILURE,
    CETTA_CALL_RAISED,
    CETTA_CALL_INTERRUPTED,
    CETTA_CALL_SUSPENDED,
} CettaCallOutcomeKind;

typedef struct {
    CettaCallOutcomeKind kind;
    Atom *term;
    /* SUSPENDED: the delayed goals, in the client's form. */
    Atom *delayed;
} CettaCallOutcome;

static inline CettaCallOutcome cetta_call_value(Atom *value) {
    return (CettaCallOutcome){.kind = CETTA_CALL_VALUE, .term = value};
}

static inline CettaCallOutcome cetta_call_failure(void) {
    return (CettaCallOutcome){.kind = CETTA_CALL_FAILURE, .term = NULL};
}

static inline CettaCallOutcome cetta_call_raised(Atom *error) {
    return (CettaCallOutcome){.kind = CETTA_CALL_RAISED, .term = error};
}

static inline CettaCallOutcome cetta_call_interrupted(void) {
    return (CettaCallOutcome){.kind = CETTA_CALL_INTERRUPTED, .term = NULL};
}

static inline CettaCallOutcome cetta_call_suspended(Atom *value,
                                                    Atom *delayed) {
    return (CettaCallOutcome){.kind = CETTA_CALL_SUSPENDED, .term = value,
                              .delayed = delayed};
}

#endif /* CETTA_CALL_OUTCOME_H */
