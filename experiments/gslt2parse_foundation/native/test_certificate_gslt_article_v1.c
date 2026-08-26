#include "certificate_gslt_article_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PP_NAME(text)                                                         \
    { (const uint8_t *)(text), (uint32_t)(sizeof(text) - 1u) }

#define PP_BVAR(index_value)                                                  \
    { .kind = PPCERTIFICATE_GSLT_PATTERN_V1_BVAR, .as.bvar = (index_value) }

#define PP_FVAR(text)                                                         \
    { .kind = PPCERTIFICATE_GSLT_PATTERN_V1_FVAR, .as.fvar = PP_NAME(text) }

#define PP_APPLY(text, children, child_count)                                 \
    {                                                                         \
        .kind = PPCERTIFICATE_GSLT_PATTERN_V1_APPLY,                                \
        .as.apply = {                                                         \
            .constructor = PP_NAME(text),                                     \
            .arguments = (children),                                          \
            .argument_len = (child_count),                                    \
        },                                                                    \
    }

static uint32_t checks_run;
static uint32_t checks_failed;

static void expect_result(const char *name,
                          PPCertificateGSLTArticleV1Result expected,
                          PPCertificateGSLTArticleV1Result actual,
                          const char *error) {
    checks_run++;
    if (expected == actual)
        return;
    checks_failed++;
    fprintf(stderr, "FAIL %s: expected %d, received %d (%s)\n",
            name, (int)expected, (int)actual,
            error && error[0] != '\0' ? error : "no diagnostic");
}

static PPCertificateGSLTArticleV1Result check_article(
    const PPCertificateGSLTPresentationV1 *presentation,
    const PPCertificateGSLTArticleV1 *article,
    bool rooted,
    const PPCertificateGSLTArticleV1Limits *limits,
    PPCertificateGSLTArticleV1Receipt *receipt,
    char error[256]) {
    memset(error, 0, 256u);
    return ppcertificate_gslt_article_v1_check(
        presentation, article, rooted, limits, receipt, error, 256u);
}

static const PPCertificateGSLTConstructorV1 prop_constructors[] = {
    {.name = PP_NAME("p"), .arity = 0u},
    {.name = PP_NAME("q"), .arity = 0u},
    {.name = PP_NAME("Imp"), .arity = 2u},
};

static const PPCertificateGSLTJudgmentV1 prop_judgments[] = {
    {.head = PP_NAME("Prov"), .arity = 1u},
};

static const PPCertificateGSLTFormalV1 given_formals[] = {
    {.name = PP_NAME("X"), .depth = 0u},
};
static const PPCertificateGSLTPatternV1 given_conclusion_args[] = {
    PP_FVAR("X"),
};
static const PPCertificateGSLTPatternV1 given_conclusion =
    PP_APPLY("Prov", given_conclusion_args, 1u);

static const PPCertificateGSLTFormalV1 mp_formals[] = {
    {.name = PP_NAME("X"), .depth = 0u},
    {.name = PP_NAME("Y"), .depth = 0u},
};
static const PPCertificateGSLTPatternV1 mp_imp_args[] = {
    PP_FVAR("X"),
    PP_FVAR("Y"),
};
static const PPCertificateGSLTPatternV1 mp_imp =
    PP_APPLY("Imp", mp_imp_args, 2u);
static const PPCertificateGSLTPatternV1 mp_first_premise_args[] = {
    mp_imp,
};
static const PPCertificateGSLTPatternV1 mp_second_premise_args[] = {
    PP_FVAR("X"),
};
static const PPCertificateGSLTPatternV1 mp_premises[] = {
    PP_APPLY("Prov", mp_first_premise_args, 1u),
    PP_APPLY("Prov", mp_second_premise_args, 1u),
};
static const PPCertificateGSLTPatternV1 mp_conclusion_args[] = {
    PP_FVAR("Y"),
};
static const PPCertificateGSLTPatternV1 mp_conclusion =
    PP_APPLY("Prov", mp_conclusion_args, 1u);

static const PPCertificateGSLTRuleSchemaV1 prop_rules[] = {
    {
        .id = PP_NAME("given"),
        .formals = given_formals,
        .formal_len = 1u,
        .conclusion = &given_conclusion,
    },
    {
        .id = PP_NAME("mp"),
        .formals = mp_formals,
        .formal_len = 2u,
        .premises = mp_premises,
        .premise_len = 2u,
        .conclusion = &mp_conclusion,
    },
};

static const PPCertificateGSLTPresentationV1 prop_presentation = {
    .constructors = prop_constructors,
    .constructor_len = 3u,
    .judgments = prop_judgments,
    .judgment_len = 1u,
    .rules = prop_rules,
    .rule_len = 2u,
};

