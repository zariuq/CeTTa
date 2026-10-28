#ifndef CETTA_LIBRARY_WORK_H
#define CETTA_LIBRARY_WORK_H
#include "call_outcome.h"
struct CettaLibraryContext;
struct Space;
#define CETTA_NATIVE_IMPORT_WORK (UINT64_C(1) << 42)
bool cetta_work_module_call(struct CettaLibraryContext *, struct Space *, Arena *,
    Atom *, Atom **, uint32_t, CettaCallOutcome *);
#endif
