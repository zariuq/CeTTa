#include "language_def_core_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t remaining_work;
    uint32_t depth_limit;
    CettaLdCoreV1Status status;
    char *error_buf;
    size_t error_buf_size;
} LdDecodeContext;

static void ld_set_error(LdDecodeContext *context, const char *format, ...) {
    va_list arguments;

    if (!context || !context->error_buf || context->error_buf_size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(context->error_buf, context->error_buf_size,
                    format, arguments);
    va_end(arguments);
}

static bool ld_fail(LdDecodeContext *context,
                    CettaLdCoreV1Status status,
                    const char *message) {
    if (context) {
        context->status = status;
        ld_set_error(context, "%s", message);
    }
    return false;
}

static bool ld_take_work(LdDecodeContext *context, uint32_t depth) {
    if (!context)
        return false;
    if (depth > context->depth_limit)
        return ld_fail(context, CETTA_LD_CORE_V1_RESOURCE_LIMIT,
                       "LanguageDef wire nesting exceeds the decode limit");
    if (context->remaining_work == 0u)
        return ld_fail(context, CETTA_LD_CORE_V1_RESOURCE_LIMIT,
                       "LanguageDef typed decode exhausted its work limit");
    context->remaining_work--;
    return true;
}

static void ld_text_free(CettaLdTextV1 *text) {
    if (!text)
        return;
    free(text->bytes);
    memset(text, 0, sizeof(*text));
}

static void ld_optional_text_free(CettaLdOptionalTextV1 *text) {
    if (!text)
        return;
    if (text->present)
        ld_text_free(&text->value);
    memset(text, 0, sizeof(*text));
}

static void ld_text_list_free(CettaLdTextListV1 *list) {
    uint32_t index;

    if (!list)
        return;
    for (index = 0u; index < list->len; index++)
        ld_text_free(&list->items[index]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static void ld_type_expr_free(CettaLdTypeExprV1 *type);
static void ld_syntax_item_free(CettaLdSyntaxItemV1 *item);
static void ld_syntax_op_free(CettaLdSyntaxPatternOpV1 *op);
static void ld_pattern_free(CettaLdPatternV1 *pattern);
static void ld_premise_free(CettaLdPremiseV1 *premise);

static void ld_type_expr_free(CettaLdTypeExprV1 *type) {
    if (!type)
        return;
    switch (type->kind) {
    case CETTA_LD_TYPE_BASE_V1:
        ld_text_free(&type->as.base);
        break;
    case CETTA_LD_TYPE_ARROW_V1:
        if (type->as.arrow.domain) {
            ld_type_expr_free(type->as.arrow.domain);
            free(type->as.arrow.domain);
        }
        if (type->as.arrow.codomain) {
            ld_type_expr_free(type->as.arrow.codomain);
            free(type->as.arrow.codomain);
        }
        break;
    case CETTA_LD_TYPE_MULTI_BINDER_V1:
        if (type->as.multi_binder_body) {
            ld_type_expr_free(type->as.multi_binder_body);
            free(type->as.multi_binder_body);
        }
        break;
    case CETTA_LD_TYPE_COLLECTION_V1:
        if (type->as.collection.element_type) {
            ld_type_expr_free(type->as.collection.element_type);
            free(type->as.collection.element_type);
        }
        break;
    }
    memset(type, 0, sizeof(*type));
}

static void ld_term_param_free(CettaLdTermParamV1 *parameter) {
    if (!parameter)
        return;
    ld_text_free(&parameter->body_name);
    ld_type_expr_free(&parameter->type);
    if (parameter->kind == CETTA_LD_PARAM_ABSTRACTION_NAMED_V1)
        ld_optional_text_free(&parameter->names.binder);
    else if (parameter->kind ==
             CETTA_LD_PARAM_MULTI_ABSTRACTION_NAMED_V1)
        ld_text_list_free(&parameter->names.binders);
    memset(parameter, 0, sizeof(*parameter));
}

static void ld_syntax_item_list_free(CettaLdSyntaxItemListV1 *list) {
    uint32_t index;

    if (!list)
        return;
    for (index = 0u; index < list->len; index++)
        ld_syntax_item_free(&list->items[index]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static void ld_syntax_op_free(CettaLdSyntaxPatternOpV1 *op) {
    if (!op)
        return;
    switch (op->kind) {
    case CETTA_LD_SYNTAX_OP_VAR_V1:
        ld_text_free(&op->as.variable);
        break;
    case CETTA_LD_SYNTAX_OP_SEP_V1:
        ld_text_free(&op->as.sep.collection);
        ld_text_free(&op->as.sep.separator);
        if (op->as.sep.source) {
            ld_syntax_op_free(op->as.sep.source);
            free(op->as.sep.source);
        }
        break;
    case CETTA_LD_SYNTAX_OP_ZIP_V1:
        ld_text_free(&op->as.zip.left);
        ld_text_free(&op->as.zip.right);
        break;
    case CETTA_LD_SYNTAX_OP_MAP_V1:
        if (op->as.map.source) {
            ld_syntax_op_free(op->as.map.source);
            free(op->as.map.source);
        }
        ld_text_list_free(&op->as.map.binders);
        ld_syntax_item_list_free(&op->as.map.body);
        break;
    case CETTA_LD_SYNTAX_OP_OPT_V1:
        ld_syntax_item_list_free(&op->as.opt);
        break;
    }
    memset(op, 0, sizeof(*op));
}

static void ld_syntax_item_free(CettaLdSyntaxItemV1 *item) {
    if (!item)
        return;
    switch (item->kind) {
    case CETTA_LD_SYNTAX_TERMINAL_V1:
    case CETTA_LD_SYNTAX_NONTERMINAL_V1:
    case CETTA_LD_SYNTAX_SEPARATOR_V1:
        ld_text_free(&item->as.text);
        break;
    case CETTA_LD_SYNTAX_DELIMITER_V1:
        ld_text_free(&item->as.delimiter.left);
        ld_text_free(&item->as.delimiter.right);
        break;
    case CETTA_LD_SYNTAX_OP_V1:
        if (item->as.op) {
            ld_syntax_op_free(item->as.op);
            free(item->as.op);
        }
        break;
    }
    memset(item, 0, sizeof(*item));
}

static void ld_grammar_rule_free(CettaLdGrammarRuleV1 *rule) {
    uint32_t index;

    if (!rule)
        return;
    ld_text_free(&rule->label);
    ld_text_free(&rule->category);
    for (index = 0u; index < rule->param_len; index++)
        ld_term_param_free(&rule->params[index]);
    free(rule->params);
    ld_syntax_item_list_free(&rule->syntax_pattern);
    memset(rule, 0, sizeof(*rule));
}

static void ld_pattern_list_free(CettaLdPatternListV1 *list) {
    uint32_t index;

    if (!list)
        return;
    for (index = 0u; index < list->len; index++)
        ld_pattern_free(&list->items[index]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static void ld_pattern_free(CettaLdPatternV1 *pattern) {
    if (!pattern)
        return;
    switch (pattern->kind) {
    case CETTA_LD_PATTERN_BVAR_V1:
        free(pattern->as.bvar_decimal);
        break;
    case CETTA_LD_PATTERN_FVAR_V1:
        ld_text_free(&pattern->as.fvar);
        break;
    case CETTA_LD_PATTERN_APPLY_V1:
        ld_text_free(&pattern->as.apply.head);
        ld_pattern_list_free(&pattern->as.apply.arguments);
        break;
    case CETTA_LD_PATTERN_LAMBDA_V1:
        ld_optional_text_free(&pattern->as.lambda.binder);
        if (pattern->as.lambda.body) {
            ld_pattern_free(pattern->as.lambda.body);
            free(pattern->as.lambda.body);
        }
        break;
    case CETTA_LD_PATTERN_MULTI_LAMBDA_V1:
        free(pattern->as.multi_lambda.arity_decimal);
        ld_text_list_free(&pattern->as.multi_lambda.binders);
        if (pattern->as.multi_lambda.body) {
            ld_pattern_free(pattern->as.multi_lambda.body);
            free(pattern->as.multi_lambda.body);
        }
        break;
    case CETTA_LD_PATTERN_SUBST_V1:
        if (pattern->as.subst.body) {
            ld_pattern_free(pattern->as.subst.body);
            free(pattern->as.subst.body);
        }
        if (pattern->as.subst.replacement) {
            ld_pattern_free(pattern->as.subst.replacement);
            free(pattern->as.subst.replacement);
        }
        break;
    case CETTA_LD_PATTERN_COLLECTION_V1:
        ld_pattern_list_free(&pattern->as.collection.elements);
        ld_optional_text_free(&pattern->as.collection.rest);
        break;
    }
    memset(pattern, 0, sizeof(*pattern));
}

void cetta_ld_pattern_v1_init(CettaLdPatternV1 *pattern) {
    if (pattern)
        memset(pattern, 0, sizeof(*pattern));
}

void cetta_ld_pattern_v1_free(CettaLdPatternV1 *pattern) {
    ld_pattern_free(pattern);
}

static void ld_premise_list_free(CettaLdPremiseListV1 *list) {
    uint32_t index;

    if (!list)
        return;
    for (index = 0u; index < list->len; index++)
        ld_premise_free(&list->items[index]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static void ld_premise_free(CettaLdPremiseV1 *premise) {
    if (!premise)
        return;
    switch (premise->kind) {
    case CETTA_LD_PREMISE_FRESHNESS_V1:
        ld_text_free(&premise->as.freshness.variable);
        ld_pattern_free(&premise->as.freshness.term);
        break;
    case CETTA_LD_PREMISE_CONGRUENCE_V1:
        ld_pattern_free(&premise->as.congruence.left);
        ld_pattern_free(&premise->as.congruence.right);
        break;
    case CETTA_LD_PREMISE_RELATION_QUERY_V1:
        ld_text_free(&premise->as.relation_query.relation);
        ld_pattern_list_free(&premise->as.relation_query.arguments);
        break;
    case CETTA_LD_PREMISE_FOR_ALL_V1:
        ld_text_free(&premise->as.for_all.collection);
        ld_text_free(&premise->as.for_all.parameter);
        if (premise->as.for_all.body) {
            ld_premise_free(premise->as.for_all.body);
            free(premise->as.for_all.body);
        }
        break;
    }
    memset(premise, 0, sizeof(*premise));
}

static void ld_relation_rule_free(CettaLdRelationRuleV1 *rule) {
    uint32_t index;

    if (!rule)
        return;
    ld_text_free(&rule->name);
    for (index = 0u; index < rule->type_context_len; index++) {
        ld_text_free(&rule->type_context[index].name);
        ld_type_expr_free(&rule->type_context[index].type);
    }
    free(rule->type_context);
    ld_premise_list_free(&rule->premises);
    ld_pattern_free(&rule->left);
    ld_pattern_free(&rule->right);
    memset(rule, 0, sizeof(*rule));
}

void cetta_language_def_core_v1_init(CettaLanguageDefCoreV1 *language) {
    if (language)
        memset(language, 0, sizeof(*language));
}

void cetta_language_def_core_v1_free(CettaLanguageDefCoreV1 *language) {
    uint32_t index;

    if (!language)
        return;
    ld_text_free(&language->name);
    for (index = 0u; index < language->type_len; index++) {
        ld_text_free(&language->types[index].name);
    }
    free(language->types);
    for (index = 0u; index < language->term_len; index++)
        ld_grammar_rule_free(&language->terms[index]);
    free(language->terms);
    for (index = 0u; index < language->equation_len; index++)
        ld_relation_rule_free(&language->equations[index]);
    free(language->equations);
    for (index = 0u; index < language->rewrite_len; index++)
        ld_relation_rule_free(&language->rewrites[index]);
    free(language->rewrites);
    memset(language, 0, sizeof(*language));
}

const char *cetta_ld_core_v1_status_name(CettaLdCoreV1Status status) {
    switch (status) {
    case CETTA_LD_CORE_V1_OK:
        return "ok";
    case CETTA_LD_CORE_V1_BAD_ARGUMENT:
        return "bad-argument";
    case CETTA_LD_CORE_V1_MALFORMED_WIRE:
        return "malformed-wire";
    case CETTA_LD_CORE_V1_RESOURCE_LIMIT:
        return "resource-limit";
    case CETTA_LD_CORE_V1_ALLOCATION_FAILURE:
        return "allocation-failure";
    }
    return "unknown";
}

static bool ld_application(const CettaOpLangV1SExpr *term,
                           const char *head,
                           uint32_t arity) {
    return cetta_op_lang_v1_application_is(term, head, arity);
}

static const CettaOpLangV1SExpr *ld_argument(
    const CettaOpLangV1SExpr *term,
    uint32_t index) {
    if (!term || term->kind != CETTA_OP_LANG_V1_SEXPR_APPLICATION ||
        index >= term->as.application.argument_len) {
        return NULL;
    }
    return term->as.application.arguments[index];
}

static bool ld_text_decode(CettaLdTextV1 *out,
                           const CettaOpLangV1SExpr *term,
                           LdDecodeContext *context,
                           uint32_t depth) {
    uint8_t *copy = NULL;

    if (!out || !ld_take_work(context, depth))
        return false;
    if (!term || term->kind != CETTA_OP_LANG_V1_SEXPR_STRING)
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "expected a quoted string in LanguageDef wire");
    if (term->as.string.len > 0u) {
        copy = malloc(term->as.string.len);
        if (!copy)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate LanguageDef string");
        memcpy(copy, term->as.string.bytes, term->as.string.len);
    }
    out->bytes = copy;
    out->len = term->as.string.len;
    return true;
}

static bool ld_natural_decode(char **out,
                              const CettaOpLangV1SExpr *term,
                              LdDecodeContext *context,
                              uint32_t depth) {
    size_t len;
    char *copy;

    if (!out || !ld_take_work(context, depth))
        return false;
    if (!term || term->kind != CETTA_OP_LANG_V1_SEXPR_NATURAL ||
        !term->as.natural)
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "expected a natural number in LanguageDef wire");
    len = strlen(term->as.natural);
    copy = malloc(len + 1u);
    if (!copy)
        return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                       "failed to allocate LanguageDef natural");
    memcpy(copy, term->as.natural, len + 1u);
    *out = copy;
    return true;
}

static bool ld_list_len(const CettaOpLangV1SExpr *term,
                        uint32_t *len_out,
                        LdDecodeContext *context) {
    const CettaOpLangV1SExpr *cursor = term;
    uint32_t len = 0u;

    if (!len_out)
        return false;
    while (ld_application(cursor, "LCons", 2u)) {
        if (len == UINT32_MAX)
            return ld_fail(context, CETTA_LD_CORE_V1_RESOURCE_LIMIT,
                           "LanguageDef list is too long");
        len++;
        cursor = ld_argument(cursor, 1u);
    }
    if (!cetta_op_lang_v1_symbol_is(cursor, "LNil"))
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "expected a canonical LNil/LCons list");
    *len_out = len;
    return true;
}

static const CettaOpLangV1SExpr *ld_list_head(
    const CettaOpLangV1SExpr *term) {
    return ld_application(term, "LCons", 2u) ? ld_argument(term, 0u) : NULL;
}

static const CettaOpLangV1SExpr *ld_list_tail(
    const CettaOpLangV1SExpr *term) {
    return ld_application(term, "LCons", 2u) ? ld_argument(term, 1u) : NULL;
}

static bool ld_optional_text_decode(CettaLdOptionalTextV1 *out,
                                    const CettaOpLangV1SExpr *term,
                                    const char *none_tag,
                                    const char *some_tag,
                                    LdDecodeContext *context,
                                    uint32_t depth) {
    if (!out || !ld_take_work(context, depth))
        return false;
    if (cetta_op_lang_v1_symbol_is(term, none_tag)) {
        memset(out, 0, sizeof(*out));
        return true;
    }
    if (!ld_application(term, some_tag, 1u))
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "malformed optional string in LanguageDef wire");
    out->present = true;
    if (!ld_text_decode(&out->value, ld_argument(term, 0u),
                        context, depth + 1u)) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

static bool ld_collection_type_decode(CettaLdCollectionTypeV1 *out,
                                      const CettaOpLangV1SExpr *term,
                                      LdDecodeContext *context,
                                      uint32_t depth) {
    static const uint8_t vec[] =
        "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec";
    static const uint8_t bag[] =
        "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashBag";
    static const uint8_t set[] =
        "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashSet";

    if (!out || !ld_take_work(context, depth))
        return false;
    if (cetta_op_lang_v1_string_is(term, vec, sizeof(vec) - 1u))
        *out = CETTA_LD_COLLECTION_VEC_V1;
    else if (cetta_op_lang_v1_string_is(term, bag, sizeof(bag) - 1u))
        *out = CETTA_LD_COLLECTION_HASH_BAG_V1;
    else if (cetta_op_lang_v1_string_is(term, set, sizeof(set) - 1u))
        *out = CETTA_LD_COLLECTION_HASH_SET_V1;
    else
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "unknown collection type in LanguageDef wire");
    return true;
}

static bool ld_carrier_decode(CettaLdCarrierKindV1 *out,
                              const CettaOpLangV1SExpr *term,
                              LdDecodeContext *context,
                              uint32_t depth) {
    if (!out || !ld_take_work(context, depth))
        return false;
    if (cetta_op_lang_v1_symbol_is(term, "CarrierAst"))
        *out = CETTA_LD_CARRIER_AST_V1;
    else if (cetta_op_lang_v1_symbol_is(term, "CarrierTokenLabel"))
        *out = CETTA_LD_CARRIER_TOKEN_LABEL_V1;
    else if (cetta_op_lang_v1_symbol_is(term, "CarrierTokenRaw"))
        *out = CETTA_LD_CARRIER_TOKEN_RAW_V1;
    else if (cetta_op_lang_v1_symbol_is(term, "CarrierTokenProof"))
        *out = CETTA_LD_CARRIER_TOKEN_PROOF_V1;
    else if (cetta_op_lang_v1_symbol_is(term, "CarrierTokenPath"))
        *out = CETTA_LD_CARRIER_TOKEN_PATH_V1;
    else if (cetta_op_lang_v1_symbol_is(term, "CarrierBuiltinInt"))
        *out = CETTA_LD_CARRIER_BUILTIN_INT_V1;
    else if (cetta_op_lang_v1_symbol_is(term, "CarrierBuiltinString"))
        *out = CETTA_LD_CARRIER_BUILTIN_STRING_V1;
    else if (cetta_op_lang_v1_symbol_is(term, "CarrierBuiltinBool"))
        *out = CETTA_LD_CARRIER_BUILTIN_BOOL_V1;
    else
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "unknown carrier kind in LanguageDef wire");
    return true;
}

static bool ld_type_expr_decode(CettaLdTypeExprV1 *out,
                                const CettaOpLangV1SExpr *term,
                                LdDecodeContext *context,
                                uint32_t depth);

static bool ld_type_expr_decode(CettaLdTypeExprV1 *out,
                                const CettaOpLangV1SExpr *term,
                                LdDecodeContext *context,
                                uint32_t depth) {
    CettaLdTypeExprV1 result;

    if (!out || !ld_take_work(context, depth))
        return false;
    memset(&result, 0, sizeof(result));
    if (ld_application(term, "TBase", 1u)) {
        result.kind = CETTA_LD_TYPE_BASE_V1;
        if (!ld_text_decode(&result.as.base, ld_argument(term, 0u),
                            context, depth + 1u))
            return false;
    } else if (ld_application(term, "TArrow", 2u)) {
        result.kind = CETTA_LD_TYPE_ARROW_V1;
        result.as.arrow.domain = calloc(1u, sizeof(*result.as.arrow.domain));
        result.as.arrow.codomain = calloc(1u, sizeof(*result.as.arrow.codomain));
        if (!result.as.arrow.domain || !result.as.arrow.codomain) {
            ld_type_expr_free(&result);
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate arrow type");
        }
        if (!ld_type_expr_decode(result.as.arrow.domain,
                                 ld_argument(term, 0u),
                                 context, depth + 1u) ||
            !ld_type_expr_decode(result.as.arrow.codomain,
                                 ld_argument(term, 1u),
                                 context, depth + 1u)) {
            ld_type_expr_free(&result);
            return false;
        }
    } else if (ld_application(term, "TMultiBinder", 1u)) {
        result.kind = CETTA_LD_TYPE_MULTI_BINDER_V1;
        result.as.multi_binder_body =
            calloc(1u, sizeof(*result.as.multi_binder_body));
        if (!result.as.multi_binder_body) {
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate multi-binder type");
        }
        if (!ld_type_expr_decode(result.as.multi_binder_body,
                                 ld_argument(term, 0u),
                                 context, depth + 1u)) {
            ld_type_expr_free(&result);
            return false;
        }
    } else if (ld_application(term, "TCollection", 2u)) {
        result.kind = CETTA_LD_TYPE_COLLECTION_V1;
        result.as.collection.element_type =
            calloc(1u, sizeof(*result.as.collection.element_type));
        if (!result.as.collection.element_type) {
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate collection type");
        }
        if (!ld_collection_type_decode(
                &result.as.collection.collection_type,
                ld_argument(term, 0u), context, depth + 1u) ||
            !ld_type_expr_decode(result.as.collection.element_type,
                                 ld_argument(term, 1u),
                                 context, depth + 1u)) {
            ld_type_expr_free(&result);
            return false;
        }
    } else {
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "unknown type expression in LanguageDef wire");
    }
    *out = result;
    return true;
}

static bool ld_text_list_decode(CettaLdTextListV1 *out,
                                const CettaOpLangV1SExpr *term,
                                LdDecodeContext *context,
                                uint32_t depth) {
    CettaLdTextListV1 result = {0};
    const CettaOpLangV1SExpr *cursor = term;
    uint32_t index;

    if (!out || !ld_take_work(context, depth) ||
        !ld_list_len(term, &result.len, context))
        return false;
    if (result.len > 0u) {
        result.items = calloc(result.len, sizeof(*result.items));
        if (!result.items)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate string list");
    }
    for (index = 0u; index < result.len; index++) {
        if (!ld_text_decode(&result.items[index], ld_list_head(cursor),
                            context, depth + 1u)) {
            ld_text_list_free(&result);
            return false;
        }
        cursor = ld_list_tail(cursor);
    }
    *out = result;
    return true;
}

static bool ld_term_param_decode(CettaLdTermParamV1 *out,
                                 const CettaOpLangV1SExpr *term,
                                 LdDecodeContext *context,
                                 uint32_t depth) {
    CettaLdTermParamV1 result;

    if (!out || !ld_take_work(context, depth))
        return false;
    memset(&result, 0, sizeof(result));
    if (ld_application(term, "TermSimple", 2u)) {
        result.kind = CETTA_LD_PARAM_SIMPLE_V1;
        if (!ld_text_decode(&result.body_name, ld_argument(term, 0u),
                            context, depth + 1u) ||
            !ld_type_expr_decode(&result.type, ld_argument(term, 1u),
                                 context, depth + 1u)) {
            ld_term_param_free(&result);
            return false;
        }
    } else if (ld_application(term, "TermAbstractionNamed", 3u)) {
        result.kind = CETTA_LD_PARAM_ABSTRACTION_NAMED_V1;
        if (!ld_optional_text_decode(
                &result.names.binder, ld_argument(term, 0u),
                "BNone", "BSome", context, depth + 1u) ||
            !ld_text_decode(&result.body_name, ld_argument(term, 1u),
                            context, depth + 1u) ||
            !ld_type_expr_decode(&result.type, ld_argument(term, 2u),
                                 context, depth + 1u)) {
            ld_term_param_free(&result);
            return false;
        }
    } else if (ld_application(term, "TermMultiAbstractionNamed", 3u)) {
        result.kind = CETTA_LD_PARAM_MULTI_ABSTRACTION_NAMED_V1;
        if (!ld_text_list_decode(&result.names.binders,
                                 ld_argument(term, 0u),
                                 context, depth + 1u) ||
            !ld_text_decode(&result.body_name, ld_argument(term, 1u),
                            context, depth + 1u) ||
            !ld_type_expr_decode(&result.type, ld_argument(term, 2u),
                                 context, depth + 1u)) {
            ld_term_param_free(&result);
            return false;
        }
    } else {
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "unknown term parameter in LanguageDef wire");
    }
    *out = result;
    return true;
}

static bool ld_term_param_list_decode(CettaLdTermParamV1 **items_out,
                                      uint32_t *len_out,
                                      const CettaOpLangV1SExpr *term,
                                      LdDecodeContext *context,
                                      uint32_t depth) {
    const CettaOpLangV1SExpr *cursor = term;
    CettaLdTermParamV1 *items = NULL;
    uint32_t len;
    uint32_t index;

    if (!items_out || !len_out || !ld_take_work(context, depth) ||
        !ld_list_len(term, &len, context))
        return false;
    if (len > 0u) {
        items = calloc(len, sizeof(*items));
        if (!items)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate term parameter list");
    }
    for (index = 0u; index < len; index++) {
        if (!ld_term_param_decode(&items[index], ld_list_head(cursor),
                                  context, depth + 1u)) {
            while (index > 0u)
                ld_term_param_free(&items[--index]);
            free(items);
            return false;
        }
        cursor = ld_list_tail(cursor);
    }
    *items_out = items;
    *len_out = len;
    return true;
}

static bool ld_syntax_item_decode(CettaLdSyntaxItemV1 *out,
                                  const CettaOpLangV1SExpr *term,
                                  LdDecodeContext *context,
                                  uint32_t depth);
static bool ld_syntax_op_decode(CettaLdSyntaxPatternOpV1 *out,
                                const CettaOpLangV1SExpr *term,
                                LdDecodeContext *context,
                                uint32_t depth);

static bool ld_syntax_item_list_decode(CettaLdSyntaxItemListV1 *out,
                                       const CettaOpLangV1SExpr *term,
                                       LdDecodeContext *context,
                                       uint32_t depth) {
    CettaLdSyntaxItemListV1 result = {0};
    const CettaOpLangV1SExpr *cursor = term;
    uint32_t index;

    if (!out || !ld_take_work(context, depth) ||
        !ld_list_len(term, &result.len, context))
        return false;
    if (result.len > 0u) {
        result.items = calloc(result.len, sizeof(*result.items));
        if (!result.items)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate syntax-item list");
    }
    for (index = 0u; index < result.len; index++) {
        if (!ld_syntax_item_decode(&result.items[index],
                                   ld_list_head(cursor),
                                   context, depth + 1u)) {
            ld_syntax_item_list_free(&result);
            return false;
        }
        cursor = ld_list_tail(cursor);
    }
    *out = result;
    return true;
}

static bool ld_eval_policy_decode(CettaLdOptionalTermEvalPolicyV1 *out,
                                  const CettaOpLangV1SExpr *term,
                                  LdDecodeContext *context,
                                  uint32_t depth) {
    const CettaOpLangV1SExpr *policy;

    if (!out || !ld_take_work(context, depth))
        return false;
    if (cetta_op_lang_v1_symbol_is(term, "EvalNone")) {
        memset(out, 0, sizeof(*out));
        return true;
    }
    if (!ld_application(term, "EvalSome", 1u))
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "malformed evaluation policy option");
    policy = ld_argument(term, 0u);
    out->present = true;
    if (cetta_op_lang_v1_symbol_is(policy, "EvalRewrite"))
        out->value = CETTA_LD_EVAL_REWRITE_V1;
    else if (cetta_op_lang_v1_symbol_is(policy, "EvalFold"))
        out->value = CETTA_LD_EVAL_FOLD_V1;
    else if (cetta_op_lang_v1_symbol_is(policy, "EvalOracle"))
        out->value = CETTA_LD_EVAL_ORACLE_V1;
    else
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "unknown evaluation policy");
    return true;
}

