#ifndef CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_RUNTIME_V1_H
#define CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_RUNTIME_V1_H

#include "oslf_native_type_vm_v1.h"
#include "relational_state_program_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    PPOSLFNativeVMOutcomeV1 outcome;
    PPRelationalStateProofV1Result proof_result;
    PPOSLFNativeVMStatsV1 stats;
    uint32_t outcome_query_priority;
    uint32_t outcome_query_attempts;
    uint32_t capability_row_len;
    uint32_t generated_event_len;
    uint32_t external_event_len;
    uint64_t constructor_chain_requests;
    uint64_t constructor_chain_nodes;
    char provider_digest[65];
    char program_digest[65];
    char capability_digest[65];
} PPProofGSLTRelationalRuntimeV1Receipt;

typedef struct {
    void *implementation;
} PPProofGSLTRelationalRuntimeV1;

void ppproof_gslt_relational_runtime_v1_init(
    PPProofGSLTRelationalRuntimeV1 *runtime);

void ppproof_gslt_relational_runtime_v1_free(
    PPProofGSLTRelationalRuntimeV1 *runtime);

/*
 * Preparation binds a generated provider program to one generated state plan
 * and one admitted native type program.  The plans and VM remain borrowed and
 * must outlive the runtime.  No relation, table, constructor, or guest policy
 * is selected outside the provider artifact.
 */
bool ppproof_gslt_relational_runtime_v1_prepare(
    PPProofGSLTRelationalRuntimeV1 *runtime,
    const char *provider_answer_path,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPOSLFNativeTypePlanV1 *native_plan,
    const PPOSLFNativeTypeVMV1 *vm,
    PPOSLFNativeVMLimitsV1 limits,
    char *error_buf,
    size_t error_buf_size);

PPRelationalStateProofV1Backend ppproof_gslt_relational_runtime_v1_backend(
    PPProofGSLTRelationalRuntimeV1 *runtime);

bool ppproof_gslt_relational_runtime_v1_last_receipt(
    PPProofGSLTRelationalRuntimeV1 *runtime,
    PPProofGSLTRelationalRuntimeV1Receipt *receipt_out);

#endif
