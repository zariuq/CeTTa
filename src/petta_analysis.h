#ifndef CETTA_PETTA_ANALYSIS_H
#define CETTA_PETTA_ANALYSIS_H

#include "atom.h"
#include "space.h"

/*
 * Generic semantic judgments consumed by the PeTTa execution machine.
 * Providers may implement typing, effect checking, or a future generated
 * analysis.  The machine depends on this vocabulary, never on one provider.
 */
typedef enum {
    PETTA_ANALYSIS_ESTABLISHED = 0,
    PETTA_ANALYSIS_REFUTED,
    PETTA_ANALYSIS_UNDETERMINED,
    PETTA_ANALYSIS_INCOMPLETE,
} PettaAnalysisVerdict;

typedef enum {
    PETTA_ANALYSIS_REASON_NONE = 0,
    PETTA_ANALYSIS_REASON_EXACT,
    PETTA_ANALYSIS_REASON_WILDCARD,
    PETTA_ANALYSIS_REASON_STRUCTURAL,
    PETTA_ANALYSIS_REASON_DECLARED,
    PETTA_ANALYSIS_REASON_OPEN_VALUE,
    PETTA_ANALYSIS_REASON_CYCLE,
    PETTA_ANALYSIS_REASON_MISMATCH,
    PETTA_ANALYSIS_REASON_NONCALLABLE,
} PettaAnalysisReason;

typedef enum {
    PETTA_ANALYSIS_FAULT_NONE = 0,
    PETTA_ANALYSIS_FAULT_INVALID_ARGUMENT,
    PETTA_ANALYSIS_FAULT_ALLOCATION,
    PETTA_ANALYSIS_FAULT_MALFORMED_REQUIREMENT,
} PettaAnalysisFault;

typedef enum {
    PETTA_ANALYSIS_CALLABLE_NO = 0,
    PETTA_ANALYSIS_CALLABLE_YES,
    PETTA_ANALYSIS_CALLABLE_UNKNOWN,
} PettaAnalysisCallable;

typedef PettaAnalysisCallable (*PettaAnalysisCallableFn)(
    void *context, Atom *value, CettaExprLen arity);

typedef struct {
    PettaAnalysisVerdict verdict;
    PettaAnalysisReason reason;
    PettaAnalysisFault fault;
    SpaceDeclaredTypeLookupCost declaration_lookup_cost;
} PettaAnalysisResult;

/* Residual argument facts consumed by a compiled call contract. */
typedef enum {
    PETTA_ANALYSIS_BOUNDARY_NONE = 0,
    PETTA_ANALYSIS_BOUNDARY_NONVAR,
    PETTA_ANALYSIS_BOUNDARY_PROPER_LIST,
    PETTA_ANALYSIS_BOUNDARY_NONEMPTY_EXPRESSION,
} PettaAnalysisBoundaryRequirement;

#endif /* CETTA_PETTA_ANALYSIS_H */
