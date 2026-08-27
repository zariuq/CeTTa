#include "native/language_def_core_v1.h"
#include "native/operational_language_def_v1.h"

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

static bool text_equal(const CettaLdTextV1 *left,
                       const CettaLdTextV1 *right) {
    return left && right && left->len == right->len &&
        (left->len == 0u ||
         memcmp(left->bytes, right->bytes, left->len) == 0);
}

static bool text_is(const CettaLdTextV1 *text, const char *literal) {
    size_t len = literal ? strlen(literal) : 0u;

    return text && literal && text->len == len &&
        (len == 0u || memcmp(text->bytes, literal, len) == 0);
}

static bool text_is_prefixed(const CettaLdTextV1 *mapped,
                             const char *prefix,
                             const CettaLdTextV1 *source) {
    size_t prefix_len = prefix ? strlen(prefix) : 0u;

    return mapped && prefix && source &&
        mapped->len == prefix_len + source->len &&
        (prefix_len == 0u ||
         memcmp(mapped->bytes, prefix, prefix_len) == 0) &&
        (source->len == 0u ||
         memcmp(mapped->bytes + prefix_len,
                source->bytes, source->len) == 0);
}

static bool optional_text_equal(const CettaLdOptionalTextV1 *left,
                                const CettaLdOptionalTextV1 *right) {
    return left && right && left->present == right->present &&
        (!left->present || text_equal(&left->value, &right->value));
}

static bool text_list_equal(const CettaLdTextListV1 *left,
                            const CettaLdTextListV1 *right) {
    uint32_t index;

    if (!left || !right || left->len != right->len)
        return false;
    for (index = 0u; index < left->len; index++) {
        if (!text_equal(&left->items[index], &right->items[index]))
            return false;
    }
    return true;
}

static bool type_expr_is_map(const CettaLdTypeExprV1 *mapped,
                             const CettaLdTypeExprV1 *source,
                             const char *prefix) {
    if (!mapped || !source || mapped->kind != source->kind)
        return false;
    switch (source->kind) {
    case CETTA_LD_TYPE_BASE_V1:
        return text_is_prefixed(&mapped->as.base, prefix, &source->as.base);
    case CETTA_LD_TYPE_ARROW_V1:
        return type_expr_is_map(mapped->as.arrow.domain,
                                source->as.arrow.domain, prefix) &&
            type_expr_is_map(mapped->as.arrow.codomain,
                             source->as.arrow.codomain, prefix);
    case CETTA_LD_TYPE_MULTI_BINDER_V1:
        return type_expr_is_map(mapped->as.multi_binder_body,
                                source->as.multi_binder_body, prefix);
    case CETTA_LD_TYPE_COLLECTION_V1:
        return mapped->as.collection.collection_type ==
                source->as.collection.collection_type &&
            type_expr_is_map(mapped->as.collection.element_type,
                             source->as.collection.element_type, prefix);
    }
    return false;
}

static bool term_param_is_map(const CettaLdTermParamV1 *mapped,
                              const CettaLdTermParamV1 *source,
                              const char *prefix) {
    if (!mapped || !source || mapped->kind != source->kind ||
        !text_equal(&mapped->body_name, &source->body_name) ||
        !type_expr_is_map(&mapped->type, &source->type, prefix))
        return false;
    switch (source->kind) {
    case CETTA_LD_PARAM_SIMPLE_V1:
        return true;
    case CETTA_LD_PARAM_ABSTRACTION_NAMED_V1:
        return optional_text_equal(&mapped->names.binder,
                                   &source->names.binder);
    case CETTA_LD_PARAM_MULTI_ABSTRACTION_NAMED_V1:
        return text_list_equal(&mapped->names.binders,
                               &source->names.binders);
    }
    return false;
}

static bool syntax_items_equal(const CettaLdSyntaxItemListV1 *left,
                               const CettaLdSyntaxItemListV1 *right);

static bool syntax_op_equal(const CettaLdSyntaxPatternOpV1 *left,
                            const CettaLdSyntaxPatternOpV1 *right) {
    if (!left || !right || left->kind != right->kind)
        return false;
    switch (left->kind) {
    case CETTA_LD_SYNTAX_OP_VAR_V1:
        return text_equal(&left->as.variable, &right->as.variable);
    case CETTA_LD_SYNTAX_OP_SEP_V1:
        return text_equal(&left->as.sep.collection,
                          &right->as.sep.collection) &&
            text_equal(&left->as.sep.separator, &right->as.sep.separator) &&
            syntax_op_equal(left->as.sep.source, right->as.sep.source);
    case CETTA_LD_SYNTAX_OP_ZIP_V1:
        return text_equal(&left->as.zip.left, &right->as.zip.left) &&
            text_equal(&left->as.zip.right, &right->as.zip.right);
    case CETTA_LD_SYNTAX_OP_MAP_V1:
        return syntax_op_equal(left->as.map.source, right->as.map.source) &&
            text_list_equal(&left->as.map.binders, &right->as.map.binders) &&
            syntax_items_equal(&left->as.map.body, &right->as.map.body);
    case CETTA_LD_SYNTAX_OP_OPT_V1:
        return syntax_items_equal(&left->as.opt, &right->as.opt);
    }
    return false;
}