static bool ld_grammar_rule_decode(CettaLdGrammarRuleV1 *out,
                                   const CettaOpLangV1SExpr *term,
                                   LdDecodeContext *context,
                                   uint32_t depth) {
    CettaLdGrammarRuleV1 result;

    if (!out || !ld_take_work(context, depth))
        return false;
    memset(&result, 0, sizeof(result));
    if (!ld_application(term, "GrammarRule", 5u))
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "expected GrammarRule in LanguageDef term list");
    if (!ld_text_decode(&result.label, ld_argument(term, 0u),
                        context, depth + 1u) ||
        !ld_text_decode(&result.category, ld_argument(term, 1u),
                        context, depth + 1u) ||
        !ld_term_param_list_decode(&result.params, &result.param_len,
                                   ld_argument(term, 2u),
                                   context, depth + 1u) ||
        !ld_syntax_item_list_decode(&result.syntax_pattern,
                                    ld_argument(term, 3u),
                                    context, depth + 1u) ||
        !ld_eval_policy_decode(&result.eval_policy,
                               ld_argument(term, 4u),
                               context, depth + 1u)) {
        ld_grammar_rule_free(&result);
        return false;
    }
    *out = result;
    return true;
}

static bool ld_pattern_decode(CettaLdPatternV1 *out,
                              const CettaOpLangV1SExpr *term,
                              LdDecodeContext *context,
                              uint32_t depth);

