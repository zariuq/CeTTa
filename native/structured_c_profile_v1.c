#include "structured_c_profile_v1.h"

#include <stdint.h>
#include <string.h>

static bool text_is(const CettaLdTextV1 *text, const char *literal) {
    size_t len;

    if (!text || !literal)
        return false;
    len = strlen(literal);
    return len == text->len &&
        (len == 0u || memcmp(text->bytes, literal, len) == 0);
}

static bool type_is_base(const CettaLdTypeExprV1 *type, const char *name) {
    return type && type->kind == CETTA_LD_TYPE_BASE_V1 &&
        text_is(&type->as.base, name);
}

static bool text_is_span(const CettaLdTextV1 *text,
                         const char *start,
                         size_t len) {
    return text && start && text->len == len &&
        (len == 0u || memcmp(text->bytes, start, len) == 0);
}

static void skip_spaces(const char **cursor) {
    while (**cursor == ' ' || **cursor == '\t' || **cursor == '\n')
        (*cursor)++;
}

static bool read_token(const char **cursor,
                       const char **start,
                       size_t *len) {
    const char *begin;

    if (!cursor || !*cursor || !start || !len)
        return false;
    skip_spaces(cursor);
    begin = *cursor;
    while (**cursor != '\0' && **cursor != ' ' && **cursor != '\t' &&
           **cursor != '\n' && **cursor != '(' && **cursor != ')' &&
           **cursor != ':' && **cursor != ',')
        (*cursor)++;
    if (*cursor == begin)
        return false;
    *start = begin;
    *len = (size_t)(*cursor - begin);
    return true;
}

static bool read_pattern_token(const char **cursor,
                               const char **start,
                               size_t *len) {
    const char *begin;

    if (!cursor || !*cursor || !start || !len)
        return false;
    skip_spaces(cursor);
    begin = *cursor;
    while (**cursor != '\0' && **cursor != ' ' && **cursor != '\t' &&
           **cursor != '\n' && **cursor != '(' && **cursor != ')')
        (*cursor)++;
    if (*cursor == begin)
        return false;
    *start = begin;
    *len = (size_t)(*cursor - begin);
    return true;
}

static bool match_pattern_at(const CettaLdPatternV1 *pattern,
                             const char **cursor) {
    const char *token;
    size_t token_len;
    uint32_t argument_index = 0u;

    if (!pattern || !cursor || !*cursor)
        return false;
    skip_spaces(cursor);
    if (**cursor == '$') {
        (*cursor)++;
        return pattern->kind == CETTA_LD_PATTERN_FVAR_V1 &&
            read_pattern_token(cursor, &token, &token_len) &&
            text_is_span(&pattern->as.fvar, token, token_len);
    }
    if (**cursor != '(' || pattern->kind != CETTA_LD_PATTERN_APPLY_V1)
        return false;
    (*cursor)++;
    if (!read_pattern_token(cursor, &token, &token_len) ||
        !text_is_span(&pattern->as.apply.head, token, token_len))
        return false;
    for (;;) {
        skip_spaces(cursor);
        if (**cursor == ')') {
            (*cursor)++;
            return argument_index == pattern->as.apply.arguments.len;
        }
        if (**cursor == '\0' ||
            argument_index >= pattern->as.apply.arguments.len ||
            !match_pattern_at(
                &pattern->as.apply.arguments.items[argument_index], cursor))
            return false;
        argument_index++;
    }
}

static bool pattern_matches(const CettaLdPatternV1 *pattern,
                            const char *expected) {
    const char *cursor = expected;

    return expected && match_pattern_at(pattern, &cursor) &&
        (skip_spaces(&cursor), *cursor == '\0');
}

