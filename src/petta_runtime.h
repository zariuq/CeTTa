#ifndef CETTA_PETTA_RUNTIME_H
#define CETTA_PETTA_RUNTIME_H

#include "atom.h"
#include "petta_semantics.h"

struct CettaLibraryContext;
struct CettaPettaRuntimeState;

struct CettaPettaRuntimeState *cetta_petta_runtime_state_new(void);
void cetta_petta_runtime_state_free(struct CettaPettaRuntimeState *state);

/*
 * Native host values of the PeTTa dialect.  These are projections of CeTTa's
 * own process state, not foreign predicates.  A recognized call may produce
 * no result (for example, an argv index outside the available range).
 */
PeTTaNamedArity cetta_petta_runtime_named_arity(
    const struct CettaLibraryContext *context,
    SymbolId head, CettaExprLen supplied);

bool cetta_petta_runtime_call(
    struct CettaLibraryContext *context,
    Arena *arena, Atom *expression,
    Atom **result, bool *recognized);

#endif
