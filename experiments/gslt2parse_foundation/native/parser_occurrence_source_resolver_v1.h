#ifndef CETTA_GSLT2PARSE_PARSER_OCCURRENCE_SOURCE_RESOLVER_V1_H
#define CETTA_GSLT2PARSE_PARSER_OCCURRENCE_SOURCE_RESOLVER_V1_H

#include <stddef.h>

#include "parser_occurrence_fold_v1.h"

typedef enum {
    PPOCCURRENCE_SOURCE_RESOLUTION_V1_ACCEPTED = 0,
    PPOCCURRENCE_SOURCE_RESOLUTION_V1_REJECTED = 1,
    PPOCCURRENCE_SOURCE_RESOLUTION_V1_RESOURCE = 2,
    PPOCCURRENCE_SOURCE_RESOLUTION_V1_INVALID = 3
} PPOccurrenceSourceResolutionV1;

typedef PPOccurrenceSourceResolutionV1
(*PPOccurrenceSourceResolverV1ResolveFn)(
    void *context,
    const PPOccurrenceFoldV1Value *source_path,
    bool skip_completed_sources,
    bool reject_active_source_cycles,
    const PPOccurrenceFoldV1Backend *nested_backend,
    char *error_buf,
    size_t error_buf_size);

typedef bool (*PPOccurrenceSourceResolverV1DigestFn)(
    void *context,
    char out[65],
    char *error_buf,
    size_t error_buf_size);

typedef bool (*PPOccurrenceSourceResolverV1CurrentFn)(
    void *context,
    uint32_t *source_id_out,
    char *error_buf,
    size_t error_buf_size);

typedef struct {
    void *context;
    PPOccurrenceSourceResolverV1ResolveFn resolve;
    PPOccurrenceSourceResolverV1DigestFn digest;
    /* Optional for state-only consumers; required by source-positioned
     * occurrence composition. */
    PPOccurrenceSourceResolverV1CurrentFn current;
} PPOccurrenceSourceResolverV1;

#endif
