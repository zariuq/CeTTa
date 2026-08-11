#ifndef CETTA_GSLT_REVISIONED_SPACE_PROVIDER_V1_H
#define CETTA_GSLT_REVISIONED_SPACE_PROVIDER_V1_H

#include "gslt_provider_runtime.h"
#include "space_match_backend.h"

#include <stdbool.h>
#include <stddef.h>

/* Fixed relational contracts:
 *
 *   open(program, world, receipt)
 *   member(world, occurrence, value)
 *   candidate(world, qpattern, occurrence, qvalue)
 *   emit(world, event, value, successor, occurrence, receipt)
 *
 * The surrounding GSLT owns match, bind, evaluation, and observation.  This
 * provider owns only immutable revision snapshots and occurrence-preserving
 * publication/enumeration. */
typedef struct {
    const char *open_relation;
    const char *open_semantic_id;
    const char *member_relation;
    const char *member_semantic_id;
    const char *candidate_relation;
    const char *candidate_semantic_id;
    const char *emit_relation;
    const char *emit_semantic_id;

    const char *program_nil_constructor;
    const char *program_cons_constructor;
    const char *world_token_constructor;
    const char *stored_occurrence_constructor;
    const char *emitted_occurrence_constructor;
    const char *open_receipt_constructor;
    const char *emit_receipt_constructor;
} CettaGsltRevisionedSpaceSchemaV1;

typedef struct CettaGsltRevisionedSpaceProviderV1
    CettaGsltRevisionedSpaceProviderV1;

CettaGsltRevisionedSpaceProviderV1 *
cetta_gslt_revisioned_space_provider_create_v1(
    SpaceEngine engine,
    const CettaGsltRevisionedSpaceSchemaV1 *schema,
    char *error,
    size_t error_size);

void cetta_gslt_revisioned_space_provider_free_v1(
    CettaGsltRevisionedSpaceProviderV1 *provider);

const CettaGsltProviderRegistryV1 *
cetta_gslt_revisioned_space_provider_registry_v1(
    const CettaGsltRevisionedSpaceProviderV1 *provider);

SpaceEngine cetta_gslt_revisioned_space_provider_engine_v1(
    const CettaGsltRevisionedSpaceProviderV1 *provider);

size_t cetta_gslt_revisioned_space_provider_world_count_v1(
    const CettaGsltRevisionedSpaceProviderV1 *provider);

/* The PathMap case is true only while every realized snapshot remains backed
 * by a live Rust bridge rather than a materialized native-C fallback. */
bool cetta_gslt_revisioned_space_provider_primary_active_v1(
    const CettaGsltRevisionedSpaceProviderV1 *provider);

#endif /* CETTA_GSLT_REVISIONED_SPACE_PROVIDER_V1_H */
