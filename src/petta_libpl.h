#ifndef CETTA_PETTA_LIBPL_H
#define CETTA_PETTA_LIBPL_H

#include "eval.h"
#include "lib_prolog.h"
#include "petta_semantics.h"

PeTTaNamedArity petta_libpl_named_arity(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied);

/*
 * Execute one optional foreign-predicate boundary.  `recognized` separates
 * an unavailable/unregistered form from a predicate whose valid result bag
 * happens to be empty.  The caller initializes and owns `outcomes`.
 */
bool petta_libpl_call(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *expression, Atom *expected,
    const Bindings *environment, OutcomeSet *outcomes,
    bool *recognized);

#endif /* CETTA_PETTA_LIBPL_H */
