#include "native/language_def_core_v1.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned passed;
    unsigned failed;
} TestCounts;

static bool expect(TestCounts *counts, bool condition, const char *name) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", name);
    return false;
}

static bool text_is(const CettaLdTextV1 *text, const char *value) {
    size_t len = value ? strlen(value) : 0u;
    return text && value && (size_t)text->len == len &&
        (len == 0u ||
         (text->bytes && memcmp(text->bytes, value, len) == 0));
}

static bool term_label_is_declared(const CettaLanguageDefCoreV1 *language,
                                   const CettaLdTextV1 *label) {
    uint32_t index;

    if (!language || !label)
        return false;
    for (index = 0u; index < language->term_len; index++) {
        const CettaLdTextV1 *candidate = &language->terms[index].label;
        if (candidate->len == label->len &&
            (label->len == 0u ||
             (candidate->bytes && label->bytes &&
              memcmp(candidate->bytes, label->bytes, label->len) == 0))) {
            return true;
        }
    }
    return false;
}

static bool pattern_heads_are_declared(
    const CettaLanguageDefCoreV1 *language,
    const CettaLdPatternV1 *pattern) {
    uint32_t index;

    if (!language || !pattern)
        return false;
    switch (pattern->kind) {
    case CETTA_LD_PATTERN_BVAR_V1:
    case CETTA_LD_PATTERN_FVAR_V1:
        return true;
    case CETTA_LD_PATTERN_APPLY_V1:
        if (!term_label_is_declared(
                language, &pattern->as.apply.head)) {
            return false;
        }
        for (index = 0u;
             index < pattern->as.apply.arguments.len;
             index++) {
            if (!pattern_heads_are_declared(
                    language, &pattern->as.apply.arguments.items[index])) {
                return false;
            }
        }
        return true;
    case CETTA_LD_PATTERN_LAMBDA_V1:
        return pattern_heads_are_declared(
            language, pattern->as.lambda.body);
    case CETTA_LD_PATTERN_MULTI_LAMBDA_V1:
        return pattern_heads_are_declared(
            language, pattern->as.multi_lambda.body);
    case CETTA_LD_PATTERN_SUBST_V1:
        return pattern_heads_are_declared(
                   language, pattern->as.subst.body) &&
            pattern_heads_are_declared(
                language, pattern->as.subst.replacement);
    case CETTA_LD_PATTERN_COLLECTION_V1:
        for (index = 0u;
             index < pattern->as.collection.elements.len;
             index++) {
            if (!pattern_heads_are_declared(
                    language,
                    &pattern->as.collection.elements.items[index])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool premise_heads_are_declared(
    const CettaLanguageDefCoreV1 *language,
    const CettaLdPremiseV1 *premise) {
    uint32_t index;

    if (!language || !premise)
        return false;
    switch (premise->kind) {
    case CETTA_LD_PREMISE_FRESHNESS_V1:
        return pattern_heads_are_declared(
            language, &premise->as.freshness.term);
    case CETTA_LD_PREMISE_CONGRUENCE_V1:
        return pattern_heads_are_declared(
                   language, &premise->as.congruence.left) &&
            pattern_heads_are_declared(
                language, &premise->as.congruence.right);
    case CETTA_LD_PREMISE_RELATION_QUERY_V1:
        for (index = 0u;
             index < premise->as.relation_query.arguments.len;
             index++) {
            if (!pattern_heads_are_declared(
                    language,
                    &premise->as.relation_query.arguments.items[index])) {
                return false;
            }
        }
        return true;
    case CETTA_LD_PREMISE_FOR_ALL_V1:
        return premise_heads_are_declared(
            language, premise->as.for_all.body);
    }
    return false;
}

static bool parse_text(CettaOperationalLanguageDefV1 *wire,
                       const char *source,
                       CettaOpLangV1Status *status,
                       char *error,
                       size_t error_size) {
    return cetta_op_lang_v1_parse_bytes(
        wire, (const uint8_t *)source, strlen(source),
        2000000u, 4000000u, status, error, error_size);
}

static void fail_closed_and_atomic_gate(TestCounts *counts) {
    static const char good[] =
        "(GSLTLanguageDefWireV1 \"Stable\" "
        "(LCons (TypeDecl \"T\" CarrierAst) LNil) "
        "LNil LNil LNil)";
    static const char bad_carrier[] =
        "(GSLTLanguageDefWireV1 \"Mutant\" "
        "(LCons (TypeDecl \"T\" CarrierInvented) LNil) "
        "LNil LNil LNil)";
    static const char bad_term[] =
        "(GSLTLanguageDefWireV1 \"Mutant\" LNil "
        "(LCons (NotGrammarRule \"x\") LNil) LNil LNil)";
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_OK;
    char error[512] = {0};

    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    (void)expect(
        counts,
        parse_text(&wire, good, &wire_status, error, sizeof(error)) &&
            cetta_language_def_core_v1_decode(
                &language, &wire, 10000u, &core_status,
                error, sizeof(error)) &&
            text_is(&language.name, "Stable"),
        error[0] ? error : "establish atomic decode baseline");

    error[0] = '\0';
    (void)expect(
        counts,
        parse_text(&wire, bad_carrier, &wire_status, error, sizeof(error)) &&
            !cetta_language_def_core_v1_decode(
                &language, &wire, 10000u, &core_status,
                error, sizeof(error)) &&
            core_status == CETTA_LD_CORE_V1_MALFORMED_WIRE &&
            text_is(&language.name, "Stable") && language.type_len == 1u,
        "unknown carrier rejects without replacing prior typed state");

    error[0] = '\0';
    (void)expect(
        counts,
        parse_text(&wire, bad_term, &wire_status, error, sizeof(error)) &&
            !cetta_language_def_core_v1_decode(
                &language, &wire, 10000u, &core_status,
                error, sizeof(error)) &&
            core_status == CETTA_LD_CORE_V1_MALFORMED_WIRE &&
            text_is(&language.name, "Stable"),
        "unknown grammar rule rejects atomically");

    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_core_v1_decode(
            &language, &wire, 1u, &core_status,
            error, sizeof(error)) &&
            core_status == CETTA_LD_CORE_V1_RESOURCE_LIMIT &&
            text_is(&language.name, "Stable"),
        "typed decode resource exhaustion is distinct and atomic");
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
}

static void constructor_coverage_gate(TestCounts *counts) {
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    const CettaLdGrammarRuleV1 *rich;
    const CettaLdRelationRuleV1 *equation;
    char error[512] = {0};

    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    (void)expect(
        counts,
        cetta_op_lang_v1_parse_file(
            &wire,
            "tests/langdef/fixtures/language_def_core_coverage_v1.metta",
            8000000u, 16000000u, &wire_status,
            error, sizeof(error)) &&
            cetta_language_def_core_v1_decode(
                &language, &wire, 200000u, &core_status,
                error, sizeof(error)),
        error[0] ? error : "decode complete LanguageDef constructor coverage");
    rich = language.term_len > 0u ? &language.terms[0] : NULL;
    equation = language.equation_len > 0u ? &language.equations[0] : NULL;
    (void)expect(
        counts,
        language.type_len == 8u &&
            language.types[0].carrier == CETTA_LD_CARRIER_AST_V1 &&
            language.types[1].carrier == CETTA_LD_CARRIER_TOKEN_LABEL_V1 &&
            language.types[2].carrier == CETTA_LD_CARRIER_TOKEN_RAW_V1 &&
            language.types[3].carrier == CETTA_LD_CARRIER_TOKEN_PROOF_V1 &&
            language.types[4].carrier == CETTA_LD_CARRIER_TOKEN_PATH_V1 &&
            language.types[5].carrier == CETTA_LD_CARRIER_BUILTIN_INT_V1 &&
            language.types[6].carrier == CETTA_LD_CARRIER_BUILTIN_STRING_V1 &&
            language.types[7].carrier == CETTA_LD_CARRIER_BUILTIN_BOOL_V1,
        "all carrier kinds decode distinctly");
    (void)expect(
        counts,
        rich && rich->param_len == 3u && rich->syntax_pattern.len == 10u &&
            rich->params[0].kind == CETTA_LD_PARAM_SIMPLE_V1 &&
            rich->params[1].kind ==
                CETTA_LD_PARAM_ABSTRACTION_NAMED_V1 &&
            rich->params[1].names.binder.present &&
            rich->params[1].type.kind == CETTA_LD_TYPE_ARROW_V1 &&
            rich->params[2].kind ==
                CETTA_LD_PARAM_MULTI_ABSTRACTION_NAMED_V1 &&
            rich->params[2].names.binders.len == 2u &&
            rich->params[2].type.kind == CETTA_LD_TYPE_COLLECTION_V1 &&
            rich->params[2].type.as.collection.collection_type ==
                CETTA_LD_COLLECTION_HASH_BAG_V1 &&
            rich->params[2].type.as.collection.element_type->kind ==
                CETTA_LD_TYPE_MULTI_BINDER_V1,
        "all term-parameter and type-expression shapes decode");
    (void)expect(
        counts,
        rich &&
            rich->syntax_pattern.items[0].kind ==
                CETTA_LD_SYNTAX_TERMINAL_V1 &&
            rich->syntax_pattern.items[1].kind ==
                CETTA_LD_SYNTAX_NONTERMINAL_V1 &&
            rich->syntax_pattern.items[2].kind ==
                CETTA_LD_SYNTAX_SEPARATOR_V1 &&
            rich->syntax_pattern.items[3].kind ==
                CETTA_LD_SYNTAX_DELIMITER_V1 &&
            rich->syntax_pattern.items[4].kind == CETTA_LD_SYNTAX_OP_V1 &&
            rich->syntax_pattern.items[4].as.op->kind ==
                CETTA_LD_SYNTAX_OP_VAR_V1 &&
            rich->syntax_pattern.items[5].kind == CETTA_LD_SYNTAX_OP_V1 &&
            rich->syntax_pattern.items[5].as.op->kind ==
                CETTA_LD_SYNTAX_OP_SEP_V1 &&
            rich->syntax_pattern.items[5].as.op->as.sep.source == NULL &&
            rich->syntax_pattern.items[6].kind == CETTA_LD_SYNTAX_OP_V1 &&
            rich->syntax_pattern.items[6].as.op->as.sep.source &&
            rich->syntax_pattern.items[6].as.op->as.sep.source->kind ==
                CETTA_LD_SYNTAX_OP_VAR_V1 &&
            rich->syntax_pattern.items[7].kind == CETTA_LD_SYNTAX_OP_V1 &&
            rich->syntax_pattern.items[7].as.op->kind ==
                CETTA_LD_SYNTAX_OP_ZIP_V1 &&
            rich->syntax_pattern.items[8].kind == CETTA_LD_SYNTAX_OP_V1 &&
            rich->syntax_pattern.items[8].as.op->kind ==
                CETTA_LD_SYNTAX_OP_MAP_V1 &&
            rich->syntax_pattern.items[9].kind == CETTA_LD_SYNTAX_OP_V1 &&
            rich->syntax_pattern.items[9].as.op->kind ==
                CETTA_LD_SYNTAX_OP_OPT_V1,
        "all syntax-item and syntax-operation shapes decode");
    (void)expect(
        counts,
        language.term_len == 4u && !language.terms[0].eval_policy.present &&
            language.terms[1].eval_policy.present &&
            language.terms[1].eval_policy.value == CETTA_LD_EVAL_REWRITE_V1 &&
            language.terms[2].eval_policy.value == CETTA_LD_EVAL_FOLD_V1 &&
            language.terms[3].eval_policy.value == CETTA_LD_EVAL_ORACLE_V1,
        "absent and all present evaluation policies decode");
    (void)expect(
        counts,
        equation && equation->type_context_len == 1u &&
            equation->premises.len == 4u &&
            equation->premises.items[0].kind ==
                CETTA_LD_PREMISE_FRESHNESS_V1 &&
            equation->premises.items[0].as.freshness.term.kind ==
                CETTA_LD_PATTERN_FVAR_V1 &&
            equation->premises.items[1].kind ==
                CETTA_LD_PREMISE_CONGRUENCE_V1 &&
            equation->premises.items[1].as.congruence.left.kind ==
                CETTA_LD_PATTERN_BVAR_V1 &&
            equation->premises.items[1].as.congruence.right.kind ==
                CETTA_LD_PATTERN_LAMBDA_V1 &&
            equation->premises.items[2].kind ==
                CETTA_LD_PREMISE_RELATION_QUERY_V1 &&
            equation->premises.items[2].as.relation_query.arguments.len == 3u &&
            equation->premises.items[2].as.relation_query.arguments.items[0].kind ==
                CETTA_LD_PATTERN_APPLY_V1 &&
            equation->premises.items[2].as.relation_query.arguments.items[1].kind ==
                CETTA_LD_PATTERN_MULTI_LAMBDA_V1 &&
            equation->premises.items[2].as.relation_query.arguments.items[1]
                    .as.multi_lambda.body->kind ==
                CETTA_LD_PATTERN_SUBST_V1 &&
            equation->premises.items[2].as.relation_query.arguments.items[2].kind ==
                CETTA_LD_PATTERN_COLLECTION_V1 &&
            equation->premises.items[3].kind ==
                CETTA_LD_PREMISE_FOR_ALL_V1,
        "all premise and pattern constructors decode proof-relevantly");
    (void)expect(
        counts,
        language.equation_len == 1u && language.rewrite_len == 1u &&
            language.rewrites[0].left.kind ==
                CETTA_LD_PATTERN_COLLECTION_V1 &&
            language.rewrites[0].left.as.collection.collection_type ==
                CETTA_LD_COLLECTION_HASH_BAG_V1 &&
            language.rewrites[0].right.kind == CETTA_LD_PATTERN_FVAR_V1,
        "equations and directional rewrites remain separate");
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
}

static void external_call_machine_operational_gate(TestCounts *counts) {
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    const CettaLdRelationRuleV1 *fuel_exhausted;
    const CettaLdRelationRuleV1 *branch_zero;
    uint32_t index;
    bool has_oracle = false;
    bool heads_declared = true;
    bool parse_ok;
    bool decode_ok = false;
    char error[512] = {0};

    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    parse_ok = cetta_op_lang_v1_parse_file(
        &wire, "langdef/machines/external_call_machine_v1.metta",
        64000000u, 128000000u, &wire_status,
        error, sizeof(error));
    if (parse_ok) {
        decode_ok = cetta_language_def_core_v1_decode(
            &language, &wire, 400000u, &core_status,
            error, sizeof(error));
    }
    if (!parse_ok || !decode_ok) {
        fprintf(stderr,
                "external-call-machine ingress failed: wire=%s core=%s detail=%s\n",
                cetta_op_lang_v1_status_name(wire_status),
                cetta_ld_core_v1_status_name(core_status),
                error[0] ? error : "none");
    }
    (void)expect(
        counts, parse_ok && decode_ok,
        error[0] ? error : "decode authored external-call machine");
    fuel_exhausted = language.rewrite_len > 0u
        ? &language.rewrites[0] : NULL;
    branch_zero = language.rewrite_len > 1u
        ? &language.rewrites[1] : NULL;
    for (index = 0u; index < language.term_len; index++) {
        if (language.terms[index].eval_policy.present &&
            language.terms[index].eval_policy.value ==
                CETTA_LD_EVAL_ORACLE_V1) {
            has_oracle = true;
        }
    }
    for (index = 0u; index < language.rewrite_len; index++) {
        uint32_t premise_index;

        heads_declared = heads_declared &&
            pattern_heads_are_declared(
                &language, &language.rewrites[index].left) &&
            pattern_heads_are_declared(
                &language, &language.rewrites[index].right);
        for (premise_index = 0u;
             premise_index < language.rewrites[index].premises.len;
             premise_index++) {
            heads_declared = heads_declared &&
                premise_heads_are_declared(
                    &language,
                    &language.rewrites[index].premises.items[premise_index]);
        }
    }
    (void)expect(
        counts,
        wire_status == CETTA_OP_LANG_V1_OK &&
            core_status == CETTA_LD_CORE_V1_OK &&
            text_is(&language.name, "ExternalCallMachine") &&
            language.type_len == 21u && language.term_len == 43u &&
            language.equation_len == 0u && language.rewrite_len == 12u &&
            text_is(&language.rewrites[11].name,
                    "external-call:return-resource-fault"),
        "external-call machine retains authored types, constructors, and transitions");
    (void)expect(
        counts,
        !has_oracle && language.terms &&
            text_is(&language.terms[0].label, "external-call:nat-zero") &&
            text_is(&language.terms[42].label, "external-call:halted") &&
            language.terms[41].eval_policy.present &&
            language.terms[41].eval_policy.value ==
                CETTA_LD_EVAL_REWRITE_V1,
        "external-call machine has no oracle-defined constructor semantics");
    (void)expect(
        counts, heads_declared,
        "every external-call rewrite head is an authored constructor");
    (void)expect(
        counts,
        fuel_exhausted && branch_zero &&
            text_is(&fuel_exhausted->name, "external-call:fuel-exhausted") &&
            fuel_exhausted->premises.len == 1u &&
            fuel_exhausted->premises.items[0].kind ==
                CETTA_LD_PREMISE_RELATION_QUERY_V1 &&
            text_is(
                &fuel_exhausted->premises.items[0]
                    .as.relation_query.relation,
                "ExternalCallStepLimitFault") &&
            text_is(&branch_zero->name, "external-call:branch-zero") &&
            branch_zero->premises.len == 4u &&
            text_is(
                &branch_zero->premises.items[1]
                    .as.relation_query.relation,
                "ExternalCallFetchInstruction") &&
            branch_zero->left.kind == CETTA_LD_PATTERN_APPLY_V1 &&
            branch_zero->right.kind == CETTA_LD_PATTERN_APPLY_V1,
        "external-call transition order and operational premises decode exactly");
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
}

static void exact_arithmetic_operational_gate(TestCounts *counts) {
    static const char *const expected_relations[] = {
        "ExactIntegerAdd",
        "ExactIntegerSub",
        "ExactIntegerMul",
        "ExactIntegerTQuot",
        "ExactIntegerFQuot",
        "ExactIntegerTRem",
        "ExactIntegerFRem",
    };
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    uint32_t index;
    bool has_oracle = false;
    bool heads_declared = true;
    bool ordered_relations = true;
    bool parse_ok;
    bool decode_ok = false;
    char error[512] = {0};

    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    parse_ok = cetta_op_lang_v1_parse_file(
        &wire, "langdef/arithmetic/exact_arithmetic_v1.metta",
        16000000u, 32000000u, &wire_status,
        error, sizeof(error));
    if (parse_ok) {
        decode_ok = cetta_language_def_core_v1_decode(
            &language, &wire, 200000u, &core_status,
            error, sizeof(error));
    }
    if (!parse_ok || !decode_ok) {
        fprintf(stderr,
                "exact-arithmetic ingress failed: wire=%s core=%s detail=%s\n",
                cetta_op_lang_v1_status_name(wire_status),
                cetta_ld_core_v1_status_name(core_status),
                error[0] ? error : "none");
    }
    (void)expect(
        counts, parse_ok && decode_ok,
        error[0] ? error : "decode authored exact-arithmetic source");
    for (index = 0u; index < language.term_len; index++) {
        if (language.terms[index].eval_policy.present &&
            language.terms[index].eval_policy.value ==
                CETTA_LD_EVAL_ORACLE_V1) {
            has_oracle = true;
        }
    }
    for (index = 0u; index < language.rewrite_len; index++) {
        const CettaLdRelationRuleV1 *rewrite = &language.rewrites[index];

        heads_declared = heads_declared &&
            pattern_heads_are_declared(&language, &rewrite->left) &&
            pattern_heads_are_declared(&language, &rewrite->right);
        if (index >= sizeof(expected_relations) /
                sizeof(expected_relations[0])) {
            ordered_relations = false;
        } else {
            ordered_relations = ordered_relations &&
                rewrite->premises.len == 1u &&
                rewrite->premises.items[0].kind ==
                    CETTA_LD_PREMISE_RELATION_QUERY_V1 &&
                text_is(
                    &rewrite->premises.items[0].as.relation_query.relation,
                    expected_relations[index]);
        }
    }
    (void)expect(
        counts,
        wire_status == CETTA_OP_LANG_V1_OK &&
            core_status == CETTA_LD_CORE_V1_OK &&
            text_is(&language.name, "ExactArithmetic") &&
            language.type_len == 4u && language.term_len == 11u &&
            language.equation_len == 0u && language.rewrite_len == 7u,
        "exact arithmetic retains its complete operational inventory");
    (void)expect(
        counts,
        language.terms && language.rewrites && !has_oracle &&
            text_is(&language.terms[0].label, "arith:add") &&
            text_is(&language.terms[6].label, "arith:frem") &&
            text_is(&language.terms[9].label, "arith:eval") &&
            language.terms[9].eval_policy.present &&
            language.terms[9].eval_policy.value ==
                CETTA_LD_EVAL_REWRITE_V1 &&
            text_is(&language.terms[10].label, "arith:halted"),
        "exact arithmetic is rewrite-owned rather than oracle-defined");
    (void)expect(
        counts, heads_declared && ordered_relations,
        "exact arithmetic rewrite order and constructor heads are exact");
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
}

int main(void) {
    TestCounts counts = {0};

    fail_closed_and_atomic_gate(&counts);
    constructor_coverage_gate(&counts);
    external_call_machine_operational_gate(&counts);
    exact_arithmetic_operational_gate(&counts);
    printf("typed LanguageDef core decode: %u passed, %u failed\n",
           counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
