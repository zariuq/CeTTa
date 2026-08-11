#ifndef CETTA_GSLT_SPACE_FACT_PROVIDER_V1_H
#define CETTA_GSLT_SPACE_FACT_PROVIDER_V1_H

#include "gslt_provider_runtime.h"
#include "space_match_backend.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaGsltSpaceFactProviderV1 CettaGsltSpaceFactProviderV1;

/* Construct one completed-bag provider over a CeTTa Space.  Native and
 * PathMap select physical storage/query realizations of the same relational
 * fact contract; neither choice changes the authored semantic identity. */
CettaGsltSpaceFactProviderV1 *cetta_gslt_space_fact_provider_create_v1(
    SpaceEngine engine,
    const char *relation,
    uint32_t arity,
    const char *semantic_id,
    char *error,
    size_t error_size);

void cetta_gslt_space_fact_provider_free_v1(
    CettaGsltSpaceFactProviderV1 *provider);

/* Publish one occurrence into the provider-owned physical space.  The fact
 * must have the provider relation and arity. */
bool cetta_gslt_space_fact_provider_admit_v1(
    CettaGsltSpaceFactProviderV1 *provider,
    const Arena *source_arena,
    Atom *fact,
    char *error,
    size_t error_size);

const CettaGsltProviderV1 *cetta_gslt_space_fact_provider_physical_v1(
    const CettaGsltSpaceFactProviderV1 *provider);

SpaceEngine cetta_gslt_space_fact_provider_engine_v1(
    const CettaGsltSpaceFactProviderV1 *provider);

/* True when the selected physical authority remains live.  In particular,
 * the PathMap case requires an active Rust bridge rather than a materialized
 * native-C fallback. */
bool cetta_gslt_space_fact_provider_primary_active_v1(
    const CettaGsltSpaceFactProviderV1 *provider);

#endif /* CETTA_GSLT_SPACE_FACT_PROVIDER_V1_H */
