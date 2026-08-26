#ifndef CETTA_GSLT2PARSE_PROOF_GSLT_PLAN_V1_H
#define CETTA_GSLT2PARSE_PROOF_GSLT_PLAN_V1_H

#include "proof_gslt_article_v1.h"

typedef struct {
    PPProofGSLTNameV1 id;
    const PPProofGSLTPatternV1 *context;
    uint32_t context_len;
    PPProofGSLTArticleV1 article;
    bool require_rooted;
} PPProofGSLTCompiledArticleV1;

typedef enum {
    PPPROOF_GSLT_CONSTRUCTOR_ORIGIN_V1_BASE = 0,
    PPPROOF_GSLT_CONSTRUCTOR_ORIGIN_V1_CALCULUS = 1
} PPProofGSLTConstructorOriginV1;

typedef struct {
    PPProofGSLTNameV1 owner;
    PPProofGSLTNameV1 base;
    PPProofGSLTPresentationV1 presentation;
    const PPProofGSLTConstructorOriginV1 *constructor_origins;
    uint32_t base_constructor_len;
    uint32_t calculus_constructor_len;
    const PPProofGSLTCompiledArticleV1 *articles;
    uint32_t article_len;
    char semantic_digest[65];
    void *storage;
} PPProofGSLTPlanV1;

void ppproof_gslt_plan_v1_init(PPProofGSLTPlanV1 *plan);
void ppproof_gslt_plan_v1_free(PPProofGSLTPlanV1 *plan);

PPProofGSLTArticleV1Result ppproof_gslt_plan_v1_load(
    PPProofGSLTPlanV1 *plan,
    const char *answer_path,
    const PPProofGSLTArticleV1Limits *limits,
    char *error_buf,
    size_t error_buf_size);

const PPProofGSLTCompiledArticleV1 *ppproof_gslt_plan_v1_find_article(
    const PPProofGSLTPlanV1 *plan,
    const char *article_id);

#endif
