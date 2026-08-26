#ifndef CETTA_GSLT2PARSE_CERTIFICATE_GSLT_SEQUENCE_EVIDENCE_V1_H
#define CETTA_GSLT2PARSE_CERTIFICATE_GSLT_SEQUENCE_EVIDENCE_V1_H

#include "certificate_gslt_plan_v1.h"

/*
 * Generated role names for an untrusted sequence-evidence producer.
 *
 * The ABI is compiled from the authored sequence-relation GSLT.  Native code
 * may use these roles to construct chronological articles, but acceptance is
 * still decided only by certificate_gslt_article_v1 against the compiled
 * presentation.
 */

typedef enum {
    PPCERTIFICATE_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_NIL = 0,
    PPCERTIFICATE_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_CONS = 1,
    PPCERTIFICATE_GSLT_SEQUENCE_CONSTRUCTOR_V1_ENVIRONMENT_NIL = 2,
    PPCERTIFICATE_GSLT_SEQUENCE_CONSTRUCTOR_V1_ENVIRONMENT_CONS = 3,
    PPCERTIFICATE_GSLT_SEQUENCE_CONSTRUCTOR_V1_LEN = 4
} PPCertificateGSLTSequenceConstructorRoleV1;

typedef enum {
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_APPEND = 0,
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_LOOKUP = 1,
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_INSTANTIATE = 2,
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_LITERAL = 3,
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_VARIABLE = 4,
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_DIFFERENT = 5,
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_APART = 6,
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_PAIR_ALLOWED = 7,
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_TOKEN_AGAINST_SEQUENCE = 8,
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_SUPPORT_APART = 9,
    PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_LEN = 10
} PPCertificateGSLTSequenceJudgmentRoleV1;

typedef enum {
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_APPEND_NIL = 0,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_APPEND_CONS = 1,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_LOOKUP_HEAD = 2,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_LOOKUP_TAIL = 3,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_NIL = 4,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_LITERAL = 5,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_VARIABLE = 6,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_PAIR_LEFT_LITERAL = 7,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_PAIR_RIGHT_LITERAL = 8,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_PAIR_APART = 9,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_TOKEN_AGAINST_NIL = 10,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_TOKEN_AGAINST_CONS = 11,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_SUPPORT_APART_NIL = 12,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_SUPPORT_APART_CONS = 13,
    PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_LEN = 14
} PPCertificateGSLTSequenceRuleRoleV1;

typedef enum {
    PPCERTIFICATE_GSLT_ASSERTION_CONSTRUCTOR_V1_ASSERTION = 0,
    PPCERTIFICATE_GSLT_ASSERTION_CONSTRUCTOR_V1_VARIABLE = 1,
    PPCERTIFICATE_GSLT_ASSERTION_CONSTRUCTOR_V1_ESSENTIAL = 2,
    PPCERTIFICATE_GSLT_ASSERTION_CONSTRUCTOR_V1_DISJOINT = 3,
    PPCERTIFICATE_GSLT_ASSERTION_CONSTRUCTOR_V1_LIST_NIL = 4,
    PPCERTIFICATE_GSLT_ASSERTION_CONSTRUCTOR_V1_LIST_CONS = 5,
    PPCERTIFICATE_GSLT_ASSERTION_CONSTRUCTOR_V1_LEN = 6
} PPCertificateGSLTAssertionConstructorRoleV1;

typedef enum {
    PPCERTIFICATE_GSLT_ASSERTION_JUDGMENT_V1_DECLARED = 0,
    PPCERTIFICATE_GSLT_ASSERTION_JUDGMENT_V1_BUILD_ENVIRONMENT = 1,
    PPCERTIFICATE_GSLT_ASSERTION_JUDGMENT_V1_CHECK_ESSENTIALS = 2,
    PPCERTIFICATE_GSLT_ASSERTION_JUDGMENT_V1_CHECK_DISJOINTS = 3,
    PPCERTIFICATE_GSLT_ASSERTION_JUDGMENT_V1_PROVABLE = 4,
    PPCERTIFICATE_GSLT_ASSERTION_JUDGMENT_V1_LEN = 5
} PPCertificateGSLTAssertionJudgmentRoleV1;

typedef enum {
    PPCERTIFICATE_GSLT_ASSERTION_RULE_V1_BUILD_ENVIRONMENT_NIL = 0,
    PPCERTIFICATE_GSLT_ASSERTION_RULE_V1_BUILD_ENVIRONMENT_CONS = 1,
    PPCERTIFICATE_GSLT_ASSERTION_RULE_V1_CHECK_ESSENTIALS_NIL = 2,
    PPCERTIFICATE_GSLT_ASSERTION_RULE_V1_CHECK_ESSENTIALS_CONS = 3,
    PPCERTIFICATE_GSLT_ASSERTION_RULE_V1_CHECK_DISJOINTS_NIL = 4,
    PPCERTIFICATE_GSLT_ASSERTION_RULE_V1_CHECK_DISJOINTS_CONS = 5,
    PPCERTIFICATE_GSLT_ASSERTION_RULE_V1_APPLY = 6,
    PPCERTIFICATE_GSLT_ASSERTION_RULE_V1_USE_PREMISE = 7,
    PPCERTIFICATE_GSLT_ASSERTION_RULE_V1_LEN = 8
} PPCertificateGSLTAssertionRuleRoleV1;