static const PPCertificateGSLTPatternV1 atom_p = PP_APPLY("p", NULL, 0u);
static const PPCertificateGSLTPatternV1 atom_q = PP_APPLY("q", NULL, 0u);
static const PPCertificateGSLTPatternV1 imp_p_q_args[] = {
    atom_p,
    atom_q,
};
static const PPCertificateGSLTPatternV1 imp_p_q =
    PP_APPLY("Imp", imp_p_q_args, 2u);
static const PPCertificateGSLTPatternV1 prov_q_args[] = {
    atom_q,
};
static const PPCertificateGSLTPatternV1 prov_q =
    PP_APPLY("Prov", prov_q_args, 1u);
static const PPCertificateGSLTPatternV1 prov_p_args[] = {
    atom_p,
};
static const PPCertificateGSLTPatternV1 prov_p =
    PP_APPLY("Prov", prov_p_args, 1u);

static const PPCertificateGSLTPatternV1 given_imp_arguments[] = {
    imp_p_q,
};
static const PPCertificateGSLTPatternV1 given_p_arguments[] = {
    atom_p,
};
static const PPCertificateGSLTPatternV1 given_q_arguments[] = {
    atom_q,
};
static const PPCertificateGSLTPatternV1 mp_arguments[] = {
    atom_p,
    atom_q,
};
static const PPCertificateGSLTReferenceV1 mp_children[] = {
    {.kind = PPCERTIFICATE_GSLT_REFERENCE_V1_NODE, .index = 10u},
    {.kind = PPCERTIFICATE_GSLT_REFERENCE_V1_NODE, .index = 20u},
};
static const PPCertificateGSLTArticleNodeV1 prop_nodes[] = {
    {
        .id = 10u,
        .rule_instance = {
            .rule_id = PP_NAME("given"),
            .arguments = given_imp_arguments,
            .argument_len = 1u,
        },
    },
    {
        .id = 20u,
        .rule_instance = {
            .rule_id = PP_NAME("given"),
            .arguments = given_p_arguments,
            .argument_len = 1u,
        },
    },
    {
        .id = 30u,
        .rule_instance = {
            .rule_id = PP_NAME("mp"),
            .arguments = mp_arguments,
            .argument_len = 2u,
        },
        .children = mp_children,
        .child_len = 2u,
    },
};
static const PPCertificateGSLTArticleV1 prop_article = {
    .version = 1u,
    .nodes = prop_nodes,
    .node_len = 3u,
    .root_id = 30u,
    .target = &prov_q,
};

static const PPCertificateGSLTPatternV1 document_symbol_left =
    PP_APPLY("document-symbol-left", NULL, 0u);
static const PPCertificateGSLTPatternV1 document_symbol_right =
    PP_APPLY("document-symbol-right", NULL, 0u);
static const PPCertificateGSLTPatternV1 document_term_args[] = {
    document_symbol_left,
    document_symbol_right,
};
static const PPCertificateGSLTPatternV1 document_term =
    PP_APPLY("document-payload", document_term_args, 2u);
static const PPCertificateGSLTPatternV1 document_target_args[] = {
    document_term,
};
static const PPCertificateGSLTPatternV1 document_target =
    PP_APPLY("Prov", document_target_args, 1u);
static const PPCertificateGSLTPatternV1 document_arguments[] = {
    document_term,
};
static const PPCertificateGSLTArticleNodeV1 document_nodes[] = {
    {
        .id = 1u,
        .rule_instance = {
            .rule_id = PP_NAME("given"),
            .arguments = document_arguments,
            .argument_len = 1u,
        },
    },
};
static const PPCertificateGSLTArticleV1 document_article = {
    .version = 1u,
    .nodes = document_nodes,
    .node_len = 1u,
    .root_id = 1u,
    .target = &document_target,
};
static const PPCertificateGSLTPatternV1 unknown_judgment_target =
    PP_APPLY("UndeclaredJudgment", document_target_args, 1u);

static void test_runtime_payload_boundary(void) {
    char error[256];
    PPCertificateGSLTArticleV1Receipt receipt;
    PPCertificateGSLTArticleV1Result result;
    PPCertificateGSLTArticleV1 wrong_judgment = document_article;

    result = check_article(
        &prop_presentation, &document_article, true, NULL,
        &receipt, error);
    expect_result("runtime-payload-is-not-a-base-declaration",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_OK, result, error);

    wrong_judgment.target = &unknown_judgment_target;
    result = check_article(
        &prop_presentation, &wrong_judgment, true, NULL,
        &receipt, error);
    expect_result("runtime-judgment-remains-declared",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED, result, error);
}

