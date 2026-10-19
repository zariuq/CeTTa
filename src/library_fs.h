#ifndef CETTA_LIBRARY_FS_H
#define CETTA_LIBRARY_FS_H

#include "call_outcome.h"
struct CettaLibraryContext;

/* Stateful filesystem observations are completed values. Resource operations
 * must be serialized with explicit close and context teardown. */
bool cetta_fs_call(struct CettaLibraryContext *ctx, Arena *arena,
                   Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);

#endif
