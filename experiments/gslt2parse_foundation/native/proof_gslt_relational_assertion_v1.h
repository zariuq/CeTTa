#ifndef CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_ASSERTION_V1_H
#define CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_ASSERTION_V1_H

#include "proof_gslt_plan_v1.h"
#include "relational_state_program_v1.h"

typedef enum {
    PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND = 0,
    PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA = 1,
    PPPROOF_GSLT_RELATIONAL_TABLE_V1_FLOATING_VARIABLE = 2,
    PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_ACTIVE_HYPOTHESIS = 3,
    PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS = 4,
    PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE = 5,
    PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_DISJOINT = 6,
    PPPROOF_GSLT_RELATIONAL_TABLE_V1_ACTIVE_APARTNESS = 7,
    PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND = 8,
    PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN = 9
} PPProofGSLTRelationalTableRoleV1;

typedef enum {
    PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_LITERAL = 0,
    PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE = 1,
    PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING = 2,
    PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL = 3,
    PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN = 4
} PPProofGSLTRelationalSelectorRoleV1;

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
} PPProofGSLTRelationalSelectorV1;

typedef struct {
    PPProofGSLTRelationalTableRoleV1 role;
    uint32_t table_id;
} PPProofGSLTRelationalTableBindingV1;

typedef enum {
    PPPROOF_GSLT_RELATIONAL_PRESENCE_V1_INVALID = 0,
    PPPROOF_GSLT_RELATIONAL_PRESENCE_V1_REQUIRED = 1,
    PPPROOF_GSLT_RELATIONAL_PRESENCE_V1_OPTIONAL_EMPTY = 2
} PPProofGSLTRelationalPresenceV1;

typedef struct {
    const char *machine;
    PPRelationalStateLiteralV1 unknown_token;
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
    PPRelationalStackProofV1UnknownPolicy unknown_policy;
    CettaGsltIndexedSavePlacementV1 save_placement;
    CettaGsltHeaderHypothesisPolicyV1 header_hypothesis_policy;
} PPProofGSLTRelationalExecutionDescriptorV1;

typedef struct {
    PPProofGSLTNameV1 owner;
    PPProofGSLTNameV1 base;
    const PPProofGSLTRelationalTableBindingV1 *table_bindings;
    uint32_t table_binding_len;
    uint32_t resolved_table_ids[PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN];
    PPProofGSLTRelationalPresenceV1
        table_presence[PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN];
    PPProofGSLTRelationalExecutionDescriptorV1 execution;
    PPProofGSLTRelationalSelectorV1
        selectors[PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN];
    char proof_plan_digest[65];
    char state_plan_digest[65];
    char semantic_digest[65];
    void *storage;
} PPProofGSLTRelationalAssertionPlanV1;

void ppproof_gslt_relational_assertion_v1_init(
    PPProofGSLTRelationalAssertionPlanV1 *plan);

void ppproof_gslt_relational_assertion_v1_free(
    PPProofGSLTRelationalAssertionPlanV1 *plan);

PPProofGSLTArticleV1Result ppproof_gslt_relational_assertion_v1_load(
    PPProofGSLTRelationalAssertionPlanV1 *plan,
    const char *answer_path,
    const PPProofGSLTPlanV1 *proof_plan,
    const PPRelationalStateProgramV1Plan *state_plan,
    char *error_buf,
    size_t error_buf_size);

#endif