static bool syntax_item_equal(const CettaLdSyntaxItemV1 *left,
                              const CettaLdSyntaxItemV1 *right) {
    if (!left || !right || left->kind != right->kind)
        return false;
    switch (left->kind) {
    case CETTA_LD_SYNTAX_TERMINAL_V1:
    case CETTA_LD_SYNTAX_NONTERMINAL_V1:
    case CETTA_LD_SYNTAX_SEPARATOR_V1:
        return text_equal(&left->as.text, &right->as.text);
    case CETTA_LD_SYNTAX_DELIMITER_V1:
        return text_equal(&left->as.delimiter.left,
                          &right->as.delimiter.left) &&
            text_equal(&left->as.delimiter.right,
                       &right->as.delimiter.right);
    case CETTA_LD_SYNTAX_OP_V1:
        return syntax_op_equal(left->as.op, right->as.op);
    }
    return false;
}

static bool syntax_items_equal(const CettaLdSyntaxItemListV1 *left,
                               const CettaLdSyntaxItemListV1 *right) {
    uint32_t index;

    if (!left || !right || left->len != right->len)
        return false;
    for (index = 0u; index < left->len; index++) {
        if (!syntax_item_equal(&left->items[index], &right->items[index]))
            return false;
    }
    return true;
}

static bool pattern_is_map(const CettaLdPatternV1 *mapped,
                           const CettaLdPatternV1 *source,
                           const char *prefix) {
    uint32_t index;

    if (!mapped || !source || mapped->kind != source->kind)
        return false;
    switch (source->kind) {
    case CETTA_LD_PATTERN_BVAR_V1:
        return mapped->as.bvar_decimal && source->as.bvar_decimal &&
            strcmp(mapped->as.bvar_decimal, source->as.bvar_decimal) == 0;
    case CETTA_LD_PATTERN_FVAR_V1:
        return text_equal(&mapped->as.fvar, &source->as.fvar);
    case CETTA_LD_PATTERN_APPLY_V1:
        if (!text_is_prefixed(&mapped->as.apply.head, prefix,
                              &source->as.apply.head) ||
            mapped->as.apply.arguments.len != source->as.apply.arguments.len)
            return false;
        for (index = 0u; index < source->as.apply.arguments.len; index++) {
            if (!pattern_is_map(&mapped->as.apply.arguments.items[index],
                                &source->as.apply.arguments.items[index],
                                prefix))
                return false;
        }
        return true;
    case CETTA_LD_PATTERN_LAMBDA_V1:
        return optional_text_equal(&mapped->as.lambda.binder,
                                   &source->as.lambda.binder) &&
            pattern_is_map(mapped->as.lambda.body,
                           source->as.lambda.body, prefix);
    case CETTA_LD_PATTERN_MULTI_LAMBDA_V1:
        return mapped->as.multi_lambda.arity_decimal &&
            source->as.multi_lambda.arity_decimal &&
            strcmp(mapped->as.multi_lambda.arity_decimal,
                   source->as.multi_lambda.arity_decimal) == 0 &&
            text_list_equal(&mapped->as.multi_lambda.binders,
                            &source->as.multi_lambda.binders) &&
            pattern_is_map(mapped->as.multi_lambda.body,
                           source->as.multi_lambda.body, prefix);
    case CETTA_LD_PATTERN_SUBST_V1:
        return pattern_is_map(mapped->as.subst.body,
                              source->as.subst.body, prefix) &&
            pattern_is_map(mapped->as.subst.replacement,
                           source->as.subst.replacement, prefix);
    case CETTA_LD_PATTERN_COLLECTION_V1:
        if (mapped->as.collection.collection_type !=
                source->as.collection.collection_type ||
            !optional_text_equal(&mapped->as.collection.rest,
                                 &source->as.collection.rest) ||
            mapped->as.collection.elements.len !=
                source->as.collection.elements.len)
            return false;
        for (index = 0u; index < source->as.collection.elements.len; index++) {
            if (!pattern_is_map(&mapped->as.collection.elements.items[index],
                                &source->as.collection.elements.items[index],
                                prefix))
                return false;
        }
        return true;
    }
    return false;
}

