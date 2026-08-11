#include "proof_gslt_sequence_evidence_v1.h"

#include "atom.h"
#include "symbol.h"

#include <stdio.h>
#include <string.h>

static int passed;
static int failed;

static void check(bool condition, const char *name) {
    if (condition) {
        passed++;
        printf("PASS: %s\n", name);
    } else {
        failed++;
        printf("FAIL: %s\n", name);
    }
}

static bool name_is(PPProofGSLTNameV1 name, const char *text) {
    size_t len = strlen(text);

    return len <= UINT32_MAX && name.len == (uint32_t)len &&
           memcmp(name.bytes, text, len) == 0;
}

static PPProofGSLTNameV1 text_name(const char *text) {
    return (PPProofGSLTNameV1){
        .bytes = (const uint8_t *)text,
        .len = (uint32_t)strlen(text),
    };
}

static void pattern_apply(
    PPProofGSLTPatternV1 *out,
    PPProofGSLTPatternV1 *argument_storage,
    PPProofGSLTNameV1 head,
    const PPProofGSLTPatternV1 *const *arguments,
    uint32_t argument_len) {
    uint32_t index;

    memset(out, 0, sizeof(*out));
    for (index = 0u; index < argument_len; index++)
        argument_storage[index] = *arguments[index];
    out->kind = PPPROOF_GSLT_PATTERN_V1_APPLY;
    out->as.apply.constructor = head;
    out->as.apply.arguments = argument_storage;
    out->as.apply.argument_len = argument_len;
}

static const PPProofGSLTPatternV1 *pattern_sequence(
    PPProofGSLTPatternV1 *term_storage,
    PPProofGSLTPatternV1 (*argument_storage)[2],
    PPProofGSLTNameV1 nil_constructor,
    PPProofGSLTNameV1 cons_constructor,
    const PPProofGSLTPatternV1 *const *items,
    uint32_t item_len) {
    const PPProofGSLTPatternV1 *arguments[2];
    uint32_t cursor;

    pattern_apply(
        &term_storage[item_len], NULL, nil_constructor, NULL, 0u);
    cursor = item_len;
    while (cursor > 0u) {
        cursor--;
        arguments[0] = items[cursor];
        arguments[1] = &term_storage[cursor + 1u];
        pattern_apply(
            &term_storage[cursor], argument_storage[cursor],
            cons_constructor, arguments, 2u);
    }
    return &term_storage[0];
}

typedef struct {
    const PPProofGSLTSequenceTokenV1 *x;
    const PPProofGSLTSequenceTokenV1 *y;
    const PPProofGSLTSequenceTokenV1 *a;
    const PPProofGSLTSequenceTokenV1 *c;
    bool wrong_apart;
} EvidenceFixture;

static bool fixture_different(
    void *context,
    const PPProofGSLTSequenceTokenV1 *left,
    const PPProofGSLTSequenceTokenV1 *right,
    PPProofGSLTReferenceV1 *evidence_out) {
    EvidenceFixture *fixture = context;

    if (!fixture || left != fixture->x || right != fixture->y)
        return false;
    *evidence_out = (PPProofGSLTReferenceV1){
        .kind = PPPROOF_GSLT_REFERENCE_V1_PREMISE,
        .index = 5u,
    };
    return true;
}

static bool fixture_apart(
    void *context,
    const PPProofGSLTSequenceTokenV1 *left,
    const PPProofGSLTSequenceTokenV1 *right,
    PPProofGSLTReferenceV1 *evidence_out) {
    EvidenceFixture *fixture = context;

    if (!fixture || left != fixture->a || right != fixture->c)
        return false;
    *evidence_out = (PPProofGSLTReferenceV1){
        .kind = PPPROOF_GSLT_REFERENCE_V1_PREMISE,
        .index = fixture->wrong_apart ? 5u : 6u,
    };
    return true;
}

