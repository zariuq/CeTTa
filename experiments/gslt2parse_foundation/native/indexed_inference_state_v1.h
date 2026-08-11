#ifndef CETTA_GSLT2PARSE_INDEXED_INFERENCE_STATE_V1_H
#define CETTA_GSLT2PARSE_INDEXED_INFERENCE_STATE_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "indexed_checker_effect_v1.h"
#include "parser_occurrence_source_resolver_v1.h"

typedef PPIndexedCheckerPolicyV1 PPIndexedInferenceV1Config;

/*
 * Source resolution is an explicit environment capability.  The semantic
 * fold supplies an apply-only nested backend, so recursively parsed sources
 * extend the same transaction without committing it early.
 */
typedef PPOccurrenceSourceResolverV1 PPIndexedInferenceV1SourceResolver;

typedef struct {
    void *implementation;
    char digest[65];
} PPIndexedInferenceV1State;

typedef struct {
    uint32_t atom_len;
    uint32_t object_len;
    uint32_t constant_len;
    uint32_t variable_len;
    uint32_t hypothesis_len;
    uint32_t rule_len;
    uint32_t active_hypothesis_len;
    uint32_t active_distinct_len;
    uint32_t scope_depth;
} PPIndexedInferenceV1Summary;

void ppindexed_inference_v1_state_init(
    PPIndexedInferenceV1State *state);

void ppindexed_inference_v1_state_free(
    PPIndexedInferenceV1State *state);

bool ppindexed_inference_v1_state_validate(
    const PPIndexedInferenceV1State *state,
    char *error_buf,
    size_t error_buf_size);

bool ppindexed_inference_v1_state_summary(
    const PPIndexedInferenceV1State *state,
    PPIndexedInferenceV1Summary *out);

typedef struct {
    uint32_t effect_len;
    uint32_t scope_open_len;
    uint32_t scope_close_len;
    uint32_t scope_complete_len;
    uint32_t declaration_len;
    uint32_t distinct_pair_len;
    uint32_t hypothesis_len;
    uint32_t rule_len;
    uint32_t proof_step_len;
    uint32_t proof_application_len;
    uint32_t unknown_step_len;
    bool committed;
    char state_digest[65];
    char occurrence_digest[65];
    char proof_digest[65];
    char source_digest[65];
} PPIndexedInferenceV1Receipt;

/*
 * Transactional occurrence-fold backend.  All updates are staged in a
 * source-owned inference state.  Parser rejection, unsupported effects, or
 * invalid declarations abort the complete update; only parser acceptance can
 * publish the new state and its proof-occurrence digest.
 */
typedef struct {
    void *implementation;
    PPIndexedInferenceV1Receipt receipt;
    PPOccurrenceFoldV1Trace occurrence_trace;
    bool active;
    bool committed;
    bool aborted;
} PPIndexedInferenceV1Fold;

bool ppindexed_inference_v1_fold_init(
    PPIndexedInferenceV1Fold *fold,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPIndexedCheckerEffectV1Plan *effect_plan,
    const PPIndexedInferenceV1SourceResolver *source_resolver,
    PPIndexedInferenceV1State *target,
    char *error_buf,
    size_t error_buf_size);

void ppindexed_inference_v1_fold_free(
    PPIndexedInferenceV1Fold *fold);

PPOccurrenceFoldV1Backend ppindexed_inference_v1_fold_backend(
    PPIndexedInferenceV1Fold *fold);

#endif