static bool premise_matches(const CettaLdPremiseV1 *premise,
                            const char *expected) {
    const char *cursor = expected;
    const char *token;
    size_t token_len;
    uint32_t argument_index = 0u;

    if (!premise || !expected ||
        premise->kind != CETTA_LD_PREMISE_RELATION_QUERY_V1)
        return false;
    skip_spaces(&cursor);
    if (*cursor != '(')
        return false;
    cursor++;
    if (!read_pattern_token(&cursor, &token, &token_len) ||
        !text_is_span(&premise->as.relation_query.relation,
                      token, token_len))
        return false;
    for (;;) {
        skip_spaces(&cursor);
        if (*cursor == ')') {
            cursor++;
            skip_spaces(&cursor);
            return *cursor == '\0' &&
                argument_index == premise->as.relation_query.arguments.len;
        }
        if (*cursor == '\0' ||
            argument_index >= premise->as.relation_query.arguments.len ||
            !match_pattern_at(
                &premise->as.relation_query.arguments.items[argument_index],
                &cursor))
            return false;
        argument_index++;
    }
}

static bool context_matches(const CettaLdRelationRuleV1 *rule,
                            const char *expected) {
    const char *cursor = expected;
    const char *name;
    const char *type;
    size_t name_len;
    size_t type_len;
    uint32_t index = 0u;

    if (!rule || !expected)
        return false;
    skip_spaces(&cursor);
    if (*cursor == '\0')
        return rule->type_context_len == 0u;
    for (;;) {
        if (index >= rule->type_context_len ||
            !read_token(&cursor, &name, &name_len) || *cursor != ':')
            return false;
        cursor++;
        if (!read_token(&cursor, &type, &type_len) ||
            !text_is_span(&rule->type_context[index].name, name, name_len) ||
            rule->type_context[index].type.kind != CETTA_LD_TYPE_BASE_V1 ||
            !text_is_span(&rule->type_context[index].type.as.base,
                          type, type_len))
            return false;
        index++;
        skip_spaces(&cursor);
        if (*cursor == '\0')
            return index == rule->type_context_len;
        if (*cursor != ',')
            return false;
        cursor++;
        skip_spaces(&cursor);
    }
}

static bool term_parameters_match(const CettaLdGrammarRuleV1 *term,
                                  const char *expected) {
    const char *cursor = expected;
    const char *name;
    const char *type;
    size_t name_len;
    size_t type_len;
    uint32_t index = 0u;

    if (!term || !expected)
        return false;
    skip_spaces(&cursor);
    if (*cursor == '\0')
        return term->param_len == 0u;
    for (;;) {
        if (index >= term->param_len ||
            !read_token(&cursor, &name, &name_len) || *cursor != ':')
            return false;
        cursor++;
        if (!read_token(&cursor, &type, &type_len) ||
            term->params[index].kind != CETTA_LD_PARAM_SIMPLE_V1 ||
            !text_is_span(&term->params[index].body_name, name, name_len) ||
            term->params[index].type.kind != CETTA_LD_TYPE_BASE_V1 ||
            !text_is_span(&term->params[index].type.as.base, type, type_len))
            return false;
        index++;
        skip_spaces(&cursor);
        if (*cursor == '\0')
            return index == term->param_len;
        if (*cursor != ',')
            return false;
        cursor++;
        skip_spaces(&cursor);
    }
}

typedef struct {
    const char *label;
    const char *category;
    const char *parameters;
    bool rewrite;
} ExpectedTerm;

static bool term_matches_exact(const CettaLdGrammarRuleV1 *term,
                               const ExpectedTerm *expected) {
    return term && expected && text_is(&term->label, expected->label) &&
        text_is(&term->category, expected->category) &&
        term_parameters_match(term, expected->parameters) &&
        term->syntax_pattern.len == 1u &&
        term->syntax_pattern.items[0].kind == CETTA_LD_SYNTAX_TERMINAL_V1 &&
        text_is(&term->syntax_pattern.items[0].as.text, expected->label) &&
        term->eval_policy.present == expected->rewrite &&
        (!expected->rewrite ||
         term->eval_policy.value == CETTA_LD_EVAL_REWRITE_V1);
}

typedef struct {
    const char *context;
    uint32_t premise_count;
    const char *premises[2];
    const char *left;
    const char *right;
} ExpectedRewrite;

static bool rewrite_matches_exact(const CettaLdRelationRuleV1 *rule,
                                  const ExpectedRewrite *expected) {
    uint32_t index;

    if (!rule || !expected ||
        !context_matches(rule, expected->context) ||
        rule->premises.len != expected->premise_count ||
        !pattern_matches(&rule->left, expected->left) ||
        !pattern_matches(&rule->right, expected->right))
        return false;
    for (index = 0u; index < expected->premise_count; index++) {
        if (!premise_matches(&rule->premises.items[index],
                             expected->premises[index]))
            return false;
    }
    return true;
}

