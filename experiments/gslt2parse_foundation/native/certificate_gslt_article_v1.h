#ifndef CETTA_GSLT2PARSE_CERTIFICATE_GSLT_ARTICLE_V1_H
#define CETTA_GSLT2PARSE_CERTIFICATE_GSLT_ARTICLE_V1_H

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
    PPCERTIFICATE_GSLT_ARTICLE_V1_OK = 0,
    PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED = 1,
    PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE = 2,
    PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID = 3,
    PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED = 4
} PPCertificateGSLTArticleV1Result;

enum {
    PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_BINDERS = UINT32_C(1) << 0,
    PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_EXPLICIT_SUBSTITUTION = UINT32_C(1) << 1,
    PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_COLLECTIONS = UINT32_C(1) << 2,
    PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_SUBSTITUTION_CONDITION = UINT32_C(1) << 3,
    PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_UNUSED_BINDER_CONDITION = UINT32_C(1) << 4,
    PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_CONVERSION_DECLARATION = UINT32_C(1) << 5,
    PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_COLLECTION_REST = UINT32_C(1) << 16
};

#define PPCERTIFICATE_GSLT_ARTICLE_V1_SUPPORTED_CAPABILITIES                       \
    (PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_BINDERS |                                   \
     PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_EXPLICIT_SUBSTITUTION |                     \
     PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_COLLECTIONS |                              \
     PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_SUBSTITUTION_CONDITION |                   \
     PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_UNUSED_BINDER_CONDITION |                  \
     PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_CONVERSION_DECLARATION)

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
} PPCertificateGSLTNameV1;

typedef enum {
    PPCERTIFICATE_GSLT_PATTERN_V1_BVAR = 0,
    PPCERTIFICATE_GSLT_PATTERN_V1_FVAR = 1,
    PPCERTIFICATE_GSLT_PATTERN_V1_APPLY = 2,
    PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA = 3,
    PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA = 4,
    PPCERTIFICATE_GSLT_PATTERN_V1_SUBST = 5,
    PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION = 6
} PPCertificateGSLTPatternKindV1;

typedef enum {
    PPCERTIFICATE_GSLT_COLLECTION_V1_VECTOR = 0,
    PPCERTIFICATE_GSLT_COLLECTION_V1_HASH_BAG = 1,
    PPCERTIFICATE_GSLT_COLLECTION_V1_HASH_SET = 2
} PPCertificateGSLTCollectionKindV1;

typedef struct PPCertificateGSLTPatternV1 PPCertificateGSLTPatternV1;

struct PPCertificateGSLTPatternV1 {
    PPCertificateGSLTPatternKindV1 kind;
    /* Runtime materialization owns only direct child allocations marked here.
     * Authored and article-input patterns are immutable borrowed DAGs. */
    bool owns_children;
    union {
        uint32_t bvar;
        PPCertificateGSLTNameV1 fvar;
        struct {
            PPCertificateGSLTNameV1 constructor;
            const PPCertificateGSLTPatternV1 *arguments;
            uint32_t argument_len;
        } apply;
        struct {
            bool binder_name_present;
            PPCertificateGSLTNameV1 binder_name;
            const PPCertificateGSLTPatternV1 *body;
        } lambda;
        struct {
            uint32_t arity;
            const PPCertificateGSLTNameV1 *binder_names;
            uint32_t binder_name_len;
            const PPCertificateGSLTPatternV1 *body;
        } multi_lambda;
        struct {
            const PPCertificateGSLTPatternV1 *body;
            const PPCertificateGSLTPatternV1 *replacement;
        } subst;
        struct {
            PPCertificateGSLTCollectionKindV1 collection_kind;
            const PPCertificateGSLTPatternV1 *elements;
            uint32_t element_len;
            bool rest_present;
            PPCertificateGSLTNameV1 rest;
        } collection;
    } as;
};

typedef struct {
    PPCertificateGSLTNameV1 name;
    uint32_t arity;
} PPCertificateGSLTConstructorV1;

typedef struct {
    PPCertificateGSLTNameV1 head;
    uint32_t arity;
} PPCertificateGSLTJudgmentV1;

typedef struct {
    PPCertificateGSLTNameV1 name;
    uint32_t depth;
} PPCertificateGSLTFormalV1;