static PPProofGSLTArticleV1Result check_produced_article(
    const PPProofGSLTPlanV1 *plan,
    const PPProofGSLTPatternV1 *context,
    uint32_t context_len,
    const PPProofGSLTSequenceEvidenceProducerV1 *producer,
    PPProofGSLTSequenceProofV1 proof,
    PPProofGSLTArticleV1Receipt *receipt,
    char *error,
    size_t error_size) {
    PPProofGSLTArticleV1 article = {
        .version = 1u,
        .nodes = producer->nodes,
        .node_len = producer->node_len,
        .root_id = proof.evidence.index,
        .target = proof.goal,
    };

    return ppproof_gslt_article_v1_check_open(
        &plan->presentation, context, context_len, &article, true,
        NULL, receipt, error, error_size);
}

static void check_evidence_producer(
    const PPProofGSLTPlanV1 *plan,
    const PPProofGSLTSequenceEvidenceABIV1 *abi) {
    PPProofGSLTPatternV1 token_terms[5];
    PPProofGSLTPatternV1 typecode_term;
    PPProofGSLTPatternV1 context[11];
    PPProofGSLTPatternV1 context_arguments[11][2];
    const PPProofGSLTPatternV1 *arguments[2];
    PPProofGSLTSequenceTokenV1 tokens[5];
    const PPProofGSLTSequenceTokenV1 *template_tokens[3];
    const PPProofGSLTSequenceTokenV1 *image_a_tokens[1];
    const PPProofGSLTSequenceTokenV1 *image_c_tokens[1];
    const PPProofGSLTSequenceTokenV1 *left_tokens[1];
    const PPProofGSLTSequenceTokenV1 *right_tokens[1];
    PPProofGSLTSequenceBindingV1 bindings[2];
    PPProofGSLTSequenceEnvironmentV1 environment;
    PPProofGSLTSequenceEvidenceSourcesV1 sources;
    PPProofGSLTSequenceEvidenceProducerV1 producer;
    PPProofGSLTMaterializedSequenceV1 result_sequence;
    PPProofGSLTSequenceProofV1 proof;
    PPProofGSLTArticleV1Receipt receipt;
    PPProofGSLTArticleV1Result result;
    EvidenceFixture fixture;
    char error[512] = {0};
    uint32_t index;
    static const char *const token_names[5] = {
        "SequenceLiteralV1", "SequenceXv1", "SequenceYv1",
        "SequenceAv1", "SequenceCv1",
    };

    for (index = 0u; index < 5u; index++)
        pattern_apply(
            &token_terms[index], NULL, text_name(token_names[index]),
            NULL, 0u);
    memset(tokens, 0, sizeof(tokens));
    for (index = 0u; index < 5u; index++)
        tokens[index].term = &token_terms[index];
    tokens[0].literal = true;
    tokens[0].literal_evidence = (PPProofGSLTReferenceV1){
        PPPROOF_GSLT_REFERENCE_V1_PREMISE, 0u};
    for (index = 1u; index < 5u; index++) {
        tokens[index].variable = true;
        tokens[index].variable_evidence = (PPProofGSLTReferenceV1){
            PPPROOF_GSLT_REFERENCE_V1_PREMISE, index};
    }

    arguments[0] = &token_terms[0];
    pattern_apply(
        &context[0], context_arguments[0],
        abi->judgments[PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LITERAL],
        arguments, 1u);
    for (index = 1u; index < 5u; index++) {
        arguments[0] = &token_terms[index];
        pattern_apply(
            &context[index], context_arguments[index],
            abi->judgments[PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_VARIABLE],
            arguments, 1u);
    }
    arguments[0] = &token_terms[1];
    arguments[1] = &token_terms[2];
    pattern_apply(
        &context[5], context_arguments[5],
        abi->judgments[PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_DIFFERENT],
        arguments, 2u);
    arguments[0] = &token_terms[3];
    arguments[1] = &token_terms[4];
    pattern_apply(
        &context[6], context_arguments[6],
        abi->judgments[PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_APART],
        arguments, 2u);

    template_tokens[0] = &tokens[0];
    template_tokens[1] = &tokens[1];
    template_tokens[2] = &tokens[2];
    image_a_tokens[0] = &tokens[3];
    image_c_tokens[0] = &tokens[4];
    bindings[0] = (PPProofGSLTSequenceBindingV1){
        .key = &tokens[1],
        .image = {image_a_tokens, 1u},
    };
    bindings[1] = (PPProofGSLTSequenceBindingV1){
        .key = &tokens[2],
        .image = {image_c_tokens, 1u},
    };
    environment = (PPProofGSLTSequenceEnvironmentV1){bindings, 2u};
    fixture = (EvidenceFixture){
        .x = &tokens[1], .y = &tokens[2],
        .a = &tokens[3], .c = &tokens[4],
    };
    sources = (PPProofGSLTSequenceEvidenceSourcesV1){
        .context = &fixture,
        .different = fixture_different,
        .apart = fixture_apart,
    };

    ppproof_gslt_sequence_evidence_producer_v1_init(&producer);
    result = ppproof_gslt_sequence_evidence_producer_v1_begin(
        &producer, abi, 0u, NULL, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "generated sequence producer starts");
    result = ppproof_gslt_sequence_evidence_producer_v1_instantiate(
        &producer,
        (PPProofGSLTTokenSequenceV1){template_tokens, 3u},
        environment, &sources, &result_sequence, &proof,
        error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "generated ABI produces instantiation evidence");
    check(result_sequence.token_len == 3u &&
              result_sequence.tokens[0] == &tokens[0] &&
              result_sequence.tokens[1] == &tokens[3] &&
              result_sequence.tokens[2] == &tokens[4],
          "sequence producer performs declared splicing");
    result = check_produced_article(
        plan, context, 7u, &producer, proof, &receipt,
        error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
              receipt.checked_node_len == producer.node_len &&
              receipt.rooted,
          "generic article checker accepts produced instantiation evidence");
    ppproof_gslt_sequence_evidence_producer_v1_free(&producer);

    left_tokens[0] = &tokens[3];
    right_tokens[0] = &tokens[4];
    ppproof_gslt_sequence_evidence_producer_v1_init(&producer);
    result = ppproof_gslt_sequence_evidence_producer_v1_begin(
        &producer, abi, 0u, NULL, error, sizeof(error));
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_sequence_evidence_producer_v1_support_apart(
            &producer,
            (PPProofGSLTTokenSequenceV1){left_tokens, 1u},
            (PPProofGSLTTokenSequenceV1){right_tokens, 1u},
            &sources, &proof, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "generated ABI produces support-apartness evidence");
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = check_produced_article(
            plan, context, 7u, &producer, proof, &receipt,
            error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
              receipt.checked_node_len == 5u,
          "generic article checker accepts produced apartness evidence");
    ppproof_gslt_sequence_evidence_producer_v1_free(&producer);

    {
        PPProofGSLTPatternV1 sequence_nil;
        PPProofGSLTPatternV1 sequence_cons;
        PPProofGSLTPatternV1 sequence_cons_arguments[2];
        PPProofGSLTPatternV1 provable_arguments[1];
        const PPProofGSLTPatternV1 *sequence_items[2];

        pattern_apply(
            &sequence_nil, NULL,
            abi->constructors[
                PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_NIL],
            NULL, 0u);
        sequence_items[0] = &token_terms[3];
        sequence_items[1] = &sequence_nil;
        pattern_apply(
            &sequence_cons, sequence_cons_arguments,
            abi->constructors[
                PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_CONS],
            sequence_items, 2u);
        arguments[0] = &sequence_cons;
        pattern_apply(
            &context[7], provable_arguments,
            abi->assertion_judgments[
                PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_PROVABLE],
            arguments, 1u);
        ppproof_gslt_sequence_evidence_producer_v1_init(&producer);
        result = ppproof_gslt_sequence_evidence_producer_v1_begin(
            &producer, abi, 0u, NULL, error, sizeof(error));
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
            result =
                ppproof_gslt_sequence_evidence_producer_v1_use_premise(
                    &producer,
                    (PPProofGSLTTokenSequenceV1){left_tokens, 1u},
                    (PPProofGSLTReferenceV1){
                        PPPROOF_GSLT_REFERENCE_V1_PREMISE, 7u},
                    &proof, error, sizeof(error));
        check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
              "generated ABI roots an explicit active premise");
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
            result = check_produced_article(
                plan, context, 8u, &producer, proof, &receipt,
                error, sizeof(error));
        check(result == PPPROOF_GSLT_ARTICLE_V1_OK && receipt.rooted,
              "generic article checker accepts explicit premise use");
        context[7] = context[0];
        result = check_produced_article(
            plan, context, 8u, &producer, proof, &receipt,
            error, sizeof(error));
        check(result == PPPROOF_GSLT_ARTICLE_V1_REJECTED,
              "falsifying the active premise kills the same article");
        ppproof_gslt_sequence_evidence_producer_v1_free(&producer);
    }

    {
        PPProofGSLTArticleV1Limits sharing_limits =
            ppproof_gslt_article_v1_default_limits();
        uint32_t repetition;

        sharing_limits.maximum_materialized_pattern_nodes = 10u;
        ppproof_gslt_sequence_evidence_producer_v1_init(&producer);
        result = ppproof_gslt_sequence_evidence_producer_v1_begin(
            &producer, abi, 0u, &sharing_limits, error, sizeof(error));
        for (repetition = 0u;
             repetition < 2u &&
             result == PPPROOF_GSLT_ARTICLE_V1_OK;
             repetition++) {
            result = ppproof_gslt_sequence_evidence_producer_v1_use_premise(
                &producer,
                (PPProofGSLTTokenSequenceV1){left_tokens, 1u},
                (PPProofGSLTReferenceV1){
                    PPPROOF_GSLT_REFERENCE_V1_PREMISE, 7u},
                &proof, error, sizeof(error));
        }
        check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
                  producer.node_len == 2u,
              "repeated immutable sequences share their canonical spine");
        ppproof_gslt_sequence_evidence_producer_v1_free(&producer);

        sharing_limits.maximum_materialized_pattern_nodes = 9u;
        ppproof_gslt_sequence_evidence_producer_v1_init(&producer);
        result = ppproof_gslt_sequence_evidence_producer_v1_begin(
            &producer, abi, 0u, &sharing_limits, error, sizeof(error));
        for (repetition = 0u;
             repetition < 2u &&
             result == PPPROOF_GSLT_ARTICLE_V1_OK;
             repetition++) {
            result = ppproof_gslt_sequence_evidence_producer_v1_use_premise(
                &producer,
                (PPProofGSLTTokenSequenceV1){left_tokens, 1u},
                (PPProofGSLTReferenceV1){
                    PPPROOF_GSLT_REFERENCE_V1_PREMISE, 7u},
                &proof, error, sizeof(error));
        }
        check(result == PPPROOF_GSLT_ARTICLE_V1_RESOURCE,
              "canonical sequence sharing preserves the pattern budget");
        ppproof_gslt_sequence_evidence_producer_v1_free(&producer);
    }

    {
        PPProofGSLTPatternV1 sequence_terms[3][3];
        PPProofGSLTPatternV1 sequence_arguments[3][3][2];
        const PPProofGSLTPatternV1 *sequence_items[3][2];
        const PPProofGSLTPatternV1 *sequence_term;
        const PPProofGSLTSequenceTokenV1 *essential_tokens[2];
        PPProofGSLTAssertionBindingV1 assertion_bindings[2];
        PPProofGSLTAssertionEssentialV1 assertion_essentials[1];
        PPProofGSLTAssertionDisjointV1 assertion_disjoints[1];
        PPProofGSLTAssertionDeclarationV1 declaration;
        PPProofGSLTAssertionApplicationV1 application;

        pattern_apply(
            &typecode_term, NULL, text_name("SequenceWffV1"), NULL, 0u);
        sequence_items[0][0] = &typecode_term;
        sequence_items[0][1] = &token_terms[3];
        sequence_items[1][0] = &typecode_term;
        sequence_items[1][1] = &token_terms[4];
        sequence_items[2][0] = &token_terms[3];
        sequence_items[2][1] = &token_terms[4];
        for (index = 0u; index < 3u; index++) {
            sequence_term = pattern_sequence(
                sequence_terms[index], sequence_arguments[index],
                abi->constructors[
                    PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_NIL],
                abi->constructors[
                    PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_CONS],
                sequence_items[index], 2u);
            arguments[0] = sequence_term;
            pattern_apply(
                &context[7u + index], context_arguments[7u + index],
                abi->assertion_judgments[
                    PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_PROVABLE],
                arguments, 1u);
        }

        assertion_bindings[0] = (PPProofGSLTAssertionBindingV1){
            .variable = &tokens[1],
            .typecode = &typecode_term,
            .image = {image_a_tokens, 1u},
            .floating_proof = {
                PPPROOF_GSLT_REFERENCE_V1_PREMISE, 7u},
        };
        assertion_bindings[1] = (PPProofGSLTAssertionBindingV1){
            .variable = &tokens[2],
            .typecode = &typecode_term,
            .image = {image_c_tokens, 1u},
            .floating_proof = {
                PPPROOF_GSLT_REFERENCE_V1_PREMISE, 8u},
        };
        essential_tokens[0] = &tokens[1];
        essential_tokens[1] = &tokens[2];
        assertion_essentials[0] = (PPProofGSLTAssertionEssentialV1){
            .template_sequence = {essential_tokens, 2u},
            .actual_proof = {
                PPPROOF_GSLT_REFERENCE_V1_PREMISE, 9u},
        };
        assertion_disjoints[0] = (PPProofGSLTAssertionDisjointV1){
            .left = &tokens[1],
            .right = &tokens[2],
        };
        declaration = (PPProofGSLTAssertionDeclarationV1){
            .bindings = assertion_bindings,
            .binding_len = 2u,
            .essentials = assertion_essentials,
            .essential_len = 1u,
            .disjoints = assertion_disjoints,
            .disjoint_len = 1u,
            .conclusion_type = &typecode_term,
            .conclusion_template = {template_tokens, 3u},
        };

        ppproof_gslt_sequence_evidence_producer_v1_init(&producer);
        result = ppproof_gslt_sequence_evidence_producer_v1_begin(
            &producer, abi, 0u, NULL, error, sizeof(error));
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
            result = ppproof_gslt_sequence_evidence_producer_v1_apply_assertion(
                &producer, &declaration, 10u, &sources, &application,
                error, sizeof(error));
        check(result == PPPROOF_GSLT_ARTICLE_V1_OK &&
                  application.result.token_len == 3u &&
                  application.result.tokens[0] == &tokens[0] &&
                  application.result.tokens[1] == &tokens[3] &&
                  application.result.tokens[2] == &tokens[4],
              "generated ABI produces a universal assertion application");
        if (result == PPPROOF_GSLT_ARTICLE_V1_OK) {
            context[10] = *application.declared_goal;
            result = check_produced_article(
                plan, context, 11u, &producer, application.proof,
                &receipt, error, sizeof(error));
        }
        check(result == PPPROOF_GSLT_ARTICLE_V1_OK && receipt.rooted,
              "generic article checker accepts the declared assertion");
        if (application.declared_goal) {
            context[10] = context[0];
            result = check_produced_article(
                plan, context, 11u, &producer, application.proof,
                &receipt, error, sizeof(error));
        }
        check(result == PPPROOF_GSLT_ARTICLE_V1_REJECTED,
              "falsifying the declaration makes the same article fail");
        ppproof_gslt_sequence_evidence_producer_v1_free(&producer);
    }

    fixture.wrong_apart = true;
    ppproof_gslt_sequence_evidence_producer_v1_init(&producer);
    result = ppproof_gslt_sequence_evidence_producer_v1_begin(
        &producer, abi, 0u, NULL, error, sizeof(error));
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = ppproof_gslt_sequence_evidence_producer_v1_support_apart(
            &producer,
            (PPProofGSLTTokenSequenceV1){left_tokens, 1u},
            (PPProofGSLTTokenSequenceV1){right_tokens, 1u},
            &sources, &proof, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "untrusted producer may emit a structurally formed article");
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        result = check_produced_article(
            plan, context, 7u, &producer, proof, &receipt,
            error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_REJECTED,
          "generic article checker rejects a lying evidence source");
    ppproof_gslt_sequence_evidence_producer_v1_free(&producer);
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    PPProofGSLTPlanV1 sequence_plan;
    PPProofGSLTPlanV1 other_plan;
    PPProofGSLTSequenceEvidenceABIV1 abi;
    PPProofGSLTArticleV1Result result;
    char error[512];

    if (argc != 5) {
        fprintf(stderr,
                "usage: %s SEQUENCE_PLAN SEQUENCE_ABI OTHER_PLAN INCOMPLETE_ABI\n",
                argv[0]);
        return 2;
    }
    ppproof_gslt_plan_v1_init(&sequence_plan);
    ppproof_gslt_plan_v1_init(&other_plan);
    ppproof_gslt_sequence_evidence_abi_v1_init(&abi);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    result = ppproof_gslt_plan_v1_load(
        &sequence_plan, argv[1], NULL, error, sizeof(error));
    if (result != PPPROOF_GSLT_ARTICLE_V1_OK)
        fprintf(stderr, "sequence plan: %s\n", error);
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "compiled sequence proof plan loads");
    result = ppproof_gslt_sequence_evidence_abi_v1_load(
        &abi, argv[2], &sequence_plan, error, sizeof(error));
    if (result != PPPROOF_GSLT_ARTICLE_V1_OK)
        fprintf(stderr, "sequence ABI: %s\n", error);
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "generated sequence evidence ABI loads");
    check(abi.semantic_digest[0] != '\0',
          "sequence evidence ABI records semantic identity");
    check(name_is(
              abi.constructors[
                  PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_CONS],
              "ProofSequenceConsV1"),
          "constructor role is compiled from the authored layer");
    check(name_is(
              abi.judgments[
                  PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_INSTANTIATE],
              "ProofSequenceInstantiateV1"),
          "judgment role is compiled from the authored layer");
    check(name_is(
              abi.rules[
                  PPPROOF_GSLT_SEQUENCE_RULE_V1_INSTANTIATE_VARIABLE],
              "proof-sequence-instantiate-variable-v1"),
          "rule role is compiled from the authored layer");
    check(name_is(
              abi.assertion_constructors[
                  PPPROOF_GSLT_ASSERTION_CONSTRUCTOR_V1_ASSERTION],
              "ProofAssertionV1"),
          "assertion constructor role is compiled from the authored layer");
    check(name_is(
              abi.assertion_judgments[
                  PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_DECLARED],
              "ProofAssertionDeclaredV1"),
          "assertion judgment role is compiled from the authored layer");
    check(name_is(
              abi.assertion_rules[PPPROOF_GSLT_ASSERTION_RULE_V1_APPLY],
              "proof-assertion-apply-v1"),
          "assertion rule role is compiled from the authored layer");
    check(name_is(
              abi.assertion_rules[
                  PPPROOF_GSLT_ASSERTION_RULE_V1_USE_PREMISE],
              "proof-assertion-use-premise-v1"),
          "premise-use rule is compiled from the authored layer");
    if (result == PPPROOF_GSLT_ARTICLE_V1_OK)
        check_evidence_producer(&sequence_plan, &abi);

    result = ppproof_gslt_plan_v1_load(
        &other_plan, argv[3], NULL, error, sizeof(error));
    if (result != PPPROOF_GSLT_ARTICLE_V1_OK)
        fprintf(stderr, "independent plan: %s\n", error);
    check(result == PPPROOF_GSLT_ARTICLE_V1_OK,
          "independent proof plan loads");
    result = ppproof_gslt_sequence_evidence_abi_v1_load(
        &abi, argv[2], &other_plan, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_INVALID,
          "sequence ABI refuses a different extension identity");

    result = ppproof_gslt_sequence_evidence_abi_v1_load(
        &abi, argv[4], &sequence_plan, error, sizeof(error));
    check(result == PPPROOF_GSLT_ARTICLE_V1_INVALID,
          "deleted authored role makes the ABI incomplete");
    check(strstr(error, "missing or duplicate role targets") != NULL,
          "incomplete ABI reports its exact admission failure");

    ppproof_gslt_sequence_evidence_abi_v1_free(&abi);
    ppproof_gslt_plan_v1_free(&other_plan);
    ppproof_gslt_plan_v1_free(&sequence_plan);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    printf("---\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