static const CettaLdGrammarRuleV1 *find_term(
    const CettaLanguageDefCoreV1 *language,
    const char *label) {
    const CettaLdGrammarRuleV1 *result = NULL;
    uint32_t index;

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

static const CettaLdTextV1 *profile_term(
    const CettaLanguageDefCoreV1 *language,
    const char *label,
    const char *category,
    uint32_t arity,
    const char *const *parameter_types) {
    const CettaLdGrammarRuleV1 *term = find_term(language, label);

    return term_signature_is(term, category, arity, parameter_types)
        ? &term->label
        : NULL;
}

static bool language_types_exact(const CettaLanguageDefCoreV1 *language) {
    static const char *const names[] = {
        "Integer", "String", "Identifier", "FunctionName", "ExternalName",
        "CType", "Value", "Expression", "Expressions", "Statement",
        "Statements", "Case", "Cases", "Parameter", "Parameters",
        "Function", "Functions", "ExternalFunction", "ExternalFunctions",
        "Program", "Environment", "Receipt", "Fault", "EvaluationResult",
        "Outcome", "Config"
    };
    uint32_t index;

    if (!language || language->type_len !=
        (uint32_t)(sizeof(names) / sizeof(names[0])))
        return false;
    for (index = 0u; index < language->type_len; index++) {
        CettaLdCarrierKindV1 expected = CETTA_LD_CARRIER_AST_V1;

        if (index == 0u)
            expected = CETTA_LD_CARRIER_BUILTIN_INT_V1;
        else if (index == 1u)
            expected = CETTA_LD_CARRIER_BUILTIN_STRING_V1;
        if (!text_is(&language->types[index].name, names[index]) ||
            language->types[index].carrier != expected)
            return false;
    }
    return true;
}

static bool language_terms_exact(const CettaLanguageDefCoreV1 *language) {
    static const ExpectedTerm expected[] = {
        {"structured-c:identifier", "Identifier", "name:String", false},
        {"structured-c:function-name", "FunctionName", "name:String", false},
        {"structured-c:external-name", "ExternalName", "name:String", false},
        {"structured-c:type-named", "CType", "name:Identifier", false},
        {"structured-c:type-pointer", "CType", "target:CType", false},
        {"structured-c:type-const", "CType", "target:CType", false},
        {"structured-c:value-integer", "Value", "value:Integer", false},
        {"structured-c:value-symbol", "Value", "name:Identifier", false},
        {"structured-c:value-unit", "Value", "", false},
        {"structured-c:expression-variable", "Expression",
         "name:Identifier", false},
        {"structured-c:expression-constant", "Expression",
         "value:Value", false},
        {"structured-c:expression-call", "Expression",
         "function:ExternalName,arguments:Expressions", false},
        {"structured-c:expressions-nil", "Expressions", "", false},
        {"structured-c:expressions-cons", "Expressions",
         "expression:Expression,rest:Expressions", false},
        {"structured-c:assign", "Statement",
         "variable:Identifier,expression:Expression", false},
        {"structured-c:declare", "Statement",
         "variable:Identifier,type:CType,expression:Expression", false},
        {"structured-c:effect", "Statement", "expression:Expression", false},
        {"structured-c:if", "Statement",
         "condition:Expression,thenBranch:Statements,elseBranch:Statements",
         false},
        {"structured-c:switch", "Statement",
         "scrutinee:Expression,cases:Cases,defaultBranch:Statements", false},
        {"structured-c:return", "Statement", "expression:Expression", false},
        {"structured-c:statements-nil", "Statements", "", false},
        {"structured-c:statements-cons", "Statements",
         "statement:Statement,rest:Statements", false},
        {"structured-c:statements-append", "Statements",
         "first:Statements,second:Statements", false},
        {"structured-c:case", "Case", "value:Value,body:Statements", false},
        {"structured-c:cases-nil", "Cases", "", false},
        {"structured-c:cases-cons", "Cases", "case:Case,rest:Cases", false},
        {"structured-c:parameter", "Parameter",
         "name:Identifier,type:CType", false},
        {"structured-c:parameters-nil", "Parameters", "", false},
        {"structured-c:parameters-cons", "Parameters",
         "parameter:Parameter,rest:Parameters", false},
        {"structured-c:function", "Function",
         "name:FunctionName,returnType:CType,parameters:Parameters,"
         "body:Statements", false},
        {"structured-c:external-function", "ExternalFunction",
         "name:ExternalName,returnType:CType,parameters:Parameters", false},
        {"structured-c:external-functions-nil", "ExternalFunctions", "",
         false},
        {"structured-c:external-functions-cons", "ExternalFunctions",
         "function:ExternalFunction,rest:ExternalFunctions", false},
        {"structured-c:functions-nil", "Functions", "", false},
        {"structured-c:functions-cons", "Functions",
         "function:Function,rest:Functions", false},
        {"structured-c:program", "Program",
         "externals:ExternalFunctions,functions:Functions", false},
        {"structured-c:evaluation-value", "EvaluationResult",
         "value:Value", false},
        {"structured-c:evaluation-fault", "EvaluationResult",
         "fault:Fault", false},
        {"structured-c:fault-language", "Fault", "message:String", false},
        {"structured-c:fault-engine", "Fault", "message:String", false},
        {"structured-c:fault-resource", "Fault", "message:String", false},
        {"structured-c:outcome-return", "Outcome", "value:Value", false},
        {"structured-c:outcome-fallthrough", "Outcome", "", false},
        {"structured-c:outcome-fault", "Outcome", "fault:Fault", false},
        {"structured-c:environment-empty", "Environment", "", false},
        {"structured-c:environment-bind", "Environment",
         "variable:Identifier,value:Value,rest:Environment", false},
        {"structured-c:receipt-ready", "Receipt", "", false},
        {"structured-c:receipt-empty", "Receipt", "", false},
        {"structured-c:receipt-step", "Receipt", "at:Value,prior:Receipt",
         false},
        {"structured-c:receipt-external", "Receipt",
         "at:Value,external:Value,outcome:Value,prior:Receipt", false},
        {"structured-c:receipt-finished", "Receipt",
         "outcome:Value,prior:Receipt", false},
        {"structured-c:receipt-incomplete", "Receipt", "prior:Receipt",
         false},
        {"structured-c:run", "Config",
         "statements:Statements,environment:Environment,receipt:Receipt",
         true},
        {"structured-c:halted", "Config",
         "outcome:Outcome,environment:Environment,receipt:Receipt", false}
    };
    uint32_t index;

    if (!language || language->term_len !=
        (uint32_t)(sizeof(expected) / sizeof(expected[0])))
        return false;
    for (index = 0u; index < language->term_len; index++) {
        if (!term_matches_exact(&language->terms[index], &expected[index]))
            return false;
    }
    return true;
}

static bool language_rewrites_exact(const CettaLanguageDefCoreV1 *language) {
    static const ExpectedRewrite expected[] = {
        {
            "environment:Environment,receipt:Receipt", 0u, {NULL, NULL},
            "(structured-c:run (structured-c:statements-nil) $environment "
            "$receipt)",
            "(structured-c:halted (structured-c:outcome-fallthrough) "
            "$environment $receipt)"
        },
        {
            "continuation:Statements,environment:Environment,receipt:Receipt",
            0u, {NULL, NULL},
            "(structured-c:run (structured-c:statements-append "
            "(structured-c:statements-nil) $continuation) $environment "
            "$receipt)",
            "(structured-c:run $continuation $environment $receipt)"
        },
        {
            "statement:Statement,tail:Statements,continuation:Statements,"
            "environment:Environment,receipt:Receipt", 0u, {NULL, NULL},
            "(structured-c:run (structured-c:statements-append "
            "(structured-c:statements-cons $statement $tail) $continuation) "
            "$environment $receipt)",
            "(structured-c:run (structured-c:statements-cons $statement "
            "(structured-c:statements-append $tail $continuation)) "
            "$environment $receipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "variable:Identifier,expression:Expression,value:Value,"
            "evaluatedEnvironment:Environment,evaluatedReceipt:Receipt,"
            "nextEnvironment:Environment", 2u,
            {
                "(StructuredCEvaluate $expression $environment "
                "$receipt (structured-c:evaluation-value $value) "
                "$evaluatedEnvironment $evaluatedReceipt)",
                "(StructuredCStore $evaluatedEnvironment $variable $value "
                "$nextEnvironment)"
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:assign $variable $expression) $rest) "
            "$environment $receipt)",
            "(structured-c:run $rest $nextEnvironment $evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "variable:Identifier,expression:Expression,fault:Fault,"
            "evaluatedEnvironment:Environment,evaluatedReceipt:Receipt", 1u,
            {
                "(StructuredCEvaluate $expression $environment "
                "$receipt (structured-c:evaluation-fault $fault) "
                "$evaluatedEnvironment $evaluatedReceipt)", NULL
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:assign $variable $expression) $rest) "
            "$environment $receipt)",
            "(structured-c:halted (structured-c:outcome-fault $fault) "
            "$evaluatedEnvironment $evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "variable:Identifier,type:CType,expression:Expression,"
            "value:Value,evaluatedEnvironment:Environment,"
            "evaluatedReceipt:Receipt,nextEnvironment:Environment", 2u,
            {
                "(StructuredCEvaluate $expression $environment "
                "$receipt (structured-c:evaluation-value $value) "
                "$evaluatedEnvironment $evaluatedReceipt)",
                "(StructuredCStore $evaluatedEnvironment $variable $value "
                "$nextEnvironment)"
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:declare $variable $type $expression) $rest) "
            "$environment $receipt)",
            "(structured-c:run $rest $nextEnvironment $evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "variable:Identifier,type:CType,expression:Expression,"
            "fault:Fault,evaluatedEnvironment:Environment,"
            "evaluatedReceipt:Receipt", 1u,
            {
                "(StructuredCEvaluate $expression $environment "
                "$receipt (structured-c:evaluation-fault $fault) "
                "$evaluatedEnvironment $evaluatedReceipt)", NULL
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:declare $variable $type $expression) $rest) "
            "$environment $receipt)",
            "(structured-c:halted (structured-c:outcome-fault $fault) "
            "$evaluatedEnvironment $evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "expression:Expression,value:Value,"
            "evaluatedEnvironment:Environment,evaluatedReceipt:Receipt", 1u,
            {
                "(StructuredCEvaluate $expression $environment "
                "$receipt (structured-c:evaluation-value $value) "
                "$evaluatedEnvironment $evaluatedReceipt)", NULL
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:effect $expression) $rest) $environment $receipt)",
            "(structured-c:run $rest $evaluatedEnvironment "
            "$evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "expression:Expression,fault:Fault,"
            "evaluatedEnvironment:Environment,evaluatedReceipt:Receipt", 1u,
            {
                "(StructuredCEvaluate $expression $environment "
                "$receipt (structured-c:evaluation-fault $fault) "
                "$evaluatedEnvironment $evaluatedReceipt)", NULL
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:effect $expression) $rest) $environment $receipt)",
            "(structured-c:halted (structured-c:outcome-fault $fault) "
            "$evaluatedEnvironment $evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "condition:Expression,thenBranch:Statements,"
            "elseBranch:Statements,value:Value,"
            "evaluatedEnvironment:Environment,evaluatedReceipt:Receipt,"
            "selected:Statements", 2u,
            {
                "(StructuredCEvaluate $condition $environment "
                "$receipt (structured-c:evaluation-value $value) "
                "$evaluatedEnvironment $evaluatedReceipt)",
                "(StructuredCSelectBranch $value $thenBranch $elseBranch "
                "$selected)"
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:if $condition $thenBranch $elseBranch) $rest) "
            "$environment $receipt)",
            "(structured-c:run (structured-c:statements-append $selected "
            "$rest) $evaluatedEnvironment $evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "condition:Expression,thenBranch:Statements,"
            "elseBranch:Statements,fault:Fault,"
            "evaluatedEnvironment:Environment,evaluatedReceipt:Receipt", 1u,
            {
                "(StructuredCEvaluate $condition $environment "
                "$receipt (structured-c:evaluation-fault $fault) "
                "$evaluatedEnvironment $evaluatedReceipt)", NULL
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:if $condition $thenBranch $elseBranch) $rest) "
            "$environment $receipt)",
            "(structured-c:halted (structured-c:outcome-fault $fault) "
            "$evaluatedEnvironment $evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "scrutinee:Expression,cases:Cases,defaultBranch:Statements,"
            "value:Value,evaluatedEnvironment:Environment,"
            "evaluatedReceipt:Receipt,selected:Statements", 2u,
            {
                "(StructuredCEvaluate $scrutinee $environment "
                "$receipt (structured-c:evaluation-value $value) "
                "$evaluatedEnvironment $evaluatedReceipt)",
                "(StructuredCSelectCase $cases $defaultBranch $value "
                "$selected)"
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:switch $scrutinee $cases $defaultBranch) $rest) "
            "$environment $receipt)",
            "(structured-c:run (structured-c:statements-append $selected "
            "$rest) $evaluatedEnvironment $evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "scrutinee:Expression,cases:Cases,defaultBranch:Statements,"
            "fault:Fault,evaluatedEnvironment:Environment,"
            "evaluatedReceipt:Receipt", 1u,
            {
                "(StructuredCEvaluate $scrutinee $environment "
                "$receipt (structured-c:evaluation-fault $fault) "
                "$evaluatedEnvironment $evaluatedReceipt)", NULL
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:switch $scrutinee $cases $defaultBranch) $rest) "
            "$environment $receipt)",
            "(structured-c:halted (structured-c:outcome-fault $fault) "
            "$evaluatedEnvironment $evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "expression:Expression,value:Value,"
            "evaluatedEnvironment:Environment,evaluatedReceipt:Receipt", 1u,
            {
                "(StructuredCEvaluate $expression $environment "
                "$receipt (structured-c:evaluation-value $value) "
                "$evaluatedEnvironment $evaluatedReceipt)", NULL
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:return $expression) $rest) $environment $receipt)",
            "(structured-c:halted (structured-c:outcome-return $value) "
            "$evaluatedEnvironment $evaluatedReceipt)"
        },
        {
            "environment:Environment,receipt:Receipt,rest:Statements,"
            "expression:Expression,fault:Fault,"
            "evaluatedEnvironment:Environment,evaluatedReceipt:Receipt", 1u,
            {
                "(StructuredCEvaluate $expression $environment "
                "$receipt (structured-c:evaluation-fault $fault) "
                "$evaluatedEnvironment $evaluatedReceipt)", NULL
            },
            "(structured-c:run (structured-c:statements-cons "
            "(structured-c:return $expression) $rest) $environment $receipt)",
            "(structured-c:halted (structured-c:outcome-fault $fault) "
            "$evaluatedEnvironment $evaluatedReceipt)"
        }
    };
    uint32_t index;

    if (!language || language->equation_len != 0u ||
        language->rewrite_len !=
            (uint32_t)(sizeof(expected) / sizeof(expected[0])))
        return false;
    for (index = 0u; index < language->rewrite_len; index++) {
        if (!rewrite_matches_exact(&language->rewrites[index],
                                   &expected[index]))
            return false;
    }
    return true;
}

