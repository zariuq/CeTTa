#ifndef CETTA_LIBRARY_PROC_H
#define CETTA_LIBRARY_PROC_H
#include "atom.h"
#include "call_outcome.h"
struct CettaLibraryContext;
/* Worker threads own only C data. Completed byte observations enter the
 * evaluator at an explicit wait/poll boundary. */
bool cetta_proc_call(struct CettaLibraryContext *ctx, Arena *arena,
                    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
#endif
