#ifndef CETTA_LIBRARY_TRANSFORM_H
#define CETTA_LIBRARY_TRANSFORM_H

#include "call_outcome.h"
#include "library.h"

#define CETTA_NATIVE_IMPORT_TRANSFORM (1u << 30)

bool cetta_eval_transform_module_call(
    CettaLibraryContext *ctx, Space *space, Arena *a,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);

/* The first answer of a transform, with no later branch fired. */
bool cetta_eval_transform_first(Space *space, Arena *a, Atom *call,
                                OutcomeSet *answers);

#endif /* CETTA_LIBRARY_TRANSFORM_H */