static void test_deep_immutable_payload(void) {
    enum { payload_depth = 5000u };
    PPCertificateGSLTPatternV1 *chain =
        calloc(payload_depth + 1u, sizeof(*chain));
    PPCertificateGSLTPatternV1 arguments[1];
    PPCertificateGSLTPatternV1 target_arguments[1];
    PPCertificateGSLTPatternV1 target;
    PPCertificateGSLTArticleNodeV1 node;
    PPCertificateGSLTArticleV1 article;
    PPCertificateGSLTArticleV1Limits limits =
        ppcertificate_gslt_article_v1_default_limits();
    PPCertificateGSLTArticleV1Receipt receipt;
    PPCertificateGSLTArticleV1Result result;
    char error[256];
    uint32_t cursor;

    checks_run++;
    if (!chain) {
        checks_failed++;
        fprintf(stderr, "FAIL deep-payload-fixture-allocation\n");
        return;
    }
    chain[payload_depth] = atom_p;
    cursor = payload_depth;
    while (cursor > 0u) {
        cursor--;
        chain[cursor] = (PPCertificateGSLTPatternV1)PP_APPLY(
            "deep-runtime-payload", &chain[cursor + 1u], 1u);
    }
    arguments[0] = chain[0];
    target_arguments[0] = chain[0];
    target = (PPCertificateGSLTPatternV1)PP_APPLY(
        "Prov", target_arguments, 1u);
    node = (PPCertificateGSLTArticleNodeV1){
        .id = 1u,
        .rule_instance = {
            .rule_id = PP_NAME("given"),
            .arguments = arguments,
            .argument_len = 1u,
        },
    };
    article = (PPCertificateGSLTArticleV1){
        .version = 1u,
        .nodes = &node,
        .node_len = 1u,
        .root_id = 1u,
        .target = &target,
    };

    result = check_article(
        &prop_presentation, &article, true, NULL, &receipt, error);
    expect_result("deep-runtime-payload-uses-explicit-work-stack",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_OK, result, error);

    limits.maximum_pattern_depth = 4096u;
    result = check_article(
        &prop_presentation, &article, true, &limits, &receipt, error);
    expect_result("deep-runtime-payload-remains-resource-bounded",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE, result, error);
    free(chain);
}

static void test_propositional_article(void) {
    char error[256];
    PPCertificateGSLTArticleV1Receipt receipt;
    PPCertificateGSLTArticleV1Result result;
    PPCertificateGSLTArticleV1 target_mutation = prop_article;
    PPCertificateGSLTPresentationV1 deletion = prop_presentation;
    PPCertificateGSLTPatternV1 mutated_second_args[] = {PP_FVAR("Y")};
    PPCertificateGSLTPatternV1 mutated_premises[2];
    PPCertificateGSLTRuleSchemaV1 mutated_rules[2];
    PPCertificateGSLTPresentationV1 premise_mutation = prop_presentation;
    PPCertificateGSLTArticleNodeV1 unrooted_nodes[4];
    PPCertificateGSLTArticleV1 unrooted = prop_article;
    PPCertificateGSLTArticleNodeV1 duplicate_nodes[3];
    PPCertificateGSLTArticleV1 duplicate = prop_article;
    PPCertificateGSLTArticleNodeV1 forward_nodes[3];
    PPCertificateGSLTArticleV1 forward = prop_article;
    PPCertificateGSLTArticleV1Limits small_limits =
        ppcertificate_gslt_article_v1_default_limits();
    PPCertificateGSLTArticleV1Limits sharing_limits =
        ppcertificate_gslt_article_v1_default_limits();

    result = check_article(
        &prop_presentation, &prop_article, true, NULL,
        &receipt, error);
    expect_result("propositional-positive",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_OK, result, error);
    checks_run++;
    if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK ||
        receipt.checked_node_len != 3u || !receipt.rooted) {
        checks_failed++;
        fprintf(stderr, "FAIL propositional-receipt\n");
    }

    target_mutation.target = &prov_p;
    result = check_article(
        &prop_presentation, &target_mutation, true, NULL,
        &receipt, error);
    expect_result("target-falsification",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED, result, error);

    deletion.rule_len = 1u;
    result = check_article(
        &deletion, &prop_article, true, NULL, &receipt, error);
    expect_result("rule-deletion",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED, result, error);

    memcpy(mutated_premises, mp_premises, sizeof(mutated_premises));
    mutated_premises[1] =
        (PPCertificateGSLTPatternV1)PP_APPLY(
            "Prov", mutated_second_args, 1u);
    memcpy(mutated_rules, prop_rules, sizeof(mutated_rules));
    mutated_rules[1].premises = mutated_premises;
    premise_mutation.rules = mutated_rules;
    result = check_article(
        &premise_mutation, &prop_article, true, NULL,
        &receipt, error);
    expect_result("premise-falsification",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED, result, error);

    memcpy(unrooted_nodes, prop_nodes, sizeof(prop_nodes));
    unrooted_nodes[3] = (PPCertificateGSLTArticleNodeV1){
        .id = 40u,
        .rule_instance = {
            .rule_id = PP_NAME("given"),
            .arguments = given_q_arguments,
            .argument_len = 1u,
        },
    };
    unrooted.nodes = unrooted_nodes;
    unrooted.node_len = 4u;
    result = check_article(
        &prop_presentation, &unrooted, false, NULL,
        &receipt, error);
    expect_result("unrooted-compatible-check",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_OK, result, error);
    checks_run++;
    if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK || receipt.rooted) {
        checks_failed++;
        fprintf(stderr, "FAIL unrooted-receipt\n");
    }
    result = check_article(
        &prop_presentation, &unrooted, true, NULL,
        &receipt, error);
    expect_result("rooted-strengthening",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED, result, error);

    memcpy(duplicate_nodes, prop_nodes, sizeof(duplicate_nodes));
    duplicate_nodes[1].id = 10u;
    duplicate.nodes = duplicate_nodes;
    result = check_article(
        &prop_presentation, &duplicate, true, NULL,
        &receipt, error);
    expect_result("duplicate-node",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED, result, error);

    forward_nodes[0] = prop_nodes[2];
    forward_nodes[1] = prop_nodes[0];
    forward_nodes[2] = prop_nodes[1];
    forward.nodes = forward_nodes;
    result = check_article(
        &prop_presentation, &forward, true, NULL, &receipt, error);
    expect_result("forward-node-reference",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED, result, error);

    small_limits.maximum_materialized_pattern_nodes = 1u;
    result = check_article(
        &prop_presentation, &prop_article, true, &small_limits,
        &receipt, error);
    expect_result("materialization-limit",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE, result, error);

    result = check_article(
        &prop_presentation, &document_article, true,
        &sharing_limits, &receipt, error);
    expect_result("immutable-argument-subtree-is-shared",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_OK, result, error);
    checks_run++;
    if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK ||
        receipt.materialized_pattern_node_len != 2u) {
        checks_failed++;
        fprintf(stderr, "FAIL immutable-sharing-receipt\n");
    }
}

