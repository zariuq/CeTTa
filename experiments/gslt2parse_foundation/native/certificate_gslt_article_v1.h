#ifndef CETTA_GSLT2PARSE_PROOF_GSLT_ARTICLE_V1_H
#define CETTA_GSLT2PARSE_PROOF_GSLT_ARTICLE_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A language-neutral executable boundary for proof-calculus GSLTs.
 *
 * A compiled presentation supplies structural patterns, ordered rule schemas,
 * generic side conditions, and declared capabilities.  An article supplies
 * explicit rule arguments and chronological child references.  This unit
 * validates and instantiates that data; it does not infer a source language's
 * frames, proof encoding, statement kinds, or search policy.
 */

typedef enum {
    PPPROOF_GSLT_ARTICLE_V1_OK = 0,
    PPPROOF_GSLT_ARTICLE_V1_REJECTED = 1,
    PPPROOF_GSLT_ARTICLE_V1_RESOURCE = 2,
    PPPROOF_GSLT_ARTICLE_V1_INVALID = 3,
    PPPROOF_GSLT_ARTICLE_V1_UNSUPPORTED = 4
} PPProofGSLTArticleV1Result;

enum {
    PPPROOF_GSLT_ARTICLE_V1_CAP_BINDERS = UINT32_C(1) << 0,
    PPPROOF_GSLT_ARTICLE_V1_CAP_EXPLICIT_SUBSTITUTION = UINT32_C(1) << 1,
    PPPROOF_GSLT_ARTICLE_V1_CAP_COLLECTIONS = UINT32_C(1) << 2,
    PPPROOF_GSLT_ARTICLE_V1_CAP_SUBSTITUTION_CONDITION = UINT32_C(1) << 3,
    PPPROOF_GSLT_ARTICLE_V1_CAP_UNUSED_BINDER_CONDITION = UINT32_C(1) << 4,
    PPPROOF_GSLT_ARTICLE_V1_CAP_CONVERSION_DECLARATION = UINT32_C(1) << 5,
    PPPROOF_GSLT_ARTICLE_V1_CAP_COLLECTION_REST = UINT32_C(1) << 16
};

#define PPPROOF_GSLT_ARTICLE_V1_SUPPORTED_CAPABILITIES                       \
    (PPPROOF_GSLT_ARTICLE_V1_CAP_BINDERS |                                   \
     PPPROOF_GSLT_ARTICLE_V1_CAP_EXPLICIT_SUBSTITUTION |                     \
     PPPROOF_GSLT_ARTICLE_V1_CAP_COLLECTIONS |                              \
     PPPROOF_GSLT_ARTICLE_V1_CAP_SUBSTITUTION_CONDITION |                   \
     PPPROOF_GSLT_ARTICLE_V1_CAP_UNUSED_BINDER_CONDITION |                  \
     PPPROOF_GSLT_ARTICLE_V1_CAP_CONVERSION_DECLARATION)

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
} PPProofGSLTNameV1;

typedef enum {
    PPPROOF_GSLT_PATTERN_V1_BVAR = 0,
    PPPROOF_GSLT_PATTERN_V1_FVAR = 1,
    PPPROOF_GSLT_PATTERN_V1_APPLY = 2,
    PPPROOF_GSLT_PATTERN_V1_LAMBDA = 3,
    PPPROOF_GSLT_PATTERN_V1_MULTI_LAMBDA = 4,
    PPPROOF_GSLT_PATTERN_V1_SUBST = 5,
    PPPROOF_GSLT_PATTERN_V1_COLLECTION = 6
} PPProofGSLTPatternKindV1;

typedef enum {
    PPPROOF_GSLT_COLLECTION_V1_VECTOR = 0,
    PPPROOF_GSLT_COLLECTION_V1_HASH_BAG = 1,
    PPPROOF_GSLT_COLLECTION_V1_HASH_SET = 2
} PPProofGSLTCollectionKindV1;

typedef struct PPProofGSLTPatternV1 PPProofGSLTPatternV1;

struct PPProofGSLTPatternV1 {
    PPProofGSLTPatternKindV1 kind;
    /* Runtime materialization owns only direct child allocations marked here.
     * Authored and article-input patterns are immutable borrowed DAGs. */
    bool owns_children;
    union {
        uint32_t bvar;
        PPProofGSLTNameV1 fvar;
        struct {
            PPProofGSLTNameV1 constructor;
            const PPProofGSLTPatternV1 *arguments;
            uint32_t argument_len;
        } apply;
        struct {
            bool binder_name_present;
            PPProofGSLTNameV1 binder_name;
            const PPProofGSLTPatternV1 *body;
        } lambda;
        struct {
            uint32_t arity;
            const PPProofGSLTNameV1 *binder_names;
            uint32_t binder_name_len;
            const PPProofGSLTPatternV1 *body;
        } multi_lambda;
        struct {
            const PPProofGSLTPatternV1 *body;
            const PPProofGSLTPatternV1 *replacement;
        } subst;
        struct {
            PPProofGSLTCollectionKindV1 collection_kind;
            const PPProofGSLTPatternV1 *elements;
            uint32_t element_len;
            bool rest_present;
            PPProofGSLTNameV1 rest;
        } collection;
    } as;
};