static bool ld_pattern_list_decode(CettaLdPatternListV1 *out,
                                   const CettaOpLangV1SExpr *term,
                                   LdDecodeContext *context,
                                   uint32_t depth) {
    CettaLdPatternListV1 result = {0};
    const CettaOpLangV1SExpr *cursor = term;
    uint32_t index;

    if (!out || !ld_take_work(context, depth) ||
        !ld_list_len(term, &result.len, context))
        return false;
    if (result.len > 0u) {
        result.items = calloc(result.len, sizeof(*result.items));
        if (!result.items)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate pattern list");
    }
    for (index = 0u; index < result.len; index++) {
        if (!ld_pattern_decode(&result.items[index], ld_list_head(cursor),
                               context, depth + 1u)) {
            ld_pattern_list_free(&result);
            return false;
        }
        cursor = ld_list_tail(cursor);
    }
    *out = result;
    return true;
}

static bool ld_premise_decode(CettaLdPremiseV1 *out,
                              const CettaOpLangV1SExpr *term,
                              LdDecodeContext *context,
                              uint32_t depth);

static bool ld_premise_list_decode(CettaLdPremiseListV1 *out,
                                   const CettaOpLangV1SExpr *term,
                                   LdDecodeContext *context,
                                   uint32_t depth) {
    CettaLdPremiseListV1 result = {0};
    const CettaOpLangV1SExpr *cursor = term;
    uint32_t index;

    if (!out || !ld_take_work(context, depth) ||
        !ld_list_len(term, &result.len, context))
        return false;
    if (result.len > 0u) {
        result.items = calloc(result.len, sizeof(*result.items));
        if (!result.items)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate premise list");
    }
    for (index = 0u; index < result.len; index++) {
        if (!ld_premise_decode(&result.items[index], ld_list_head(cursor),
                               context, depth + 1u)) {
            ld_premise_list_free(&result);
            return false;
        }
        cursor = ld_list_tail(cursor);
    }
    *out = result;
    return true;
}