static bool premise_is_map(const CettaLdPremiseV1 *mapped,
                           const CettaLdPremiseV1 *source,
                           const char *prefix) {
    uint32_t index;

    if (!mapped || !source || mapped->kind != source->kind)
        return false;
    switch (source->kind) {
    case CETTA_LD_PREMISE_FRESHNESS_V1:
        return text_equal(&mapped->as.freshness.variable,
                          &source->as.freshness.variable) &&
            pattern_is_map(&mapped->as.freshness.term,
                           &source->as.freshness.term, prefix);
    case CETTA_LD_PREMISE_CONGRUENCE_V1:
        return pattern_is_map(&mapped->as.congruence.left,
                              &source->as.congruence.left, prefix) &&
            pattern_is_map(&mapped->as.congruence.right,
                           &source->as.congruence.right, prefix);
    case CETTA_LD_PREMISE_RELATION_QUERY_V1:
        if (!text_is_prefixed(&mapped->as.relation_query.relation, prefix,
                              &source->as.relation_query.relation) ||
            mapped->as.relation_query.arguments.len !=
                source->as.relation_query.arguments.len)
            return false;
        for (index = 0u;
             index < source->as.relation_query.arguments.len; index++) {
            if (!pattern_is_map(
                    &mapped->as.relation_query.arguments.items[index],
                    &source->as.relation_query.arguments.items[index],
                    prefix))
                return false;
        }
        return true;
    case CETTA_LD_PREMISE_FOR_ALL_V1:
        return text_equal(&mapped->as.for_all.collection,
                          &source->as.for_all.collection) &&
            text_equal(&mapped->as.for_all.parameter,
                       &source->as.for_all.parameter) &&
            premise_is_map(mapped->as.for_all.body,
                           source->as.for_all.body, prefix);
    }
    return false;
}

static bool grammar_rule_is_map(const CettaLdGrammarRuleV1 *mapped,
                                const CettaLdGrammarRuleV1 *source,
                                const char *prefix) {
    uint32_t index;

    if (!mapped || !source ||
        !text_is_prefixed(&mapped->label, prefix, &source->label) ||
        !text_is_prefixed(&mapped->category, prefix, &source->category) ||
        mapped->param_len != source->param_len ||
        !syntax_items_equal(&mapped->syntax_pattern,
                            &source->syntax_pattern) ||
        mapped->eval_policy.present != source->eval_policy.present ||
        (mapped->eval_policy.present && mapped->eval_policy.value !=
            source->eval_policy.value))
        return false;
    for (index = 0u; index < source->param_len; index++) {
        if (!term_param_is_map(&mapped->params[index],
                               &source->params[index], prefix))
            return false;
    }
    return true;
}

static bool relation_rule_is_map(const CettaLdRelationRuleV1 *mapped,
                                 const CettaLdRelationRuleV1 *source,
                                 const char *prefix) {
    uint32_t index;

    if (!mapped || !source ||
        !text_is_prefixed(&mapped->name, prefix, &source->name) ||
        mapped->type_context_len != source->type_context_len ||
        mapped->premises.len != source->premises.len ||
        !pattern_is_map(&mapped->left, &source->left, prefix) ||
        !pattern_is_map(&mapped->right, &source->right, prefix))
        return false;
    for (index = 0u; index < source->type_context_len; index++) {
        if (!text_equal(&mapped->type_context[index].name,
                        &source->type_context[index].name) ||
            !type_expr_is_map(&mapped->type_context[index].type,
                              &source->type_context[index].type, prefix))
            return false;
    }
    for (index = 0u; index < source->premises.len; index++) {
        if (!premise_is_map(&mapped->premises.items[index],
                            &source->premises.items[index], prefix))
            return false;
    }
    return true;
}

static bool component_is_embedded(const CettaLanguageDefCoreV1 *joint,
                                  const CettaLanguageDefCoreV1 *source,
                                  const char *prefix,
                                  uint32_t type_offset,
                                  uint32_t term_offset,
                                  uint32_t equation_offset,
                                  uint32_t rewrite_offset) {
    uint32_t index;

    for (index = 0u; index < source->type_len; index++) {
        const CettaLdTypeDeclV1 *mapped = &joint->types[type_offset + index];
        const CettaLdTypeDeclV1 *original = &source->types[index];
        if (!text_is_prefixed(&mapped->name, prefix, &original->name) ||
            mapped->carrier != original->carrier)
            return false;
    }
    for (index = 0u; index < source->term_len; index++) {
        if (!grammar_rule_is_map(&joint->terms[term_offset + index],
                                 &source->terms[index], prefix))
            return false;
    }
    for (index = 0u; index < source->equation_len; index++) {
        if (!relation_rule_is_map(&joint->equations[equation_offset + index],
                                  &source->equations[index], prefix))
            return false;
    }
    for (index = 0u; index < source->rewrite_len; index++) {
        if (!relation_rule_is_map(&joint->rewrites[rewrite_offset + index],
                                  &source->rewrites[index], prefix))
            return false;
    }
    return true;
}

