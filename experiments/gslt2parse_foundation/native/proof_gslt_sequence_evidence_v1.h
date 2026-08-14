#ifndef CETTA_GSLT2PARSE_PROOF_GSLT_SEQUENCE_EVIDENCE_V1_H
#define CETTA_GSLT2PARSE_PROOF_GSLT_SEQUENCE_EVIDENCE_V1_H

#include "proof_gslt_plan_v1.h"

/*
 * Generated role names for an untrusted sequence-evidence producer.
 *
 * The ABI is compiled from the authored sequence-relation GSLT.  Native code
 * may use these roles to construct chronological articles, but acceptance is
 * still decided only by proof_gslt_article_v1 against the compiled
 * presentation.
 */

typedef enum {
    PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_NIL = 0,
    PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_CONS = 1,
    PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_ENVIRONMENT_NIL = 2,
    PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_ENVIRONMENT_CONS = 3,
    PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_LEN = 4
} PPProofGSLTSequenceConstructorRoleV1;

typedef enum {
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_APPEND = 0,
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LOOKUP = 1,
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_INSTANTIATE = 2,
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LITERAL = 3,
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_VARIABLE = 4,
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_DIFFERENT = 5,
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_APART = 6,
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_PAIR_ALLOWED = 7,
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_TOKEN_AGAINST_SEQUENCE = 8,
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_SUPPORT_APART = 9,
    PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LEN = 10
} PPProofGSLTSequenceJudgmentRoleV1;

typedef enum {
    PPPROOF_GSLT_SEQUENCE_RULE_V1_APPEND_NIL = 0,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_APPEND_CONS = 1,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_LOOKUP_HEAD = 2,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_LOOKUP_TAIL = 3,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_NIL = 4,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_LITERAL = 5,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_VARIABLE = 6,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_PAIR_LEFT_LITERAL = 7,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_PAIR_RIGHT_LITERAL = 8,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_PAIR_APART = 9,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_TOKEN_AGAINST_NIL = 10,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_TOKEN_AGAINST_CONS = 11,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_SUPPORT_APART_NIL = 12,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_SUPPORT_APART_CONS = 13,
    PPPROOF_GSLT_SEQUENCE_RULE_V1_LEN = 14
} PPProofGSLTSequenceRuleRoleV1;

typedef enum {
    PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_ASSERTION = 0,
    PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_VARIABLE = 1,
    PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_ESSENTIAL = 2,
    PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_DISJOINT = 3,
    PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LIST_NIL = 4,
    PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LIST_CONS = 5,
    PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LEN = 6
} PPProofGSLTAssertionConstructorRoleV1;

typedef enum {
    PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_DECLARED = 0,
    PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_BUILD_ENVIRONMENT = 1,
    PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_CHECK_ESSENTIALS = 2,
    PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_CHECK_DISJOINTS = 3,
    PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_PROVABLE = 4,
    PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_LEN = 5
} PPProofGSLTAssertionJudgmentRoleV1;

typedef enum {
    PPPROOF_GSLT_ASSERTION_RULE_V1_BUILD_ENVIRONMENT_NIL = 0,
    PPPROOF_GSLT_ASSERTION_RULE_V1_BUILD_ENVIRONMENT_CONS = 1,
    PPPROOF_GSLT_ASSERTION_RULE_V1_CHECK_ESSENTIALS_NIL = 2,
    PPPROOF_GSLT_ASSERTION_RULE_V1_CHECK_ESSENTIALS_CONS = 3,
    PPPROOF_GSLT_ASSERTION_RULE_V1_CHECK_DISJOINTS_NIL = 4,
    PPPROOF_GSLT_ASSERTION_RULE_V1_CHECK_DISJOINTS_CONS = 5,
    PPPROOF_GSLT_ASSERTION_RULE_V1_APPLY = 6,
    PPPROOF_GSLT_ASSERTION_RULE_V1_USE_PREMISE = 7,
    PPPROOF_GSLT_ASSERTION_RULE_V1_LEN = 8
} PPProofGSLTAssertionRuleRoleV1;

typedef struct {
    PPProofGSLTNameV1 owner;
    PPProofGSLTNameV1 base;
    PPProofGSLTNameV1
        constructors[PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_LEN];
    PPProofGSLTNameV1
        judgments[PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LEN];
    PPProofGSLTNameV1 rules[PPPROOF_GSLT_SEQUENCE_RULE_V1_LEN];
    PPProofGSLTNameV1 assertion_constructors[
        PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_LEN];
    PPProofGSLTNameV1 assertion_judgments[
        PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_LEN];
    PPProofGSLTNameV1 assertion_rules[
        PPPROOF_GSLT_ASSERTION_RULE_V1_LEN];
    char semantic_digest[65];
    void *storage;
} PPProofGSLTSequenceEvidenceABIV1;

void ppproof_gslt_sequence_evidence_abi_v1_init(
    PPProofGSLTSequenceEvidenceABIV1 *abi);

void ppproof_gslt_sequence_evidence_abi_v1_free(
    PPProofGSLTSequenceEvidenceABIV1 *abi);

PPProofGSLTArticleV1Result ppproof_gslt_sequence_evidence_abi_v1_load(
    PPProofGSLTSequenceEvidenceABIV1 *abi,
    const char *answer_path,
    const PPProofGSLTPlanV1 *plan,
    char *error_buf,
    size_t error_buf_size);

