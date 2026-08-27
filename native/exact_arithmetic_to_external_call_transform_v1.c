#include "exact_arithmetic_to_external_call_transform_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *relation;
    const char *provider_link;
    bool partial;
} OperationSpec;

static const OperationSpec operation_specs[CETTA_EXACT_ARITHMETIC_OP_COUNT_V1] = {
    {"ExactIntegerAdd", "cetta_external_call_exact_integer_add_v1", false},
    {"ExactIntegerSub", "cetta_external_call_exact_integer_sub_v1", false},
    {"ExactIntegerMul", "cetta_external_call_exact_integer_mul_v1", false},
    {"ExactIntegerTQuot", "cetta_external_call_exact_integer_tquot_v1", true},
    {"ExactIntegerFQuot", "cetta_external_call_exact_integer_fquot_v1", true},
    {"ExactIntegerTRem", "cetta_external_call_exact_integer_trem_v1", true},
    {"ExactIntegerFRem", "cetta_external_call_exact_integer_frem_v1", true},
};

typedef struct {
    const CettaLdTextV1 *nat_zero;
    const CettaLdTextV1 *nat_succ;
    const CettaLdTextV1 *slot_id;
    const CettaLdTextV1 *label;
    const CettaLdTextV1 *external_id;
    const CettaLdTextV1 *branch_zero;
    const CettaLdTextV1 *call_binary;
    const CettaLdTextV1 *return_value;
    const CettaLdTextV1 *return_declined;
    const CettaLdTextV1 *return_language_fault;
    const CettaLdTextV1 *return_engine_fault;
    const CettaLdTextV1 *return_resource_fault;
    const CettaLdTextV1 *instruction_nil;
    const CettaLdTextV1 *instruction_cons;
    const CettaLdTextV1 *binary_external;
    const CettaLdTextV1 *external_nil;
    const CettaLdTextV1 *external_cons;
    const CettaLdTextV1 *program;
    const CettaLdTextV1 *fault;
} TargetProfile;

static void set_status(CettaExactArithmeticExternalCallTransformStatusV1 *status,
                       CettaExactArithmeticExternalCallTransformStatusV1 value) {
    if (status)
        *status = value;
}

