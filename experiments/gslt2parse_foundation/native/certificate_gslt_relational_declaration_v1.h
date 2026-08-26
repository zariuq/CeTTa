#ifndef CETTA_GSLT2PARSE_CERTIFICATE_GSLT_RELATIONAL_DECLARATION_V1_H
#define CETTA_GSLT2PARSE_CERTIFICATE_GSLT_RELATIONAL_DECLARATION_V1_H

#include "gslt_reusable_buffer_v1.h"
#include "certificate_gslt_relational_assertion_v1.h"
#include "certificate_gslt_sequence_evidence_v1.h"
#include "relational_store_v1.h"

typedef enum {
    PPCERTIFICATE_GSLT_RELATIONAL_HYPOTHESIS_V1_BINDING = 0,
    PPCERTIFICATE_GSLT_RELATIONAL_HYPOTHESIS_V1_ESSENTIAL = 1
} PPCertificateGSLTRelationalHypothesisKindV1;

typedef struct {
    const PPCertificateGSLTSequenceTokenV1 *variable;
    const PPCertificateGSLTSequenceTokenV1 *typecode;
    uint32_t hypothesis_value;
} PPCertificateGSLTRelationalBindingSchemaV1;

typedef struct {
    PPCertificateGSLTTokenSequenceV1 template_sequence;
    uint32_t hypothesis_value;
} PPCertificateGSLTRelationalEssentialSchemaV1;

typedef struct {
    PPCertificateGSLTRelationalHypothesisKindV1 kind;
    uint32_t schema_index;
    uint32_t source_position;
} PPCertificateGSLTRelationalOrderedHypothesisV1;

typedef struct {
    uint32_t assertion_value;
    const PPCertificateGSLTRelationalBindingSchemaV1 *bindings;
    uint32_t binding_len;
    const PPCertificateGSLTRelationalEssentialSchemaV1 *essentials;
    uint32_t essential_len;
    const PPCertificateGSLTAssertionDisjointV1 *disjoints;
    uint32_t disjoint_len;
    const PPCertificateGSLTRelationalOrderedHypothesisV1 *ordered;
    uint32_t ordered_len;
    const PPCertificateGSLTPatternV1 *conclusion_type;
    PPCertificateGSLTTokenSequenceV1 conclusion_template;
    void *storage;
} PPCertificateGSLTRelationalDeclarationV1;

typedef struct {
    PPCertificateGSLTTokenSequenceV1 formula;
    PPCertificateGSLTReferenceV1 proof;
} PPCertificateGSLTRelationalActualHypothesisV1;

typedef struct {
    uint32_t position;
    uint32_t label;
} PPCertificateGSLTRelationalOrderedRowV1;

/* Call-local control headers for declaration and assertion preparation.
 * Their arrays may be owned by reusable buffers; owns_storage distinguishes a
 * fallback heap header from a header supplied by an admitted call region. */
typedef struct {
    PPCertificateGSLTRelationalBindingSchemaV1 *bindings;
    uint32_t binding_cap;
    PPCertificateGSLTRelationalEssentialSchemaV1 *essentials;
    uint32_t essential_cap;
    PPCertificateGSLTAssertionDisjointV1 *disjoints;
    uint32_t disjoint_cap;
    PPCertificateGSLTRelationalOrderedHypothesisV1 *ordered;
    uint32_t ordered_cap;
    bool owns_arrays;
    bool owns_storage;
} PPCertificateGSLTRelationalDeclarationStorageV1;

typedef struct {
    PPCertificateGSLTAssertionBindingV1 *bindings;
    PPCertificateGSLTAssertionEssentialV1 *essentials;
    bool owns_arrays;
    bool owns_storage;
} PPCertificateGSLTRelationalPreparedStorageV1;

/* Scratch carriers supplied by an admitted proof-call workspace.  Both
 * buffers must hold active leases for the duration of elaboration. */
typedef struct {
    CettaGsltReusableBufferV1 *required_binder_ids;
    CettaGsltReusableBufferV1 *ordered_rows;
    CettaGsltReusableBufferV1 *binding_schemas;
    CettaGsltReusableBufferV1 *premise_schemas;
    CettaGsltReusableBufferV1 *apartness_pairs;
    CettaGsltReusableBufferV1 *ordered_premises;
    PPCertificateGSLTRelationalDeclarationStorageV1 *control;
} PPCertificateGSLTRelationalDeclarationWorkspaceV1;

typedef struct {
    CettaGsltReusableBufferV1 *bindings;
    CettaGsltReusableBufferV1 *premises;
    PPCertificateGSLTRelationalPreparedStorageV1 *control;
} PPCertificateGSLTRelationalPreparedWorkspaceV1;

typedef struct {
    PPCertificateGSLTAssertionDeclarationV1 declaration;
    void *storage;
} PPCertificateGSLTRelationalPreparedAssertionV1;

typedef struct {
    void *implementation;
} PPCertificateGSLTRelationalContextV1;

typedef struct {
    PPCertificateGSLTRelationalContextV1 *context;
    uint32_t active_apartness_table;
} PPCertificateGSLTRelationalEvidenceV1;

