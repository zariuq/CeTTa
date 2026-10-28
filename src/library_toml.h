#ifndef CETTA_LIBRARY_TOML_H
#define CETTA_LIBRARY_TOML_H
#include "call_outcome.h"
struct CettaLibraryContext;

/* (__cetta_lib_toml OPERATION (OPERANDS...)), the operations of lib/toml. */
bool cetta_toml_call(struct CettaLibraryContext *, Arena *, Atom *, Atom **,
                     uint32_t, CettaCallOutcome *);
#endif