static void set_error(char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool text_is(const CettaLdTextV1 *text, const char *literal) {
    size_t len;

    if (!text || !literal)
        return false;
    len = strlen(literal);
    return len == text->len &&
        (len == 0u || memcmp(text->bytes, literal, len) == 0);
}

static bool text_equal(const CettaLdTextV1 *left,
                       const CettaLdTextV1 *right) {
    return left && right && left->len == right->len &&
        (left->len == 0u ||
         memcmp(left->bytes, right->bytes, left->len) == 0);
}

static bool text_copy(CettaLdTextV1 *out, const CettaLdTextV1 *source) {
    uint8_t *bytes = NULL;

    if (!out || !source)
        return false;
    if (source->len > 0u) {
        bytes = malloc(source->len);
        if (!bytes)
            return false;
        memcpy(bytes, source->bytes, source->len);
    }
    out->bytes = bytes;
    out->len = source->len;
    return true;
}

static bool text_from_c(CettaLdTextV1 *out, const char *source) {
    CettaLdTextV1 view;
    size_t len;

    if (!out || !source)
        return false;
    len = strlen(source);
    if (len > UINT32_MAX)
        return false;
    view.bytes = (uint8_t *)(uintptr_t)source;
    view.len = (uint32_t)len;
    return text_copy(out, &view);
}

static bool type_is_base(const CettaLdTypeExprV1 *type,
                         const char *name) {
    return type && type->kind == CETTA_LD_TYPE_BASE_V1 &&
        text_is(&type->as.base, name);
}

static const CettaLdGrammarRuleV1 *find_term(
    const CettaLanguageDefCoreV1 *language,
    const char *label) {
    uint32_t index;
    const CettaLdGrammarRuleV1 *result = NULL;

    if (!language || !label)
        return NULL;
    for (index = 0u; index < language->term_len; index++) {
        if (!text_is(&language->terms[index].label, label))
            continue;
        if (result)
            return NULL;
        result = &language->terms[index];
    }
    return result;
}

static const CettaLdGrammarRuleV1 *find_term_text(
    const CettaLanguageDefCoreV1 *language,
    const CettaLdTextV1 *label) {
    uint32_t index;
    const CettaLdGrammarRuleV1 *result = NULL;

    if (!language || !label)
        return NULL;
    for (index = 0u; index < language->term_len; index++) {
        if (!text_equal(&language->terms[index].label, label))
            continue;
        if (result)
            return NULL;
        result = &language->terms[index];
    }
    return result;
}

static bool term_signature_is(const CettaLdGrammarRuleV1 *term,
                              const char *category,
                              uint32_t arity,
                              const char *const *parameter_types) {
    uint32_t index;

    if (!term || !text_is(&term->category, category) ||
        term->param_len != arity)
        return false;
    for (index = 0u; index < arity; index++) {
        if (term->params[index].kind != CETTA_LD_PARAM_SIMPLE_V1 ||
            !type_is_base(&term->params[index].type, parameter_types[index]))
            return false;
    }
    return true;
}

static bool pattern_is_apply(const CettaLdPatternV1 *pattern,
                             const CettaLdTextV1 *head,
                             uint32_t arity) {
    return pattern && pattern->kind == CETTA_LD_PATTERN_APPLY_V1 &&
        text_equal(&pattern->as.apply.head, head) &&
        pattern->as.apply.arguments.len == arity;
}

static bool pattern_head_is(const CettaLdPatternV1 *pattern,
                            const char *head) {
    return pattern && pattern->kind == CETTA_LD_PATTERN_APPLY_V1 &&
        text_is(&pattern->as.apply.head, head);
}

static bool pattern_contains_head(const CettaLdPatternV1 *pattern,
                                  const char *head) {
    uint32_t index;

    if (!pattern)
        return false;
    if (pattern->kind == CETTA_LD_PATTERN_APPLY_V1) {
        if (text_is(&pattern->as.apply.head, head))
            return true;
        for (index = 0u; index < pattern->as.apply.arguments.len; index++) {
            if (pattern_contains_head(
                    &pattern->as.apply.arguments.items[index], head))
                return true;
        }
        return false;
    }
    if (pattern->kind == CETTA_LD_PATTERN_LAMBDA_V1)
        return pattern_contains_head(pattern->as.lambda.body, head);
    if (pattern->kind == CETTA_LD_PATTERN_MULTI_LAMBDA_V1)
        return pattern_contains_head(pattern->as.multi_lambda.body, head);
    if (pattern->kind == CETTA_LD_PATTERN_SUBST_V1)
        return pattern_contains_head(pattern->as.subst.body, head) ||
            pattern_contains_head(pattern->as.subst.replacement, head);
    if (pattern->kind == CETTA_LD_PATTERN_COLLECTION_V1) {
        for (index = 0u; index < pattern->as.collection.elements.len; index++) {
            if (pattern_contains_head(
                    &pattern->as.collection.elements.items[index], head))
                return true;
        }
    }
    return false;
}

static bool pattern_is_fvar(const CettaLdPatternV1 *pattern,
                            const CettaLdTextV1 *name) {
    return pattern && pattern->kind == CETTA_LD_PATTERN_FVAR_V1 &&
        text_equal(&pattern->as.fvar, name);
}

static bool rule_has_relation(const CettaLdRelationRuleV1 *rule,
                              const char *relation) {
    uint32_t index;

    if (!rule || !relation)
        return false;
    for (index = 0u; index < rule->premises.len; index++) {
        const CettaLdPremiseV1 *premise = &rule->premises.items[index];
        if (premise->kind == CETTA_LD_PREMISE_RELATION_QUERY_V1 &&
            text_is(&premise->as.relation_query.relation, relation))
            return true;
    }
    return false;
}

static bool rule_contains_head(const CettaLdRelationRuleV1 *rule,
                               const char *head) {
    uint32_t premise_index;
    uint32_t argument_index;

    if (!rule || !head)
        return false;
    if (pattern_contains_head(&rule->left, head) ||
        pattern_contains_head(&rule->right, head))
        return true;
    for (premise_index = 0u; premise_index < rule->premises.len;
         premise_index++) {
        const CettaLdPremiseV1 *premise =
            &rule->premises.items[premise_index];
        if (premise->kind != CETTA_LD_PREMISE_RELATION_QUERY_V1)
            continue;
        for (argument_index = 0u;
             argument_index < premise->as.relation_query.arguments.len;
             argument_index++) {
            if (pattern_contains_head(
                    &premise->as.relation_query.arguments.items[argument_index],
                    head))
                return true;
        }
    }
    return false;
}

static bool target_rule_class_once(const CettaLanguageDefCoreV1 *target,
                                   const char *relation,
                                   const char *instruction,
                                   const char *outcome) {
    uint32_t index;
    uint32_t count = 0u;

    for (index = 0u; index < target->rewrite_len; index++) {
        const CettaLdRelationRuleV1 *rule = &target->rewrites[index];
        if (rule_has_relation(rule, relation) &&
            rule_contains_head(rule, instruction) &&
            (!outcome || rule_contains_head(rule, outcome)))
            count++;
    }
    return count == 1u;
}

static bool target_rules_supported(const CettaLanguageDefCoreV1 *target) {
    uint32_t index;

    if (!target || target->equation_len != 0u || target->rewrite_len != 12u)
        return false;
    for (index = 0u; index < target->rewrite_len; index++) {
        const CettaLdRelationRuleV1 *rule = &target->rewrites[index];
        if (!pattern_head_is(&rule->left, "external-call:run") ||
            !(pattern_head_is(&rule->right, "external-call:run") ||
              pattern_head_is(&rule->right, "external-call:halted")))
            return false;
        if (!rule_has_relation(rule, "ExternalCallStepLimitFault") &&
            (!rule_has_relation(rule, "ExternalCallConsumeFuel") ||
             !rule_has_relation(rule, "ExternalCallFetchInstruction")))
            return false;
    }
    return
        target_rule_class_once(target, "ExternalCallStepLimitFault", "external-call:fuel-zero",
                               "external-call:outcome-resource-fault") &&
        target_rule_class_once(target, "ExternalCallIsZero", "external-call:branch-zero",
                               NULL) &&
        target_rule_class_once(target, "ExternalCallIsNonzero", "external-call:branch-zero",
                               NULL) &&
        target_rule_class_once(target, "ExternalCallCallBinaryExternal",
                               "external-call:call-binary", "external-call:external-value") &&
        target_rule_class_once(target, "ExternalCallCallBinaryExternal",
                               "external-call:call-binary",
                               "external-call:external-language-fault") &&
        target_rule_class_once(target, "ExternalCallCallBinaryExternal",
                               "external-call:call-binary",
                               "external-call:external-engine-fault") &&
        target_rule_class_once(target, "ExternalCallCallBinaryExternal",
                               "external-call:call-binary",
                               "external-call:external-resource-fault") &&
        target_rule_class_once(target, "ExternalCallReadSlot", "external-call:return-value",
                               "external-call:outcome-value") &&
        target_rule_class_once(target, "ExternalCallFetchInstruction",
                               "external-call:return-declined",
                               "external-call:outcome-declined") &&
        target_rule_class_once(target, "ExternalCallFetchInstruction",
                               "external-call:return-language-fault",
                               "external-call:outcome-language-fault") &&
        target_rule_class_once(target, "ExternalCallFetchInstruction",
                               "external-call:return-engine-fault",
                               "external-call:outcome-engine-fault") &&
        target_rule_class_once(target, "ExternalCallFetchInstruction",
                               "external-call:return-resource-fault",
                               "external-call:outcome-resource-fault");
}

static bool target_profile(const CettaLanguageDefCoreV1 *target,
                           TargetProfile *profile) {
    static const char *one_nat[] = {"Nat"};
    static const char *one_slot[] = {"SlotId"};
    static const char *one_fault[] = {"Fault"};
    static const char *branch[] = {"SlotId", "Label", "Label"};
    static const char *call[] = {
        "ExternalId", "Label", "Label", "Label", "Label"};
    static const char *cons_instruction[] = {"Instruction", "InstructionList"};
    static const char *binary[] = {
        "ExternalId", "String", "SlotId", "SlotId", "SlotId"};
    static const char *cons_external[] = {"ExternalDecl", "ExternalList"};
    static const char *program[] = {"InstructionList", "ExternalList", "Label"};
    const CettaLdGrammarRuleV1 *terms[19];
    uint32_t index;
    const char *labels[19] = {
        "external-call:nat-zero", "external-call:nat-succ", "external-call:slot-id", "external-call:label",
        "external-call:external-id", "external-call:branch-zero", "external-call:call-binary",
        "external-call:return-value", "external-call:return-declined",
        "external-call:return-language-fault", "external-call:return-engine-fault",
        "external-call:return-resource-fault", "external-call:instruction-nil",
        "external-call:instruction-cons", "external-call:binary-external", "external-call:external-nil",
        "external-call:external-cons", "external-call:program", "external-call:fault"};

    if (!target || !profile || target->term_len != 43u ||
        target->type_len != 21u || !target_rules_supported(target))
        return false;
    for (index = 0u; index < 19u; index++) {
        terms[index] = find_term(target, labels[index]);
        if (!terms[index])
            return false;
    }
    if (!term_signature_is(terms[0], "Nat", 0u, NULL) ||
        !term_signature_is(terms[1], "Nat", 1u, one_nat) ||
        !term_signature_is(terms[2], "SlotId", 1u, one_nat) ||
        !term_signature_is(terms[3], "Label", 1u, one_nat) ||
        !term_signature_is(terms[4], "ExternalId", 1u, one_nat) ||
        !term_signature_is(terms[5], "Instruction", 3u, branch) ||
        !term_signature_is(terms[6], "Instruction", 5u, call) ||
        !term_signature_is(terms[7], "Instruction", 1u, one_slot) ||
        !term_signature_is(terms[8], "Instruction", 0u, NULL) ||
        !term_signature_is(terms[9], "Instruction", 1u, one_fault) ||
        !term_signature_is(terms[10], "Instruction", 1u, one_fault) ||
        !term_signature_is(terms[11], "Instruction", 1u, one_fault) ||
        !term_signature_is(terms[12], "InstructionList", 0u, NULL) ||
        !term_signature_is(terms[13], "InstructionList", 2u,
                           cons_instruction) ||
        !term_signature_is(terms[14], "ExternalDecl", 5u, binary) ||
        !term_signature_is(terms[15], "ExternalList", 0u, NULL) ||
        !term_signature_is(terms[16], "ExternalList", 2u, cons_external) ||
        !term_signature_is(terms[17], "Program", 3u, program) ||
        !term_signature_is(terms[18], "Fault", 1u,
                           (const char *const[]){"String"}))
        return false;
    profile->nat_zero = &terms[0]->label;
    profile->nat_succ = &terms[1]->label;
    profile->slot_id = &terms[2]->label;
    profile->label = &terms[3]->label;
    profile->external_id = &terms[4]->label;
    profile->branch_zero = &terms[5]->label;
    profile->call_binary = &terms[6]->label;
    profile->return_value = &terms[7]->label;
    profile->return_declined = &terms[8]->label;
    profile->return_language_fault = &terms[9]->label;
    profile->return_engine_fault = &terms[10]->label;
    profile->return_resource_fault = &terms[11]->label;
    profile->instruction_nil = &terms[12]->label;
    profile->instruction_cons = &terms[13]->label;
    profile->binary_external = &terms[14]->label;
    profile->external_nil = &terms[15]->label;
    profile->external_cons = &terms[16]->label;
    profile->program = &terms[17]->label;
    profile->fault = &terms[18]->label;
    return true;
}

static int operation_for_relation(const CettaLdTextV1 *relation) {
    uint32_t index;

    for (index = 0u; index < CETTA_EXACT_ARITHMETIC_OP_COUNT_V1; index++) {
        if (text_is(relation, operation_specs[index].relation))
            return (int)index;
    }
    return -1;
}

static bool source_rule_operation(
    const CettaLanguageDefCoreV1 *source,
    const CettaLdRelationRuleV1 *rule,
    CettaExactArithmeticOperationV1 *operation,
    const CettaLdTextV1 **operation_head) {
    const CettaLdPremiseV1 *premise;
    const CettaLdPatternV1 *left_args;
    const CettaLdPatternV1 *right_arg;
    const CettaLdPatternV1 *query_args;
    const CettaLdGrammarRuleV1 *op_term;
    const CettaLdGrammarRuleV1 *eval_term;
    const CettaLdGrammarRuleV1 *halted_term;
    static const char *eval_types[] = {"Operation", "Integer", "Integer"};
    static const char *halted_types[] = {"Outcome"};
    int found;

    if (!source || !rule || !operation || !operation_head ||
        rule->premises.len != 1u)
        return false;
    premise = &rule->premises.items[0];
    if (premise->kind != CETTA_LD_PREMISE_RELATION_QUERY_V1 ||
        premise->as.relation_query.arguments.len != 3u)
        return false;
    found = operation_for_relation(&premise->as.relation_query.relation);
    if (found < 0)
        return false;
    if (rule->left.kind != CETTA_LD_PATTERN_APPLY_V1 ||
        rule->left.as.apply.arguments.len != 3u ||
        rule->right.kind != CETTA_LD_PATTERN_APPLY_V1 ||
        rule->right.as.apply.arguments.len != 1u)
        return false;
    left_args = rule->left.as.apply.arguments.items;
    right_arg = &rule->right.as.apply.arguments.items[0];
    query_args = premise->as.relation_query.arguments.items;
    if (left_args[0].kind != CETTA_LD_PATTERN_APPLY_V1 ||
        left_args[0].as.apply.arguments.len != 0u ||
        query_args[0].kind != CETTA_LD_PATTERN_FVAR_V1 ||
        query_args[1].kind != CETTA_LD_PATTERN_FVAR_V1 ||
        query_args[2].kind != CETTA_LD_PATTERN_FVAR_V1 ||
        !pattern_is_fvar(&left_args[1], &query_args[0].as.fvar) ||
        !pattern_is_fvar(&left_args[2], &query_args[1].as.fvar) ||
        !pattern_is_fvar(right_arg, &query_args[2].as.fvar))
        return false;
    op_term = find_term_text(source, &left_args[0].as.apply.head);
    eval_term = find_term_text(source, &rule->left.as.apply.head);
    halted_term = find_term_text(source, &rule->right.as.apply.head);
    if (!op_term || !eval_term || !halted_term ||
        !term_signature_is(op_term, "Operation", 0u, NULL) ||
        !term_signature_is(eval_term, "Config", 3u, eval_types) ||
        !eval_term->eval_policy.present ||
        eval_term->eval_policy.value != CETTA_LD_EVAL_REWRITE_V1 ||
        !term_signature_is(halted_term, "Config", 1u, halted_types))
        return false;
    *operation = (CettaExactArithmeticOperationV1)found;
    *operation_head = &op_term->label;
    return true;
}

static bool source_supported(
    const CettaLanguageDefCoreV1 *source,
    const CettaLdRelationRuleV1 *rules[CETTA_EXACT_ARITHMETIC_OP_COUNT_V1],
    const CettaLdTextV1 *heads[CETTA_EXACT_ARITHMETIC_OP_COUNT_V1]) {
    uint32_t index;

    if (!source || source->type_len != 4u || source->term_len != 11u ||
        source->equation_len != 0u ||
        source->rewrite_len != CETTA_EXACT_ARITHMETIC_OP_COUNT_V1)
        return false;
    memset(rules, 0,
           sizeof(*rules) * CETTA_EXACT_ARITHMETIC_OP_COUNT_V1);
    memset(heads, 0,
           sizeof(*heads) * CETTA_EXACT_ARITHMETIC_OP_COUNT_V1);
    for (index = 0u; index < source->rewrite_len; index++) {
        CettaExactArithmeticOperationV1 operation;
        const CettaLdTextV1 *head;
        if (!source_rule_operation(source, &source->rewrites[index],
                                   &operation, &head) ||
            rules[operation])
            return false;
        rules[operation] = &source->rewrites[index];
        heads[operation] = head;
    }
    for (index = 0u; index < CETTA_EXACT_ARITHMETIC_OP_COUNT_V1; index++) {
        if (!rules[index] || !heads[index])
            return false;
    }
    return true;
}

static bool make_apply(CettaLdPatternV1 *out,
                       const CettaLdTextV1 *head,
                       uint32_t arity) {
    CettaLdPatternV1 result;

    if (!out || !head)
        return false;
    cetta_ld_pattern_v1_init(&result);
    result.kind = CETTA_LD_PATTERN_APPLY_V1;
    if (!text_copy(&result.as.apply.head, head))
        return false;
    if (arity > 0u) {
        result.as.apply.arguments.items = calloc(
            arity, sizeof(*result.as.apply.arguments.items));
        if (!result.as.apply.arguments.items) {
            cetta_ld_pattern_v1_free(&result);
            return false;
        }
    }
    result.as.apply.arguments.len = arity;
    *out = result;
    return true;
}

static bool make_apply_c(CettaLdPatternV1 *out,
                         const char *head,
                         uint32_t arity) {
    CettaLdTextV1 text = {0};
    bool ok;

    if (!text_from_c(&text, head))
        return false;
    ok = make_apply(out, &text, arity);
    free(text.bytes);
    return ok;
}

static void move_pattern(CettaLdPatternV1 *out, CettaLdPatternV1 *source) {
    *out = *source;
    cetta_ld_pattern_v1_init(source);
}

static bool make_nat(CettaLdPatternV1 *out,
                     const TargetProfile *profile,
                     uint32_t value) {
    CettaLdPatternV1 result;

    if (value == 0u)
        return make_apply(out, profile->nat_zero, 0u);
    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->nat_succ, 1u) ||
        !make_nat(&result.as.apply.arguments.items[0], profile, value - 1u)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool make_indexed(CettaLdPatternV1 *out,
                         const CettaLdTextV1 *head,
                         const TargetProfile *profile,
                         uint32_t value) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, head, 1u) ||
        !make_nat(&result.as.apply.arguments.items[0], profile, value)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool make_fault(CettaLdPatternV1 *out,
                       const TargetProfile *profile,
                       const char *fault_name) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->fault, 1u) ||
        !make_apply_c(&result.as.apply.arguments.items[0], fault_name, 0u)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool make_return_fault(CettaLdPatternV1 *out,
                              const TargetProfile *profile,
                              const CettaLdTextV1 *head,
                              const char *fault_name) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, head, 1u) ||
        !make_fault(&result.as.apply.arguments.items[0], profile, fault_name)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool prepend_instruction(CettaLdPatternV1 *list,
                                const TargetProfile *profile,
                                CettaLdPatternV1 *instruction) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->instruction_cons, 2u))
        return false;
    move_pattern(&result.as.apply.arguments.items[0], instruction);
    move_pattern(&result.as.apply.arguments.items[1], list);
    move_pattern(list, &result);
    return true;
}

