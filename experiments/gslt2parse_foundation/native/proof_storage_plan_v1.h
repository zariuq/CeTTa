#ifndef CETTA_GSLT2PARSE_PROOF_STORAGE_PLAN_V1_H
#define CETTA_GSLT2PARSE_PROOF_STORAGE_PLAN_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gslt_indexed_instruction_decoder_v1.h"
#include "gslt_header_hypothesis_policy_v1.h"
#include "relational_state_program_v1.h"

typedef enum {
    PPPROOF_STORAGE_LIFETIME_V1_INVALID = 0,
    PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT,
    PPPROOF_STORAGE_LIFETIME_V1_SCOPED,
    PPPROOF_STORAGE_LIFETIME_V1_TRANSACTIONAL
} PPProofStorageLifetimeV1;

typedef struct {
    const char *table;
    uint32_t arity;
    uint32_t key_arity;
    PPProofStorageLifetimeV1 lifetime;
    const char *region;
} PPProofStorageTableV1;

typedef struct {
    const char *machine;
    const char *owner;
    const char *base;
    const char *provable;
} PPProofStorageMachineV1;

typedef struct {
    const char *machine;
    const char *role;
    const char *table;
    PPProofStorageLifetimeV1 lifetime;
} PPProofStorageReadV1;

typedef struct {
    const char *owner;
    const char *base;
    const char *provable;
    const char *cons;
    const char *nil;
    const char *layout;
    const char *region;
} PPProofStorageSequenceV1;

typedef struct {
    const char *operation;
    uint32_t action_index;
    const char *machine;
    const char *owner;
    const char *provable;
    const char *region;
    const char *layout;
    const char *observation;
} PPProofStorageCallV1;

typedef struct {
    const char *operation;
    uint32_t action_index;
    const char *machine;
    const char *carrier;
    const char *region;
    const char *observation;
} PPProofWorkspacePlanV1;

typedef struct {
    const char *operation;
    uint32_t action_index;
    const char *machine;
    const char *key_carrier;
    const char *value_carrier;
    const char *admission_policy;
    const char *snapshot_policy;
    const char *region;
} PPProofRepetitionCachePlanV1;

typedef struct {
    const char *owner;
    const char *cons;
    const char *nil;
    const char *support_apart;
    const char *token_against;
    const char *pair_allowed;
    const char *apart;
    const char *literal;
    const char *variable;
    const char *support_carrier;
    const char *apartness_carrier;
} PPProofFiniteSupportPlanV1;

typedef struct {
    const char *operation;
    uint32_t action_index;
    const char *machine;
    const char *header_role;
    const char *code_role;
    const char *carrier;
    const char *region;
} PPProofIndexedValuePlanV1;

typedef enum {
    PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_INVALID = 0,
    PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_REJECT,
    PPPROOF_INDEXED_EFFECT_UNKNOWN_V1_USE
} PPProofIndexedEffectUnknownV1;

typedef enum {
    PPPROOF_PREPARED_ACTION_V1_INVALID = 0,
    PPPROOF_PREPARED_ACTION_V1_PUSH_DECLARED,
    PPPROOF_PREPARED_ACTION_V1_APPLY_FRAME
} PPProofPreparedActionV1;

typedef struct {
    const char *machine;
    const char *source_kind;
    PPProofPreparedActionV1 action;
} PPProofPreparedActionCaseV1;

typedef struct {
    const char *operation;
    uint32_t action_index;
    const char *machine;
    const char *carrier;
    const char *prepared_effect;
    const char *saved_effect;
    const char *save_effect;
    PPProofIndexedEffectUnknownV1 unknown_effect;
    const char *region;
} PPProofIndexedEffectMachinePlanV1;

