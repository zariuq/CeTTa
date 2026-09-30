#ifndef CETTA_PETTA_LIBPL_H
#define CETTA_PETTA_LIBPL_H

#include "delay_service.h"
#include "call_outcome.h"
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

/* Ordered occurrences from one changed predicate. Each occurrence is either
 * a borrowed syntax row or an immutable literal id in the sink's universe.
 * The two representations never stand for two occurrences. The sink owns
 * retained syntax before this synchronous visit returns. */
typedef struct {
    Arena *arena;
    Atom **rows;
    const AtomId *literal_ids;
    uint32_t count;
} PettaLibplStaticRows;

/* Begin is delivered once before the clause query opens. A sink may supply
 * its universe to admit ground literal data directly; NULL requires syntax
 * for every row. Equations, declarations, callables and non-literal terms
 * always use syntax. An open clause query owns the borrowed row batch. */
typedef bool (*PettaLibplStaticPredicateVisit)(
    void *context, Atom *target, CettaExprLen length, bool multifile,
    bool begin, TermUniverse **literal_universe,
    const PettaLibplStaticRows *batch);

/* static-import! (SWI-PeTTa's lib_import): load `file`, relative to the
 * working directory, as the reference's importer does, from its .qlf, else its
 * .pl compiled to one, else its .metta converted to both, consulting it into
 * the one module that holds the session's imported files, so SWI's consult
 * rules decide the facts (a reload, a multifile predicate's facts gathered, a
 * redefinition); then visit each predicate the load changed (a file converted
 * for one space keeps that space), in synchronous batches. `end` is RAISED with the
 * error loading raised.  False when no Prolog is available, or a visit
 * declines. */
bool petta_libpl_static_import(
    CettaLibPrologRuntime *runtime, Arena *arena, SymbolId space,
    const char *file, PettaLibplStaticPredicateVisit visit, void *context,
    CettaCallOutcome *end);

/* SWI's warning that a load redefined the static procedure name/arity, as
 * its consult prints it: rows a space held that a static-import! load
 * redefined. */
void petta_libpl_warn_redefined(
    CettaLibPrologRuntime *runtime, SymbolId name, size_t arity);

/* Whether the embedded Prolog defines the predicate name/arity now
 * (current_predicate/1), registering nothing.  An engine this runtime has
 * not started defines none, and is not started to say so. */
bool petta_libpl_predicate_defined(
    CettaLibPrologRuntime *runtime, SymbolId name, size_t arity);

/*
 * Execute one optional foreign-predicate boundary.  `recognized` separates
 * an unavailable/unregistered form from a predicate whose valid result bag
 * happens to be empty.  The caller initializes and owns `outcomes`, and
 * `end` says how the call ended (call_outcome.h): FAILURE after its
 * answers, or RAISED with the Error term of the Prolog error the goal
 * raised, which adds no outcome; the caller propagates it.
 */
bool petta_libpl_call(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *expression, Atom *expected,
    const Bindings *environment, OutcomeSet *outcomes,
    bool *recognized, CettaCallOutcome *end,
    const CettaDelayView *delay);

/*
 * PeTTa arithmetic off CeTTa's native fast path.  The embedded Prolog
 * evaluates functor(args...) with is/2, or with no functor the single
 * argument, so the value, the float flags in force and any error are
 * exactly SWI-PeTTa's.  *out is the number as a VALUE, or the error is/2
 * raised as (Error Formal Context), RAISED.  False when no embedded Prolog
 * is available.
 */
bool petta_libpl_evaluate_arithmetic(
    Arena *arena, const char *functor, Atom **args, uint32_t nargs,
    CettaCallOutcome *out);

/* SWI arithmetic comparison, including rational/float promotion and its
 * overflow rules. The result is a PeTTa truth value or a raised payload. */
bool petta_libpl_compare_arithmetic(
    Arena *arena, const char *relation, Atom **args, CettaCallOutcome *out);

/* SWI's prefer_rationals flag in the embedded Prolog: whether PeTTa's /
 * gives a rational for integers that do not divide. */
bool petta_libpl_prefer_rationals(void);

#endif /* CETTA_PETTA_LIBPL_H */