typedef enum {
    PPCERTIFICATE_GSLT_SIDE_CONDITION_V1_EXPLICIT_SUBSTITUTION = 0,
    PPCERTIFICATE_GSLT_SIDE_CONDITION_V1_UNUSED_BINDER_ELIMINATION = 1
} PPCertificateGSLTSideConditionKindV1;

typedef struct {
    PPCertificateGSLTSideConditionKindV1 kind;
    uint32_t ambient_depth;
    uint32_t body_argument;
    uint32_t replacement_argument;
    uint32_t result_argument;
} PPCertificateGSLTSideConditionV1;

typedef struct {
    PPCertificateGSLTNameV1 id;
    const PPCertificateGSLTFormalV1 *formals;
    uint32_t formal_len;
    const PPCertificateGSLTPatternV1 *premises;
    uint32_t premise_len;
    const PPCertificateGSLTPatternV1 *conclusion;
    const PPCertificateGSLTSideConditionV1 *side_conditions;
    uint32_t side_condition_len;
} PPCertificateGSLTRuleSchemaV1;

typedef struct {
    bool present;
    PPCertificateGSLTNameV1 judgment_head;
    PPCertificateGSLTNameV1 version;
} PPCertificateGSLTConversionV1;

typedef struct {
    const PPCertificateGSLTConstructorV1 *constructors;
    uint32_t constructor_len;
    const PPCertificateGSLTJudgmentV1 *judgments;
    uint32_t judgment_len;
    const PPCertificateGSLTRuleSchemaV1 *rules;
    uint32_t rule_len;
    PPCertificateGSLTConversionV1 conversion;
    uint32_t required_capabilities;
} PPCertificateGSLTPresentationV1;

typedef struct {
    PPCertificateGSLTNameV1 rule_id;
    const PPCertificateGSLTPatternV1 *arguments;
    uint32_t argument_len;
} PPCertificateGSLTRuleInstanceV1;

typedef enum {
    PPCERTIFICATE_GSLT_REFERENCE_V1_PREMISE = 0,
    PPCERTIFICATE_GSLT_REFERENCE_V1_NODE = 1
} PPCertificateGSLTReferenceKindV1;

typedef struct {
    PPCertificateGSLTReferenceKindV1 kind;
    uint32_t index;
} PPCertificateGSLTReferenceV1;

typedef struct {
    uint32_t id;
    PPCertificateGSLTRuleInstanceV1 rule_instance;
    const PPCertificateGSLTReferenceV1 *children;
    uint32_t child_len;
} PPCertificateGSLTArticleNodeV1;

typedef struct {
    uint32_t version;
    const PPCertificateGSLTArticleNodeV1 *nodes;
    uint32_t node_len;
    uint32_t root_id;
    const PPCertificateGSLTPatternV1 *target;
} PPCertificateGSLTArticleV1;

typedef struct {
    uint32_t maximum_pattern_depth;
    uint32_t maximum_presentation_pattern_nodes;
    uint32_t maximum_materialized_pattern_nodes;
    uint32_t maximum_article_nodes;
} PPCertificateGSLTArticleV1Limits;

typedef struct {
    uint32_t checked_node_len;
    uint32_t materialized_pattern_node_len;
    uint32_t used_capabilities;
    bool rooted;
} PPCertificateGSLTArticleV1Receipt;

PPCertificateGSLTArticleV1Limits ppcertificate_gslt_article_v1_default_limits(void);

bool ppcertificate_gslt_article_v1_name_equal(
    PPCertificateGSLTNameV1 left, PPCertificateGSLTNameV1 right);

PPCertificateGSLTArticleV1Result ppcertificate_gslt_article_v1_presentation_validate(
    const PPCertificateGSLTPresentationV1 *presentation,
    const PPCertificateGSLTArticleV1Limits *limits,
    uint32_t *used_capabilities_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result ppcertificate_gslt_article_v1_check_open(
    const PPCertificateGSLTPresentationV1 *presentation,
    const PPCertificateGSLTPatternV1 *context,
    uint32_t context_len,
    const PPCertificateGSLTArticleV1 *article,
    bool require_rooted,
    const PPCertificateGSLTArticleV1Limits *limits,
    PPCertificateGSLTArticleV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size);

PPCertificateGSLTArticleV1Result ppcertificate_gslt_article_v1_check(
    const PPCertificateGSLTPresentationV1 *presentation,
    const PPCertificateGSLTArticleV1 *article,
    bool require_rooted,
    const PPCertificateGSLTArticleV1Limits *limits,
    PPCertificateGSLTArticleV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size);

#endif
