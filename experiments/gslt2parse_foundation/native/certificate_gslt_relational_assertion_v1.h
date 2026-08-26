#ifndef CETTA_GSLT2PARSE_CERTIFICATE_GSLT_RELATIONAL_ASSERTION_V1_H
#define CETTA_GSLT2PARSE_CERTIFICATE_GSLT_RELATIONAL_ASSERTION_V1_H

#include "certificate_gslt_plan_v1.h"
#include "relational_state_program_v1.h"

typedef enum {
    PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND = 0,
    PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_FORMULA = 1,
    PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_FLOATING_VARIABLE = 2,
    PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_ASSERTION_ACTIVE_HYPOTHESIS = 3,
    PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS = 4,
    PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE = 5,
    PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_ASSERTION_DISJOINT = 6,
    PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_ACTIVE_APARTNESS = 7,
    PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND = 8,
    PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN = 9
} PPCertificateGSLTRelationalTableRoleV1;

typedef enum {
    PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_LITERAL = 0,
    PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE = 1,
    PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING = 2,
    PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL = 3,
    PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_LEN = 4
} PPCertificateGSLTRelationalSelectorRoleV1;

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
} PPCertificateGSLTRelationalSelectorV1;

typedef struct {
    PPCertificateGSLTRelationalTableRoleV1 role;
    uint32_t table_id;
} PPCertificateGSLTRelationalTableBindingV1;

typedef enum {
    PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_INVALID = 0,
    PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_REQUIRED = 1,
    PPCERTIFICATE_GSLT_RELATIONAL_PRESENCE_V1_OPTIONAL_EMPTY = 2
} PPCertificateGSLTRelationalPresenceV1;

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
} PPCertificateGSLTRelationalExecutionDescriptorV1;

typedef struct {
    PPCertificateGSLTNameV1 owner;
    PPCertificateGSLTNameV1 base;
    const PPCertificateGSLTRelationalTableBindingV1 *table_bindings;
    uint32_t table_binding_len;
    uint32_t resolved_table_ids[PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN];
    PPCertificateGSLTRelationalPresenceV1
        table_presence[PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN];
    PPCertificateGSLTRelationalExecutionDescriptorV1 execution;
    PPCertificateGSLTRelationalSelectorV1
        selectors[PPCERTIFICATE_GSLT_RELATIONAL_SELECTOR_V1_LEN];
    char proof_plan_digest[65];
    char state_plan_digest[65];
    char semantic_digest[65];
    void *storage;
} PPCertificateGSLTRelationalAssertionPlanV1;

void ppcertificate_gslt_relational_assertion_v1_init(
    PPCertificateGSLTRelationalAssertionPlanV1 *plan);

void ppcertificate_gslt_relational_assertion_v1_free(
    PPCertificateGSLTRelationalAssertionPlanV1 *plan);

PPCertificateGSLTArticleV1Result ppcertificate_gslt_relational_assertion_v1_load(
    PPCertificateGSLTRelationalAssertionPlanV1 *plan,
    const char *answer_path,
    const PPCertificateGSLTPlanV1 *proof_plan,
    const PPRelationalStateProgramV1Plan *state_plan,
    char *error_buf,
    size_t error_buf_size);

#endif