static bool language_profile_exact(const CettaLanguageDefCoreV1 *language) {
    return language && text_is(&language->name, "StructuredC") &&
        language_types_exact(language) && language_terms_exact(language) &&
        language_rewrites_exact(language);
}

bool cetta_structured_c_profile_v1_admit(
    const CettaLanguageDefCoreV1 *language,
    CettaStructuredCProfileV1 *profile) {
    static const char *const string1[] = {"String"};
    static const char *const integer1[] = {"Integer"};
    static const char *const identifier1[] = {"Identifier"};
    static const char *const ctype1[] = {"CType"};
    static const char *const value1[] = {"Value"};
    static const char *const expression1[] = {"Expression"};
    static const char *const expressions2[] = {"Expression", "Expressions"};
    static const char *const declare3[] = {"Identifier", "CType", "Expression"};
    static const char *const if3[] = {"Expression", "Statements", "Statements"};
    static const char *const switch3[] = {"Expression", "Cases", "Statements"};
    static const char *const statements2[] = {"Statement", "Statements"};
    static const char *const case2[] = {"Value", "Statements"};
    static const char *const cases2[] = {"Case", "Cases"};
    static const char *const parameter2[] = {"Identifier", "CType"};
    static const char *const parameters2[] = {"Parameter", "Parameters"};
    static const char *const function4[] = {
        "FunctionName", "CType", "Parameters", "Statements"};
    static const char *const external3[] = {
        "ExternalName", "CType", "Parameters"};
    static const char *const externals2[] = {
        "ExternalFunction", "ExternalFunctions"};
    static const char *const functions2[] = {"Function", "Functions"};
    static const char *const program2[] = {"ExternalFunctions", "Functions"};

#define PROFILE(field, label, category, arity, types) \
    do { \
        profile->field = profile_term(language, label, category, arity, types); \
        if (!profile->field) \
            return false; \
    } while (0)

    if (!profile || !language_profile_exact(language))
        return false;
    memset(profile, 0, sizeof(*profile));
    PROFILE(identifier, "structured-c:identifier", "Identifier", 1u, string1);
    PROFILE(function_name, "structured-c:function-name", "FunctionName", 1u,
            string1);
    PROFILE(external_name, "structured-c:external-name", "ExternalName", 1u,
            string1);
    PROFILE(type_named, "structured-c:type-named", "CType", 1u, identifier1);
    PROFILE(type_pointer, "structured-c:type-pointer", "CType", 1u, ctype1);
    PROFILE(type_const, "structured-c:type-const", "CType", 1u, ctype1);
    PROFILE(value_integer, "structured-c:value-integer", "Value", 1u, integer1);
    PROFILE(value_symbol, "structured-c:value-symbol", "Value", 1u, identifier1);
    PROFILE(expression_variable, "structured-c:expression-variable", "Expression",
            1u, identifier1);
    PROFILE(expression_constant, "structured-c:expression-constant", "Expression",
            1u, value1);
    {
        static const char *const call2[] = {"ExternalName", "Expressions"};
        PROFILE(expression_call, "structured-c:expression-call", "Expression",
                2u, call2);
    }
    PROFILE(expressions_nil, "structured-c:expressions-nil", "Expressions", 0u,
            NULL);
    PROFILE(expressions_cons, "structured-c:expressions-cons", "Expressions", 2u,
            expressions2);
    PROFILE(declare, "structured-c:declare", "Statement", 3u, declare3);
    PROFILE(effect, "structured-c:effect", "Statement", 1u, expression1);
    PROFILE(if_statement, "structured-c:if", "Statement", 3u, if3);
    PROFILE(switch_statement, "structured-c:switch", "Statement", 3u, switch3);
    PROFILE(return_statement, "structured-c:return", "Statement", 1u,
            expression1);
    PROFILE(statements_nil, "structured-c:statements-nil", "Statements", 0u,
            NULL);
    PROFILE(statements_cons, "structured-c:statements-cons", "Statements", 2u,
            statements2);
    PROFILE(case_statement, "structured-c:case", "Case", 2u, case2);
    PROFILE(cases_nil, "structured-c:cases-nil", "Cases", 0u, NULL);
    PROFILE(cases_cons, "structured-c:cases-cons", "Cases", 2u, cases2);
    PROFILE(parameter, "structured-c:parameter", "Parameter", 2u, parameter2);
    PROFILE(parameters_nil, "structured-c:parameters-nil", "Parameters", 0u,
            NULL);
    PROFILE(parameters_cons, "structured-c:parameters-cons", "Parameters", 2u,
            parameters2);
    PROFILE(function, "structured-c:function", "Function", 4u, function4);
    PROFILE(external_function, "structured-c:external-function",
            "ExternalFunction", 3u, external3);
    PROFILE(external_functions_nil, "structured-c:external-functions-nil",
            "ExternalFunctions", 0u, NULL);
    PROFILE(external_functions_cons, "structured-c:external-functions-cons",
            "ExternalFunctions", 2u, externals2);
    PROFILE(functions_nil, "structured-c:functions-nil", "Functions", 0u, NULL);
    PROFILE(functions_cons, "structured-c:functions-cons", "Functions", 2u,
            functions2);
    PROFILE(program, "structured-c:program", "Program", 2u, program2);
#undef PROFILE
    return true;
}
