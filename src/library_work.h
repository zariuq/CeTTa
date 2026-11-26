#ifndef CETTA_LIBRARY_WORK_H
#define CETTA_LIBRARY_WORK_H
#include "call_outcome.h"
#include "import_mask.h"
struct CettaLibraryContext;
struct Space;
#define CETTA_NATIVE_IMPORT_WORK CETTA_NATIVE_IMPORT_BIT(1)
bool cetta_work_module_call(struct CettaLibraryContext *, struct Space *, Arena *,
    Atom *, Atom **, uint32_t, CettaCallOutcome *);
#endif