typedef struct {
    PPCertificateGSLTNameV1 owner;
    PPCertificateGSLTNameV1 base;
    PPCertificateGSLTNameV1
        constructors[PPCERTIFICATE_GSLT_SEQUENCE_CONSTRUCTOR_V1_LEN];
    PPCertificateGSLTNameV1
        judgments[PPCERTIFICATE_GSLT_SEQUENCE_JUDGMENT_V1_LEN];
    PPCertificateGSLTNameV1 rules[PPCERTIFICATE_GSLT_SEQUENCE_RULE_V1_LEN];
    PPCertificateGSLTNameV1 assertion_constructors[
        PPCERTIFICATE_GSLT_ASSERTION_CONSTRUCTOR_V1_LEN];
    PPCertificateGSLTNameV1 assertion_judgments[
        PPCERTIFICATE_GSLT_ASSERTION_JUDGMENT_V1_LEN];
    PPCertificateGSLTNameV1 assertion_rules[
        PPCERTIFICATE_GSLT_ASSERTION_RULE_V1_LEN];
    char semantic_digest[65];
    void *storage;
} PPCertificateGSLTSequenceEvidenceABIV1;

void ppcertificate_gslt_sequence_evidence_abi_v1_init(
    PPCertificateGSLTSequenceEvidenceABIV1 *abi);

void ppcertificate_gslt_sequence_evidence_abi_v1_free(
    PPCertificateGSLTSequenceEvidenceABIV1 *abi);

PPCertificateGSLTArticleV1Result ppcertificate_gslt_sequence_evidence_abi_v1_load(
    PPCertificateGSLTSequenceEvidenceABIV1 *abi,
    const char *answer_path,
    const PPCertificateGSLTPlanV1 *plan,
    char *error_buf,
    size_t error_buf_size);

typedef struct PPCertificateGSLTSequenceTokenV1 PPCertificateGSLTSequenceTokenV1;

struct PPCertificateGSLTSequenceTokenV1 {
    const PPCertificateGSLTPatternV1 *term;
    bool literal;
    PPCertificateGSLTReferenceV1 literal_evidence;
    bool variable;
    PPCertificateGSLTReferenceV1 variable_evidence;
};

typedef struct {
    const PPCertificateGSLTSequenceTokenV1 *const *tokens;
    uint32_t token_len;
} PPCertificateGSLTTokenSequenceV1;

typedef struct {
    const PPCertificateGSLTSequenceTokenV1 *key;
    PPCertificateGSLTTokenSequenceV1 image;
} PPCertificateGSLTSequenceBindingV1;

typedef struct {
    const PPCertificateGSLTSequenceBindingV1 *bindings;
    uint32_t binding_len;
} PPCertificateGSLTSequenceEnvironmentV1;

typedef struct {
    const PPCertificateGSLTSequenceTokenV1 *variable;
    const PPCertificateGSLTPatternV1 *typecode;
    PPCertificateGSLTTokenSequenceV1 image;
    PPCertificateGSLTReferenceV1 floating_proof;
} PPCertificateGSLTAssertionBindingV1;

typedef struct {
    PPCertificateGSLTTokenSequenceV1 template_sequence;
    PPCertificateGSLTTokenSequenceV1 actual_sequence;
    PPCertificateGSLTReferenceV1 actual_proof;
} PPCertificateGSLTAssertionEssentialV1;

typedef struct {
    const PPCertificateGSLTSequenceTokenV1 *left;
    const PPCertificateGSLTSequenceTokenV1 *right;
} PPCertificateGSLTAssertionDisjointV1;

typedef struct {
    const PPCertificateGSLTAssertionBindingV1 *bindings;
    uint32_t binding_len;
    const PPCertificateGSLTAssertionEssentialV1 *essentials;
    uint32_t essential_len;
    const PPCertificateGSLTAssertionDisjointV1 *disjoints;
    uint32_t disjoint_len;
    const PPCertificateGSLTPatternV1 *conclusion_type;
    PPCertificateGSLTTokenSequenceV1 conclusion_template;
} PPCertificateGSLTAssertionDeclarationV1;

typedef bool (*PPCertificateGSLTSequenceBinaryEvidenceV1)(
    void *context,
    const PPCertificateGSLTSequenceTokenV1 *left,
    const PPCertificateGSLTSequenceTokenV1 *right,
    PPCertificateGSLTReferenceV1 *evidence_out);