void ppcertificate_gslt_relational_context_v1_init(
    PPCertificateGSLTRelationalContextV1 *context);

void ppcertificate_gslt_relational_context_v1_free(
    PPCertificateGSLTRelationalContextV1 *context);

PPCertificateGSLTArticleV1Result ppcertificate_gslt_relational_context_v1_begin(
    PPCertificateGSLTRelationalContextV1 *context,
    const PPRelationalStoreV1 *store,
    const PPCertificateGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPCertificateGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPCertificateGSLTArticleV1Limits *limits,
    char *error_buf,
    size_t error_buf_size);

typedef struct {
    uint64_t store_identity;
    uint32_t present_mask;
    uint32_t row_lens[PPCERTIFICATE_GSLT_RELATIONAL_TABLE_V1_LEN];
} PPCertificateGSLTRelationalDeclarationSnapshotV1;

/* Capture and later revalidate the exact append-only prefixes read while
 * elaborating declarations.  Prefix immutability plus unchanged lengths
 * implies unchanged rows.  Optional-empty relations need no snapshot. */
bool ppcertificate_gslt_relational_context_v1_declaration_snapshot(
    const PPCertificateGSLTRelationalContextV1 *context,
    PPCertificateGSLTRelationalDeclarationSnapshotV1 *snapshot_out);
bool ppcertificate_gslt_relational_context_v1_declaration_snapshot_matches(
    const PPCertificateGSLTRelationalContextV1 *context,
    const PPCertificateGSLTRelationalDeclarationSnapshotV1 *snapshot);

PPCertificateGSLTArticleV1Result ppcertificate_gslt_relational_context_v1_formula(
    PPCertificateGSLTRelationalContextV1 *context,
    uint32_t formula_value,
    PPCertificateGSLTTokenSequenceV1 *formula_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_relational_context_v1_typed_formula(
    PPCertificateGSLTRelationalContextV1 *context,
    const PPCertificateGSLTPatternV1 *typecode,
    PPCertificateGSLTTokenSequenceV1 body,
    PPCertificateGSLTTokenSequenceV1 *formula_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_relational_context_v1_add_provable_premise(
    PPCertificateGSLTRelationalContextV1 *context,
    PPCertificateGSLTTokenSequenceV1 formula,
    PPCertificateGSLTReferenceV1 *reference_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_relational_context_v1_reserve_premise(
    PPCertificateGSLTRelationalContextV1 *context,
    PPCertificateGSLTReferenceV1 *reference_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_relational_context_v1_fill_premise(
    PPCertificateGSLTRelationalContextV1 *context,
    PPCertificateGSLTReferenceV1 reference,
    const PPCertificateGSLTPatternV1 *premise,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result ppcertificate_gslt_relational_context_v1_view(
    const PPCertificateGSLTRelationalContextV1 *context,
    const PPCertificateGSLTPatternV1 **premises_out,
    uint32_t *premise_len_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_relational_context_v1_evidence_sources(
    PPCertificateGSLTRelationalContextV1 *context,
    PPCertificateGSLTRelationalEvidenceV1 *evidence,
    PPCertificateGSLTSequenceEvidenceSourcesV1 *sources_out,
    char *error_buf,
    size_t error_buf_size);

void ppcertificate_gslt_relational_declaration_v1_init(
    PPCertificateGSLTRelationalDeclarationV1 *declaration);

void ppcertificate_gslt_relational_declaration_v1_free(
    PPCertificateGSLTRelationalDeclarationV1 *declaration);

PPCertificateGSLTArticleV1Result ppcertificate_gslt_relational_declaration_v1_elaborate(
    PPCertificateGSLTRelationalContextV1 *context,
    uint32_t assertion_value,
    PPCertificateGSLTRelationalDeclarationV1 *declaration,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_relational_declaration_v1_elaborate_with_workspace(
    PPCertificateGSLTRelationalContextV1 *context,
    uint32_t assertion_value,
    const PPCertificateGSLTRelationalDeclarationWorkspaceV1 *workspace,
    PPCertificateGSLTRelationalDeclarationV1 *declaration,
    char *error_buf,
    size_t error_buf_size);

void ppcertificate_gslt_relational_prepared_assertion_v1_init(
    PPCertificateGSLTRelationalPreparedAssertionV1 *prepared);

void ppcertificate_gslt_relational_prepared_assertion_v1_free(
    PPCertificateGSLTRelationalPreparedAssertionV1 *prepared);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_relational_prepared_assertion_v1_build(
    const PPCertificateGSLTRelationalDeclarationV1 *schema,
    const PPCertificateGSLTRelationalActualHypothesisV1 *actuals,
    uint32_t actual_len,
    PPCertificateGSLTRelationalPreparedAssertionV1 *prepared,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_relational_prepared_assertion_v1_build_with_workspace(
    const PPCertificateGSLTRelationalDeclarationV1 *schema,
    const PPCertificateGSLTRelationalActualHypothesisV1 *actuals,
    uint32_t actual_len,
    const PPCertificateGSLTRelationalPreparedWorkspaceV1 *workspace,
    PPCertificateGSLTRelationalPreparedAssertionV1 *prepared,
    char *error_buf,
    size_t error_buf_size);

#endif
