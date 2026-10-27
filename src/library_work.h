#ifndef CETTA_LIBRARY_WORK_H
#define CETTA_LIBRARY_WORK_H
#include "call_outcome.h"
struct CettaLibraryContext;
struct Space;
#define CETTA_NATIVE_IMPORT_WORK (1u << 19)
bool cetta_work_module_call(struct CettaLibraryContext *, struct Space *, Arena *,
    Atom *, Atom **, uint32_t, CettaCallOutcome *);
#endif