static bool append_return_tail(CettaLdPatternV1 *list,
                               const TargetProfile *profile) {
    CettaLdPatternV1 instruction;

    cetta_ld_pattern_v1_init(list);
    cetta_ld_pattern_v1_init(&instruction);
    if (!make_apply(list, profile->instruction_nil, 0u))
        return false;
    if (!make_return_fault(&instruction, profile,
                           profile->return_resource_fault,
                           "resource-fault") ||
        !prepend_instruction(list, profile, &instruction) ||
        !make_return_fault(&instruction, profile,
                           profile->return_engine_fault,
                           "engine-fault") ||
        !prepend_instruction(list, profile, &instruction) ||
        !make_return_fault(&instruction, profile,
                           profile->return_language_fault,
                           "language-fault") ||
        !prepend_instruction(list, profile, &instruction) ||
        !make_apply(&instruction, profile->return_value, 1u) ||
        !make_indexed(&instruction.as.apply.arguments.items[0],
                      profile->slot_id, profile, 2u) ||
        !prepend_instruction(list, profile, &instruction)) {
        cetta_ld_pattern_v1_free(&instruction);
        cetta_ld_pattern_v1_free(list);
        return false;
    }
    return true;
}

static bool make_call(CettaLdPatternV1 *out,
                      const TargetProfile *profile,
                      uint32_t value_target) {
    CettaLdPatternV1 result;
    uint32_t index;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->call_binary, 5u) ||
        !make_indexed(&result.as.apply.arguments.items[0],
                      profile->external_id, profile, 0u)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    for (index = 0u; index < 4u; index++) {
        if (!make_indexed(&result.as.apply.arguments.items[index + 1u],
                          profile->label, profile, value_target + index)) {
            cetta_ld_pattern_v1_free(&result);
            return false;
        }
    }
    move_pattern(out, &result);
    return true;
}

