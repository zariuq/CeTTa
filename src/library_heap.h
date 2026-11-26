#ifndef CETTA_LIBRARY_HEAP_H
#define CETTA_LIBRARY_HEAP_H
#include "call_outcome.h"
#include "import_mask.h"
struct CettaLibraryContext;
struct Space;
#define CETTA_NATIVE_IMPORT_HEAP CETTA_NATIVE_IMPORT_BIT(2)
bool cetta_heap_module_call(struct CettaLibraryContext *, struct Space *, Arena *,
    Atom *, Atom **, uint32_t, CettaCallOutcome *);
#endif
