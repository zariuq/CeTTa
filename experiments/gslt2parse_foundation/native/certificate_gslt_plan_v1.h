#ifndef CETTA_GSLT2PARSE_CERTIFICATE_GSLT_PLAN_V1_H
#define CETTA_GSLT2PARSE_CERTIFICATE_GSLT_PLAN_V1_H

#include "certificate_gslt_article_v1.h"

typedef struct {
    PPCertificateGSLTNameV1 id;
    const PPCertificateGSLTPatternV1 *context;
    uint32_t context_len;
    PPCertificateGSLTArticleV1 article;
    bool require_rooted;
} PPCertificateGSLTCompiledArticleV1;

typedef enum {
    PPCERTIFICATE_GSLT_CONSTRUCTOR_ORIGIN_V1_BASE = 0,
    PPCERTIFICATE_GSLT_CONSTRUCTOR_ORIGIN_V1_CALCULUS = 1
} PPCertificateGSLTConstructorOriginV1;

typedef struct {
    PPCertificateGSLTNameV1 owner;
    PPCertificateGSLTNameV1 base;
    PPCertificateGSLTPresentationV1 presentation;
    const PPCertificateGSLTConstructorOriginV1 *constructor_origins;
    uint32_t base_constructor_len;
    uint32_t calculus_constructor_len;
    const PPCertificateGSLTCompiledArticleV1 *articles;
    uint32_t article_len;
    char semantic_digest[65];
    void *storage;
} PPCertificateGSLTPlanV1;

void ppcertificate_gslt_plan_v1_init(PPCertificateGSLTPlanV1 *plan);
void ppcertificate_gslt_plan_v1_free(PPCertificateGSLTPlanV1 *plan);

PPCertificateGSLTArticleV1Result ppcertificate_gslt_plan_v1_load(
    PPCertificateGSLTPlanV1 *plan,
    const char *answer_path,
    const PPCertificateGSLTArticleV1Limits *limits,
    char *error_buf,
    size_t error_buf_size);

const PPCertificateGSLTCompiledArticleV1 *ppcertificate_gslt_plan_v1_find_article(
    const PPCertificateGSLTPlanV1 *plan,
    const char *article_id);

#endif