static const PPCertificateGSLTConstructorV1 even_constructors[] = {
    {.name = PP_NAME("Z"), .arity = 0u},
    {.name = PP_NAME("S"), .arity = 1u},
};
static const PPCertificateGSLTJudgmentV1 even_judgments[] = {
    {.head = PP_NAME("Even"), .arity = 1u},
};
static const PPCertificateGSLTPatternV1 even_z = PP_APPLY("Z", NULL, 0u);
static const PPCertificateGSLTPatternV1 even_zero_args[] = {even_z};
static const PPCertificateGSLTPatternV1 even_zero_conclusion =
    PP_APPLY("Even", even_zero_args, 1u);
static const PPCertificateGSLTFormalV1 even_step_formals[] = {
    {.name = PP_NAME("N"), .depth = 0u},
};
static const PPCertificateGSLTPatternV1 even_n_args[] = {PP_FVAR("N")};
static const PPCertificateGSLTPatternV1 even_step_premises[] = {
    PP_APPLY("Even", even_n_args, 1u),
};
static const PPCertificateGSLTPatternV1 even_s_n_args[] = {PP_FVAR("N")};
static const PPCertificateGSLTPatternV1 even_s_n =
    PP_APPLY("S", even_s_n_args, 1u);
static const PPCertificateGSLTPatternV1 even_ss_n_args[] = {even_s_n};
static const PPCertificateGSLTPatternV1 even_ss_n =
    PP_APPLY("S", even_ss_n_args, 1u);
static const PPCertificateGSLTPatternV1 even_step_conclusion_args[] = {even_ss_n};
static const PPCertificateGSLTPatternV1 even_step_conclusion =
    PP_APPLY("Even", even_step_conclusion_args, 1u);
static const PPCertificateGSLTRuleSchemaV1 even_rules[] = {
    {
        .id = PP_NAME("zero"),
        .conclusion = &even_zero_conclusion,
    },
    {
        .id = PP_NAME("step-two"),
        .formals = even_step_formals,
        .formal_len = 1u,
        .premises = even_step_premises,
        .premise_len = 1u,
        .conclusion = &even_step_conclusion,
    },
};
static const PPCertificateGSLTPresentationV1 even_presentation = {
    .constructors = even_constructors,
    .constructor_len = 2u,
    .judgments = even_judgments,
    .judgment_len = 1u,
    .rules = even_rules,
    .rule_len = 2u,
};
static const PPCertificateGSLTReferenceV1 even_step_children[] = {
    {.kind = PPCERTIFICATE_GSLT_REFERENCE_V1_NODE, .index = 1u},
};
static const PPCertificateGSLTPatternV1 even_step_arguments[] = {even_z};
static const PPCertificateGSLTPatternV1 even_concrete_s_z_args[] = {even_z};
static const PPCertificateGSLTPatternV1 even_concrete_s_z =
    PP_APPLY("S", even_concrete_s_z_args, 1u);
