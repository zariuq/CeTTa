#ifndef CETTA_GSLT2PARSE_PARSER_OCCURRENCE_FILE_RESOLVER_V1_H
#define CETTA_GSLT2PARSE_PARSER_OCCURRENCE_FILE_RESOLVER_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_occurrence_source_resolver_v1.h"

/*
 * File-system realization of the generic source-resolution capability.
 * Canonical paths are used only for duplicate/cycle identity.  The published
 * source-DAG digest is host-independent: it commits to ordered authored paths,
 * expansion/skip decisions, dense source identities, and exact source bytes.
 */
typedef struct {
    void *implementation;
    uint32_t source_len;
    uint32_t skipped_source_len;
    uint32_t max_depth;
} PPOccurrenceFileResolverV1;

void ppoccurrence_file_resolver_v1_init(
    PPOccurrenceFileResolverV1 *resolver);

void ppoccurrence_file_resolver_v1_free(
    PPOccurrenceFileResolverV1 *resolver);

/*
 * Register the root source and prepare recursive parsing with the same
 * generated program and occurrence plan.  depth_limit is a runtime resource
 * bound, not a language verdict or grammar policy.
 */
bool ppoccurrence_file_resolver_v1_configure(
    PPOccurrenceFileResolverV1 *resolver,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const char *root_path,
    const uint8_t *root_bytes,
    size_t root_byte_len,
    PPGuardedLexCursorV1Observation observation,
    uint64_t minimum_work_limit,
    uint64_t work_per_byte,
    uint32_t depth_limit,
    char *error_buf,
    size_t error_buf_size);

PPOccurrenceSourceResolverV1
ppoccurrence_file_resolver_v1_interface(
    PPOccurrenceFileResolverV1 *resolver);

#endif