typedef struct {
    PPProofGSLTNameV1 name;
    uint32_t arity;
} PPProofGSLTConstructorV1;

typedef struct {
    PPProofGSLTNameV1 head;
    uint32_t arity;
} PPProofGSLTJudgmentV1;

typedef struct {
    PPProofGSLTNameV1 name;
    uint32_t depth;
} PPProofGSLTFormalV1;

typedef enum {
    PPPROOF_GSLT_SIDE_CONDITION_V1_EXPLICIT_SUBSTITUTION = 0,
    PPPROOF_GSLT_SIDE_CONDITION_V1_UNUSED_BINDER_ELIMINATION = 1
} PPProofGSLTSideConditionKindV1;

typedef struct {
    PPProofGSLTSideConditionKindV1 kind;
    uint32_t ambient_depth;
    uint32_t body_argument;
    uint32_t replacement_argument;
    uint32_t result_argument;
} PPProofGSLTSideConditionV1;

typedef struct {
    PPProofGSLTNameV1 id;
    const PPProofGSLTFormalV1 *formals;
    uint32_t formal_len;
    const PPProofGSLTPatternV1 *premises;
    uint32_t premise_len;
    const PPProofGSLTPatternV1 *conclusion;
    const PPProofGSLTSideConditionV1 *side_conditions;
    uint32_t side_condition_len;
} PPProofGSLTRuleSchemaV1;

typedef struct {
    bool present;
    PPProofGSLTNameV1 judgment_head;
    PPProofGSLTNameV1 version;
} PPProofGSLTConversionV1;

typedef struct {
    const PPProofGSLTConstructorV1 *constructors;
    uint32_t constructor_len;
    const PPProofGSLTJudgmentV1 *judgments;
    uint32_t judgment_len;
    const PPProofGSLTRuleSchemaV1 *rules;
    uint32_t rule_len;
    PPProofGSLTConversionV1 conversion;
    uint32_t required_capabilities;
} PPProofGSLTPresentationV1;

typedef struct {
    PPProofGSLTNameV1 rule_id;
    const PPProofGSLTPatternV1 *arguments;
    uint32_t argument_len;
} PPProofGSLTRuleInstanceV1;

typedef enum {
    PPPROOF_GSLT_REFERENCE_V1_PREMISE = 0,
    PPPROOF_GSLT_REFERENCE_V1_NODE = 1
} PPProofGSLTReferenceKindV1;

typedef struct {
    PPProofGSLTReferenceKindV1 kind;
    uint32_t index;
} PPProofGSLTReferenceV1;

typedef struct {
    uint32_t id;
    PPProofGSLTRuleInstanceV1 rule_instance;
    const PPProofGSLTReferenceV1 *children;
    uint32_t child_len;
} PPProofGSLTArticleNodeV1;

typedef struct {
    uint32_t version;
    const PPProofGSLTArticleNodeV1 *nodes;
    uint32_t node_len;
    uint32_t root_id;
    const PPProofGSLTPatternV1 *target;
} PPProofGSLTArticleV1;

typedef struct {
    uint32_t maximum_pattern_depth;
    uint32_t maximum_presentation_pattern_nodes;
    uint32_t maximum_materialized_pattern_nodes;
    uint32_t maximum_article_nodes;
} PPProofGSLTArticleV1Limits;

typedef struct {
    uint32_t checked_node_len;
    uint32_t materialized_pattern_node_len;
    uint32_t used_capabilities;
    bool rooted;
} PPProofGSLTArticleV1Receipt;

PPProofGSLTArticleV1Limits ppproof_gslt_article_v1_default_limits(void);

bool ppproof_gslt_article_v1_name_equal(
    PPProofGSLTNameV1 left, PPProofGSLTNameV1 right);

PPProofGSLTArticleV1Result ppproof_gslt_article_v1_presentation_validate(
    const PPProofGSLTPresentationV1 *presentation,
    const PPProofGSLTArticleV1Limits *limits,
    uint32_t *used_capabilities_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result ppproof_gslt_article_v1_check_open(
    const PPProofGSLTPresentationV1 *presentation,
    const PPProofGSLTPatternV1 *context,
    uint32_t context_len,
    const PPProofGSLTArticleV1 *article,
    bool require_rooted,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTArticleV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size);

PPProofGSLTArticleV1Result ppproof_gslt_article_v1_check(
    const PPProofGSLTPresentationV1 *presentation,
    const PPProofGSLTArticleV1 *article,
    bool require_rooted,
    const PPProofGSLTArticleV1Limits *limits,
    PPProofGSLTArticleV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size);

#endif
