#ifndef CETTA_LIBRARY_TRANSFORM_H
#define CETTA_LIBRARY_TRANSFORM_H

#include "call_outcome.h"
#include "library.h"

#define CETTA_NATIVE_IMPORT_TRANSFORM (1ull << 43)

bool cetta_eval_transform_module_call(
    CettaLibraryContext *ctx, Space *space, Arena *a,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);

/* Read one answer, optionally limiting an effectful transform to one firing. */
bool cetta_eval_transform_first(Space *space, Arena *a, Atom *call,
                                bool first_firing, OutcomeSet *answers);

#endif /* CETTA_LIBRARY_TRANSFORM_H */