static const PPCertificateGSLTPatternV1 even_concrete_ss_z_args[] = {
    even_concrete_s_z,
};
static const PPCertificateGSLTPatternV1 even_concrete_ss_z =
    PP_APPLY("S", even_concrete_ss_z_args, 1u);
static const PPCertificateGSLTPatternV1 even_target_args[] = {
    even_concrete_ss_z,
};
static const PPCertificateGSLTPatternV1 even_target =
    PP_APPLY("Even", even_target_args, 1u);
static const PPCertificateGSLTArticleNodeV1 even_nodes[] = {
    {
        .id = 1u,
        .rule_instance = {.rule_id = PP_NAME("zero")},
    },
    {
        .id = 2u,
        .rule_instance = {
            .rule_id = PP_NAME("step-two"),
            .arguments = even_step_arguments,
            .argument_len = 1u,
        },
        .children = even_step_children,
        .child_len = 1u,
    },
};
static const PPCertificateGSLTArticleV1 even_article = {
    .version = 1u,
    .nodes = even_nodes,
    .node_len = 2u,
    .root_id = 2u,
    .target = &even_target,
};

static void test_second_calculus(void) {
    char error[256];
    PPCertificateGSLTArticleV1Receipt receipt;
    PPCertificateGSLTArticleV1Result result = check_article(
        &even_presentation, &even_article, true, NULL,
        &receipt, error);
    expect_result("independent-even-calculus",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_OK, result, error);
}

static const PPCertificateGSLTConstructorV1 binder_constructors[] = {
    {.name = PP_NAME("K"), .arity = 0u},
    {.name = PP_NAME("L"), .arity = 0u},
};
static const PPCertificateGSLTJudgmentV1 binder_judgments[] = {
    {.head = PP_NAME("Rel"), .arity = 2u},
};
static const PPCertificateGSLTFormalV1 binder_formals[] = {
    {.name = PP_NAME("body"), .depth = 1u},
    {.name = PP_NAME("replacement"), .depth = 0u},
    {.name = PP_NAME("result"), .depth = 0u},
};
static const PPCertificateGSLTPatternV1 binder_schema_body = PP_FVAR("body");
static const PPCertificateGSLTPatternV1 binder_schema_replacement =
    PP_FVAR("replacement");
static const PPCertificateGSLTPatternV1 binder_schema_subst = {
    .kind = PPCERTIFICATE_GSLT_PATTERN_V1_SUBST,
    .as.subst = {
        .body = &binder_schema_body,
        .replacement = &binder_schema_replacement,
    },
};
static const PPCertificateGSLTPatternV1 binder_conclusion_args[] = {
    binder_schema_subst,
    PP_FVAR("result"),
};
static const PPCertificateGSLTPatternV1 binder_conclusion =
    PP_APPLY("Rel", binder_conclusion_args, 2u);
static const PPCertificateGSLTSideConditionV1 binder_conditions[] = {
    {
        .kind = PPCERTIFICATE_GSLT_SIDE_CONDITION_V1_EXPLICIT_SUBSTITUTION,
        .ambient_depth = 0u,
        .body_argument = 0u,
        .replacement_argument = 1u,
        .result_argument = 2u,
    },
};
static const PPCertificateGSLTRuleSchemaV1 binder_rules[] = {
    {
        .id = PP_NAME("subst-certificate"),
        .formals = binder_formals,
        .formal_len = 3u,
        .conclusion = &binder_conclusion,
        .side_conditions = binder_conditions,
        .side_condition_len = 1u,
    },
};
static const PPCertificateGSLTPresentationV1 binder_presentation = {
    .constructors = binder_constructors,
    .constructor_len = 2u,
    .judgments = binder_judgments,
    .judgment_len = 1u,
    .rules = binder_rules,
    .rule_len = 1u,
    .required_capabilities =
        PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_EXPLICIT_SUBSTITUTION |
        PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_SUBSTITUTION_CONDITION,
};
static const PPCertificateGSLTPatternV1 binder_k = PP_APPLY("K", NULL, 0u);
static const PPCertificateGSLTPatternV1 binder_l = PP_APPLY("L", NULL, 0u);
static const PPCertificateGSLTPatternV1 binder_arguments[] = {
    PP_BVAR(0u),
    binder_k,
    binder_k,
};
static const PPCertificateGSLTPatternV1 binder_bad_arguments[] = {
    PP_BVAR(0u),
    binder_k,
    binder_l,
};
static const PPCertificateGSLTPatternV1 binder_target_subst_body = PP_BVAR(0u);
static const PPCertificateGSLTPatternV1 binder_target_subst_replacement =
    PP_APPLY("K", NULL, 0u);