static bool joint_is_exact_coproduct(const CettaLanguageDefCoreV1 *joint,
                                     const CettaLanguageDefCoreV1 *external,
                                     const CettaLanguageDefCoreV1 *radix) {
    return joint && external && radix &&
        text_is(&joint->name, "ArithmeticTargetMachines") &&
        joint->type_len == external->type_len + radix->type_len &&
        joint->term_len == external->term_len + radix->term_len &&
        joint->equation_len == external->equation_len + radix->equation_len &&
        joint->rewrite_len == external->rewrite_len + radix->rewrite_len &&
        component_is_embedded(joint, external, "E:", 0u, 0u, 0u, 0u) &&
        component_is_embedded(joint, radix, "R:",
            external->type_len, external->term_len,
            external->equation_len, external->rewrite_len);
}

static bool load_language(const char *path,
                          CettaOperationalLanguageDefV1 *wire,
                          CettaLanguageDefCoreV1 *core,
                          char *error,
                          size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;

    return cetta_op_lang_v1_parse_file(
               wire, path, 64000000u, 128000000u, &wire_status,
               error, error_size) &&
        cetta_language_def_core_v1_decode(
            core, wire, 800000u, &core_status, error, error_size);
}

int main(void) {
    TestCounts counts = {0};
    CettaOperationalLanguageDefV1 external_wire;
    CettaOperationalLanguageDefV1 radix_wire;
    CettaOperationalLanguageDefV1 joint_wire;
    CettaLanguageDefCoreV1 external;
    CettaLanguageDefCoreV1 radix;
    CettaLanguageDefCoreV1 joint;
    char error[512] = {0};
    bool loaded;

    cetta_op_lang_v1_init(&external_wire);
    cetta_op_lang_v1_init(&radix_wire);
    cetta_op_lang_v1_init(&joint_wire);
    cetta_language_def_core_v1_init(&external);
    cetta_language_def_core_v1_init(&radix);
    cetta_language_def_core_v1_init(&joint);
    loaded = load_language("langdef/machines/external_call_machine_v1.metta",
                           &external_wire, &external, error, sizeof(error)) &&
        load_language("langdef/machines/radix_digit_machine_v1.metta",
                      &radix_wire, &radix, error, sizeof(error)) &&
        load_language("langdef/machines/arithmetic_target_machines_v1.metta",
                      &joint_wire, &joint, error, sizeof(error));
    (void)expect(&counts, loaded,
                 error[0] ? error : "load component and joint GSLTs");
    if (loaded) {
        (void)expect(&counts,
            joint_is_exact_coproduct(&joint, &external, &radix),
            "joint wire is the full structural coproduct of both machines");
        if (joint.term_len > 0u && joint.terms[0].label.len > 0u) {
            uint8_t saved = joint.terms[0].label.bytes[0];
            joint.terms[0].label.bytes[0] = (uint8_t)'X';
            (void)expect(&counts,
                !joint_is_exact_coproduct(&joint, &external, &radix),
                "constructor drift breaks exact coproduct admission");
            joint.terms[0].label.bytes[0] = saved;
        } else {
            (void)expect(&counts, false,
                "joint coproduct has a mutable constructor canary");
        }
        if (joint.rewrite_len > 0u &&
            joint.rewrites[0].right.kind == CETTA_LD_PATTERN_APPLY_V1 &&
            joint.rewrites[0].right.as.apply.head.len > 0u) {
            uint8_t saved = joint.rewrites[0].right.as.apply.head.bytes[0];
            joint.rewrites[0].right.as.apply.head.bytes[0] = (uint8_t)'X';
            (void)expect(&counts,
                !joint_is_exact_coproduct(&joint, &external, &radix),
                "operational drift breaks exact coproduct admission");
            joint.rewrites[0].right.as.apply.head.bytes[0] = saved;
        } else {
            (void)expect(&counts, false,
                "joint coproduct has an operational mutation canary");
        }
    }

    cetta_language_def_core_v1_free(&joint);
    cetta_language_def_core_v1_free(&radix);
    cetta_language_def_core_v1_free(&external);
    cetta_op_lang_v1_free(&joint_wire);
    cetta_op_lang_v1_free(&radix_wire);
    cetta_op_lang_v1_free(&external_wire);
    printf("(ArithmeticTargetCoproductSummary passed=%u failed=%u)\n",
           counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