static bool make_branch(CettaLdPatternV1 *out,
                        const TargetProfile *profile) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->branch_zero, 3u) ||
        !make_indexed(&result.as.apply.arguments.items[0],
                      profile->slot_id, profile, 1u) ||
        !make_indexed(&result.as.apply.arguments.items[1],
                      profile->label, profile, 1u) ||
        !make_indexed(&result.as.apply.arguments.items[2],
                      profile->label, profile, 2u)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool make_external_list(CettaLdPatternV1 *out,
                               const TargetProfile *profile,
                               const char *provider_link) {
    CettaLdPatternV1 declaration;
    CettaLdPatternV1 nil;
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&declaration);
    cetta_ld_pattern_v1_init(&nil);
    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&declaration, profile->binary_external, 5u) ||
        !make_indexed(&declaration.as.apply.arguments.items[0],
                      profile->external_id, profile, 0u) ||
        !make_apply_c(&declaration.as.apply.arguments.items[1],
                      provider_link, 0u) ||
        !make_indexed(&declaration.as.apply.arguments.items[2],
                      profile->slot_id, profile, 0u) ||
        !make_indexed(&declaration.as.apply.arguments.items[3],
                      profile->slot_id, profile, 1u) ||
        !make_indexed(&declaration.as.apply.arguments.items[4],
                      profile->slot_id, profile, 2u) ||
        !make_apply(&nil, profile->external_nil, 0u) ||
        !make_apply(&result, profile->external_cons, 2u)) {
        cetta_ld_pattern_v1_free(&declaration);
        cetta_ld_pattern_v1_free(&nil);
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(&result.as.apply.arguments.items[0], &declaration);
    move_pattern(&result.as.apply.arguments.items[1], &nil);
    move_pattern(out, &result);
    return true;
}

