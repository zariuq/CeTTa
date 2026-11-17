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
/* Enter a source occurrence with its already captured plan, without
 * translating it again in a later declaration context. The plan belongs to
 * this source library and must outlive the query; a candidate lease also
 * retains its occurrence provenance. Source variables keep their identities. */
CettaEvalQuery *eval_query_open_with_plan(Space *, Arena *persistent, Registry *,
    Atom *, const struct PettaPlanNode *, int fuel);
/* Structured application: operands are already values, including expressions. */
CettaEvalQuery *eval_query_open_values(Space *, Arena *, Registry *, Atom *call, int fuel);
CettaEvalQuery *eval_query_open_exports(Space *, Arena *persistent, Registry *,
    Atom *source, Atom *variables, int fuel);
Atom *eval_query_pattern_variables(Arena *, Atom *pattern);
CettaEvalQueryStep eval_query_step(CettaEvalQuery *, Arena *, uint64_t physical_allowance,
    EvalOutcome *, CettaEvalQueryAnswer *, uint64_t *transitions);
void eval_query_close(CettaEvalQuery *);
bool eval_query_has_retained_driver(const CettaEvalQuery *);

typedef enum {
    CETTA_QUERY_OUTPUT_OPAQUE,
    CETTA_QUERY_OUTPUT_PATTERN,
    /* This driver has no projection service, or interpretation has entered.
     * The ordinary computation remains authoritative. */
    CETTA_QUERY_OUTPUT_UNAVAILABLE,
    CETTA_QUERY_OUTPUT_STOPPED,
} CettaEvalQueryOutput;
/* Before the first transition, inspect the captured root under the actual
 * source host. This does not consume fuel, enter effects or publish bindings.
 * PATTERN is owned by arena; OPAQUE/UNAVAILABLE impose no constraint.
 * STOPPED records a resource or ownership failure, never logical emptiness. */
CettaEvalQueryOutput eval_query_project_output(CettaEvalQuery *, Arena *,
    Atom **pattern, CettaEvalCompletion *completion);

#endif
