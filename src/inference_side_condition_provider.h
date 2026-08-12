#ifndef CETTA_INFERENCE_SIDE_CONDITION_PROVIDER_H
#define CETTA_INFERENCE_SIDE_CONDITION_PROVIDER_H

#include "gslt_provider_runtime.h"

typedef enum {
    CETTA_INFERENCE_PATTERN_ABT_OK = 0,
    CETTA_INFERENCE_PATTERN_ABT_NO_RESULT = 1,
    CETTA_INFERENCE_PATTERN_ABT_INVALID = 2,
    CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT = 3,
} CettaInferencePatternAbtStatusV1;

CettaInferencePatternAbtStatusV1
cetta_inference_pattern_explicit_substitution_v1(
    Arena *arena,
    Atom *body,
    Atom *replacement,
    Atom **result);

CettaInferencePatternAbtStatusV1
cetta_inference_pattern_unused_binder_elimination_v1(
    Arena *arena,
    Atom *body,
    Atom **result);

/* Validate the exact locally-nameless Pattern carrier at one ambient binder
   depth.  This is the physical interpretation of
   InferenceChecker.argumentValidAt: no schema metavariables, canonical
   binder metadata, and every de Bruijn index inside its support. */
CettaInferencePatternAbtStatusV1
cetta_inference_pattern_supported_at_v1(
    Arena *arena,
    uint64_t depth,
    Atom *pattern);

bool cetta_inference_pattern_collection_type_valid_v1(
    const Atom *collection_type);

const CettaGsltProviderRegistryV1 *
cetta_inference_side_condition_provider_registry_v1(void);

#endif /* CETTA_INFERENCE_SIDE_CONDITION_PROVIDER_H */
