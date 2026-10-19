#ifndef CETTA_LIBRARY_RE_H
#define CETTA_LIBRARY_RE_H
#include "call_outcome.h"
struct CettaLibraryContext;
bool cetta_re_call(struct CettaLibraryContext *, Arena *, Atom *, Atom **,
                   uint32_t, CettaCallOutcome *);
#endif
