#ifndef CETTA_TMETTA_H
#define CETTA_TMETTA_H

#include "eval.h"

struct CettaLibraryContext;
struct CettaTmettaRuntime;

void cetta_tmetta_eval(struct CettaLibraryContext *context, Space *space,
    Registry *registry, Arena *output, Atom *expression, int fuel, EvalOutcome *result);

/* Advance the same evaluation in its original live session. A completed
 * frontier is consumed. Pending answers are delivered once the run completes. */
void cetta_tmetta_resume(struct CettaLibraryContext *context, Space *space,
    Registry *registry, Arena *output, Atom *frontier, int fuel, EvalOutcome *result);

/* Deliver a closed value to suspended reads of a query input and advance the
 * frontier. Use the variable's original source identity or a qualified
 * captured identity. Return the number of branches woken. Unrelated inputs
 * leave sleeping branches alone, and completed answers are unchanged. */
size_t cetta_tmetta_resume_with_value(struct CettaLibraryContext *context, Space *space,
    Registry *registry, Arena *output, Atom *frontier, Atom *variable,
    Atom *value, int fuel, EvalOutcome *result);

typedef struct {
    size_t runnable;
    size_t suspended;
    size_t collecting;
    size_t completed;
    size_t observed_spaces;
} CettaTmettaFrontier;

bool cetta_tmetta_frontier_info(struct CettaLibraryContext *context, Atom *frontier,
    CettaTmettaFrontier *info);
void cetta_tmetta_runtime_free(struct CettaTmettaRuntime *runtime);

#endif
