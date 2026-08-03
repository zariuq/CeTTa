#ifndef CETTA_LIB_PROLOG_H
#define CETTA_LIB_PROLOG_H

#include "atom.h"

#include <stdbool.h>

typedef struct CettaLibPrologRuntime CettaLibPrologRuntime;

typedef enum {
    CETTA_LIB_PROLOG_QUERY_UNAVAILABLE = 0,
    CETTA_LIB_PROLOG_QUERY_FAILED,
    CETTA_LIB_PROLOG_QUERY_OK,
} CettaLibPrologQueryStatus;

CettaLibPrologRuntime *cetta_lib_prolog_runtime_new(void);
void cetta_lib_prolog_runtime_free(CettaLibPrologRuntime *runtime);

/*
 * Final process-level shutdown for an adapter-owned Prolog engine.  Runtime
 * contexts must be freed first; this call intentionally does not support
 * creating another runtime afterward.
 */
void cetta_lib_prolog_global_shutdown(void);

/*
 * Probe the optional engine and prepare this context's private module.
 * Merely constructing a CeTTa language context never initializes Prolog.
 */
bool cetta_lib_prolog_runtime_available(
    CettaLibPrologRuntime *runtime);

/*
 * Run `goal` in the context-private Prolog module and return one explicit
 * MeTTa bag containing `projection` once per solution, in Prolog order.
 * Duplicate projections are retained.  A successful query with no solutions
 * returns the empty expression.
 */
CettaLibPrologQueryStatus cetta_lib_prolog_query(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *goal, Atom *projection, Atom **answers);

#endif /* CETTA_LIB_PROLOG_H */
