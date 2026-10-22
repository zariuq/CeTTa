#ifndef CETTA_LIBRARY_TENSOR_H
#define CETTA_LIBRARY_TENSOR_H
#include "call_outcome.h"
struct CettaLibraryContext;
struct Space;
bool cetta_tensor_call(struct CettaLibraryContext *context, struct Space *space, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
#endif
