#ifndef CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_MACHINE_V1_H
#define CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_MACHINE_V1_H

#include "proof_gslt_relational_declaration_v1.h"
#include "proof_storage_plan_v1.h"
#include "relational_value_list_v1.h"

typedef enum {
    PPPROOF_GSLT_RELATIONAL_MACHINE_V1_OK = 0,
    PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INCOMPLETE = 1,
    PPPROOF_GSLT_RELATIONAL_MACHINE_V1_REJECTED = 2,
    PPPROOF_GSLT_RELATIONAL_MACHINE_V1_RESOURCE = 3,
    PPPROOF_GSLT_RELATIONAL_MACHINE_V1_INVALID = 4,
    PPPROOF_GSLT_RELATIONAL_MACHINE_V1_UNSUPPORTED = 5
} PPProofGSLTRelationalMachineV1Result;

typedef struct {
    PPRelationalValueV1Slice label;
    const PPRelationalValueV1Slice *claim;
    uint32_t claim_len;
    const PPRelationalValueV1Slice *steps;
    uint32_t step_len;
} PPProofGSLTRelationalNormalInputV1;

typedef struct {
    PPRelationalValueV1Slice label;
    const PPRelationalValueV1Slice *claim;
    uint32_t claim_len;
    const PPRelationalValueV1Slice *header;
    uint32_t header_len;
    const PPRelationalValueV1Slice *code;
    uint32_t code_len;
} PPProofGSLTRelationalCompressedInputV1;

typedef enum {
    PPPROOF_GSLT_RELATIONAL_EXECUTION_V1_NONE = 0,
    PPPROOF_GSLT_RELATIONAL_EXECUTION_V1_LABEL_STREAM = 1,
    PPPROOF_GSLT_RELATIONAL_EXECUTION_V1_INDEXED_INSTRUCTION_STREAM = 2
} PPProofGSLTRelationalExecutionV1;

typedef struct {
    uint32_t proof_step_len;
    uint32_t context_premise_len;
    uint32_t article_node_len;
    uint64_t decoded_byte_len;
    uint64_t decoded_instruction_len;
    uint32_t prepared_value_len;
    uint32_t saved_value_len;
    PPProofGSLTArticleV1Receipt article;
    PPProofGSLTRelationalExecutionV1 execution;
    bool complete;
} PPProofGSLTRelationalMachineV1Receipt;

PPProofGSLTRelationalMachineV1Result
ppproof_gslt_relational_machine_v1_normal(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofGSLTRelationalNormalInputV1 *input,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTRelationalMachineV1Result
ppproof_gslt_relational_machine_v1_compressed(
    const PPRelationalStoreV1 *store,
    const PPRelationalStateProgramV1Plan *state_plan,
    uint32_t proof_machine_id,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofIndexedValuePlanV1 *indexed_value_plan,
    const PPProofFrameIndexPlanV1 *frame_index_plan,
    const PPProofGSLTRelationalCompressedInputV1 *input,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTRelationalMachineV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size);

#endif
