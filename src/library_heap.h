#ifndef CETTA_LIBRARY_HEAP_H
#define CETTA_LIBRARY_HEAP_H
#include "call_outcome.h"
struct CettaLibraryContext;
struct Space;
#define CETTA_NATIVE_IMPORT_HEAP (UINT64_C(1) << 39)
bool cetta_heap_module_call(struct CettaLibraryContext *, struct Space *, Arena *,
    Atom *, Atom **, uint32_t, CettaCallOutcome *);
#endif
