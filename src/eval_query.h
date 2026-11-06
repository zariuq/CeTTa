#ifndef CETTA_EVAL_QUERY_H
#define CETTA_EVAL_QUERY_H
#include "eval.h"
#include "call_outcome.h"

typedef struct CettaEvalQuery CettaEvalQuery;
typedef struct {
    /* Owned syntax-opaque substitution and conditional-goal packet. Both
     * are published in the caller's arena, independently of the query. */
    Atom *bindings;
    Atom *delayed;
    CettaCallOutcomeKind kind;
} CettaEvalQueryAnswer;
typedef enum {
    CETTA_QUERY_ANSWER,
    CETTA_QUERY_PAUSED,
    CETTA_QUERY_EXHAUSTED,
    CETTA_QUERY_STOPPED,
    /* The source has not entered. Its canonical retained driver is pending. */
    CETTA_QUERY_DRIVER_PENDING,
} CettaEvalQueryStep;

/* The enclosing runtime supplies its library/name context. The query owns
 * its source, execution banks and cumulative source purse until close.
 * Each published answer is copied into the caller's independent arena. */
CettaEvalQuery *eval_query_open(Space *, Arena *persistent, Registry *, Atom *, int fuel);
/* Structured application: operands are already values, including expressions. */
CettaEvalQuery *eval_query_open_values(Space *, Arena *, Registry *, Atom *call, int fuel);
CettaEvalQuery *eval_query_open_exports(Space *, Arena *persistent, Registry *,
    Atom *source, Atom *variables, int fuel);
Atom *eval_query_pattern_variables(Arena *, Atom *pattern);
CettaEvalQueryStep eval_query_step(CettaEvalQuery *, Arena *, uint64_t physical_allowance,
    EvalOutcome *, CettaEvalQueryAnswer *, uint64_t *transitions);
void eval_query_close(CettaEvalQuery *);
bool eval_query_has_retained_driver(const CettaEvalQuery *);

#endif
