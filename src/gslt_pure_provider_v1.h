#ifndef CETTA_GSLT_PURE_PROVIDER_V1_H
#define CETTA_GSLT_PURE_PROVIDER_V1_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    CETTA_GSLT_PURE_DECLINED_V1 = 0,
    CETTA_GSLT_PURE_PRODUCED_V1 = 1,
    CETTA_GSLT_PURE_FAULT_V1 = 2,
} CettaGsltPureOutcomeV1;

/*
 * A versioned, deterministic capability slice used by authored support-
 * transform profiles.  Acceptance is structural and side-effect free;
 * evaluation never invokes the ambient MeTTa evaluator or reads a space.
 */
bool cetta_gslt_pure_f64_accepts_v1(const Atom *call);

CettaGsltPureOutcomeV1 cetta_gslt_pure_f64_evaluate_v1(
    Arena *arena, Atom *call, Atom **result,
    char *error, size_t error_size);

#endif /* CETTA_GSLT_PURE_PROVIDER_V1_H */
