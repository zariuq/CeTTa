#ifndef CETTA_PETTA_LIBPL_H
#define CETTA_PETTA_LIBPL_H

#include "eval.h"
#include "lib_prolog.h"
#include "petta_semantics.h"

PeTTaNamedArity petta_libpl_named_arity(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied);

/*
 * Monotone identity for the currently published foreign-call capability
 * syntax.  Consumers may cache classifications only while this value is
 * unchanged; reading it does not initialize an SWI engine.
 */
uint64_t petta_libpl_capability_revision(
    const CettaLibPrologRuntime *runtime);

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
 * happens to be empty.  The caller initializes and owns `outcomes`.  A goal
 * that raises a Prolog error sets `raised` to its Error term and adds no
 * outcome; the caller propagates it.
 */
bool petta_libpl_call(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *expression, Atom *expected,
    const Bindings *environment, OutcomeSet *outcomes,
    bool *recognized, Atom **raised);

/*
 * PeTTa arithmetic off CeTTa's native fast path.  The embedded Prolog
 * evaluates functor(args...) with is/2, or with no functor the single
 * argument, so the value, the float flags in force and any error are
 * exactly SWI-PeTTa's.  *out receives the number, or the raised error as
 * (Error Formal Context).  False when no embedded Prolog is available.
 */
bool petta_libpl_evaluate_arithmetic(
    Arena *arena, const char *functor, Atom **args, uint32_t nargs,
    Atom **out);

/* SWI's prefer_rationals flag in the embedded Prolog: whether PeTTa's /
 * gives a rational for integers that do not divide. */
bool petta_libpl_prefer_rationals(void);

#endif /* CETTA_PETTA_LIBPL_H */