typedef struct {
    void *context;
    PPCertificateGSLTSequenceBinaryEvidenceV1 different;
    PPCertificateGSLTSequenceBinaryEvidenceV1 apart;
} PPCertificateGSLTSequenceEvidenceSourcesV1;

typedef struct {
    const PPCertificateGSLTPatternV1 *term;
    const PPCertificateGSLTSequenceTokenV1 *const *tokens;
    uint32_t token_len;
} PPCertificateGSLTMaterializedSequenceV1;

typedef struct {
    const PPCertificateGSLTPatternV1 *goal;
    PPCertificateGSLTReferenceV1 evidence;
} PPCertificateGSLTSequenceProofV1;

typedef struct {
    const PPCertificateGSLTPatternV1 *declared_goal;
    PPCertificateGSLTMaterializedSequenceV1 result;
    PPCertificateGSLTSequenceProofV1 proof;
} PPCertificateGSLTAssertionApplicationV1;

typedef struct {
    const PPCertificateGSLTArticleNodeV1 *nodes;
    uint32_t node_len;
    void *implementation;
} PPCertificateGSLTSequenceEvidenceProducerV1;

typedef struct {
    size_t arena_reserved_bytes;
    size_t arena_used_bytes;
    uint32_t node_capacity;
    uint32_t canonical_cache_capacity;
} PPCertificateGSLTSequenceEvidenceWorkspaceStatsV1;

void ppcertificate_gslt_sequence_evidence_producer_v1_init(
    PPCertificateGSLTSequenceEvidenceProducerV1 *producer);

void ppcertificate_gslt_sequence_evidence_producer_v1_free(
    PPCertificateGSLTSequenceEvidenceProducerV1 *producer);

bool ppcertificate_gslt_sequence_evidence_producer_v1_workspace_stats(
    const PPCertificateGSLTSequenceEvidenceProducerV1 *producer,
    PPCertificateGSLTSequenceEvidenceWorkspaceStatsV1 *stats_out);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_sequence_evidence_producer_v1_begin(
    PPCertificateGSLTSequenceEvidenceProducerV1 *producer,
    const PPCertificateGSLTSequenceEvidenceABIV1 *abi,
    uint32_t first_node_id,
    const PPCertificateGSLTArticleV1Limits *limits,
    char *error_buf,
    size_t error_buf_size);

/*
 * Inputs and their token patterns must remain alive until the producer is
 * begun again or freed.  Beginning an existing producer resets its logical
 * contents while retaining its private workspace.  Returned terms, goals,
 * and article-node storage are producer-owned.
 */
PPCertificateGSLTArticleV1Result
ppcertificate_gslt_sequence_evidence_producer_v1_instantiate(
    PPCertificateGSLTSequenceEvidenceProducerV1 *producer,
    PPCertificateGSLTTokenSequenceV1 source,
    PPCertificateGSLTSequenceEnvironmentV1 environment,
    const PPCertificateGSLTSequenceEvidenceSourcesV1 *sources,
    PPCertificateGSLTMaterializedSequenceV1 *result_out,
    PPCertificateGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_sequence_evidence_producer_v1_instantiate_expected(
    PPCertificateGSLTSequenceEvidenceProducerV1 *producer,
    PPCertificateGSLTTokenSequenceV1 source,
    PPCertificateGSLTSequenceEnvironmentV1 environment,
    PPCertificateGSLTTokenSequenceV1 expected,
    const PPCertificateGSLTSequenceEvidenceSourcesV1 *sources,
    PPCertificateGSLTMaterializedSequenceV1 *result_out,
    PPCertificateGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_sequence_evidence_producer_v1_support_apart(
    PPCertificateGSLTSequenceEvidenceProducerV1 *producer,
    PPCertificateGSLTTokenSequenceV1 left,
    PPCertificateGSLTTokenSequenceV1 right,
    const PPCertificateGSLTSequenceEvidenceSourcesV1 *sources,
    PPCertificateGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_sequence_evidence_producer_v1_apply_assertion(
    PPCertificateGSLTSequenceEvidenceProducerV1 *producer,
    const PPCertificateGSLTAssertionDeclarationV1 *declaration,
    uint32_t declaration_premise_index,
    const PPCertificateGSLTSequenceEvidenceSourcesV1 *sources,
    PPCertificateGSLTAssertionApplicationV1 *application_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result
ppcertificate_gslt_sequence_evidence_producer_v1_use_premise(
    PPCertificateGSLTSequenceEvidenceProducerV1 *producer,
    PPCertificateGSLTTokenSequenceV1 formula,
    PPCertificateGSLTReferenceV1 premise,
    PPCertificateGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size);

#endif