static bool ld_premise_decode(CettaLdPremiseV1 *out,
                              const CettaOpLangV1SExpr *term,
                              LdDecodeContext *context,
                              uint32_t depth) {
    CettaLdPremiseV1 result;

    if (!out || !ld_take_work(context, depth))
        return false;
    memset(&result, 0, sizeof(result));
    if (ld_application(term, "Freshness", 2u)) {
        result.kind = CETTA_LD_PREMISE_FRESHNESS_V1;
        if (!ld_text_decode(&result.as.freshness.variable,
                            ld_argument(term, 0u),
                            context, depth + 1u) ||
            !ld_pattern_decode(&result.as.freshness.term,
                               ld_argument(term, 1u),
                               context, depth + 1u)) {
            ld_premise_free(&result);
            return false;
        }
    } else if (ld_application(term, "Congruence", 2u)) {
        result.kind = CETTA_LD_PREMISE_CONGRUENCE_V1;
        if (!ld_pattern_decode(&result.as.congruence.left,
                               ld_argument(term, 0u),
                               context, depth + 1u) ||
            !ld_pattern_decode(&result.as.congruence.right,
                               ld_argument(term, 1u),
                               context, depth + 1u)) {
            ld_premise_free(&result);
            return false;
        }
    } else if (ld_application(term, "RelationQuery", 2u)) {
        result.kind = CETTA_LD_PREMISE_RELATION_QUERY_V1;
        if (!ld_text_decode(&result.as.relation_query.relation,
                            ld_argument(term, 0u),
                            context, depth + 1u) ||
            !ld_pattern_list_decode(&result.as.relation_query.arguments,
                                    ld_argument(term, 1u),
                                    context, depth + 1u)) {
            ld_premise_free(&result);
            return false;
        }
    } else if (ld_application(term, "ForAll", 3u)) {
        result.kind = CETTA_LD_PREMISE_FOR_ALL_V1;
        result.as.for_all.body = calloc(1u, sizeof(*result.as.for_all.body));
        if (!result.as.for_all.body)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate quantified premise");
        if (!ld_text_decode(&result.as.for_all.collection,
                            ld_argument(term, 0u),
                            context, depth + 1u) ||
            !ld_text_decode(&result.as.for_all.parameter,
                            ld_argument(term, 1u),
                            context, depth + 1u) ||
            !ld_premise_decode(result.as.for_all.body,
                               ld_argument(term, 2u),
                               context, depth + 1u)) {
            ld_premise_free(&result);
            return false;
        }
    } else {
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "unknown premise in LanguageDef wire");
    }
    *out = result;
    return true;
}

