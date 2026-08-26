#ifndef CETTA_GSLT2PARSE_CERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_H
#define CETTA_GSLT2PARSE_CERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_H

#include "gslt_repetition_admission_v1.h"
#include "certificate_gslt_relational_declaration_v1.h"
#include "proof_storage_plan_v1.h"
#include "relational_value_list_v1.h"

typedef enum {
    PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_OK = 0,
    PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE = 1,
    PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_REJECTED = 2,
    PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_RESOURCE = 3,
    PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_INVALID = 4,
    PPCERTIFICATE_GSLT_RELATIONAL_MACHINE_V1_UNSUPPORTED = 5
} PPCertificateGSLTRelationalMachineV1Result;

typedef struct {
    PPRelationalValueV1Slice label;
    const PPRelationalValueV1Slice *claim;
    uint32_t claim_len;
    const PPRelationalValueV1Slice *steps;
    uint32_t step_len;
} PPCertificateGSLTRelationalNormalInputV1;

typedef struct {
    PPRelationalValueV1Slice label;
    const PPRelationalValueV1Slice *claim;
    uint32_t claim_len;
    const PPRelationalValueV1Slice *header;
    uint32_t header_len;
    const PPRelationalValueV1Slice *code;
    uint32_t code_len;
} PPCertificateGSLTRelationalCompressedInputV1;

typedef enum {
    PPCERTIFICATE_GSLT_RELATIONAL_EXECUTION_V1_NONE = 0,
    PPCERTIFICATE_GSLT_RELATIONAL_EXECUTION_V1_LABEL_STREAM = 1,
    PPCERTIFICATE_GSLT_RELATIONAL_EXECUTION_V1_INDEXED_INSTRUCTION_STREAM = 2
} PPCertificateGSLTRelationalExecutionV1;

/* Opaque storage retained across proof calls.  The proof-call storage plan
 * decides whether a public call may supply this workspace; the proof machine
 * remains semantically complete when the pointer is NULL or already leased. */
typedef struct {
    void *impl;
} PPCertificateGSLTRelationalMachineV1Workspace;

typedef struct {
    uint32_t proof_step_len;
    uint32_t context_premise_len;
    uint32_t article_node_len;
    uint64_t decoded_byte_len;
    uint64_t decoded_instruction_len;
    uint32_t prepared_value_len;
    uint32_t prepared_classification_len;
    uint64_t prepared_classification_use_len;
    uint32_t saved_value_len;
    uint32_t workspace_stack_capacity;
    uint32_t workspace_prepared_capacity;
    uint32_t workspace_saved_capacity;
    uint32_t workspace_action_index_capacity;
    uint32_t workspace_actual_capacity;
    uint32_t workspace_required_binder_capacity;
    uint32_t workspace_ordered_row_capacity;
    uint32_t workspace_binder_count_capacity;
    uint32_t workspace_required_binder_index_capacity;
    uint32_t workspace_premise_label_index_capacity;
    uint32_t workspace_binding_schema_capacity;
    uint32_t workspace_premise_schema_capacity;
    uint32_t workspace_apartness_capacity;
    uint32_t workspace_ordered_premise_capacity;
    uint32_t workspace_prepared_binding_capacity;
    uint32_t workspace_prepared_premise_capacity;
    uint32_t workspace_declaration_cache_capacity;
    uint32_t workspace_call_control_count;
    uint64_t workspace_evidence_arena_capacity;
    uint32_t workspace_evidence_node_capacity;
    uint32_t workspace_evidence_cache_capacity;
    uint64_t declaration_cache_hit_len;
    uint64_t declaration_cache_miss_len;
    uint32_t declaration_cache_promotion_len;
    bool declaration_cache_snapshot_rejected;
    PPCertificateGSLTArticleV1Receipt article;
    PPCertificateGSLTRelationalExecutionV1 execution;
    bool workspace_used;
    bool workspace_reused;
    bool complete;
} PPCertificateGSLTRelationalMachineV1Receipt;

/* Map the public machine's measured repetition counters into the generic cost
 * qualifier.  A rejected source snapshot is outside the admitted cost model
 * and therefore fails closed. */
CettaGsltRepetitionCostQualificationV1
ppcertificate_gslt_relational_machine_v1_receipt_repetition_cost_qualify(
    const PPCertificateGSLTRelationalMachineV1Receipt *receipt,
    const CettaGsltRepetitionCostModelV1 *model,
    uint64_t *cached_cost_out,
    uint64_t *fresh_cost_out);

bool ppcertificate_gslt_relational_machine_v1_workspace_init(
    PPCertificateGSLTRelationalMachineV1Workspace *workspace);

bool ppcertificate_gslt_relational_machine_v1_workspace_set_repetition_policy(
    PPCertificateGSLTRelationalMachineV1Workspace *workspace,
    CettaGsltRepetitionPolicyV1 policy);

void ppcertificate_gslt_relational_machine_v1_workspace_free(
    PPCertificateGSLTRelationalMachineV1Workspace *workspace);

PPCertificateGSLTRelationalMachineV1Result
ppcertificate_gslt_relational_machine_v1_normal(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPCertificateGSLTPlanV1 *proof_plan,
    const PPCertificateGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPCertificateGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofPreparedActionCaseV1 *action_cases,
    uint32_t action_case_len,
    const PPCertificateGSLTRelationalNormalInputV1 *input,
    const PPCertificateGSLTArticleV1Limits *limits,
    PPCertificateGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTRelationalMachineV1Result
ppcertificate_gslt_relational_machine_v1_normal_with_workspace(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPCertificateGSLTPlanV1 *proof_plan,
    const PPCertificateGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPCertificateGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofPreparedActionCaseV1 *action_cases,
    uint32_t action_case_len,
    PPCertificateGSLTRelationalMachineV1Workspace *workspace,
    const PPCertificateGSLTRelationalNormalInputV1 *input,
    const PPCertificateGSLTArticleV1Limits *limits,
    PPCertificateGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTRelationalMachineV1Result
ppcertificate_gslt_relational_machine_v1_compressed(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPCertificateGSLTPlanV1 *proof_plan,
    const PPCertificateGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPCertificateGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofIndexedProgramPlanV1 *indexed_program_plan,
    const PPProofFrameIndexPlanV1 *frame_index_plan,
    const PPCertificateGSLTRelationalCompressedInputV1 *input,
    const PPCertificateGSLTArticleV1Limits *limits,
    PPCertificateGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTRelationalMachineV1Result
ppcertificate_gslt_relational_machine_v1_compressed_with_workspace(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPCertificateGSLTPlanV1 *proof_plan,
    const PPCertificateGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPCertificateGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofIndexedProgramPlanV1 *indexed_program_plan,
    const PPProofFrameIndexPlanV1 *frame_index_plan,
    PPCertificateGSLTRelationalMachineV1Workspace *workspace,
    const PPCertificateGSLTRelationalCompressedInputV1 *input,
    const PPCertificateGSLTArticleV1Limits *limits,
    PPCertificateGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size);

#endif
