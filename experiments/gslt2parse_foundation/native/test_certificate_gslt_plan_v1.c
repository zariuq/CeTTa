#include "certificate_gslt_plan_v1.h"

#include "atom.h"
#include "symbol.h"

#include <stdio.h>
#include <string.h>

static uint32_t checks_run;
static uint32_t checks_failed;

static void expect_true(const char *name, bool condition,
                        const char *detail) {
    checks_run++;
    if (condition)
        return;
    checks_failed++;
    fprintf(stderr, "FAIL %s: %s\n", name,
            detail && detail[0] != '\0' ? detail : "condition is false");
}

static bool name_is(PPCertificateGSLTNameV1 name, const char *text) {
    size_t len = strlen(text);
    return len <= UINT32_MAX && name.len == (uint32_t)len &&
           memcmp(name.bytes, text, len) == 0;
}

static void check_plan(const char *path,
                       const char *owner,
                       const char *base,
                       const char *article_id,
                       const char *digest,
                       uint32_t expected_base_constructors,
                       uint32_t expected_calculus_constructors,
                       uint32_t expected_articles,
                       uint32_t checked_nodes) {
    PPCertificateGSLTPlanV1 plan;
    const PPCertificateGSLTCompiledArticleV1 *article;
    PPCertificateGSLTArticleV1Receipt receipt;
    PPCertificateGSLTArticleV1Result result;
    uint32_t base_constructors = 0u;
    uint32_t calculus_constructors = 0u;
    uint32_t index;
    char error[512] = {0};

    ppcertificate_gslt_plan_v1_init(&plan);
    result = ppcertificate_gslt_plan_v1_load(
        &plan, path, NULL, error, sizeof(error));
    expect_true("compiled-plan-load",
                result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK, error);
    if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
        ppcertificate_gslt_plan_v1_free(&plan);
        return;
    }
    expect_true("compiled-plan-owner", name_is(plan.owner, owner), NULL);
    expect_true("compiled-plan-base", name_is(plan.base, base), NULL);
    expect_true("compiled-plan-semantic-digest",
                strcmp(plan.semantic_digest, digest) == 0,
                plan.semantic_digest);
    expect_true("compiled-plan-article-count",
                plan.article_len == expected_articles, NULL);
    for (index = 0u; index < plan.presentation.constructor_len; index++) {
        if (plan.constructor_origins[index] ==
            PPCERTIFICATE_GSLT_CONSTRUCTOR_ORIGIN_V1_BASE)
            base_constructors++;
        else if (plan.constructor_origins[index] ==
                 PPCERTIFICATE_GSLT_CONSTRUCTOR_ORIGIN_V1_CALCULUS)
            calculus_constructors++;
    }
    expect_true(
        "compiled-plan-base-constructor-count",
        plan.base_constructor_len == expected_base_constructors &&
            base_constructors == expected_base_constructors,
        NULL);
    expect_true(
        "compiled-plan-calculus-constructor-count",
        plan.calculus_constructor_len == expected_calculus_constructors &&
            calculus_constructors == expected_calculus_constructors,
        NULL);
    article = ppcertificate_gslt_plan_v1_find_article(&plan, article_id);
    expect_true("compiled-plan-article-lookup", article != NULL, NULL);
    if (article) {
        memset(error, 0, sizeof(error));
        result = ppcertificate_gslt_article_v1_check_open(
            &plan.presentation, article->context, article->context_len,
            &article->article, article->require_rooted, NULL,
            &receipt, error, sizeof(error));
        expect_true("compiled-plan-article-check",
                    result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK, error);
        expect_true("compiled-plan-article-receipt",
                    result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
                        receipt.checked_node_len == checked_nodes &&
                        receipt.rooted,
                    NULL);
    }
    expect_true("compiled-plan-foreign-article-absent",
                ppcertificate_gslt_plan_v1_find_article(
                    &plan, "not-this-calculus") == NULL,
                NULL);
    ppcertificate_gslt_plan_v1_free(&plan);
}

static void check_binder_details(const char *path) {
    const uint32_t expected_capabilities =
        PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_BINDERS |
        PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_EXPLICIT_SUBSTITUTION |
        PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_SUBSTITUTION_CONDITION |
        PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_UNUSED_BINDER_CONDITION |
        PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_CONVERSION_DECLARATION;
    PPCertificateGSLTPlanV1 plan;
    const PPCertificateGSLTCompiledArticleV1 *article;
    const PPCertificateGSLTRuleSchemaV1 *substitution = NULL;
    PPCertificateGSLTArticleV1Receipt receipt;
    PPCertificateGSLTArticleV1Result result;
    uint32_t index;
    char error[512] = {0};

    ppcertificate_gslt_plan_v1_init(&plan);
    result = ppcertificate_gslt_plan_v1_load(
        &plan, path, NULL, error, sizeof(error));
    expect_true("binder-plan-reload",
                result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK, error);
    if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
        ppcertificate_gslt_plan_v1_free(&plan);
        return;
    }
    expect_true("binder-capability-set",
                plan.presentation.required_capabilities ==
                    expected_capabilities,
                NULL);
    expect_true("binder-conversion",
                plan.presentation.conversion.present &&
                    name_is(
                        plan.presentation.conversion.judgment_head, "Rel") &&
                    name_is(plan.presentation.conversion.version, "beta-v1"),
                NULL);
    for (index = 0u; index < plan.presentation.rule_len; index++) {
        if (name_is(plan.presentation.rules[index].id,
                    "subst-certificate")) {
            substitution = &plan.presentation.rules[index];
            break;
        }
    }
    expect_true(
        "named-side-condition-lowered-to-indexed-abi",
        substitution && substitution->side_condition_len == 1u &&
            substitution->side_conditions[0].kind ==
                PPCERTIFICATE_GSLT_SIDE_CONDITION_V1_EXPLICIT_SUBSTITUTION &&
            substitution->side_conditions[0].body_argument == 0u &&
            substitution->side_conditions[0].replacement_argument == 1u &&
            substitution->side_conditions[0].result_argument == 2u,
        NULL);
    article = ppcertificate_gslt_plan_v1_find_article(
        &plan, "binder-unused-canary");
    expect_true("binder-second-article-lookup", article != NULL, NULL);
    if (article) {
        result = ppcertificate_gslt_article_v1_check_open(
            &plan.presentation, article->context, article->context_len,
            &article->article, article->require_rooted, NULL,
            &receipt, error, sizeof(error));
        expect_true("binder-unused-article-check",
                    result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK &&
                        receipt.checked_node_len == 1u,
                    error);
    }
    ppcertificate_gslt_plan_v1_free(&plan);
}