static bool make_program(CettaLdPatternV1 *out,
                         const TargetProfile *profile,
                         CettaExactArithmeticOperationV1 operation) {
    CettaLdPatternV1 instructions;
    CettaLdPatternV1 instruction;
    CettaLdPatternV1 externals;
    CettaLdPatternV1 result;
    bool partial = operation_specs[operation].partial;
    bool ok;

    cetta_ld_pattern_v1_init(&instructions);
    cetta_ld_pattern_v1_init(&instruction);
    cetta_ld_pattern_v1_init(&externals);
    cetta_ld_pattern_v1_init(&result);
    ok = append_return_tail(&instructions, profile) &&
        make_call(&instruction, profile, partial ? 3u : 1u) &&
        prepend_instruction(&instructions, profile, &instruction);
    if (ok && partial) {
        ok = make_apply(&instruction, profile->return_declined, 0u) &&
            prepend_instruction(&instructions, profile, &instruction) &&
            make_branch(&instruction, profile) &&
            prepend_instruction(&instructions, profile, &instruction);
    }
    if (ok) {
        ok = make_external_list(&externals, profile,
                                operation_specs[operation].provider_link) &&
            make_apply(&result, profile->program, 3u);
    }
    if (ok) {
        move_pattern(&result.as.apply.arguments.items[0], &instructions);
        move_pattern(&result.as.apply.arguments.items[1], &externals);
        ok = make_indexed(&result.as.apply.arguments.items[2], profile->label,
                          profile, 0u);
    }
    if (ok)
        move_pattern(out, &result);
    cetta_ld_pattern_v1_free(&instructions);
    cetta_ld_pattern_v1_free(&instruction);
    cetta_ld_pattern_v1_free(&externals);
    cetta_ld_pattern_v1_free(&result);
    return ok;
}

