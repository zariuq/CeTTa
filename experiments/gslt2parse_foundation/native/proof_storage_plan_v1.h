#ifndef CETTA_GSLT2PARSE_PROOF_STORAGE_PLAN_V1_H
#define CETTA_GSLT2PARSE_PROOF_STORAGE_PLAN_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    PPProofFiniteSupportPlanV1 *finite_supports;
    uint32_t finite_support_len;
    PPProofIndexedValuePlanV1 *indexed_values;
    uint32_t indexed_value_len;
    PPProofLiteralHolePlanV1 *literal_holes;
    uint32_t literal_hole_len;
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
const PPProofFiniteSupportPlanV1 *ppproof_storage_plan_v1_finite_support(
    const PPProofStoragePlanV1 *plan, const char *owner);
const PPProofIndexedValuePlanV1 *ppproof_storage_plan_v1_indexed_value(
    const PPProofStoragePlanV1 *plan, const char *machine);
const PPProofLiteralHolePlanV1 *ppproof_storage_plan_v1_literal_hole(
    const PPProofStoragePlanV1 *plan, const char *machine);

#endif
