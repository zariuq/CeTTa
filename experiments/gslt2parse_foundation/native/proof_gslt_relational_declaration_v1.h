#ifndef CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_DECLARATION_V1_H
#define CETTA_GSLT2PARSE_PROOF_GSLT_RELATIONAL_DECLARATION_V1_H

#include "proof_gslt_relational_assertion_v1.h"
#include "proof_gslt_sequence_evidence_v1.h"
#include "relational_store_v1.h"

typedef enum {
    PPPROOF_GSLT_RELATIONAL_HYPOTHESIS_V1_BINDING = 0,
    PPPROOF_GSLT_RELATIONAL_HYPOTHESIS_V1_ESSENTIAL = 1
} PPProofGSLTRelationalHypothesisKindV1;

typedef struct {
    const PPProofGSLTSequenceTokenV1 *variable;
    const PPProofGSLTSequenceTokenV1 *typecode;
    uint32_t hypothesis_value;
} PPProofGSLTRelationalBindingSchemaV1;

typedef struct {
    PPProofGSLTTokenSequenceV1 template_sequence;
    uint32_t hypothesis_value;
} PPProofGSLTRelationalEssentialSchemaV1;

typedef struct {
    PPProofGSLTRelationalHypothesisKindV1 kind;
    uint32_t schema_index;
    uint32_t source_position;
} PPProofGSLTRelationalOrderedHypothesisV1;

typedef struct {
    uint32_t assertion_value;
    const PPProofGSLTRelationalBindingSchemaV1 *bindings;
    uint32_t binding_len;
    const PPProofGSLTRelationalEssentialSchemaV1 *essentials;
    uint32_t essential_len;
    const PPProofGSLTAssertionDisjointV1 *disjoints;
    uint32_t disjoint_len;
    const PPProofGSLTRelationalOrderedHypothesisV1 *ordered;
    uint32_t ordered_len;
    const PPProofGSLTPatternV1 *conclusion_type;
    PPProofGSLTTokenSequenceV1 conclusion_template;
    void *storage;
} PPProofGSLTRelationalDeclarationV1;

typedef struct {
    PPProofGSLTTokenSequenceV1 formula;
    PPProofGSLTReferenceV1 proof;
} PPProofGSLTRelationalActualHypothesisV1;

typedef struct {
    PPProofGSLTAssertionDeclarationV1 declaration;
    void *storage;
} PPProofGSLTRelationalPreparedAssertionV1;

typedef struct {
    void *implementation;
} PPProofGSLTRelationalContextV1;

typedef struct {
    PPProofGSLTRelationalContextV1 *context;
    uint32_t active_apartness_table;
} PPProofGSLTRelationalEvidenceV1;

void ppproof_gslt_relational_context_v1_init(
    PPProofGSLTRelationalContextV1 *context);

void ppproof_gslt_relational_context_v1_free(
    PPProofGSLTRelationalContextV1 *context);

PPProofGSLTArticleV1Result ppproof_gslt_relational_context_v1_begin(
    PPProofGSLTRelationalContextV1 *context,
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTArticleV1Limits *limits,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result ppproof_gslt_relational_context_v1_formula(
    PPProofGSLTRelationalContextV1 *context,
    uint32_t formula_value,
    PPProofGSLTTokenSequenceV1 *formula_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result
ppproof_gslt_relational_context_v1_typed_formula(
    PPProofGSLTRelationalContextV1 *context,
    const PPProofGSLTPatternV1 *typecode,
    PPProofGSLTTokenSequenceV1 body,
    PPProofGSLTTokenSequenceV1 *formula_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result
ppproof_gslt_relational_context_v1_add_provable_premise(
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTTokenSequenceV1 formula,
    PPProofGSLTReferenceV1 *reference_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result
ppproof_gslt_relational_context_v1_reserve_premise(
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTReferenceV1 *reference_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result
ppproof_gslt_relational_context_v1_fill_premise(
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTReferenceV1 reference,
    const PPProofGSLTPatternV1 *premise,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result ppproof_gslt_relational_context_v1_view(
    const PPProofGSLTRelationalContextV1 *context,
    const PPProofGSLTPatternV1 **premises_out,
    uint32_t *premise_len_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result
ppproof_gslt_relational_context_v1_evidence_sources(
    PPProofGSLTRelationalContextV1 *context,
    uint32_t active_apartness_table,
    PPProofGSLTRelationalEvidenceV1 *evidence,
    PPProofGSLTSequenceEvidenceSourcesV1 *sources_out,
    char *error_buf,
    size_t error_buf_size);

void ppproof_gslt_relational_declaration_v1_init(
    PPProofGSLTRelationalDeclarationV1 *declaration);

void ppproof_gslt_relational_declaration_v1_free(
    PPProofGSLTRelationalDeclarationV1 *declaration);

PPProofGSLTArticleV1Result ppproof_gslt_relational_declaration_v1_elaborate(
    PPProofGSLTRelationalContextV1 *context,
    uint32_t assertion_value,
    PPProofGSLTRelationalDeclarationV1 *declaration,
    char *error_buf,
    size_t error_buf_size);

void ppproof_gslt_relational_prepared_assertion_v1_init(
    PPProofGSLTRelationalPreparedAssertionV1 *prepared);

void ppproof_gslt_relational_prepared_assertion_v1_free(
    PPProofGSLTRelationalPreparedAssertionV1 *prepared);

PPProofGSLTArticleV1Result
ppproof_gslt_relational_prepared_assertion_v1_build(
    const PPProofGSLTRelationalDeclarationV1 *schema,
    const PPProofGSLTRelationalActualHypothesisV1 *actuals,
    uint32_t actual_len,
    PPProofGSLTRelationalPreparedAssertionV1 *prepared,
    char *error_buf,
    size_t error_buf_size);

#endif