typedef struct {
    const char *operation;
    uint32_t action_index;
    const char *machine;
    const char *header_role;
    const char *code_role;
    uint8_t terminal_low;
    uint8_t terminal_high;
    uint8_t continuation_low;
    uint8_t continuation_high;
    uint8_t save_byte;
    uint8_t unknown_byte;
    uint32_t terminal_radix;
    uint32_t terminal_digit_bias;
    uint32_t continuation_radix;
    uint32_t continuation_digit_bias;
    PPProofIndexedEffectUnknownV1 unknown_effect;
    CettaGsltIndexedSavePlacementV1 save_placement;
    CettaGsltHeaderHypothesisPolicyV1 header_hypothesis_policy;
    const char *indexed_carrier;
    const char *effect_carrier;
    const char *prepared_effect;
    const char *saved_effect;
    const char *save_effect;
    const char *region;
    const PPProofPreparedActionCaseV1 *action_cases;
    uint32_t action_case_len;
} PPProofIndexedProgramPlanV1;

typedef struct {
    const char *operation;
    uint32_t action_index;
    const char *machine;
    const char *carrier;
    const char *validation;
    const char *region;
} PPProofFrameIndexPlanV1;

typedef struct {
    const char *machine;
    const char *owner;
    const char *cons;
    const char *nil;
    const char *carrier;
    const char *source_region;
    const char *execution_region;
} PPProofLiteralHolePlanV1;

typedef struct {
    const char *operation;
    uint32_t action_index;
    const char *machine;
    const char *carrier;
    const char *template_carrier;
    const char *literal_head_policy;
    const char *slot_carrier;
    const char *binder_validation;
    const char *stack_discipline;
    const char *region;
} PPProofTwoPhaseFramePlanV1;

typedef struct {
    PPProofStorageTableV1 *tables;
    uint32_t table_len;
    PPProofStorageMachineV1 *machines;
    uint32_t machine_len;
    PPProofStorageReadV1 *reads;
    uint32_t read_len;
    PPProofStorageSequenceV1 *sequences;
    uint32_t sequence_len;
    PPProofStorageCallV1 *calls;
    uint32_t call_len;
    PPProofWorkspacePlanV1 *workspaces;
    uint32_t workspace_len;
    PPProofRepetitionCachePlanV1 *repetition_caches;
    uint32_t repetition_cache_len;
    PPProofFiniteSupportPlanV1 *finite_supports;
    uint32_t finite_support_len;
    PPProofIndexedValuePlanV1 *indexed_values;
    uint32_t indexed_value_len;
    PPProofIndexedEffectMachinePlanV1 *indexed_effect_machines;
    uint32_t indexed_effect_machine_len;
    PPProofPreparedActionCaseV1 *prepared_action_cases;
    uint32_t prepared_action_case_len;
    PPProofIndexedProgramPlanV1 *indexed_programs;
    uint32_t indexed_program_len;
    PPProofFrameIndexPlanV1 *frame_indexes;
    uint32_t frame_index_len;
    PPProofLiteralHolePlanV1 *literal_holes;
    uint32_t literal_hole_len;
    PPProofTwoPhaseFramePlanV1 *two_phase_frames;
    uint32_t two_phase_frame_len;
    char semantic_digest[65];
    void *storage;
} PPProofStoragePlanV1;

void ppproof_storage_plan_v1_init(PPProofStoragePlanV1 *plan);
void ppproof_storage_plan_v1_free(PPProofStoragePlanV1 *plan);

bool ppproof_storage_plan_v1_load(
    PPProofStoragePlanV1 *plan,
    const char *answer_path,
    char *error_buf,
    size_t error_buf_size);

const PPProofStorageTableV1 *ppproof_storage_plan_v1_table(
    const PPProofStoragePlanV1 *plan, const char *table);
const PPProofStorageMachineV1 *ppproof_storage_plan_v1_machine(
    const PPProofStoragePlanV1 *plan, const char *machine);
const PPProofStorageReadV1 *ppproof_storage_plan_v1_read(
    const PPProofStoragePlanV1 *plan,
    const char *machine,
    const char *role);
const PPProofStorageSequenceV1 *ppproof_storage_plan_v1_sequence(
    const PPProofStoragePlanV1 *plan,
    const char *owner,
    const char *provable);