static bool decode_nat(const CettaLdPatternV1 *pattern,
                       const TargetProfile *profile,
                       uint32_t *value) {
    uint32_t result = 0u;
    const CettaLdPatternV1 *cursor = pattern;

    if (!pattern || !profile || !value)
        return false;
    while (pattern_is_apply(cursor, profile->nat_succ, 1u)) {
        if (result == UINT32_MAX)
            return false;
        result++;
        cursor = &cursor->as.apply.arguments.items[0];
    }
    if (!pattern_is_apply(cursor, profile->nat_zero, 0u))
        return false;
    *value = result;
    return true;
}

static bool decode_indexed(const CettaLdPatternV1 *pattern,
                           const CettaLdTextV1 *head,
                           const TargetProfile *profile,
                           uint32_t expected) {
    uint32_t value;

    return pattern_is_apply(pattern, head, 1u) &&
        decode_nat(&pattern->as.apply.arguments.items[0], profile, &value) &&
        value == expected;
}

static bool decode_instruction_list(
    const CettaLdPatternV1 *pattern,
    const TargetProfile *profile,
    const CettaLdPatternV1 *items[7],
    uint32_t *len) {
    const CettaLdPatternV1 *cursor = pattern;
    uint32_t count = 0u;

    if (!pattern || !profile || !items || !len)
        return false;
    while (pattern_is_apply(cursor, profile->instruction_cons, 2u)) {
        if (count == 7u)
            return false;
        items[count++] = &cursor->as.apply.arguments.items[0];
        cursor = &cursor->as.apply.arguments.items[1];
    }
    if (!pattern_is_apply(cursor, profile->instruction_nil, 0u))
        return false;
    *len = count;
    return true;
}

