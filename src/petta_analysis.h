#ifndef CETTA_PETTA_ANALYSIS_H
#define CETTA_PETTA_ANALYSIS_H

#include <stdint.h>

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

/*
 * Combine semantic verdicts for finite conjunction and alternatives.  These
 * operations preserve UNDETERMINED and INCOMPLETE as distinct outcomes; they
 * neither assign cardinality grades nor choose a search schedule.
 */
static inline PettaAnalysisVerdict petta_analysis_verdict_all(
    PettaAnalysisVerdict left, PettaAnalysisVerdict right) {
    if (left == PETTA_ANALYSIS_REFUTED ||
        right == PETTA_ANALYSIS_REFUTED) {
        return PETTA_ANALYSIS_REFUTED;
    }
    if (left == PETTA_ANALYSIS_INCOMPLETE ||
        right == PETTA_ANALYSIS_INCOMPLETE) {
        return PETTA_ANALYSIS_INCOMPLETE;
    }
    if (left == PETTA_ANALYSIS_UNDETERMINED ||
        right == PETTA_ANALYSIS_UNDETERMINED) {
        return PETTA_ANALYSIS_UNDETERMINED;
    }
    return PETTA_ANALYSIS_ESTABLISHED;
}

static inline PettaAnalysisVerdict petta_analysis_verdict_any(
    PettaAnalysisVerdict left, PettaAnalysisVerdict right) {
    if (left == PETTA_ANALYSIS_ESTABLISHED ||
        right == PETTA_ANALYSIS_ESTABLISHED) {
        return PETTA_ANALYSIS_ESTABLISHED;
    }
    if (left == PETTA_ANALYSIS_INCOMPLETE ||
        right == PETTA_ANALYSIS_INCOMPLETE) {
        return PETTA_ANALYSIS_INCOMPLETE;
    }
    if (left == PETTA_ANALYSIS_UNDETERMINED ||
        right == PETTA_ANALYSIS_UNDETERMINED) {
        return PETTA_ANALYSIS_UNDETERMINED;
    }
    return PETTA_ANALYSIS_REFUTED;
}

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

/*
 * A cardinality grade is evidence about how many answers an expression may
 * yield.  UNDETERMINED means that no usable grade has been established; it is
 * distinct from a known nondeterministic computation.  The join preserves the
 * historical v2 aggregation law and is shared by native typing providers.
 */
typedef enum {
    PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC = 0,
    PETTA_ANALYSIS_CARDINALITY_SEMIDETERMINISTIC,
    PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC,
    PETTA_ANALYSIS_CARDINALITY_UNDETERMINED,
} PettaAnalysisCardinality;

static inline PettaAnalysisCardinality petta_analysis_cardinality_join(
    PettaAnalysisCardinality left, PettaAnalysisCardinality right) {
    if (left == PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC ||
        right == PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC) {
        return PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC;
    }
    if (left == PETTA_ANALYSIS_CARDINALITY_UNDETERMINED ||
        right == PETTA_ANALYSIS_CARDINALITY_UNDETERMINED) {
        return PETTA_ANALYSIS_CARDINALITY_UNDETERMINED;
    }
    return left > right ? left : right;
}

/*
 * Declared arrow modes use a finite compatibility relation.  This is kept
 * separate from cardinality aggregation: plain, nondeterministic, and
 * effect-variable requirements deliberately admit every well-formed mode.
 */
typedef enum {
    PETTA_ANALYSIS_ARROW_MODE_INVALID = 0,
    PETTA_ANALYSIS_ARROW_MODE_PLAIN,
    PETTA_ANALYSIS_ARROW_MODE_DETERMINISTIC,
    PETTA_ANALYSIS_ARROW_MODE_SEMIDETERMINISTIC,
    PETTA_ANALYSIS_ARROW_MODE_NONDETERMINISTIC,
    PETTA_ANALYSIS_ARROW_MODE_EFFECT,
} PettaAnalysisArrowMode;

static inline bool petta_analysis_arrow_mode_fits(
    PettaAnalysisArrowMode actual, PettaAnalysisArrowMode required) {
    if (actual == PETTA_ANALYSIS_ARROW_MODE_INVALID ||
        required == PETTA_ANALYSIS_ARROW_MODE_INVALID) {
        return false;
    }
    if (required == PETTA_ANALYSIS_ARROW_MODE_PLAIN ||
        required == PETTA_ANALYSIS_ARROW_MODE_NONDETERMINISTIC ||
        required == PETTA_ANALYSIS_ARROW_MODE_EFFECT) {
        return true;
    }
    if (required == PETTA_ANALYSIS_ARROW_MODE_DETERMINISTIC)
        return actual == PETTA_ANALYSIS_ARROW_MODE_DETERMINISTIC;
    if (required == PETTA_ANALYSIS_ARROW_MODE_SEMIDETERMINISTIC) {
        return actual == PETTA_ANALYSIS_ARROW_MODE_DETERMINISTIC ||
               actual == PETTA_ANALYSIS_ARROW_MODE_SEMIDETERMINISTIC;
    }
    return false;
}

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

/* Boundary facts are explicit evidence, not an implicitly closed theory. */
typedef uint8_t PettaAnalysisBoundaryFacts;

enum {
    PETTA_ANALYSIS_BOUNDARY_FACT_NONE = 0u,
    PETTA_ANALYSIS_BOUNDARY_FACT_NONVAR = 1u << 0,
    PETTA_ANALYSIS_BOUNDARY_FACT_PROPER_LIST = 1u << 1,
    PETTA_ANALYSIS_BOUNDARY_FACT_NONEMPTY_EXPRESSION = 1u << 2,
};

static inline PettaAnalysisBoundaryRequirement
petta_analysis_boundary_requirement_join(
    PettaAnalysisBoundaryRequirement left,
    PettaAnalysisBoundaryRequirement right) {
    return left > right ? left : right;
}

static inline PettaAnalysisBoundaryFacts petta_analysis_boundary_facts_join(
    PettaAnalysisBoundaryFacts left, PettaAnalysisBoundaryFacts right) {
    return (PettaAnalysisBoundaryFacts)(left | right);
}

static inline bool petta_analysis_boundary_facts_satisfy(
    PettaAnalysisBoundaryFacts facts,
    PettaAnalysisBoundaryRequirement requirement) {
    if (requirement == PETTA_ANALYSIS_BOUNDARY_NONE)
        return true;
    if (requirement == PETTA_ANALYSIS_BOUNDARY_NONVAR) {
        return (facts & (PETTA_ANALYSIS_BOUNDARY_FACT_NONVAR |
                         PETTA_ANALYSIS_BOUNDARY_FACT_PROPER_LIST)) != 0u;
    }
    if (requirement == PETTA_ANALYSIS_BOUNDARY_PROPER_LIST) {
        return (facts & (PETTA_ANALYSIS_BOUNDARY_FACT_PROPER_LIST |
                         PETTA_ANALYSIS_BOUNDARY_FACT_NONEMPTY_EXPRESSION)) != 0u;
    }
    if (requirement == PETTA_ANALYSIS_BOUNDARY_NONEMPTY_EXPRESSION) {
        return (facts & PETTA_ANALYSIS_BOUNDARY_FACT_NONEMPTY_EXPRESSION) != 0u;
    }
    return false;
}

#endif /* CETTA_PETTA_ANALYSIS_H */
