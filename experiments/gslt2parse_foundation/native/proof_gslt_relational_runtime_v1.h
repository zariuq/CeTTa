#ifndef CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_RUNTIME_V1_H
#define CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_RUNTIME_V1_H

#include "oslf_native_type_vm_v1.h"
#include "relational_state_program_v1.h"
#include "gslt_language_runtime.h"

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
    uint32_t compiled_audit_attempts;
    uint32_t compiled_audit_agreements;
    uint64_t compiled_rule_attempts;
    uint64_t compiled_rule_matches;
    uint64_t compiled_dispatch_rejects;
    uint64_t compiled_outer_head_elisions;
    uint64_t compiled_prefilter_rejects;
    uint64_t compiled_ground_dense_attempts;
    uint64_t compiled_flat_head_attempts;
    uint64_t compiled_general_head_attempts;
    uint64_t compiled_constructor_guided_attempts;
    uint64_t compiled_constructor_guided_matches;
    uint64_t compiled_constructor_nodes_elided;
    uint64_t compiled_flat_head_matches;
    uint64_t compiled_ground_dense_matches;
    uint64_t compiled_variable_slot_buffer_uses;
    uint64_t compiled_variable_slot_bytes_elided;
    uint64_t compiled_variable_slot_clear_bytes_elided;
    uint64_t compiled_ground_subterm_cache_hits;
    uint64_t compiled_ground_subterm_nodes_elided;
    uint64_t compiled_worklist_states_created;
    uint64_t compiled_worklist_states_reclaimed;
    uint64_t compiled_worklist_pending_peak;
    uint64_t compiled_worklist_state_bytes_peak;
    uint32_t compiled_maximum_goal_depth;
    uint64_t constructor_chain_requests;
    uint64_t constructor_chain_nodes;
    char provider_digest[65];
    char program_digest[65];
    char capability_digest[65];
} PPProofGSLTRelationalRuntimeV1Receipt;

/* Aggregate of every generated outcome query executed by one prepared
 * runtime.  It is diagnostic only and never participates in a verdict. */
typedef struct {
    uint64_t query_executions;
    PPOSLFNativeVMStatsV1 stats;
} PPProofGSLTRelationalRuntimeV1Profile;

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

/* Attach an independent generated finite-Horn realization for differential
 * checking.  It consumes the same generated capability rows as the primary
 * VM and may reject a disagreement, but it is not promoted as proof authority
 * by this operation. */
bool ppproof_gslt_relational_runtime_v1_attach_compiled_audit(
    PPProofGSLTRelationalRuntimeV1 *runtime,
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    const CettaGsltProviderCatalogV1 *catalog,
    CettaGsltHornLimits limits,
    char *error_buf,
    size_t error_buf_size);

PPRelationalStateProofV1Backend ppproof_gslt_relational_runtime_v1_backend(
    PPProofGSLTRelationalRuntimeV1 *runtime);

bool ppproof_gslt_relational_runtime_v1_last_receipt(
    PPProofGSLTRelationalRuntimeV1 *runtime,
    PPProofGSLTRelationalRuntimeV1Receipt *receipt_out);

bool ppproof_gslt_relational_runtime_v1_profile(
    const PPProofGSLTRelationalRuntimeV1 *runtime,
    PPProofGSLTRelationalRuntimeV1Profile *profile_out);

#endif