static bool decode_call(const CettaLdPatternV1 *pattern,
                        const TargetProfile *profile,
                        uint32_t first_target) {
    uint32_t index;

    if (!pattern_is_apply(pattern, profile->call_binary, 5u) ||
        !decode_indexed(&pattern->as.apply.arguments.items[0],
                        profile->external_id, profile, 0u))
        return false;
    for (index = 0u; index < 4u; index++) {
        if (!decode_indexed(&pattern->as.apply.arguments.items[index + 1u],
                            profile->label, profile,
                            first_target + index))
            return false;
    }
    return true;
}

static bool decode_fault_return(const CettaLdPatternV1 *pattern,
                                const CettaLdTextV1 *return_head,
                                const TargetProfile *profile,
                                const char *fault_name) {
    const CettaLdPatternV1 *fault;
    const CettaLdPatternV1 *name;

    if (!pattern_is_apply(pattern, return_head, 1u))
        return false;
    fault = &pattern->as.apply.arguments.items[0];
    if (!pattern_is_apply(fault, profile->fault, 1u))
        return false;
    name = &fault->as.apply.arguments.items[0];
    return name->kind == CETTA_LD_PATTERN_APPLY_V1 &&
        name->as.apply.arguments.len == 0u &&
        text_is(&name->as.apply.head, fault_name);
}

bool cetta_exact_arithmetic_external_call_program_v1_inspect(
    const CettaLanguageDefCoreV1 *target,
    const CettaLdPatternV1 *program,
    CettaExactArithmeticExternalCallProgramViewV1 *view) {
    TargetProfile profile;
    const CettaLdPatternV1 *instructions[7] = {0};
    const CettaLdPatternV1 *external_list;
    const CettaLdPatternV1 *declaration;
    const CettaLdPatternV1 *provider;
    uint32_t instruction_len;
    bool guarded;

    if (!target || !program || !view || !target_profile(target, &profile) ||
        !pattern_is_apply(program, profile.program, 3u) ||
        !decode_instruction_list(&program->as.apply.arguments.items[0],
                                 &profile, instructions,
                                 &instruction_len) ||
        !decode_indexed(&program->as.apply.arguments.items[2], profile.label,
                        &profile, 0u))
        return false;
    external_list = &program->as.apply.arguments.items[1];
    if (!pattern_is_apply(external_list, profile.external_cons, 2u) ||
        !pattern_is_apply(&external_list->as.apply.arguments.items[1],
                          profile.external_nil, 0u))
        return false;
    declaration = &external_list->as.apply.arguments.items[0];
    if (!pattern_is_apply(declaration, profile.binary_external, 5u) ||
        !decode_indexed(&declaration->as.apply.arguments.items[0],
                        profile.external_id, &profile, 0u) ||
        !decode_indexed(&declaration->as.apply.arguments.items[2],
                        profile.slot_id, &profile, 0u) ||
        !decode_indexed(&declaration->as.apply.arguments.items[3],
                        profile.slot_id, &profile, 1u) ||
        !decode_indexed(&declaration->as.apply.arguments.items[4],
                        profile.slot_id, &profile, 2u))
        return false;
    provider = &declaration->as.apply.arguments.items[1];
    if (provider->kind != CETTA_LD_PATTERN_APPLY_V1 ||
        provider->as.apply.arguments.len != 0u)
        return false;
    guarded = instruction_len == 7u;
    if (!(guarded || instruction_len == 5u))
        return false;
    if (guarded &&
        (!pattern_is_apply(instructions[0], profile.branch_zero, 3u) ||
         !decode_indexed(&instructions[0]->as.apply.arguments.items[0],
                         profile.slot_id, &profile, 1u) ||
         !decode_indexed(&instructions[0]->as.apply.arguments.items[1],
                         profile.label, &profile, 1u) ||
         !decode_indexed(&instructions[0]->as.apply.arguments.items[2],
                         profile.label, &profile, 2u) ||
         !pattern_is_apply(instructions[1], profile.return_declined, 0u)))
        return false;
    if (!decode_call(instructions[guarded ? 2u : 0u], &profile,
                     guarded ? 3u : 1u) ||
        !pattern_is_apply(instructions[guarded ? 3u : 1u],
                          profile.return_value, 1u) ||
        !decode_indexed(
            &instructions[guarded ? 3u : 1u]->as.apply.arguments.items[0],
            profile.slot_id, &profile, 2u) ||
        !decode_fault_return(instructions[guarded ? 4u : 2u],
                             profile.return_language_fault, &profile,
                             "language-fault") ||
        !decode_fault_return(instructions[guarded ? 5u : 3u],
                             profile.return_engine_fault, &profile,
                             "engine-fault") ||
        !decode_fault_return(instructions[guarded ? 6u : 4u],
                             profile.return_resource_fault, &profile,
                             "resource-fault"))
        return false;
    view->guarded = guarded;
    view->provider_link = &provider->as.apply.head;
    return true;
}

