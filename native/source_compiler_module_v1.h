#ifndef CETTA_SOURCE_COMPILER_MODULE_V1_H
#define CETTA_SOURCE_COMPILER_MODULE_V1_H
#include "src/atom.h"
struct CettaLibraryContext;
Atom *cetta_source_compiler_dispatch_v1(struct CettaLibraryContext *, Arena *, Atom *, Atom **, uint32_t);
#endif
