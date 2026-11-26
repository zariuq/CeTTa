#ifndef CETTA_LIBRARY_SYNTAX_H
#define CETTA_LIBRARY_SYNTAX_H
#include "call_outcome.h"
#include "import_mask.h"
struct CettaLibraryContext;
struct Space;
#define CETTA_NATIVE_IMPORT_SYNTAX CETTA_NATIVE_IMPORT_BIT(4)
bool cetta_syntax_module_call(struct CettaLibraryContext *, struct Space *, Arena *,
    Atom *, Atom **, uint32_t, CettaCallOutcome *);
#endif