void cetta_exact_arithmetic_external_call_transform_v1_init(
    CettaExactArithmeticExternalCallTransformV1 *transform) {
    if (transform)
        memset(transform, 0, sizeof(*transform));
}

void cetta_exact_arithmetic_external_call_transform_v1_free(
    CettaExactArithmeticExternalCallTransformV1 *transform) {
    uint32_t index;

    if (!transform)
        return;
    for (index = 0u; index < transform->entry_len; index++) {
        cetta_ld_pattern_v1_free(&transform->entries[index].source_operation);
        cetta_ld_pattern_v1_free(&transform->entries[index].target_program);
    }
    memset(transform, 0, sizeof(*transform));
}

bool cetta_exact_arithmetic_to_external_call_transform_v1(
    CettaExactArithmeticExternalCallTransformV1 *out,
    const CettaLanguageDefCoreV1 *source,
    const CettaLanguageDefCoreV1 *target,
    CettaExactArithmeticExternalCallTransformStatusV1 *status,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLdRelationRuleV1
        *source_rules[CETTA_EXACT_ARITHMETIC_OP_COUNT_V1];
    const CettaLdTextV1
        *source_heads[CETTA_EXACT_ARITHMETIC_OP_COUNT_V1];
    TargetProfile profile;
    CettaExactArithmeticExternalCallTransformV1 candidate;
    uint32_t operation;

    set_status(status, CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_OK_V1);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out || !source || !target) {
        set_status(status,
                   CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_BAD_ARGUMENT_V1);
        set_error(error_buf, error_buf_size,
                  "transform requires output, source, and target LanguageDefs");
        return false;
    }
    if (!source_supported(source, source_rules, source_heads)) {
        set_status(status,
                   CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_UNSUPPORTED_SOURCE_V1);
        set_error(error_buf, error_buf_size,
                  "source is not the supported structural ExactArithmetic profile");
        return false;
    }
    memset(&profile, 0, sizeof(profile));
    if (!target_profile(target, &profile)) {
        set_status(status,
                   CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_UNSUPPORTED_TARGET_V1);
        set_error(error_buf, error_buf_size,
                  "target is not the supported structural external-call profile");
        return false;
    }
    cetta_exact_arithmetic_external_call_transform_v1_init(&candidate);
    for (operation = 0u;
         operation < CETTA_EXACT_ARITHMETIC_OP_COUNT_V1;
         operation++) {
        CettaExactArithmeticExternalCallEntryV1 *entry =
            &candidate.entries[operation];
        entry->operation = (CettaExactArithmeticOperationV1)operation;
        entry->source_rewrite_index =
            (uint32_t)(source_rules[operation] - source->rewrites);
        if (!make_apply(&entry->source_operation, source_heads[operation], 0u) ||
            !make_program(&entry->target_program, &profile,
                          (CettaExactArithmeticOperationV1)operation)) {
            candidate.entry_len = operation + 1u;
            cetta_exact_arithmetic_external_call_transform_v1_free(&candidate);
            set_status(status,
                CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_ALLOCATION_FAILURE_V1);
            set_error(error_buf, error_buf_size,
                      "failed to allocate canonical source/target Patterns");
            return false;
        }
        candidate.entry_len++;
    }
    cetta_exact_arithmetic_external_call_transform_v1_free(out);
    *out = candidate;
    return true;
}

const char *cetta_exact_arithmetic_operation_v1_name(
    CettaExactArithmeticOperationV1 operation) {
    static const char *names[CETTA_EXACT_ARITHMETIC_OP_COUNT_V1] = {
        "add", "sub", "mul", "tquot", "fquot", "trem", "frem"};
    return operation < CETTA_EXACT_ARITHMETIC_OP_COUNT_V1
        ? names[operation]
        : "invalid";
}

const char *cetta_exact_arithmetic_external_call_transform_status_v1_name(
    CettaExactArithmeticExternalCallTransformStatusV1 status) {
    switch (status) {
    case CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_OK_V1:
        return "ok";
    case CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_BAD_ARGUMENT_V1:
        return "bad_argument";
    case CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_UNSUPPORTED_SOURCE_V1:
        return "unsupported_source";
    case CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_UNSUPPORTED_TARGET_V1:
        return "unsupported_target";
    case CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_ALLOCATION_FAILURE_V1:
        return "allocation_failure";
    }
    return "unknown";
}