static const PPCertificateGSLTPatternV1 binder_target_subst = {
    .kind = PPCERTIFICATE_GSLT_PATTERN_V1_SUBST,
    .as.subst = {
        .body = &binder_target_subst_body,
        .replacement = &binder_target_subst_replacement,
    },
};
static const PPCertificateGSLTPatternV1 binder_target_args[] = {
    binder_target_subst,
    binder_k,
};
static const PPCertificateGSLTPatternV1 binder_target =
    PP_APPLY("Rel", binder_target_args, 2u);
static const PPCertificateGSLTPatternV1 binder_bad_target_args[] = {
    binder_target_subst,
    binder_l,
};
static const PPCertificateGSLTPatternV1 binder_bad_target =
    PP_APPLY("Rel", binder_bad_target_args, 2u);
static const PPCertificateGSLTArticleNodeV1 binder_nodes[] = {
    {
        .id = 1u,
        .rule_instance = {
            .rule_id = PP_NAME("subst-certificate"),
            .arguments = binder_arguments,
            .argument_len = 3u,
        },
    },
};
static const PPCertificateGSLTArticleNodeV1 binder_bad_nodes[] = {
    {
        .id = 1u,
        .rule_instance = {
            .rule_id = PP_NAME("subst-certificate"),
            .arguments = binder_bad_arguments,
            .argument_len = 3u,
        },
    },
};
static const PPCertificateGSLTArticleV1 binder_article = {
    .version = 1u,
    .nodes = binder_nodes,
    .node_len = 1u,
    .root_id = 1u,
    .target = &binder_target,
};
static const PPCertificateGSLTArticleV1 binder_bad_article = {
    .version = 1u,
    .nodes = binder_bad_nodes,
    .node_len = 1u,
    .root_id = 1u,
    .target = &binder_bad_target,
};

static void test_structural_side_condition(void) {
    char error[256];
    PPCertificateGSLTArticleV1Receipt receipt;
    PPCertificateGSLTArticleV1Result result;
    PPCertificateGSLTPresentationV1 omitted_capability = binder_presentation;

    result = check_article(
        &binder_presentation, &binder_article, true, NULL,
        &receipt, error);
    expect_result("explicit-substitution-positive",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_OK, result, error);
    result = check_article(
        &binder_presentation, &binder_bad_article, true, NULL,
        &receipt, error);
    expect_result("explicit-substitution-negative",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED, result, error);

    omitted_capability.required_capabilities = 0u;
    result = check_article(
        &omitted_capability, &binder_article, true, NULL,
        &receipt, error);
    expect_result("undeclared-capability",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID, result, error);
}

