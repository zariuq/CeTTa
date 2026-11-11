#ifndef CETTA_LIBRARY_PROVENANCE_H
#define CETTA_LIBRARY_PROVENANCE_H
#include "call_outcome.h"
struct CettaLibraryContext;
/* The provenance library's native operation:
 *   (__cetta_lib_provenance_supports FACTS RULES GOALS BOUND)
 * over a rule program: FACTS holds (fact PRED A ...) base facts, RULES holds
 * (rule LABEL (lit HEAD V ...) ((lit PRED V ...) ...)) with V variable indices,
 * GOALS holds (goal PRED A ...) where an A may be a variable, and BOUND is
 * (steps K) or (labels K).  It answers the sorted list of the supports of the
 * goals' derivations, each the sorted list of the labels a derivation uses: of
 * at most K rule instances, or using at most K distinct labels.  See
 * lib/provenance.metta. */
bool cetta_provenance_call(struct CettaLibraryContext *context, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
#endif