static bool ld_type_binding_decode(CettaLdTypeBindingV1 *out,
                                   const CettaOpLangV1SExpr *term,
                                   LdDecodeContext *context,
                                   uint32_t depth) {
    CettaLdTypeBindingV1 result;

    if (!out || !ld_take_work(context, depth))
        return false;
    memset(&result, 0, sizeof(result));
    if (!ld_application(term, "TypeBinding", 2u))
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "expected TypeBinding in rule context");
    if (!ld_text_decode(&result.name, ld_argument(term, 0u),
                        context, depth + 1u) ||
        !ld_type_expr_decode(&result.type, ld_argument(term, 1u),
                             context, depth + 1u)) {
        ld_text_free(&result.name);
        ld_type_expr_free(&result.type);
        return false;
    }
    *out = result;
    return true;
}

static bool ld_type_binding_list_decode(CettaLdTypeBindingV1 **items_out,
                                        uint32_t *len_out,
                                        const CettaOpLangV1SExpr *term,
                                        LdDecodeContext *context,
                                        uint32_t depth) {
    const CettaOpLangV1SExpr *cursor = term;
    CettaLdTypeBindingV1 *items = NULL;
    uint32_t len;
    uint32_t index;

    if (!items_out || !len_out || !ld_take_work(context, depth) ||
        !ld_list_len(term, &len, context))
        return false;
    if (len > 0u) {
        items = calloc(len, sizeof(*items));
        if (!items)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate type context");
    }
    for (index = 0u; index < len; index++) {
        if (!ld_type_binding_decode(&items[index], ld_list_head(cursor),
                                    context, depth + 1u)) {
            while (index > 0u) {
                index--;
                ld_text_free(&items[index].name);
                ld_type_expr_free(&items[index].type);
            }
            free(items);
            return false;
        }
        cursor = ld_list_tail(cursor);
    }
    *items_out = items;
    *len_out = len;
    return true;
}

