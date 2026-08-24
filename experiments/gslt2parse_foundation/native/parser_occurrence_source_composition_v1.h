#ifndef CETTA_GSLT2PARSE_PARSER_OCCURRENCE_SOURCE_COMPOSITION_V1_H
#define CETTA_GSLT2PARSE_PARSER_OCCURRENCE_SOURCE_COMPOSITION_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_occurrence_source_resolver_v1.h"
#include "relational_state_program_v1.h"

/*
 * Composes recursively resolved sources into one ordered occurrence stream.
 * Source-resolution operations and their policies come exclusively from the
 * compiled state plan.  Such an operation is consumed at this layer and its
 * included occurrences are forwarded in place; every other operation is
 * forwarded unchanged, tagged with its dense source identity.
 */
typedef struct {
    const PPOccurrenceFoldV1Plan *occurrence_plan;
    const PPRelationalStateProgramV1Plan *state_plan;
    PPOccurrenceSourceResolverV1 resolver;
    PPOccurrenceFoldV1Backend downstream;
    uint32_t nested_run_depth;
    uint32_t forwarded_step_len;
    uint32_t resolution_len;
    bool active;
    bool committed;
    bool aborted;
    char source_digest[65];
} PPOccurrenceSourceCompositionV1;

bool ppoccurrence_source_composition_v1_init(
    PPOccurrenceSourceCompositionV1 *composition,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPOccurrenceSourceResolverV1 *resolver,
    const PPOccurrenceFoldV1Backend *downstream,
    char *error_buf,
    size_t error_buf_size);

PPOccurrenceFoldV1Backend ppoccurrence_source_composition_v1_backend(
    PPOccurrenceSourceCompositionV1 *composition);

#endif
