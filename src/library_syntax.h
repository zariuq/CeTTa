#ifndef CETTA_LIBRARY_SYNTAX_H
#define CETTA_LIBRARY_SYNTAX_H
#include "call_outcome.h"
struct CettaLibraryContext;
struct Space;
#define CETTA_NATIVE_IMPORT_SYNTAX (1u << 22)
bool cetta_syntax_module_call(struct CettaLibraryContext *, struct Space *, Arena *,
    Atom *, Atom **, uint32_t, CettaCallOutcome *);
#endif