typedef struct PPProofGSLTSequenceTokenV1 PPProofGSLTSequenceTokenV1;

struct PPProofGSLTSequenceTokenV1 {
    const PPProofGSLTPatternV1 *term;
    bool literal;
    PPProofGSLTReferenceV1 literal_evidence;
    bool variable;
    PPProofGSLTReferenceV1 variable_evidence;
};

typedef struct {
    const PPProofGSLTSequenceTokenV1 *const *tokens;
    uint32_t token_len;
} PPProofGSLTTokenSequenceV1;

typedef struct {
    const PPProofGSLTSequenceTokenV1 *key;
    PPProofGSLTTokenSequenceV1 image;
} PPProofGSLTSequenceBindingV1;

typedef struct {
    const PPProofGSLTSequenceBindingV1 *bindings;
    uint32_t binding_len;
} PPProofGSLTSequenceEnvironmentV1;

typedef struct {
    const PPProofGSLTSequenceTokenV1 *variable;
    const PPProofGSLTPatternV1 *typecode;
    PPProofGSLTTokenSequenceV1 image;
    PPProofGSLTReferenceV1 floating_proof;
} PPProofGSLTAssertionBindingV1;

typedef struct {
    PPProofGSLTTokenSequenceV1 template_sequence;
    PPProofGSLTReferenceV1 actual_proof;
} PPProofGSLTAssertionEssentialV1;

typedef struct {
    const PPProofGSLTSequenceTokenV1 *left;
    const PPProofGSLTSequenceTokenV1 *right;
} PPProofGSLTAssertionDisjointV1;

typedef struct {
    const PPProofGSLTAssertionBindingV1 *bindings;
    uint32_t binding_len;
    const PPProofGSLTAssertionEssentialV1 *essentials;
    uint32_t essential_len;
    const PPProofGSLTAssertionDisjointV1 *disjoints;
    uint32_t disjoint_len;
    const PPProofGSLTPatternV1 *conclusion_type;
    PPProofGSLTTokenSequenceV1 conclusion_template;
} PPProofGSLTAssertionDeclarationV1;

typedef bool (*PPProofGSLTSequenceBinaryEvidenceV1)(
    void *context,
    const PPProofGSLTSequenceTokenV1 *left,
    const PPProofGSLTSequenceTokenV1 *right,
    PPProofGSLTReferenceV1 *evidence_out);

typedef struct {
    void *context;
    PPProofGSLTSequenceBinaryEvidenceV1 different;
    PPProofGSLTSequenceBinaryEvidenceV1 apart;
} PPProofGSLTSequenceEvidenceSourcesV1;

typedef struct {
    const PPProofGSLTPatternV1 *term;
    const PPProofGSLTSequenceTokenV1 *const *tokens;
    uint32_t token_len;
} PPProofGSLTMaterializedSequenceV1;

typedef struct {
    const PPProofGSLTPatternV1 *goal;
    PPProofGSLTReferenceV1 evidence;
} PPProofGSLTSequenceProofV1;

typedef struct {
    const PPProofGSLTPatternV1 *declared_goal;
    PPProofGSLTMaterializedSequenceV1 result;
    PPProofGSLTSequenceProofV1 proof;
} PPProofGSLTAssertionApplicationV1;

typedef struct {
    const PPProofGSLTArticleNodeV1 *nodes;
    uint32_t node_len;
    void *implementation;
} PPProofGSLTSequenceEvidenceProducerV1;

typedef struct {
    size_t arena_reserved_bytes;
    uint32_t node_capacity;
    uint32_t canonical_cache_capacity;
} PPProofGSLTSequenceEvidenceWorkspaceStatsV1;

void ppproof_gslt_sequence_evidence_producer_v1_init(
    PPProofGSLTSequenceEvidenceProducerV1 *producer);

void ppproof_gslt_sequence_evidence_producer_v1_free(
    PPProofGSLTSequenceEvidenceProducerV1 *producer);

bool ppproof_gslt_sequence_evidence_producer_v1_workspace_stats(
    const PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTSequenceEvidenceWorkspaceStatsV1 *stats_out);

PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_begin(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    const PPProofGSLTSequenceEvidenceABIV1 *abi,
    uint32_t first_node_id,
    const PPProofGSLTArticleV1Limits *limits,
    char *error_buf,
    size_t error_buf_size);

/*
 * Inputs and their token patterns must remain alive until the producer is
 * begun again or freed.  Beginning an existing producer resets its logical
 * contents while retaining its private workspace.  Returned terms, goals,
 * and article-node storage are producer-owned.
 */
PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_instantiate(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTTokenSequenceV1 source,
    PPProofGSLTSequenceEnvironmentV1 environment,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTMaterializedSequenceV1 *result_out,
    PPProofGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_support_apart(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTTokenSequenceV1 left,
    PPProofGSLTTokenSequenceV1 right,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_apply_assertion(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    const PPProofGSLTAssertionDeclarationV1 *declaration,
    uint32_t declaration_premise_index,
    const PPProofGSLTSequenceEvidenceSourcesV1 *sources,
    PPProofGSLTAssertionApplicationV1 *application_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result
ppproof_gslt_sequence_evidence_producer_v1_use_premise(
    PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTTokenSequenceV1 formula,
    PPProofGSLTReferenceV1 premise,
    PPProofGSLTSequenceProofV1 *proof_out,
    char *error_buf,
    size_t error_buf_size);

#endif
