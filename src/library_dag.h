#ifndef CETTA_LIBRARY_DAG_H
#define CETTA_LIBRARY_DAG_H
#include "call_outcome.h"
struct CettaLibraryContext;
struct Space;
#define CETTA_NATIVE_IMPORT_DAG (UINT64_C(1) << 40)
bool cetta_dag_module_call(struct CettaLibraryContext *, struct Space *, Arena *,
    Atom *, Atom **, uint32_t, CettaCallOutcome *);
#endif