static void test_unused_binder_side_condition(void) {
    static const PPCertificateGSLTFormalV1 formals[] = {
        {.name = PP_NAME("body"), .depth = 1u},
        {.name = PP_NAME("result"), .depth = 0u},
    };
    static const PPCertificateGSLTPatternV1 schema_body = PP_FVAR("body");
    static const PPCertificateGSLTPatternV1 schema_lambda = {
        .kind = PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA,
        .as.lambda = {.body = &schema_body},
    };
    static const PPCertificateGSLTPatternV1 conclusion_args[] = {
        schema_lambda,
        PP_FVAR("result"),
    };
    static const PPCertificateGSLTPatternV1 conclusion =
        PP_APPLY("Rel", conclusion_args, 2u);
    static const PPCertificateGSLTSideConditionV1 conditions[] = {
        {
            .kind =
                PPCERTIFICATE_GSLT_SIDE_CONDITION_V1_UNUSED_BINDER_ELIMINATION,
            .ambient_depth = 0u,
            .body_argument = 0u,
            .result_argument = 1u,
        },
    };
    static const PPCertificateGSLTRuleSchemaV1 rules[] = {
        {
            .id = PP_NAME("drop-unused"),
            .formals = formals,
            .formal_len = 2u,
            .conclusion = &conclusion,
            .side_conditions = conditions,
            .side_condition_len = 1u,
        },
    };
    static const PPCertificateGSLTPresentationV1 presentation = {
        .constructors = binder_constructors,
        .constructor_len = 2u,
        .judgments = binder_judgments,
        .judgment_len = 1u,
        .rules = rules,
        .rule_len = 1u,
        .required_capabilities =
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_BINDERS |
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_UNUSED_BINDER_CONDITION,
    };
    static const PPCertificateGSLTPatternV1 good_arguments[] = {
        PP_APPLY("K", NULL, 0u),
        PP_APPLY("K", NULL, 0u),
    };
    static const PPCertificateGSLTPatternV1 bad_arguments[] = {
        PP_BVAR(0u),
        PP_APPLY("K", NULL, 0u),
    };
    static const PPCertificateGSLTPatternV1 target_lambda_body =
        PP_APPLY("K", NULL, 0u);
    static const PPCertificateGSLTPatternV1 target_lambda = {
        .kind = PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA,
        .as.lambda = {.body = &target_lambda_body},
    };
    static const PPCertificateGSLTPatternV1 target_args[] = {
        target_lambda,
        PP_APPLY("K", NULL, 0u),
    };
    static const PPCertificateGSLTPatternV1 target =
        PP_APPLY("Rel", target_args, 2u);
    static const PPCertificateGSLTPatternV1 bad_target_lambda_body = PP_BVAR(0u);
    static const PPCertificateGSLTPatternV1 bad_target_lambda = {
        .kind = PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA,
        .as.lambda = {.body = &bad_target_lambda_body},
    };
    static const PPCertificateGSLTPatternV1 bad_target_args[] = {
        bad_target_lambda,
        PP_APPLY("K", NULL, 0u),
    };
    static const PPCertificateGSLTPatternV1 bad_target =
        PP_APPLY("Rel", bad_target_args, 2u);
    static const PPCertificateGSLTArticleNodeV1 good_nodes[] = {
        {
            .id = 1u,
            .rule_instance = {
                .rule_id = PP_NAME("drop-unused"),
                .arguments = good_arguments,
                .argument_len = 2u,
            },
        },
    };
    static const PPCertificateGSLTArticleNodeV1 bad_nodes[] = {
        {
            .id = 1u,
            .rule_instance = {
                .rule_id = PP_NAME("drop-unused"),
                .arguments = bad_arguments,
                .argument_len = 2u,
            },
        },
    };
    static const PPCertificateGSLTArticleV1 good_article = {
        .version = 1u,
        .nodes = good_nodes,
        .node_len = 1u,
        .root_id = 1u,
        .target = &target,
    };
    static const PPCertificateGSLTArticleV1 bad_article = {
        .version = 1u,
        .nodes = bad_nodes,
        .node_len = 1u,
        .root_id = 1u,
        .target = &bad_target,
    };
    char error[256];
    PPCertificateGSLTArticleV1Receipt receipt;
    PPCertificateGSLTArticleV1Result result;

    result = check_article(
        &presentation, &good_article, true, NULL, &receipt, error);
    expect_result("unused-binder-positive",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_OK, result, error);
    result = check_article(
        &presentation, &bad_article, true, NULL, &receipt, error);
    expect_result("unused-binder-negative",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED, result, error);
}

static void test_open_article(void) {
    static const PPCertificateGSLTPatternV1 identity_premises[] = {
        PP_APPLY("Prov", prov_p_args, 1u),
    };
    static const PPCertificateGSLTRuleSchemaV1 identity_rules[] = {
        {
            .id = PP_NAME("identity"),
            .premises = identity_premises,
            .premise_len = 1u,
            .conclusion = &prov_p,
        },
    };
    static const PPCertificateGSLTPresentationV1 identity_presentation = {
        .constructors = prop_constructors,
        .constructor_len = 3u,
        .judgments = prop_judgments,
        .judgment_len = 1u,
        .rules = identity_rules,
        .rule_len = 1u,
    };
    static const PPCertificateGSLTReferenceV1 identity_children[] = {
        {.kind = PPCERTIFICATE_GSLT_REFERENCE_V1_PREMISE, .index = 0u},
    };
    static const PPCertificateGSLTArticleNodeV1 identity_nodes[] = {
        {
            .id = 1u,
            .rule_instance = {.rule_id = PP_NAME("identity")},
            .children = identity_children,
            .child_len = 1u,
        },
    };
    static const PPCertificateGSLTArticleV1 identity_article = {
        .version = 1u,
        .nodes = identity_nodes,
        .node_len = 1u,
        .root_id = 1u,
        .target = &prov_p,
    };
    char error[256] = {0};
    PPCertificateGSLTArticleV1Receipt receipt;
    PPCertificateGSLTArticleV1Result result =
        ppcertificate_gslt_article_v1_check_open(
            &identity_presentation, &prov_p, 1u,
            &identity_article, true, NULL, &receipt,
            error, sizeof(error));
    expect_result("open-context-reference",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_OK, result, error);
}

