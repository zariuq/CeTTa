#ifndef CETTA_LIBRARY_POSTINGS_H
#define CETTA_LIBRARY_POSTINGS_H
#include "call_outcome.h"
struct CettaLibraryContext;
bool cetta_postings_call(struct CettaLibraryContext *context, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
#endif