static bool ld_relation_rule_decode(CettaLdRelationRuleV1 *out,
                                    const CettaOpLangV1SExpr *term,
                                    const char *tag,
                                    LdDecodeContext *context,
                                    uint32_t depth) {
    CettaLdRelationRuleV1 result;

    if (!out || !tag || !ld_take_work(context, depth))
        return false;
    memset(&result, 0, sizeof(result));
    if (!ld_application(term, tag, 5u)) {
        ld_set_error(context, "expected %s in LanguageDef wire", tag);
        context->status = CETTA_LD_CORE_V1_MALFORMED_WIRE;
        return false;
    }
    if (!ld_text_decode(&result.name, ld_argument(term, 0u),
                        context, depth + 1u) ||
        !ld_type_binding_list_decode(&result.type_context,
                                     &result.type_context_len,
                                     ld_argument(term, 1u),
                                     context, depth + 1u) ||
        !ld_premise_list_decode(&result.premises,
                                ld_argument(term, 2u),
                                context, depth + 1u) ||
        !ld_pattern_decode(&result.left, ld_argument(term, 3u),
                           context, depth + 1u) ||
        !ld_pattern_decode(&result.right, ld_argument(term, 4u),
                           context, depth + 1u)) {
        ld_relation_rule_free(&result);
        return false;
    }
    *out = result;
    return true;
}

static bool ld_type_decl_decode(CettaLdTypeDeclV1 *out,
                                const CettaOpLangV1SExpr *term,
                                LdDecodeContext *context,
                                uint32_t depth) {
    CettaLdTypeDeclV1 result;

    if (!out || !ld_take_work(context, depth))
        return false;
    memset(&result, 0, sizeof(result));
    if (!ld_application(term, "TypeDecl", 2u))
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "expected TypeDecl in LanguageDef type list");
    if (!ld_text_decode(&result.name, ld_argument(term, 0u),
                        context, depth + 1u) ||
        !ld_carrier_decode(&result.carrier, ld_argument(term, 1u),
                           context, depth + 1u)) {
        ld_text_free(&result.name);
        return false;
    }
    *out = result;
    return true;
}