static void test_admission_failures(void) {
    char error[256] = {0};
    PPCertificateGSLTArticleV1Result result;
    uint32_t capabilities = 0u;
    PPCertificateGSLTFormalV1 wrong_depth_formals[] = {
        {.name = PP_NAME("X"), .depth = 1u},
    };
    PPCertificateGSLTRuleSchemaV1 wrong_depth_rules[] = {prop_rules[0]};
    PPCertificateGSLTPresentationV1 wrong_depth = prop_presentation;
    PPCertificateGSLTPatternV1 rest_element = atom_p;
    PPCertificateGSLTPatternV1 rest_collection = {
        .kind = PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION,
        .as.collection = {
            .collection_kind = PPCERTIFICATE_GSLT_COLLECTION_V1_VECTOR,
            .elements = &rest_element,
            .element_len = 1u,
            .rest_present = true,
            .rest = PP_NAME("tail"),
        },
    };
    PPCertificateGSLTPatternV1 rest_conclusion_args[] = {rest_collection};
    PPCertificateGSLTPatternV1 rest_conclusion =
        (PPCertificateGSLTPatternV1)PP_APPLY(
            "Prov", rest_conclusion_args, 1u);
    PPCertificateGSLTRuleSchemaV1 rest_rules[] = {
        {
            .id = PP_NAME("rest-rule"),
            .conclusion = &rest_conclusion,
        },
    };
    PPCertificateGSLTPresentationV1 rest_presentation = prop_presentation;
    PPCertificateGSLTArticleV1Limits tiny_limits =
        ppcertificate_gslt_article_v1_default_limits();
    PPCertificateGSLTSideConditionV1 overflow_condition = binder_conditions[0];
    PPCertificateGSLTRuleSchemaV1 overflow_rule = binder_rules[0];
    PPCertificateGSLTPresentationV1 overflow_presentation = binder_presentation;
    PPCertificateGSLTPatternV1 unknown_fixed =
        (PPCertificateGSLTPatternV1)PP_APPLY(
            "UndeclaredFixedConstructor", NULL, 0u);
    PPCertificateGSLTPatternV1 unknown_fixed_args[] = {unknown_fixed};
    PPCertificateGSLTPatternV1 unknown_fixed_conclusion =
        (PPCertificateGSLTPatternV1)PP_APPLY(
            "Prov", unknown_fixed_args, 1u);
    PPCertificateGSLTRuleSchemaV1 unknown_fixed_rule = {
        .id = PP_NAME("unknown-fixed"),
        .conclusion = &unknown_fixed_conclusion,
    };
    PPCertificateGSLTPresentationV1 unknown_fixed_presentation =
        prop_presentation;

    wrong_depth_rules[0].formals = wrong_depth_formals;
    wrong_depth.rules = wrong_depth_rules;
    wrong_depth.rule_len = 1u;
    result = ppcertificate_gslt_article_v1_presentation_validate(
        &wrong_depth, NULL, &capabilities, error, sizeof(error));
    expect_result("formal-depth-admission",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID, result, error);

    memset(error, 0, sizeof(error));
    rest_presentation.rules = rest_rules;
    rest_presentation.rule_len = 1u;
    rest_presentation.required_capabilities =
        PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_COLLECTIONS;
    result = ppcertificate_gslt_article_v1_presentation_validate(
        &rest_presentation, NULL, &capabilities,
        error, sizeof(error));
    expect_result("collection-rest-refusal",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED, result, error);

    tiny_limits.maximum_presentation_pattern_nodes = 1u;
    result = ppcertificate_gslt_article_v1_presentation_validate(
        &prop_presentation, &tiny_limits, &capabilities, NULL, 0u);
    expect_result("resource-result-without-diagnostic-buffer",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE, result, NULL);

    overflow_condition.ambient_depth = UINT32_MAX;
    overflow_rule.side_conditions = &overflow_condition;
    overflow_presentation.rules = &overflow_rule;
    overflow_presentation.rule_len = 1u;
    result = ppcertificate_gslt_article_v1_presentation_validate(
        &overflow_presentation, NULL, &capabilities,
        error, sizeof(error));
    expect_result("side-condition-depth-overflow",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID, result, error);

    unknown_fixed_presentation.rules = &unknown_fixed_rule;
    unknown_fixed_presentation.rule_len = 1u;
    result = ppcertificate_gslt_article_v1_presentation_validate(
        &unknown_fixed_presentation, NULL, &capabilities,
        error, sizeof(error));
    expect_result("fixed-schema-constructor-remains-declared",
                  PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID, result, error);
}

int main(void) {
    test_propositional_article();
    test_runtime_payload_boundary();
    test_deep_immutable_payload();
    test_second_calculus();
    test_structural_side_condition();
    test_unused_binder_side_condition();
    test_open_article();
    test_admission_failures();
    printf("(CertificateGSLTArticleV1Summary %u %u %u)\n",
           checks_run, checks_run - checks_failed, checks_failed);
    return checks_failed == 0u ? 0 : 1;
}