static void check_mutated_plan(const char *path) {
    PPCertificateGSLTPlanV1 plan;
    PPCertificateGSLTArticleV1Result result;
    char error[512] = {0};

    ppcertificate_gslt_plan_v1_init(&plan);
    result = ppcertificate_gslt_plan_v1_load(
        &plan, path, NULL, error, sizeof(error));
    expect_true(
        "source-rule-deletion-is-observable",
        result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK ||
            (strcmp(
                 plan.semantic_digest,
                 "f6c9f2ba4044d173cfb9921bff28b50a69d7c76ae89c47910fa283c4df9b049f") !=
                 0 &&
             ppcertificate_gslt_plan_v1_find_article(
                 &plan, "prop-mp-canary") == NULL),
        error);
    ppcertificate_gslt_plan_v1_free(&plan);
}

static void check_sequence_mutated_plan(const char *path) {
    PPCertificateGSLTPlanV1 plan;
    PPCertificateGSLTArticleV1Result result;
    char error[512] = {0};

    ppcertificate_gslt_plan_v1_init(&plan);
    result = ppcertificate_gslt_plan_v1_load(
        &plan, path, NULL, error, sizeof(error));
    expect_true(
        "sequence-apartness-deletion-is-observable",
        result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
        error);
    ppcertificate_gslt_plan_v1_free(&plan);
}

static void check_sequence_assertion_mutated_plan(const char *path) {
    PPCertificateGSLTPlanV1 plan;
    PPCertificateGSLTArticleV1Result result;
    char error[512] = {0};

    ppcertificate_gslt_plan_v1_init(&plan);
    result = ppcertificate_gslt_plan_v1_load(
        &plan, path, NULL, error, sizeof(error));
    expect_true(
        "sequence-assertion-deletion-is-observable",
        result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
        error);
    ppcertificate_gslt_plan_v1_free(&plan);
}

static void check_resource_classification(const char *path) {
    PPCertificateGSLTArticleV1Limits limits =
        ppcertificate_gslt_article_v1_default_limits();
    PPCertificateGSLTPlanV1 plan;
    PPCertificateGSLTArticleV1Result result;
    char error[512] = {0};

    limits.maximum_presentation_pattern_nodes = 1u;
    ppcertificate_gslt_plan_v1_init(&plan);
    result = ppcertificate_gslt_plan_v1_load(
        &plan, path, &limits, error, sizeof(error));
    expect_true(
        "record-limit-is-resource-refusal",
        result == PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
        error);
    ppcertificate_gslt_plan_v1_free(&plan);
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    int status;

    if (argc != 8) {
        fprintf(stderr,
                "usage: %s PROP.answers EVEN.answers BINDER.answers "
                "SEQUENCE.answers PROP-MUTATED.answers "
                "SEQUENCE-MUTATED.answers "
                "SEQUENCE-ASSERTION-MUTATED.answers\n",
                argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    check_plan(
        argv[1], "PropProofV1", "PropLanguageV1", "prop-mp-canary",
        "f6c9f2ba4044d173cfb9921bff28b50a69d7c76ae89c47910fa283c4df9b049f",
        3u, 0u, 1u, 3u);
    check_plan(
        argv[2], "EvenProofV1", "NatLanguageV1", "even-two-canary",
        "fdacd59a7b9073cd73ed9454b3cf863200c742469d26bcb4905caf755b6921f3",
        2u, 0u, 1u, 2u);
    check_plan(
        argv[3], "BinderProofV1", "BinderLanguageV1",
        "binder-substitution-canary",
        "f6aaaf99d0b0f29b02fe21aebf92d8a294dc67caa946ad0096af7c2bcdd875ce",
        2u, 0u, 2u, 1u);
    check_plan(
        argv[4], "SequenceProofV1", "SequenceLanguageV1",
        "sequence-splice-apart-canary",
        "6fa006dd3ebb3e785d1cc50d550664558898c4f34e42860430c12a4577f3e6f8",
        6u, 10u, 1u, 34u);
    check_binder_details(argv[3]);
    check_resource_classification(argv[1]);
    check_mutated_plan(argv[5]);
    check_sequence_mutated_plan(argv[6]);
    check_sequence_assertion_mutated_plan(argv[7]);
    printf("(CertificateGSLTPlanV1Summary %u %u %u)\n",
           checks_run, checks_run - checks_failed, checks_failed);
    status = checks_failed == 0u ? 0 : 1;
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return status;
}
