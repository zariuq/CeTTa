#ifndef CETTA_PETTA_LIBPL_H
#define CETTA_PETTA_LIBPL_H

#include "eval.h"
#include "lib_prolog.h"
#include "petta_semantics.h"

PeTTaNamedArity petta_libpl_named_arity(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied);

/*
 * Plan-time resolution of a source application head against the live
 * engine, mirroring the reference translator: a name current_predicate/1
 * enumerates (or an arity/2 row declares) is registered as an import on
 * first proof of existence, so every runtime seam afterwards sees it
 * through the ordinary registry.  Data positions never consult this — the
 * reference calls engine predicates only where it compiles calls.
 */
PeTTaNamedArity petta_libpl_named_arity_including_resolved(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied);
PeTTaNamedArity petta_libpl_named_arity_resolving(
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