bool cetta_language_def_core_v1_decode(
    CettaLanguageDefCoreV1 *out,
    const CettaOperationalLanguageDefV1 *wire,
    uint32_t work_limit,
    CettaLdCoreV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaLanguageDefCoreV1 candidate;
    LdDecodeContext context;
    const CettaOpLangV1SExpr *cursor;
    const CettaOpLangV1SExpr *name_term;
    uint32_t len;
    uint32_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (status)
        *status = CETTA_LD_CORE_V1_OK;
    if (!out || !wire || !wire->root || work_limit == 0u) {
        if (status)
            *status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
        if (error_buf && error_buf_size > 0u)
            (void)snprintf(error_buf, error_buf_size,
                           "bad LanguageDef core decode arguments");
        return false;
    }
    cetta_language_def_core_v1_init(&candidate);
    context = (LdDecodeContext){
        .remaining_work = work_limit,
        .depth_limit = work_limit < 1024u ? work_limit : 1024u,
        .status = CETTA_LD_CORE_V1_OK,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
    };
    if (!ld_application(wire->root, "GSLTLanguageDefWireV1", 5u)) {
        (void)ld_fail(&context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                      "typed decoder received the wrong wire root");
        goto done;
    }
    name_term = ld_argument(wire->root, 0u);
    if (!ld_text_decode(&candidate.name, name_term, &context, 0u))
        goto done;

    cursor = wire->types_field;
    if (!ld_list_len(cursor, &len, &context))
        goto done;
    if (len > 0u) {
        candidate.types = calloc(len, sizeof(*candidate.types));
        if (!candidate.types) {
            (void)ld_fail(&context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                          "failed to allocate LanguageDef types");
            goto done;
        }
    }
    for (index = 0u; index < len; index++) {
        if (!ld_type_decl_decode(&candidate.types[index],
                                 ld_list_head(cursor),
                                 &context, 1u))
            goto done;
        candidate.type_len++;
        cursor = ld_list_tail(cursor);
    }

    cursor = wire->terms_field;
    if (!ld_list_len(cursor, &len, &context))
        goto done;
    if (len > 0u) {
        candidate.terms = calloc(len, sizeof(*candidate.terms));
        if (!candidate.terms) {
            (void)ld_fail(&context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                          "failed to allocate LanguageDef grammar rules");
            goto done;
        }
    }
    for (index = 0u; index < len; index++) {
        if (!ld_grammar_rule_decode(&candidate.terms[index],
                                    ld_list_head(cursor),
                                    &context, 1u))
            goto done;
        candidate.term_len++;
        cursor = ld_list_tail(cursor);
    }

    cursor = wire->equations_field;
    if (!ld_list_len(cursor, &len, &context))
        goto done;
    if (len > 0u) {
        candidate.equations = calloc(len, sizeof(*candidate.equations));
        if (!candidate.equations) {
            (void)ld_fail(&context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                          "failed to allocate LanguageDef equations");
            goto done;
        }
    }
    for (index = 0u; index < len; index++) {
        if (!ld_relation_rule_decode(&candidate.equations[index],
                                     ld_list_head(cursor), "Equation",
                                     &context, 1u))
            goto done;
        candidate.equation_len++;
        cursor = ld_list_tail(cursor);
    }

    cursor = wire->rewrites_field;
    if (!ld_list_len(cursor, &len, &context))
        goto done;
    if (len > 0u) {
        candidate.rewrites = calloc(len, sizeof(*candidate.rewrites));
        if (!candidate.rewrites) {
            (void)ld_fail(&context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                          "failed to allocate LanguageDef rewrites");
            goto done;
        }
    }
    for (index = 0u; index < len; index++) {
        if (!ld_relation_rule_decode(&candidate.rewrites[index],
                                     ld_list_head(cursor), "RewriteRule",
                                     &context, 1u))
            goto done;
        candidate.rewrite_len++;
        cursor = ld_list_tail(cursor);
    }

    cetta_language_def_core_v1_free(out);
    *out = candidate;
    memset(&candidate, 0, sizeof(candidate));
    ok = true;

done:
    if (!ok)
        cetta_language_def_core_v1_free(&candidate);
    if (status)
        *status = context.status;
    return ok;
}


static bool ld_pattern_decode(CettaLdPatternV1 *out,
                              const CettaOpLangV1SExpr *term,
                              LdDecodeContext *context,
                              uint32_t depth) {
    CettaLdPatternV1 result;

    if (!out || !ld_take_work(context, depth))
        return false;
    memset(&result, 0, sizeof(result));
    if (ld_application(term, "Var", 1u)) {
        result.kind = CETTA_LD_PATTERN_BVAR_V1;
        if (!ld_natural_decode(&result.as.bvar_decimal,
                               ld_argument(term, 0u),
                               context, depth + 1u))
            return false;
    } else if (ld_application(term, "FVar", 1u)) {
        result.kind = CETTA_LD_PATTERN_FVAR_V1;
        if (!ld_text_decode(&result.as.fvar, ld_argument(term, 0u),
                            context, depth + 1u))
            return false;
    } else if (ld_application(term, "PApp", 2u)) {
        result.kind = CETTA_LD_PATTERN_APPLY_V1;
        if (!ld_text_decode(&result.as.apply.head, ld_argument(term, 0u),
                            context, depth + 1u) ||
            !ld_pattern_list_decode(&result.as.apply.arguments,
                                    ld_argument(term, 1u),
                                    context, depth + 1u)) {
            ld_pattern_free(&result);
            return false;
        }
    } else if (ld_application(term, "PLam", 2u)) {
        result.kind = CETTA_LD_PATTERN_LAMBDA_V1;
        result.as.lambda.body = calloc(1u, sizeof(*result.as.lambda.body));
        if (!result.as.lambda.body)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate lambda body");
        if (!ld_optional_text_decode(&result.as.lambda.binder,
                                     ld_argument(term, 0u),
                                     "BNone", "BSome",
                                     context, depth + 1u) ||
            !ld_pattern_decode(result.as.lambda.body,
                               ld_argument(term, 1u),
                               context, depth + 1u)) {
            ld_pattern_free(&result);
            return false;
        }
    } else if (ld_application(term, "PMultiLam", 3u)) {
        result.kind = CETTA_LD_PATTERN_MULTI_LAMBDA_V1;
        result.as.multi_lambda.body =
            calloc(1u, sizeof(*result.as.multi_lambda.body));
        if (!result.as.multi_lambda.body)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate multi-lambda body");
        if (!ld_natural_decode(&result.as.multi_lambda.arity_decimal,
                               ld_argument(term, 0u),
                               context, depth + 1u) ||
            !ld_text_list_decode(&result.as.multi_lambda.binders,
                                 ld_argument(term, 1u),
                                 context, depth + 1u) ||
            !ld_pattern_decode(result.as.multi_lambda.body,
                               ld_argument(term, 2u),
                               context, depth + 1u)) {
            ld_pattern_free(&result);
            return false;
        }
    } else if (ld_application(term, "PSubst", 2u)) {
        result.kind = CETTA_LD_PATTERN_SUBST_V1;
        result.as.subst.body = calloc(1u, sizeof(*result.as.subst.body));
        result.as.subst.replacement =
            calloc(1u, sizeof(*result.as.subst.replacement));
        if (!result.as.subst.body || !result.as.subst.replacement) {
            ld_pattern_free(&result);
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate substitution pattern");
        }
        if (!ld_pattern_decode(result.as.subst.body,
                               ld_argument(term, 0u),
                               context, depth + 1u) ||
            !ld_pattern_decode(result.as.subst.replacement,
                               ld_argument(term, 1u),
                               context, depth + 1u)) {
            ld_pattern_free(&result);
            return false;
        }
    } else if (ld_application(term, "PCollection", 3u)) {
        result.kind = CETTA_LD_PATTERN_COLLECTION_V1;
        if (!ld_collection_type_decode(
                &result.as.collection.collection_type,
                ld_argument(term, 0u), context, depth + 1u) ||
            !ld_pattern_list_decode(&result.as.collection.elements,
                                    ld_argument(term, 1u),
                                    context, depth + 1u) ||
            !ld_optional_text_decode(&result.as.collection.rest,
                                     ld_argument(term, 2u),
                                     "RNone", "RSome",
                                     context, depth + 1u)) {
            ld_pattern_free(&result);
            return false;
        }
    } else {
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "unknown pattern in LanguageDef wire");
    }
    *out = result;
    return true;
}