const PPProofStorageCallV1 *ppproof_storage_plan_v1_call(
    const PPProofStoragePlanV1 *plan,
    const char *operation,
    uint32_t action_index);
bool ppproof_storage_call_v1_admits_reusable_workspace(
    const PPProofStorageCallV1 *call,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *region);
const PPProofWorkspacePlanV1 *ppproof_storage_plan_v1_workspace(
    const PPProofStoragePlanV1 *plan,
    const char *operation,
    uint32_t action_index);
const PPProofRepetitionCachePlanV1 *
ppproof_storage_plan_v1_repetition_cache(
    const PPProofStoragePlanV1 *plan,
    const char *operation,
    uint32_t action_index);
bool ppproof_repetition_cache_plan_v1_admits(
    const PPProofRepetitionCachePlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *region);
bool ppproof_workspace_plan_v1_admits(
    const PPProofWorkspacePlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *carrier,
    const char *region);
const PPProofFiniteSupportPlanV1 *ppproof_storage_plan_v1_finite_support(
    const PPProofStoragePlanV1 *plan, const char *owner);
const PPProofIndexedValuePlanV1 *ppproof_storage_plan_v1_indexed_value(
    const PPProofStoragePlanV1 *plan, const char *machine);
bool ppproof_indexed_value_plan_v1_admits(
    const PPProofIndexedValuePlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *header_role,
    const char *code_role);
const PPProofIndexedEffectMachinePlanV1 *
ppproof_storage_plan_v1_indexed_effect_machine(
    const PPProofStoragePlanV1 *plan, const char *machine);
bool ppproof_indexed_effect_machine_plan_v1_admits(
    const PPProofIndexedEffectMachinePlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *region,
    PPProofIndexedEffectUnknownV1 unknown_effect);
const PPProofIndexedProgramPlanV1 *ppproof_storage_plan_v1_indexed_program(
    const PPProofStoragePlanV1 *plan, const char *machine);
bool ppproof_storage_plan_v1_prepared_action_cases(
    const PPProofStoragePlanV1 *plan,
    const char *machine,
    const PPProofPreparedActionCaseV1 **cases_out,
    uint32_t *case_len_out);
/* Derive exact label-to-action admission from the existing state-table,
 * machine-read, and prepared-action facts.  No parallel plan record is
 * accepted: both loaded artifacts must describe the same machine, immutable
 * one-column label key, and complete four-way stack action relation. */
bool ppproof_storage_plan_v1_exact_action_selector(
    const PPProofStoragePlanV1 *plan,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    const PPProofPreparedActionCaseV1 **cases_out,
    uint32_t *case_len_out,
    char *error_buf,
    size_t error_buf_size);
bool ppproof_indexed_program_plan_v1_admits(
    const PPProofIndexedProgramPlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *header_role,
    const char *code_role,
    const char *region,
    PPProofIndexedEffectUnknownV1 unknown_effect,
    CettaGsltIndexedSavePlacementV1 save_placement,
    CettaGsltHeaderHypothesisPolicyV1 header_hypothesis_policy);
const PPProofFrameIndexPlanV1 *ppproof_storage_plan_v1_frame_index(
    const PPProofStoragePlanV1 *plan, const char *machine);
bool ppproof_frame_index_plan_v1_admits(
    const PPProofFrameIndexPlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *region);
const PPProofLiteralHolePlanV1 *ppproof_storage_plan_v1_literal_hole(
    const PPProofStoragePlanV1 *plan, const char *machine);
const PPProofTwoPhaseFramePlanV1 *ppproof_storage_plan_v1_two_phase_frame(
    const PPProofStoragePlanV1 *plan,
    const char *operation,
    uint32_t action_index);
bool ppproof_two_phase_frame_plan_v1_admits(
    const PPProofTwoPhaseFramePlanV1 *plan,
    const char *operation,
    uint32_t action_index,
    const char *machine,
    const char *template_carrier,
    const char *region);

#endif
