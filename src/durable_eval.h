#ifndef CETTA_DURABLE_EVAL_H
#define CETTA_DURABLE_EVAL_H
#include "durable_host.h"

/* Internal host mechanism shared by decision and intake tickets. A fresh
 * program projection, registry and result lifetime; never a worker API. */
typedef struct {
    Arena persistent, scratch;
    Space space;
    Registry registry;
    EvalOutcome outcome;
} CettaHostEvaluation;
CettaDurableStatus cetta_host_eval_create(const CettaHostProgram *program,
                                         CettaHostEvaluation **out);
void cetta_host_eval_free(CettaHostEvaluation *evaluation);
CettaDurableStatus cetta_host_eval_run(CettaHostEvaluation *evaluation,
    const CettaHostProgram *program, Atom *expression, int fuel);
#endif