static bool ld_optional_syntax_op_decode(CettaLdSyntaxPatternOpV1 **out,
                                         const CettaOpLangV1SExpr *term,
                                         LdDecodeContext *context,
                                         uint32_t depth) {
    CettaLdSyntaxPatternOpV1 *value;

    if (!out || !ld_take_work(context, depth))
        return false;
    if (cetta_op_lang_v1_symbol_is(term, "OpNone")) {
        *out = NULL;
        return true;
    }
    if (!ld_application(term, "OpSome", 1u))
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "malformed optional syntax operation");
    value = calloc(1u, sizeof(*value));
    if (!value)
        return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                       "failed to allocate optional syntax operation");
    if (!ld_syntax_op_decode(value, ld_argument(term, 0u),
                             context, depth + 1u)) {
        free(value);
        return false;
    }
    *out = value;
    return true;
}

static bool ld_syntax_op_decode(CettaLdSyntaxPatternOpV1 *out,
                                const CettaOpLangV1SExpr *term,
                                LdDecodeContext *context,
                                uint32_t depth) {
    CettaLdSyntaxPatternOpV1 result;

    if (!out || !ld_take_work(context, depth))
        return false;
    memset(&result, 0, sizeof(result));
    if (ld_application(term, "SyntaxVar", 1u)) {
        result.kind = CETTA_LD_SYNTAX_OP_VAR_V1;
        if (!ld_text_decode(&result.as.variable, ld_argument(term, 0u),
                            context, depth + 1u))
            return false;
    } else if (ld_application(term, "SyntaxSep", 3u)) {
        result.kind = CETTA_LD_SYNTAX_OP_SEP_V1;
        if (!ld_text_decode(&result.as.sep.collection,
                            ld_argument(term, 0u),
                            context, depth + 1u) ||
            !ld_text_decode(&result.as.sep.separator,
                            ld_argument(term, 1u),
                            context, depth + 1u) ||
            !ld_optional_syntax_op_decode(&result.as.sep.source,
                                          ld_argument(term, 2u),
                                          context, depth + 1u)) {
            ld_syntax_op_free(&result);
            return false;
        }
    } else if (ld_application(term, "SyntaxZip", 2u)) {
        result.kind = CETTA_LD_SYNTAX_OP_ZIP_V1;
        if (!ld_text_decode(&result.as.zip.left, ld_argument(term, 0u),
                            context, depth + 1u) ||
            !ld_text_decode(&result.as.zip.right, ld_argument(term, 1u),
                            context, depth + 1u)) {
            ld_syntax_op_free(&result);
            return false;
        }
    } else if (ld_application(term, "SyntaxMap", 3u)) {
        result.kind = CETTA_LD_SYNTAX_OP_MAP_V1;
        result.as.map.source = calloc(1u, sizeof(*result.as.map.source));
        if (!result.as.map.source)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate mapped syntax source");
        if (!ld_syntax_op_decode(result.as.map.source,
                                 ld_argument(term, 0u),
                                 context, depth + 1u) ||
            !ld_text_list_decode(&result.as.map.binders,
                                 ld_argument(term, 1u),
                                 context, depth + 1u) ||
            !ld_syntax_item_list_decode(&result.as.map.body,
                                        ld_argument(term, 2u),
                                        context, depth + 1u)) {
            ld_syntax_op_free(&result);
            return false;
        }
    } else if (ld_application(term, "SyntaxOpt", 1u)) {
        result.kind = CETTA_LD_SYNTAX_OP_OPT_V1;
        if (!ld_syntax_item_list_decode(&result.as.opt,
                                        ld_argument(term, 0u),
                                        context, depth + 1u))
            return false;
    } else {
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "unknown syntax-pattern operation");
    }
    *out = result;
    return true;
}

static bool ld_syntax_item_decode(CettaLdSyntaxItemV1 *out,
                                  const CettaOpLangV1SExpr *term,
                                  LdDecodeContext *context,
                                  uint32_t depth) {
    CettaLdSyntaxItemV1 result;

    if (!out || !ld_take_work(context, depth))
        return false;
    memset(&result, 0, sizeof(result));
    if (ld_application(term, "SyntaxTerminal", 1u)) {
        result.kind = CETTA_LD_SYNTAX_TERMINAL_V1;
        if (!ld_text_decode(&result.as.text, ld_argument(term, 0u),
                            context, depth + 1u))
            return false;
    } else if (ld_application(term, "SyntaxNonTerminal", 1u)) {
        result.kind = CETTA_LD_SYNTAX_NONTERMINAL_V1;
        if (!ld_text_decode(&result.as.text, ld_argument(term, 0u),
                            context, depth + 1u))
            return false;
    } else if (ld_application(term, "SyntaxSeparator", 1u)) {
        result.kind = CETTA_LD_SYNTAX_SEPARATOR_V1;
        if (!ld_text_decode(&result.as.text, ld_argument(term, 0u),
                            context, depth + 1u))
            return false;
    } else if (ld_application(term, "SyntaxDelimiter", 2u)) {
        result.kind = CETTA_LD_SYNTAX_DELIMITER_V1;
        if (!ld_text_decode(&result.as.delimiter.left,
                            ld_argument(term, 0u),
                            context, depth + 1u) ||
            !ld_text_decode(&result.as.delimiter.right,
                            ld_argument(term, 1u),
                            context, depth + 1u)) {
            ld_syntax_item_free(&result);
            return false;
        }
    } else if (ld_application(term, "SyntaxOp", 1u)) {
        result.kind = CETTA_LD_SYNTAX_OP_V1;
        result.as.op = calloc(1u, sizeof(*result.as.op));
        if (!result.as.op)
            return ld_fail(context, CETTA_LD_CORE_V1_ALLOCATION_FAILURE,
                           "failed to allocate syntax operation");
        if (!ld_syntax_op_decode(result.as.op, ld_argument(term, 0u),
                                 context, depth + 1u)) {
            ld_syntax_item_free(&result);
            return false;
        }
    } else {
        return ld_fail(context, CETTA_LD_CORE_V1_MALFORMED_WIRE,
                       "unknown syntax item in LanguageDef wire");
    }
    *out = result;
    return true;
}
