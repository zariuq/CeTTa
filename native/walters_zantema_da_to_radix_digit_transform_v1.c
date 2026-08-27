#include "walters_zantema_da_to_radix_digit_transform_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WALTERS_ZANTEMA_DA_RADIX_DIGIT_MAX_RADIX 16u
#define WALTERS_ZANTEMA_DA_RADIX_DIGIT_MAX_PRODUCT_DIGITS 2u
#define WALTERS_ZANTEMA_DA_RADIX_DIGIT_MAX_VECTOR_DIGITS 8u

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
} StringView;

typedef struct {
    uint32_t first;
    uint32_t second;
    uint32_t output;
    bool carry;
    uint32_t rule_index;
} AddEntry;

typedef struct {
    uint32_t input;
    uint32_t output;
    bool carry;
    uint32_t rule_index;
} SuccEntry;

typedef struct {
    uint32_t digit;
    uint32_t rule_index;
} DigitEntry;

typedef struct {
    uint32_t first;
    uint32_t second;
    uint32_t digits[WALTERS_ZANTEMA_DA_RADIX_DIGIT_MAX_PRODUCT_DIGITS];
    uint32_t digit_len;
    uint32_t rule_index;
} ProductEntry;

typedef struct {
    uint32_t radix;
    StringView *digit_labels;
    StringView *star_labels;
    AddEntry *additions;
    uint32_t addition_len;
    SuccEntry *successors;
    uint32_t successor_len;
    DigitEntry *multiplications;
    uint32_t multiplication_len;
    DigitEntry *star_zeros;
    uint32_t star_zero_len;
    ProductEntry *products;
    uint32_t product_len;
    uint32_t rule1_count;
    uint32_t rule2_count;
    uint32_t rule3_count;
    uint32_t rule5_count;
    uint32_t rule7_count;
} SourceProfile;

typedef struct {
    uint32_t digits[WALTERS_ZANTEMA_DA_RADIX_DIGIT_MAX_VECTOR_DIGITS];
    uint32_t digit_len;
    uint32_t origins[32];
    uint32_t origin_len;
} VectorResult;

typedef struct {
    const char *label;
    const char *category;
    const char *parameter_categories[6];
    uint32_t parameter_len;
    bool rewrite;
} GrammarSpec;

static void set_error(
    CettaWaltersZantemaDaRadixDigitV1Status *status,
    CettaWaltersZantemaDaRadixDigitV1Status value,
    char *error_buf,
    size_t error_buf_size,
    const char *format,
    ...) {
    va_list arguments;

    if (status)
        *status = value;
    if (!error_buf || error_buf_size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(error_buf, error_buf_size, format, arguments);
    va_end(arguments);
}

static bool string_view_equal(StringView left, StringView right) {
    return left.len == right.len &&
        (left.len == 0u || memcmp(left.bytes, right.bytes, left.len) == 0);
}

static bool string_view_is(StringView value, const char *text) {
    size_t len = strlen(text);
    return len <= UINT32_MAX && value.len == (uint32_t)len &&
        (len == 0u || memcmp(value.bytes, text, len) == 0);
}

static bool string_view_prefix(StringView value, const char *prefix) {
    size_t len = strlen(prefix);
    return len <= value.len &&
        (len == 0u || memcmp(value.bytes, prefix, len) == 0);
}

static bool as_string(const CettaOpLangV1SExpr *expression, StringView *out) {
    if (!expression || !out || expression->kind != CETTA_OP_LANG_V1_SEXPR_STRING)
        return false;
    out->bytes = expression->as.string.bytes;
    out->len = expression->as.string.len;
    return true;
}

static bool application(
    const CettaOpLangV1SExpr *expression,
    const char *head,
    uint32_t arity) {
    return cetta_op_lang_v1_application_is(expression, head, arity);
}

static const CettaOpLangV1SExpr *argument(
    const CettaOpLangV1SExpr *application_expression,
    uint32_t index) {
    if (!application_expression ||
            application_expression->kind != CETTA_OP_LANG_V1_SEXPR_APPLICATION ||
            index >= application_expression->as.application.argument_len)
        return NULL;
    return application_expression->as.application.arguments[index];
}

static bool list_is_empty(const CettaOpLangV1SExpr *list) {
    return cetta_op_lang_v1_field_len(list) == 0u;
}

static bool pattern_app(
    const CettaOpLangV1SExpr *pattern,
    StringView *label,
    const CettaOpLangV1SExpr **arguments) {
    if (!application(pattern, "PApp", 2u) ||
            !as_string(argument(pattern, 0u), label))
        return false;
    *arguments = argument(pattern, 1u);
    return true;
}

static bool pattern_app_is(
    const CettaOpLangV1SExpr *pattern,
    const char *label,
    uint32_t arity,
    const CettaOpLangV1SExpr **arguments) {
    StringView actual;
    if (!pattern_app(pattern, &actual, arguments) ||
            !string_view_is(actual, label))
        return false;
    return cetta_op_lang_v1_field_len(*arguments) == arity;
}

static bool pattern_var(
    const CettaOpLangV1SExpr *pattern,
    StringView *name) {
    return application(pattern, "FVar", 1u) &&
        as_string(argument(pattern, 0u), name);
}

static bool same_variable(
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right) {
    StringView left_name;
    StringView right_name;
    return pattern_var(left, &left_name) && pattern_var(right, &right_name) &&
        string_view_equal(left_name, right_name);
}

static bool empty_pattern(const CettaOpLangV1SExpr *pattern) {
    const CettaOpLangV1SExpr *arguments;
    return pattern_app_is(pattern, "da:empty", 0u, &arguments);
}

static bool grammar_view(
    const CettaOpLangV1SExpr *term,
    StringView *label,
    StringView *category,
    uint32_t *arity) {
    if (!application(term, "GrammarRule", 5u) ||
            !as_string(argument(term, 0u), label) ||
            !as_string(argument(term, 1u), category))
        return false;
    *arity = cetta_op_lang_v1_field_len(argument(term, 2u));
    return true;
}

static bool type_declaration_is(
    const CettaOpLangV1SExpr *declaration,
    const char *label) {
    StringView actual;
    return application(declaration, "TypeDecl", 2u) &&
        as_string(argument(declaration, 0u), &actual) &&
        string_view_is(actual, label) &&
        cetta_op_lang_v1_symbol_is(argument(declaration, 1u), "CarrierAst");
}

static bool type_declarations_exact(
    const CettaOpLangV1SExpr *field,
    const char *const *labels,
    uint32_t label_len) {
    bool found[16] = {false};
    uint32_t declaration_len = cetta_op_lang_v1_field_len(field);
    uint32_t declaration_index;

    if (label_len > 16u || declaration_len != label_len)
        return false;
    for (declaration_index = 0u; declaration_index < declaration_len;
            ++declaration_index) {
        const CettaOpLangV1SExpr *declaration =
            cetta_op_lang_v1_field_entry(field, declaration_index);
        uint32_t label_index;
        bool matched = false;
        for (label_index = 0u; label_index < label_len; ++label_index) {
            if (type_declaration_is(declaration, labels[label_index])) {
                if (found[label_index])
                    return false;
                found[label_index] = true;
                matched = true;
                break;
            }
        }
        if (!matched)
            return false;
    }
    for (declaration_index = 0u; declaration_index < label_len;
            ++declaration_index)
        if (!found[declaration_index])
            return false;
    return true;
}

static bool grammar_rule_matches(
    const CettaOpLangV1SExpr *term,
    const GrammarSpec *specification) {
    StringView label;
    StringView category;
    StringView parameter_names[6];
    const CettaOpLangV1SExpr *parameters;
    const CettaOpLangV1SExpr *syntax;
    const CettaOpLangV1SExpr *policy;
    uint32_t arity;
    uint32_t index;

    if (!term || !specification || specification->parameter_len > 6u ||
            !grammar_view(term, &label, &category, &arity) ||
            !string_view_is(label, specification->label) ||
            !string_view_is(category, specification->category) ||
            arity != specification->parameter_len)
        return false;
    parameters = argument(term, 2u);
    for (index = 0u; index < arity; ++index) {
        const CettaOpLangV1SExpr *parameter =
            cetta_op_lang_v1_field_entry(parameters, index);
        const CettaOpLangV1SExpr *type;
        StringView parameter_category;
        uint32_t previous;
        if (!application(parameter, "TermSimple", 2u) ||
                !as_string(argument(parameter, 0u), &parameter_names[index]))
            return false;
        type = argument(parameter, 1u);
        if (!application(type, "TBase", 1u) ||
                !as_string(argument(type, 0u), &parameter_category) ||
                !string_view_is(parameter_category,
                    specification->parameter_categories[index]))
            return false;
        for (previous = 0u; previous < index; ++previous)
            if (string_view_equal(parameter_names[index], parameter_names[previous]))
                return false;
    }
    syntax = argument(term, 3u);
    if (cetta_op_lang_v1_field_len(syntax) != 1u ||
            !application(cetta_op_lang_v1_field_entry(syntax, 0u),
                "SyntaxTerminal", 1u) ||
            !as_string(argument(cetta_op_lang_v1_field_entry(syntax, 0u), 0u),
                &label) ||
            !string_view_is(label, specification->label))
        return false;
    policy = argument(term, 4u);
    if (specification->rewrite)
        return application(policy, "EvalSome", 1u) &&
            cetta_op_lang_v1_symbol_is(argument(policy, 0u), "EvalRewrite");
    return cetta_op_lang_v1_symbol_is(policy, "EvalNone");
}

static bool grammar_specs_exact(
    const CettaOpLangV1SExpr *field,
    const GrammarSpec *specifications,
    uint32_t specification_len,
    uint32_t *indices) {
    bool found[32] = {false};
    uint32_t term_len = cetta_op_lang_v1_field_len(field);
    uint32_t term_index;

    if (specification_len > 32u || term_len != specification_len)
        return false;
    for (term_index = 0u; term_index < term_len; ++term_index) {
        const CettaOpLangV1SExpr *term =
            cetta_op_lang_v1_field_entry(field, term_index);
        uint32_t specification_index;
        bool matched = false;
        for (specification_index = 0u; specification_index < specification_len;
                ++specification_index) {
            if (grammar_rule_matches(term, &specifications[specification_index])) {
                if (found[specification_index])
                    return false;
                found[specification_index] = true;
                if (indices)
                    indices[specification_index] = term_index;
                matched = true;
                break;
            }
        }
        if (!matched)
            return false;
    }
    for (term_index = 0u; term_index < specification_len; ++term_index)
        if (!found[term_index])
            return false;
    return true;
}

static bool rule_view(
    const CettaOpLangV1SExpr *entry,
    const CettaOpLangV1SExpr **premises,
    const CettaOpLangV1SExpr **left,
    const CettaOpLangV1SExpr **right) {
    if (!application(entry, "RewriteRule", 5u))
        return false;
    *premises = argument(entry, 2u);
    *left = argument(entry, 3u);
    *right = argument(entry, 4u);
    return true;
}

static bool collect_pattern_variables(
    const CettaOpLangV1SExpr *expression,
    StringView *variables,
    uint32_t *variable_len,
    uint32_t capacity) {
    uint32_t index;
    StringView name;
    if (!expression)
        return false;
    if (pattern_var(expression, &name)) {
        for (index = 0u; index < *variable_len; ++index)
            if (string_view_equal(variables[index], name))
                return true;
        if (*variable_len >= capacity)
            return false;
        variables[(*variable_len)++] = name;
        return true;
    }
    if (expression->kind != CETTA_OP_LANG_V1_SEXPR_APPLICATION)
        return true;
    for (index = 0u; index < expression->as.application.argument_len; ++index)
        if (!collect_pattern_variables(argument(expression, index),
                variables, variable_len, capacity))
            return false;
    return true;
}

static bool source_rule_context_exact(
    const CettaOpLangV1SExpr *entry,
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right) {
    StringView variables[16];
    bool bound[16] = {false};
    uint32_t variable_len = 0u;
    const CettaOpLangV1SExpr *context = argument(entry, 1u);
    uint32_t context_len = cetta_op_lang_v1_field_len(context);
    uint32_t index;

    if (!collect_pattern_variables(left, variables, &variable_len, 16u) ||
            !collect_pattern_variables(right, variables, &variable_len, 16u) ||
            context_len != variable_len)
        return false;
    for (index = 0u; index < context_len; ++index) {
        const CettaOpLangV1SExpr *binding =
            cetta_op_lang_v1_field_entry(context, index);
        const CettaOpLangV1SExpr *type;
        StringView name;
        StringView category;
        uint32_t variable_index;
        bool matched = false;
        if (!application(binding, "TypeBinding", 2u) ||
                !as_string(argument(binding, 0u), &name))
            return false;
        type = argument(binding, 1u);
        if (!application(type, "TBase", 1u) ||
                !as_string(argument(type, 0u), &category) ||
                !string_view_is(category, "Nat"))
            return false;
        for (variable_index = 0u; variable_index < variable_len; ++variable_index) {
            if (string_view_equal(name, variables[variable_index])) {
                if (bound[variable_index])
                    return false;
                bound[variable_index] = true;
                matched = true;
                break;
            }
        }
        if (!matched)
            return false;
    }
    return true;
}

static int32_t label_index(
    const StringView *labels,
    uint32_t label_len,
    StringView label) {
    uint32_t index;
    for (index = 0u; index < label_len; ++index) {
        if (string_view_equal(labels[index], label))
            return (int32_t)index;
    }
    return -1;
}

static bool digit_view(
    const SourceProfile *profile,
    const CettaOpLangV1SExpr *pattern,
    uint32_t *digit,
    const CettaOpLangV1SExpr **body) {
    StringView label;
    const CettaOpLangV1SExpr *arguments;
    int32_t index;

    if (!pattern_app(pattern, &label, &arguments) ||
            cetta_op_lang_v1_field_len(arguments) != 1u)
        return false;
    index = label_index(profile->digit_labels, profile->radix, label);
    if (index < 0)
        return false;
    *digit = (uint32_t)index;
    *body = cetta_op_lang_v1_field_entry(arguments, 0u);
    return *body != NULL;
}

static bool star_view(
    const SourceProfile *profile,
    const CettaOpLangV1SExpr *pattern,
    uint32_t *digit,
    const CettaOpLangV1SExpr **body) {
    StringView label;
    const CettaOpLangV1SExpr *arguments;
    int32_t index;

    if (!pattern_app(pattern, &label, &arguments) ||
            cetta_op_lang_v1_field_len(arguments) != 1u)
        return false;
    index = label_index(profile->star_labels, profile->radix, label);
    if (index < 0)
        return false;
    *digit = (uint32_t)index;
    *body = cetta_op_lang_v1_field_entry(arguments, 0u);
    return *body != NULL;
}

static bool parse_numeral(
    const SourceProfile *profile,
    const CettaOpLangV1SExpr *pattern,
    uint32_t *digits,
    uint32_t *digit_len) {
    uint32_t count = 0u;

    while (!empty_pattern(pattern)) {
        uint32_t digit;
        const CettaOpLangV1SExpr *body;
        if (count >= WALTERS_ZANTEMA_DA_RADIX_DIGIT_MAX_PRODUCT_DIGITS ||
                !digit_view(profile, pattern, &digit, &body))
            return false;
        digits[count++] = digit;
        pattern = body;
    }
    *digit_len = count;
    return true;
}

static void source_profile_init(SourceProfile *profile) {
    memset(profile, 0, sizeof(*profile));
}

static void source_profile_free(SourceProfile *profile) {
    if (!profile)
        return;
    free(profile->digit_labels);
    free(profile->star_labels);
    free(profile->additions);
    free(profile->successors);
    free(profile->multiplications);
    free(profile->star_zeros);
    free(profile->products);
    source_profile_init(profile);
}

static bool infer_vocabulary(
    SourceProfile *profile,
    const CettaOperationalLanguageDefV1 *source) {
    static const char *const source_types[] = {"Nat"};
    static const GrammarSpec empty_spec = {
        "da:empty", "Nat", {NULL}, 0u, false
    };
    static const GrammarSpec add_spec = {
        "da:add", "Nat", {"Nat", "Nat"}, 2u, true
    };
    static const GrammarSpec mul_spec = {
        "da:mul", "Nat", {"Nat", "Nat"}, 2u, true
    };
    static const GrammarSpec succ_spec = {
        "da:succ", "Nat", {"Nat"}, 1u, true
    };
    uint32_t term_len = cetta_op_lang_v1_field_len(source->terms_field);
    uint32_t index;
    uint32_t radix = 0u;
    StringView label;
    StringView category;
    uint32_t arity;

    if (!type_declarations_exact(source->types_field, source_types, 1u) ||
            term_len < 8u ||
            !grammar_rule_matches(
                cetta_op_lang_v1_field_entry(source->terms_field, 0u),
                &empty_spec))
        return false;
    for (index = 1u; index < term_len; ++index) {
        if (!grammar_view(cetta_op_lang_v1_field_entry(source->terms_field, index),
                &label, &category, &arity))
            return false;
        if (!string_view_prefix(label, "da:digit:"))
            break;
        ++radix;
    }
    if (radix < 2u || radix > WALTERS_ZANTEMA_DA_RADIX_DIGIT_MAX_RADIX ||
            term_len != 4u + 2u * radix)
        return false;
    profile->digit_labels = calloc(radix, sizeof(*profile->digit_labels));
    profile->star_labels = calloc(radix, sizeof(*profile->star_labels));
    if (!profile->digit_labels || !profile->star_labels)
        return false;
    profile->radix = radix;
    for (index = 0u; index < radix; ++index) {
        char expected_label[32];
        GrammarSpec digit_spec = {
            expected_label, "Nat", {"Nat"}, 1u, false
        };
        if (snprintf(expected_label, sizeof(expected_label), "da:digit:%u", index) < 0 ||
                !grammar_rule_matches(cetta_op_lang_v1_field_entry(
                    source->terms_field, 1u + index), &digit_spec) ||
                !as_string(argument(cetta_op_lang_v1_field_entry(
                    source->terms_field, 1u + index), 0u), &label))
            return false;
        profile->digit_labels[index] = label;
    }
    if (!grammar_rule_matches(cetta_op_lang_v1_field_entry(
            source->terms_field, 1u + radix), &add_spec))
        return false;
    if (!grammar_rule_matches(cetta_op_lang_v1_field_entry(
            source->terms_field, 2u + radix), &mul_spec))
        return false;
    if (!grammar_rule_matches(cetta_op_lang_v1_field_entry(
            source->terms_field, 3u + radix), &succ_spec))
        return false;
    for (index = 0u; index < radix; ++index) {
        char expected_label[32];
        GrammarSpec star_spec = {
            expected_label, "Nat", {"Nat"}, 1u, true
        };
        if (snprintf(expected_label, sizeof(expected_label), "da:star:%u", index) < 0 ||
                !grammar_rule_matches(cetta_op_lang_v1_field_entry(
                    source->terms_field, 4u + radix + index), &star_spec) ||
                !as_string(argument(cetta_op_lang_v1_field_entry(
                    source->terms_field, 4u + radix + index), 0u), &label))
            return false;
        profile->star_labels[index] = label;
    }
    return true;
}

static bool parse_rule1(
    const SourceProfile *profile,
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right) {
    uint32_t digit;
    const CettaOpLangV1SExpr *body;
    return digit_view(profile, left, &digit, &body) && digit == 0u &&
        empty_pattern(body) && empty_pattern(right);
}

static bool parse_rule2(
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right) {
    const CettaOpLangV1SExpr *arguments;
    return pattern_app_is(left, "da:add", 2u, &arguments) &&
        empty_pattern(cetta_op_lang_v1_field_entry(arguments, 0u)) &&
        same_variable(cetta_op_lang_v1_field_entry(arguments, 1u), right);
}

static bool parse_rule3(
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right) {
    const CettaOpLangV1SExpr *arguments;
    return pattern_app_is(left, "da:add", 2u, &arguments) &&
        same_variable(cetta_op_lang_v1_field_entry(arguments, 0u), right) &&
        empty_pattern(cetta_op_lang_v1_field_entry(arguments, 1u));
}

static bool add_body_carry(
    const CettaOpLangV1SExpr *body,
    const CettaOpLangV1SExpr *first_variable,
    const CettaOpLangV1SExpr *second_variable,
    bool *carry) {
    const CettaOpLangV1SExpr *arguments;
    const CettaOpLangV1SExpr *add_arguments;

    if (pattern_app_is(body, "da:add", 2u, &arguments) &&
            same_variable(cetta_op_lang_v1_field_entry(arguments, 0u), first_variable) &&
            same_variable(cetta_op_lang_v1_field_entry(arguments, 1u), second_variable)) {
        *carry = false;
        return true;
    }
    if (!pattern_app_is(body, "da:succ", 1u, &arguments) ||
            !pattern_app_is(cetta_op_lang_v1_field_entry(arguments, 0u),
                "da:add", 2u, &add_arguments))
        return false;
    if (!same_variable(cetta_op_lang_v1_field_entry(add_arguments, 0u), first_variable) ||
            !same_variable(cetta_op_lang_v1_field_entry(add_arguments, 1u), second_variable))
        return false;
    *carry = true;
    return true;
}

static bool parse_rule4(
    const SourceProfile *profile,
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right,
    uint32_t rule_index,
    AddEntry *entry) {
    const CettaOpLangV1SExpr *add_arguments;
    const CettaOpLangV1SExpr *left_body;
    const CettaOpLangV1SExpr *right_body;
    const CettaOpLangV1SExpr *output_body;

    if (!pattern_app_is(left, "da:add", 2u, &add_arguments) ||
            !digit_view(profile, cetta_op_lang_v1_field_entry(add_arguments, 0u),
                &entry->first, &left_body) ||
            !digit_view(profile, cetta_op_lang_v1_field_entry(add_arguments, 1u),
                &entry->second, &right_body) ||
            !digit_view(profile, right, &entry->output, &output_body) ||
            !pattern_var(left_body, &(StringView){0}) ||
            !pattern_var(right_body, &(StringView){0}) ||
            !add_body_carry(output_body, left_body, right_body, &entry->carry))
        return false;
    entry->rule_index = rule_index;
    return true;
}

static bool parse_rule5(
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right) {
    const CettaOpLangV1SExpr *arguments;
    StringView ignored;
    return pattern_app_is(left, "da:mul", 2u, &arguments) &&
        empty_pattern(cetta_op_lang_v1_field_entry(arguments, 0u)) &&
        pattern_var(cetta_op_lang_v1_field_entry(arguments, 1u), &ignored) &&
        empty_pattern(right);
}

static bool parse_rule6(
    const SourceProfile *profile,
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right,
    uint32_t rule_index,
    DigitEntry *entry) {
    const CettaOpLangV1SExpr *mul_arguments;
    const CettaOpLangV1SExpr *add_arguments;
    const CettaOpLangV1SExpr *left_body;
    const CettaOpLangV1SExpr *shift_body;
    const CettaOpLangV1SExpr *recursive_arguments;
    const CettaOpLangV1SExpr *star_body;
    uint32_t zero_digit;
    uint32_t star_digit;
    StringView ignored;

    if (!pattern_app_is(left, "da:mul", 2u, &mul_arguments) ||
            !digit_view(profile, cetta_op_lang_v1_field_entry(mul_arguments, 0u),
                &entry->digit, &left_body) ||
            !pattern_var(left_body, &ignored) ||
            !pattern_var(cetta_op_lang_v1_field_entry(mul_arguments, 1u), &ignored) ||
            !pattern_app_is(right, "da:add", 2u, &add_arguments) ||
            !digit_view(profile, cetta_op_lang_v1_field_entry(add_arguments, 0u),
                &zero_digit, &shift_body) || zero_digit != 0u ||
            !pattern_app_is(shift_body, "da:mul", 2u, &recursive_arguments) ||
            !star_view(profile, cetta_op_lang_v1_field_entry(add_arguments, 1u),
                &star_digit, &star_body) || star_digit != entry->digit ||
            !same_variable(left_body, cetta_op_lang_v1_field_entry(recursive_arguments, 0u)) ||
            !same_variable(cetta_op_lang_v1_field_entry(mul_arguments, 1u),
                cetta_op_lang_v1_field_entry(recursive_arguments, 1u)) ||
            !same_variable(cetta_op_lang_v1_field_entry(mul_arguments, 1u), star_body))
        return false;
    entry->rule_index = rule_index;
    return true;
}

static bool parse_rule7(
    const SourceProfile *profile,
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right) {
    const CettaOpLangV1SExpr *arguments;
    const CettaOpLangV1SExpr *body;
    uint32_t digit;
    return pattern_app_is(left, "da:succ", 1u, &arguments) &&
        empty_pattern(cetta_op_lang_v1_field_entry(arguments, 0u)) &&
        digit_view(profile, right, &digit, &body) && digit == 1u &&
        empty_pattern(body);
}

static bool parse_rule8(
    const SourceProfile *profile,
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right,
    uint32_t rule_index,
    SuccEntry *entry) {
    const CettaOpLangV1SExpr *arguments;
    const CettaOpLangV1SExpr *input_body;
    const CettaOpLangV1SExpr *output_body;
    const CettaOpLangV1SExpr *succ_arguments;

    if (!pattern_app_is(left, "da:succ", 1u, &arguments) ||
            !digit_view(profile, cetta_op_lang_v1_field_entry(arguments, 0u),
                &entry->input, &input_body) ||
            !digit_view(profile, right, &entry->output, &output_body))
        return false;
    if (same_variable(input_body, output_body)) {
        entry->carry = false;
    } else if (pattern_app_is(output_body, "da:succ", 1u, &succ_arguments) &&
            same_variable(input_body,
                cetta_op_lang_v1_field_entry(succ_arguments, 0u))) {
        entry->carry = true;
    } else {
        return false;
    }
    entry->rule_index = rule_index;
    return true;
}

static bool parse_rule9(
    const SourceProfile *profile,
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right,
    uint32_t rule_index,
    DigitEntry *entry) {
    const CettaOpLangV1SExpr *body;
    if (!star_view(profile, left, &entry->digit, &body) ||
            !empty_pattern(body) || !empty_pattern(right))
        return false;
    entry->rule_index = rule_index;
    return true;
}

static bool parse_rule10(
    const SourceProfile *profile,
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right,
    uint32_t rule_index,
    ProductEntry *entry) {
    const CettaOpLangV1SExpr *input;
    const CettaOpLangV1SExpr *input_body;
    const CettaOpLangV1SExpr *add_arguments;
    const CettaOpLangV1SExpr *shift_body;
    const CettaOpLangV1SExpr *recursive_body;
    uint32_t zero_digit;
    uint32_t recursive_star;

    if (!star_view(profile, left, &entry->first, &input) ||
            !digit_view(profile, input, &entry->second, &input_body) ||
            !pattern_app_is(right, "da:add", 2u, &add_arguments) ||
            !digit_view(profile, cetta_op_lang_v1_field_entry(add_arguments, 0u),
                &zero_digit, &shift_body) || zero_digit != 0u ||
            !star_view(profile, shift_body, &recursive_star, &recursive_body) ||
            recursive_star != entry->first ||
            !same_variable(input_body, recursive_body) ||
            !parse_numeral(profile,
                cetta_op_lang_v1_field_entry(add_arguments, 1u),
                entry->digits, &entry->digit_len))
        return false;
    entry->rule_index = rule_index;
    return true;
}

static uint32_t count_digit_entry(
    const DigitEntry *entries,
    uint32_t len,
    uint32_t digit) {
    uint32_t count = 0u;
    uint32_t index;
    for (index = 0u; index < len; ++index)
        if (entries[index].digit == digit)
            ++count;
    return count;
}

static uint32_t count_add_entry(
    const AddEntry *entries,
    uint32_t len,
    uint32_t first,
    uint32_t second) {
    uint32_t count = 0u;
    uint32_t index;
    for (index = 0u; index < len; ++index)
        if (entries[index].first == first && entries[index].second == second)
            ++count;
    return count;
}

static uint32_t count_succ_entry(
    const SuccEntry *entries,
    uint32_t len,
    uint32_t input) {
    uint32_t count = 0u;
    uint32_t index;
    for (index = 0u; index < len; ++index)
        if (entries[index].input == input)
            ++count;
    return count;
}

static uint32_t count_product_entry(
    const ProductEntry *entries,
    uint32_t len,
    uint32_t first,
    uint32_t second) {
    uint32_t count = 0u;
    uint32_t index;
    for (index = 0u; index < len; ++index)
        if (entries[index].first == first && entries[index].second == second)
            ++count;
    return count;
}

static bool source_profile_complete(const SourceProfile *profile) {
    uint32_t first;
    uint32_t second;
    if (profile->rule1_count != 1u || profile->rule2_count != 1u ||
            profile->rule3_count != 1u || profile->rule5_count != 1u ||
            profile->rule7_count != 1u ||
            profile->addition_len != profile->radix * profile->radix ||
            profile->successor_len != profile->radix ||
            profile->multiplication_len != profile->radix ||
            profile->star_zero_len != profile->radix ||
            profile->product_len != profile->radix * profile->radix)
        return false;
    for (first = 0u; first < profile->radix; ++first) {
        if (count_digit_entry(profile->multiplications,
                profile->multiplication_len, first) != 1u ||
                count_digit_entry(profile->star_zeros,
                    profile->star_zero_len, first) != 1u ||
                count_succ_entry(profile->successors,
                    profile->successor_len, first) != 1u)
            return false;
        for (second = 0u; second < profile->radix; ++second) {
            if (count_add_entry(profile->additions, profile->addition_len,
                    first, second) != 1u ||
                    count_product_entry(profile->products, profile->product_len,
                        first, second) != 1u)
                return false;
        }
    }
    return true;
}

static bool inspect_source(
    SourceProfile *profile,
    const CettaOperationalLanguageDefV1 *source,
    CettaWaltersZantemaDaRadixDigitV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t rule_len;
    uint32_t index;
    uint32_t classified = 0u;

    if (!list_is_empty(source->equations_field) || !infer_vocabulary(profile, source)) {
        set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_SOURCE,
            error_buf, error_buf_size, "source is not a supported finite DA presentation");
        return false;
    }
    rule_len = cetta_op_lang_v1_field_len(source->rewrites_field);
    profile->additions = calloc(rule_len, sizeof(*profile->additions));
    profile->successors = calloc(rule_len, sizeof(*profile->successors));
    profile->multiplications = calloc(rule_len, sizeof(*profile->multiplications));
    profile->star_zeros = calloc(rule_len, sizeof(*profile->star_zeros));
    profile->products = calloc(rule_len, sizeof(*profile->products));
    if (!profile->additions || !profile->successors ||
            !profile->multiplications || !profile->star_zeros || !profile->products) {
        set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_ALLOCATION_FAILURE,
            error_buf, error_buf_size, "unable to allocate source profile");
        return false;
    }
    for (index = 0u; index < rule_len; ++index) {
        const CettaOpLangV1SExpr *entry =
            cetta_op_lang_v1_field_entry(source->rewrites_field, index);
        const CettaOpLangV1SExpr *premises;
        const CettaOpLangV1SExpr *left;
        const CettaOpLangV1SExpr *right;
        uint32_t matches = 0u;
        AddEntry add_entry;
        SuccEntry succ_entry;
        DigitEntry digit_entry;
        ProductEntry product_entry;

        if (!rule_view(entry, &premises, &left, &right) ||
                !list_is_empty(premises) ||
                !source_rule_context_exact(entry, left, right)) {
            set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_SOURCE,
                error_buf, error_buf_size,
                "source rule %u has malformed premises or typing context", index);
            return false;
        }
        if (parse_rule1(profile, left, right)) {
            ++profile->rule1_count;
            ++matches;
        }
        if (parse_rule2(left, right)) {
            ++profile->rule2_count;
            ++matches;
        }
        if (parse_rule3(left, right)) {
            ++profile->rule3_count;
            ++matches;
        }
        if (parse_rule4(profile, left, right, index, &add_entry)) {
            profile->additions[profile->addition_len++] = add_entry;
            ++matches;
        }
        if (parse_rule5(left, right)) {
            ++profile->rule5_count;
            ++matches;
        }
        if (parse_rule6(profile, left, right, index, &digit_entry)) {
            profile->multiplications[profile->multiplication_len++] = digit_entry;
            ++matches;
        }
        if (parse_rule7(profile, left, right)) {
            ++profile->rule7_count;
            ++matches;
        }
        if (parse_rule8(profile, left, right, index, &succ_entry)) {
            profile->successors[profile->successor_len++] = succ_entry;
            ++matches;
        }
        if (parse_rule9(profile, left, right, index, &digit_entry)) {
            profile->star_zeros[profile->star_zero_len++] = digit_entry;
            ++matches;
        }
        if (parse_rule10(profile, left, right, index, &product_entry)) {
            profile->products[profile->product_len++] = product_entry;
            ++matches;
        }
        if (matches != 1u) {
            set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_INCONSISTENT_SOURCE,
                error_buf, error_buf_size,
                "source rule %u belongs to %u recognized DA families", index, matches);
            return false;
        }
        ++classified;
    }
    if (classified != rule_len || !source_profile_complete(profile)) {
        set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_INCONSISTENT_SOURCE,
            error_buf, error_buf_size, "source DA rule families are incomplete or duplicated");
        return false;
    }
    return true;
}

static bool relation_query_is(
    const CettaOpLangV1SExpr *premise,
    const char *relation) {
    StringView name;
    return application(premise, "RelationQuery", 2u) &&
        as_string(argument(premise, 0u), &name) && string_view_is(name, relation);
}

static uint32_t count_pattern_application(
    const CettaOpLangV1SExpr *expression,
    const char *label) {
    uint32_t count = 0u;
    uint32_t index;

    if (!expression)
        return 0u;
    if (expression->kind != CETTA_OP_LANG_V1_SEXPR_APPLICATION)
        return 0u;
    if (application(expression, "PApp", 2u)) {
        StringView actual;
        if (as_string(argument(expression, 0u), &actual) &&
                string_view_is(actual, label))
            ++count;
    }
    for (index = 0u; index < expression->as.application.argument_len; ++index)
        count += count_pattern_application(argument(expression, index), label);
    return count;
}

typedef enum {
    RADIX_DIGIT_TARGET_FUEL_EXHAUSTED = 0,
    RADIX_DIGIT_TARGET_MISSING_PC,
    RADIX_DIGIT_TARGET_EXECUTE_NEXT,
    RADIX_DIGIT_TARGET_EXECUTE_VALUE,
    RADIX_DIGIT_TARGET_EXECUTE_LANGUAGE_FAULT,
    RADIX_DIGIT_TARGET_EXECUTE_ENGINE_FAULT,
    RADIX_DIGIT_TARGET_EXECUTE_RESOURCE_FAULT,
    RADIX_DIGIT_TARGET_RULE_KIND_COUNT,
    RADIX_DIGIT_TARGET_RULE_INVALID
} RadixDigitTargetRuleKind;

static RadixDigitTargetRuleKind classify_target_rule(
    const CettaOpLangV1SExpr *premises,
    const CettaOpLangV1SExpr *right) {
    uint32_t premise_len = cetta_op_lang_v1_field_len(premises);
    uint32_t execute_count = 0u;
    uint32_t fetch_count = 0u;
    uint32_t consume_count = 0u;
    uint32_t missing_count = 0u;
    uint32_t fuel_count = 0u;
    uint32_t index;

    for (index = 0u; index < premise_len; ++index) {
        const CettaOpLangV1SExpr *premise =
            cetta_op_lang_v1_field_entry(premises, index);
        if (relation_query_is(premise, "RadixDigitExecuteInstruction"))
            ++execute_count;
        else if (relation_query_is(premise, "RadixDigitFetch"))
            ++fetch_count;
        else if (relation_query_is(premise, "RadixDigitConsumeFuel"))
            ++consume_count;
        else if (relation_query_is(premise, "RadixDigitMissingProgramCounter"))
            ++missing_count;
        else if (relation_query_is(premise, "RadixDigitFuelExhaustedFault"))
            ++fuel_count;
        else
            return RADIX_DIGIT_TARGET_RULE_INVALID;
    }

    if (premise_len == 1u && fuel_count == 1u &&
            count_pattern_application(right, "radix-digit:halted") == 1u &&
            count_pattern_application(right, "radix-digit:outcome-resource-fault") == 1u &&
            count_pattern_application(right, "radix-digit:resource-fault-event") == 1u &&
            count_pattern_application(right, "radix-digit:receipt-cons") == 1u)
        return RADIX_DIGIT_TARGET_FUEL_EXHAUSTED;

    if (premise_len == 2u && consume_count == 1u && missing_count == 1u &&
            count_pattern_application(right, "radix-digit:halted") == 1u &&
            count_pattern_application(right, "radix-digit:outcome-engine-fault") == 1u &&
            count_pattern_application(right, "radix-digit:engine-fault-event") == 1u &&
            count_pattern_application(right, "radix-digit:receipt-cons") == 1u)
        return RADIX_DIGIT_TARGET_MISSING_PC;

    if (premise_len == 3u && consume_count == 1u && fetch_count == 1u &&
            execute_count == 1u &&
            count_pattern_application(premises, "radix-digit:result-next") == 1u &&
            count_pattern_application(right, "radix-digit:run") == 1u &&
            count_pattern_application(premises, "radix-digit:execute-event") == 1u &&
            count_pattern_application(premises, "radix-digit:receipt-cons") == 1u)
        return RADIX_DIGIT_TARGET_EXECUTE_NEXT;

    if (premise_len == 3u && consume_count == 1u && fetch_count == 1u &&
            execute_count == 1u &&
            count_pattern_application(premises, "radix-digit:result-value") == 1u &&
            count_pattern_application(right, "radix-digit:halted") == 1u &&
            count_pattern_application(right, "radix-digit:outcome-value") == 1u &&
            count_pattern_application(premises, "radix-digit:execute-event") == 1u &&
            count_pattern_application(premises, "radix-digit:receipt-cons") == 1u)
        return RADIX_DIGIT_TARGET_EXECUTE_VALUE;

    if (premise_len == 3u && consume_count == 1u && fetch_count == 1u &&
            execute_count == 1u &&
            count_pattern_application(premises, "radix-digit:result-language-fault") == 1u &&
            count_pattern_application(right, "radix-digit:halted") == 1u &&
            count_pattern_application(right, "radix-digit:outcome-language-fault") == 1u &&
            count_pattern_application(premises, "radix-digit:execute-event") == 1u &&
            count_pattern_application(premises, "radix-digit:receipt-cons") == 1u)
        return RADIX_DIGIT_TARGET_EXECUTE_LANGUAGE_FAULT;

    if (premise_len == 3u && consume_count == 1u && fetch_count == 1u &&
            execute_count == 1u &&
            count_pattern_application(premises, "radix-digit:result-engine-fault") == 1u &&
            count_pattern_application(right, "radix-digit:halted") == 1u &&
            count_pattern_application(right, "radix-digit:outcome-engine-fault") == 1u &&
            count_pattern_application(premises, "radix-digit:execute-event") == 1u &&
            count_pattern_application(premises, "radix-digit:receipt-cons") == 1u)
        return RADIX_DIGIT_TARGET_EXECUTE_ENGINE_FAULT;

    if (premise_len == 3u && consume_count == 1u && fetch_count == 1u &&
            execute_count == 1u &&
            count_pattern_application(premises, "radix-digit:result-resource-fault") == 1u &&
            count_pattern_application(right, "radix-digit:halted") == 1u &&
            count_pattern_application(right, "radix-digit:outcome-resource-fault") == 1u &&
            count_pattern_application(premises, "radix-digit:execute-event") == 1u &&
            count_pattern_application(premises, "radix-digit:receipt-cons") == 1u)
        return RADIX_DIGIT_TARGET_EXECUTE_RESOURCE_FAULT;

    return RADIX_DIGIT_TARGET_RULE_INVALID;
}

static bool query_arguments(
    const CettaOpLangV1SExpr *premise,
    const char *relation,
    uint32_t arity,
    const CettaOpLangV1SExpr **arguments) {
    StringView name;
    if (!application(premise, "RelationQuery", 2u) ||
            !as_string(argument(premise, 0u), &name) ||
            !string_view_is(name, relation))
        return false;
    *arguments = argument(premise, 1u);
    return cetta_op_lang_v1_field_len(*arguments) == arity;
}

static bool pattern_arguments(
    const CettaOpLangV1SExpr *pattern,
    const char *label,
    uint32_t arity,
    const CettaOpLangV1SExpr **arguments) {
    return pattern_app_is(pattern, label, arity, arguments);
}

static bool list_variable_is(
    const CettaOpLangV1SExpr *list,
    uint32_t index,
    const CettaOpLangV1SExpr *expected) {
    return same_variable(cetta_op_lang_v1_field_entry(list, index), expected);
}

static bool variables_are_distinct(
    const CettaOpLangV1SExpr *const *variables,
    uint32_t len) {
    uint32_t left;
    uint32_t right;
    for (left = 0u; left < len; ++left) {
        StringView ignored;
        if (!pattern_var(variables[left], &ignored))
            return false;
        for (right = left + 1u; right < len; ++right)
            if (same_variable(variables[left], variables[right]))
                return false;
    }
    return true;
}

static bool match_single_event_receipt(
    const CettaOpLangV1SExpr *receipt,
    const char *event_label,
    const CettaOpLangV1SExpr *pc,
    const CettaOpLangV1SExpr *fault,
    const CettaOpLangV1SExpr *tail) {
    const CettaOpLangV1SExpr *receipt_arguments;
    const CettaOpLangV1SExpr *event_arguments;
    uint32_t event_arity = fault ? 2u : 1u;
    if (!pattern_arguments(receipt, "radix-digit:receipt-cons", 2u,
            &receipt_arguments) ||
            !pattern_arguments(cetta_op_lang_v1_field_entry(receipt_arguments, 0u),
                event_label, event_arity, &event_arguments) ||
            !list_variable_is(event_arguments, 0u, pc) ||
            !same_variable(cetta_op_lang_v1_field_entry(receipt_arguments, 1u), tail))
        return false;
    return !fault || list_variable_is(event_arguments, 1u, fault);
}

static bool match_fault_outcome(
    const CettaOpLangV1SExpr *right,
    const char *outcome_label,
    const char *event_label,
    const CettaOpLangV1SExpr *pc,
    const CettaOpLangV1SExpr *fault,
    const CettaOpLangV1SExpr *receipt,
    bool include_execute) {
    const CettaOpLangV1SExpr *halted_arguments;
    const CettaOpLangV1SExpr *outcome_arguments;
    const CettaOpLangV1SExpr *receipt_value;
    if (!pattern_arguments(right, "radix-digit:halted", 2u, &halted_arguments) ||
            !pattern_arguments(cetta_op_lang_v1_field_entry(halted_arguments, 0u),
                outcome_label, 1u, &outcome_arguments) ||
            !list_variable_is(outcome_arguments, 0u, fault))
        return false;
    receipt_value = cetta_op_lang_v1_field_entry(halted_arguments, 1u);
    if (!include_execute)
        return match_single_event_receipt(receipt_value, event_label,
            pc, fault, receipt);
    {
        const CettaOpLangV1SExpr *outer_arguments;
        const CettaOpLangV1SExpr *fault_event_arguments;
        const CettaOpLangV1SExpr *inner;
        if (!pattern_arguments(receipt_value, "radix-digit:receipt-cons", 2u,
                &outer_arguments) ||
                !pattern_arguments(
                    cetta_op_lang_v1_field_entry(outer_arguments, 0u),
                    event_label, 2u, &fault_event_arguments) ||
                !list_variable_is(fault_event_arguments, 0u, pc) ||
                !list_variable_is(fault_event_arguments, 1u, fault))
            return false;
        inner = cetta_op_lang_v1_field_entry(outer_arguments, 1u);
        return match_single_event_receipt(inner, "radix-digit:execute-event",
            pc, NULL, receipt);
    }
}

static bool context_binding_view(
    const CettaOpLangV1SExpr *binding,
    StringView *name,
    StringView *category) {
    const CettaOpLangV1SExpr *type;
    if (!application(binding, "TypeBinding", 2u) ||
            !as_string(argument(binding, 0u), name))
        return false;
    type = argument(binding, 1u);
    return application(type, "TBase", 1u) &&
        as_string(argument(type, 0u), category);
}

static bool context_well_formed(const CettaOpLangV1SExpr *context) {
    uint32_t len = cetta_op_lang_v1_field_len(context);
    uint32_t left;
    uint32_t right;
    for (left = 0u; left < len; ++left) {
        StringView left_name;
        StringView ignored;
        if (!context_binding_view(cetta_op_lang_v1_field_entry(context, left),
                &left_name, &ignored))
            return false;
        for (right = left + 1u; right < len; ++right) {
            StringView right_name;
            if (!context_binding_view(cetta_op_lang_v1_field_entry(context, right),
                    &right_name, &ignored) ||
                    string_view_equal(left_name, right_name))
                return false;
        }
    }
    return true;
}

static uint32_t context_category_count(
    const CettaOpLangV1SExpr *context,
    const char *category) {
    uint32_t len = cetta_op_lang_v1_field_len(context);
    uint32_t count = 0u;
    uint32_t index;
    for (index = 0u; index < len; ++index) {
        StringView ignored;
        StringView actual;
        if (context_binding_view(cetta_op_lang_v1_field_entry(context, index),
                &ignored, &actual) && string_view_is(actual, category))
            ++count;
    }
    return count;
}

static bool variable_has_category(
    const CettaOpLangV1SExpr *context,
    const CettaOpLangV1SExpr *variable,
    const char *category) {
    StringView variable_name;
    uint32_t len = cetta_op_lang_v1_field_len(context);
    uint32_t index;
    if (!pattern_var(variable, &variable_name))
        return false;
    for (index = 0u; index < len; ++index) {
        StringView binding_name;
        StringView binding_category;
        if (!context_binding_view(cetta_op_lang_v1_field_entry(context, index),
                &binding_name, &binding_category))
            return false;
        if (string_view_equal(variable_name, binding_name))
            return string_view_is(binding_category, category);
    }
    return false;
}

static bool common_running_categories(
    const CettaOpLangV1SExpr *context,
    const CettaOpLangV1SExpr *const left[6]) {
    return variable_has_category(context, left[0], "Program") &&
        variable_has_category(context, left[1], "Nat") &&
        variable_has_category(context, left[2], "Buffers") &&
        variable_has_category(context, left[3], "Registers") &&
        variable_has_category(context, left[4], "Fuel") &&
        variable_has_category(context, left[5], "Receipt");
}

static bool category_counts_are(
    const CettaOpLangV1SExpr *context,
    uint32_t program,
    uint32_t nat,
    uint32_t buffers,
    uint32_t registers,
    uint32_t fuel,
    uint32_t receipt,
    uint32_t instruction,
    uint32_t digit_buffer,
    uint32_t fault) {
    uint32_t expected = program + nat + buffers + registers + fuel + receipt +
        instruction + digit_buffer + fault;
    return cetta_op_lang_v1_field_len(context) == expected &&
        context_category_count(context, "Program") == program &&
        context_category_count(context, "Nat") == nat &&
        context_category_count(context, "Buffers") == buffers &&
        context_category_count(context, "Registers") == registers &&
        context_category_count(context, "Fuel") == fuel &&
        context_category_count(context, "Receipt") == receipt &&
        context_category_count(context, "Instruction") == instruction &&
        context_category_count(context, "DigitBuffer") == digit_buffer &&
        context_category_count(context, "Fault") == fault;
}

static bool target_rule_wiring_exact(
    RadixDigitTargetRuleKind kind,
    const CettaOpLangV1SExpr *context,
    const CettaOpLangV1SExpr *premises,
    const CettaOpLangV1SExpr *left_arguments,
    const CettaOpLangV1SExpr *right) {
    const CettaOpLangV1SExpr *left[6];
    const CettaOpLangV1SExpr *premise_arguments;
    const CettaOpLangV1SExpr *next_fuel;
    const CettaOpLangV1SExpr *instruction;
    const CettaOpLangV1SExpr *result;
    const CettaOpLangV1SExpr *result_arguments;
    const CettaOpLangV1SExpr *right_arguments;
    const CettaOpLangV1SExpr *distinct[12];
    uint32_t index;

    if (!context_well_formed(context))
        return false;
    for (index = 0u; index < 6u; ++index)
        left[index] = cetta_op_lang_v1_field_entry(left_arguments, index);

    if (kind == RADIX_DIGIT_TARGET_FUEL_EXHAUSTED) {
        const CettaOpLangV1SExpr *fuel_arguments;
        if (!category_counts_are(context,
                1u, 1u, 1u, 1u, 0u, 1u, 0u, 0u, 1u) ||
                !variables_are_distinct((const CettaOpLangV1SExpr *const[]){
                left[0], left[1], left[2], left[3], left[5]}, 5u) ||
                !variable_has_category(context, left[0], "Program") ||
                !variable_has_category(context, left[1], "Nat") ||
                !variable_has_category(context, left[2], "Buffers") ||
                !variable_has_category(context, left[3], "Registers") ||
                !variable_has_category(context, left[5], "Receipt") ||
                !pattern_arguments(left[4], "radix-digit:fuel-zero", 0u,
                    &fuel_arguments) ||
                !query_arguments(cetta_op_lang_v1_field_entry(premises, 0u),
                    "RadixDigitFuelExhaustedFault", 1u, &premise_arguments))
            return false;
        return match_fault_outcome(right, "radix-digit:outcome-resource-fault",
            "radix-digit:resource-fault-event", left[1],
            cetta_op_lang_v1_field_entry(premise_arguments, 0u), left[5], false);
    }

    if (!variables_are_distinct(left, 6u) ||
            !common_running_categories(context, left))
        return false;

    if (kind == RADIX_DIGIT_TARGET_MISSING_PC) {
        const CettaOpLangV1SExpr *missing_arguments;
        if (!category_counts_are(context,
                1u, 1u, 1u, 1u, 2u, 1u, 1u, 0u, 1u) ||
                !query_arguments(cetta_op_lang_v1_field_entry(premises, 0u),
                "RadixDigitConsumeFuel", 2u, &premise_arguments) ||
                !list_variable_is(premise_arguments, 0u, left[4]))
            return false;
        next_fuel = cetta_op_lang_v1_field_entry(premise_arguments, 1u);
        if (!pattern_var(next_fuel, &(StringView){0}) ||
                !query_arguments(cetta_op_lang_v1_field_entry(premises, 1u),
                    "RadixDigitMissingProgramCounter", 3u, &missing_arguments) ||
                !list_variable_is(missing_arguments, 0u, left[0]) ||
                !list_variable_is(missing_arguments, 1u, left[1]))
            return false;
        if (!variable_has_category(context, next_fuel, "Fuel") ||
                same_variable(next_fuel, left[4]) ||
                !variable_has_category(context,
                    cetta_op_lang_v1_field_entry(missing_arguments, 2u), "Fault"))
            return false;
        return match_fault_outcome(right, "radix-digit:outcome-engine-fault",
            "radix-digit:engine-fault-event", left[1],
            cetta_op_lang_v1_field_entry(missing_arguments, 2u), left[5], false);
    }

    if (!query_arguments(cetta_op_lang_v1_field_entry(premises, 0u),
            "RadixDigitConsumeFuel", 2u, &premise_arguments) ||
            !list_variable_is(premise_arguments, 0u, left[4]))
        return false;
    next_fuel = cetta_op_lang_v1_field_entry(premise_arguments, 1u);
    if (!query_arguments(cetta_op_lang_v1_field_entry(premises, 1u),
            "RadixDigitFetch", 3u, &premise_arguments) ||
            !list_variable_is(premise_arguments, 0u, left[0]) ||
            !list_variable_is(premise_arguments, 1u, left[1]))
        return false;
    instruction = cetta_op_lang_v1_field_entry(premise_arguments, 2u);
    if (!query_arguments(cetta_op_lang_v1_field_entry(premises, 2u),
            "RadixDigitExecuteInstruction", 5u, &premise_arguments) ||
            !list_variable_is(premise_arguments, 0u, instruction) ||
            !list_variable_is(premise_arguments, 1u, left[2]) ||
            !list_variable_is(premise_arguments, 2u, left[3]) ||
            !match_single_event_receipt(
                cetta_op_lang_v1_field_entry(premise_arguments, 3u),
                "radix-digit:execute-event", left[1], NULL, left[5]))
        return false;
    result = cetta_op_lang_v1_field_entry(premise_arguments, 4u);

    if (kind == RADIX_DIGIT_TARGET_EXECUTE_NEXT) {
        const CettaOpLangV1SExpr *next_buffers;
        const CettaOpLangV1SExpr *next_registers;
        const CettaOpLangV1SExpr *next_pc;
        const CettaOpLangV1SExpr *next_receipt;
        if (!pattern_arguments(result, "radix-digit:result-next", 4u,
                &result_arguments))
            return false;
        next_buffers = cetta_op_lang_v1_field_entry(result_arguments, 0u);
        next_registers = cetta_op_lang_v1_field_entry(result_arguments, 1u);
        next_pc = cetta_op_lang_v1_field_entry(result_arguments, 2u);
        next_receipt = cetta_op_lang_v1_field_entry(result_arguments, 3u);
        distinct[0] = left[0]; distinct[1] = left[1]; distinct[2] = left[2];
        distinct[3] = left[3]; distinct[4] = left[4]; distinct[5] = left[5];
        distinct[6] = next_fuel; distinct[7] = instruction;
        distinct[8] = next_buffers; distinct[9] = next_registers;
        distinct[10] = next_pc; distinct[11] = next_receipt;
        if (!category_counts_are(context,
                1u, 2u, 2u, 2u, 2u, 2u, 1u, 0u, 0u) ||
                !variables_are_distinct(distinct, 12u) ||
                !variable_has_category(context, next_fuel, "Fuel") ||
                !variable_has_category(context, instruction, "Instruction") ||
                !variable_has_category(context, next_buffers, "Buffers") ||
                !variable_has_category(context, next_registers, "Registers") ||
                !variable_has_category(context, next_pc, "Nat") ||
                !variable_has_category(context, next_receipt, "Receipt") ||
                !pattern_var(next_buffers, &(StringView){0}) ||
                !pattern_var(next_registers, &(StringView){0}) ||
                same_variable(next_buffers, next_registers) ||
                !pattern_arguments(right, "radix-digit:run", 6u, &right_arguments) ||
                !list_variable_is(right_arguments, 0u, left[0]) ||
                !list_variable_is(right_arguments, 1u, next_pc) ||
                !list_variable_is(right_arguments, 2u, next_buffers) ||
                !list_variable_is(right_arguments, 3u, next_registers) ||
                !list_variable_is(right_arguments, 4u, next_fuel) ||
                !list_variable_is(right_arguments, 5u, next_receipt))
            return false;
        return true;
    }

    if (kind == RADIX_DIGIT_TARGET_EXECUTE_VALUE) {
        const CettaOpLangV1SExpr *digits;
        const CettaOpLangV1SExpr *next_receipt;
        const CettaOpLangV1SExpr *outcome_arguments;
        if (!category_counts_are(context,
                1u, 1u, 1u, 1u, 2u, 2u, 1u, 1u, 0u) ||
                !pattern_arguments(result, "radix-digit:result-value", 2u,
                &result_arguments))
            return false;
        digits = cetta_op_lang_v1_field_entry(result_arguments, 0u);
        next_receipt = cetta_op_lang_v1_field_entry(result_arguments, 1u);
        if (!variable_has_category(context, next_fuel, "Fuel") ||
                same_variable(next_fuel, left[4]) ||
                !variable_has_category(context, instruction, "Instruction") ||
                !variable_has_category(context, digits, "DigitBuffer") ||
                !variable_has_category(context, next_receipt, "Receipt") ||
                !pattern_arguments(right, "radix-digit:halted", 2u, &right_arguments) ||
                !pattern_arguments(cetta_op_lang_v1_field_entry(right_arguments, 0u),
                    "radix-digit:outcome-value", 1u, &outcome_arguments) ||
                !list_variable_is(outcome_arguments, 0u, digits) ||
                !list_variable_is(right_arguments, 1u, next_receipt))
            return false;
        return true;
    }

    {
        const char *result_label;
        const char *outcome_label;
        const CettaOpLangV1SExpr *fault;
        const CettaOpLangV1SExpr *next_receipt;
        if (kind == RADIX_DIGIT_TARGET_EXECUTE_LANGUAGE_FAULT) {
            result_label = "radix-digit:result-language-fault";
            outcome_label = "radix-digit:outcome-language-fault";
        } else if (kind == RADIX_DIGIT_TARGET_EXECUTE_ENGINE_FAULT) {
            result_label = "radix-digit:result-engine-fault";
            outcome_label = "radix-digit:outcome-engine-fault";
        } else if (kind == RADIX_DIGIT_TARGET_EXECUTE_RESOURCE_FAULT) {
            result_label = "radix-digit:result-resource-fault";
            outcome_label = "radix-digit:outcome-resource-fault";
        } else {
            return false;
        }
        if (!pattern_arguments(result, result_label, 2u, &result_arguments))
            return false;
        fault = cetta_op_lang_v1_field_entry(result_arguments, 0u);
        next_receipt = cetta_op_lang_v1_field_entry(result_arguments, 1u);
        if (!category_counts_are(context,
                1u, 1u, 1u, 1u, 2u, 2u, 1u, 0u, 1u) ||
                !variable_has_category(context, next_fuel, "Fuel") ||
                same_variable(next_fuel, left[4]) ||
                !variable_has_category(context, instruction, "Instruction") ||
                !variable_has_category(context, fault, "Fault") ||
                !variable_has_category(context, next_receipt, "Receipt") ||
                !pattern_arguments(right, "radix-digit:halted", 2u, &right_arguments) ||
                !pattern_arguments(cetta_op_lang_v1_field_entry(right_arguments, 0u),
                    outcome_label, 1u, &result_arguments) ||
                !list_variable_is(result_arguments, 0u, fault) ||
                !list_variable_is(right_arguments, 1u, next_receipt))
            return false;
        return true;
    }
}

static bool inspect_target(
    CettaRadixDigitV1TargetProfile *profile,
    const CettaOperationalLanguageDefV1 *target,
    CettaWaltersZantemaDaRadixDigitV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    static const char *const target_types[] = {
        "Config", "Program", "Buffers", "Registers", "Fuel", "Receipt",
        "Instruction", "RegisterList", "Table", "StepResult", "Outcome",
        "Fault", "Event", "DigitBuffer", "Nat"
    };
    static const GrammarSpec target_terms[] = {
        {"radix-digit:run", "Config",
            {"Program", "Nat", "Buffers", "Registers", "Fuel", "Receipt"}, 6u, true},
        {"radix-digit:halted", "Config", {"Outcome", "Receipt"}, 2u, false},
        {"radix-digit:fuel-zero", "Fuel", {NULL}, 0u, false},
        {"radix-digit:set", "Instruction", {"Nat", "Nat", "Nat"}, 3u, false},
        {"radix-digit:copy", "Instruction", {"Nat", "Nat", "Nat"}, 3u, false},
        {"radix-digit:increment", "Instruction", {"Nat", "Nat"}, 2u, false},
        {"radix-digit:length", "Instruction", {"Nat", "Nat", "Nat"}, 3u, false},
        {"radix-digit:read-or-zero", "Instruction",
            {"Nat", "Nat", "Nat", "Nat"}, 4u, false},
        {"radix-digit:write", "Instruction", {"Nat", "Nat", "Nat", "Nat"}, 4u, false},
        {"radix-digit:lookup", "Instruction",
            {"RegisterList", "RegisterList", "Table", "Nat"}, 4u, false},
        {"radix-digit:branch-lt", "Instruction", {"Nat", "Nat", "Nat", "Nat"}, 4u, false},
        {"radix-digit:branch-eq", "Instruction", {"Nat", "Nat", "Nat", "Nat"}, 4u, false},
        {"radix-digit:jump", "Instruction", {"Nat"}, 1u, false},
        {"radix-digit:return-buffer", "Instruction", {"Nat"}, 1u, false},
        {"radix-digit:fail-language", "Instruction", {"Fault"}, 1u, false},
        {"radix-digit:fail-engine", "Instruction", {"Fault"}, 1u, false},
        {"radix-digit:fail-resource", "Instruction", {"Fault"}, 1u, false},
        {"radix-digit:result-next", "StepResult",
            {"Buffers", "Registers", "Nat", "Receipt"}, 4u, false},
        {"radix-digit:result-value", "StepResult",
            {"DigitBuffer", "Receipt"}, 2u, false},
        {"radix-digit:result-language-fault", "StepResult",
            {"Fault", "Receipt"}, 2u, false},
        {"radix-digit:result-engine-fault", "StepResult",
            {"Fault", "Receipt"}, 2u, false},
        {"radix-digit:result-resource-fault", "StepResult",
            {"Fault", "Receipt"}, 2u, false},
        {"radix-digit:outcome-value", "Outcome", {"DigitBuffer"}, 1u, false},
        {"radix-digit:outcome-language-fault", "Outcome", {"Fault"}, 1u, false},
        {"radix-digit:outcome-engine-fault", "Outcome", {"Fault"}, 1u, false},
        {"radix-digit:outcome-resource-fault", "Outcome", {"Fault"}, 1u, false},
        {"radix-digit:receipt-cons", "Receipt", {"Event", "Receipt"}, 2u, false},
        {"radix-digit:execute-event", "Event", {"Nat"}, 1u, false},
        {"radix-digit:language-fault-event", "Event", {"Nat", "Fault"}, 2u, false},
        {"radix-digit:engine-fault-event", "Event", {"Nat", "Fault"}, 2u, false},
        {"radix-digit:resource-fault-event", "Event", {"Nat", "Fault"}, 2u, false}
    };
    uint32_t target_term_indices[sizeof(target_terms) / sizeof(target_terms[0])];
    uint32_t rule_len = cetta_op_lang_v1_field_len(target->rewrites_field);
    uint32_t index;
    uint32_t execute_count = 0u;
    uint32_t fetch_count = 0u;
    uint32_t consume_count = 0u;
    uint32_t missing_count = 0u;
    uint32_t fuel_count = 0u;
    bool rule_kinds[RADIX_DIGIT_TARGET_RULE_KIND_COUNT] = {false};

    if (!type_declarations_exact(target->types_field, target_types,
            (uint32_t)(sizeof(target_types) / sizeof(target_types[0]))) ||
            !grammar_specs_exact(target->terms_field, target_terms,
                (uint32_t)(sizeof(target_terms) / sizeof(target_terms[0])),
                target_term_indices) ||
            !list_is_empty(target->equations_field) || rule_len != 7u) {
        set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET,
            error_buf, error_buf_size, "target is not the supported RadixDigit operational profile");
        return false;
    }
    for (index = 0u; index < CETTA_RADIX_DIGIT_V1_INSTRUCTION_COUNT; ++index)
        profile->instruction_term_indices[index] =
            target_term_indices[3u + index];
    for (index = 0u; index < rule_len; ++index) {
        const CettaOpLangV1SExpr *entry =
            cetta_op_lang_v1_field_entry(target->rewrites_field, index);
        const CettaOpLangV1SExpr *premises;
        const CettaOpLangV1SExpr *left;
        const CettaOpLangV1SExpr *right;
        const CettaOpLangV1SExpr *left_arguments;
        StringView right_label;
        const CettaOpLangV1SExpr *right_arguments;
        uint32_t premise_len;
        uint32_t premise_index;
        RadixDigitTargetRuleKind rule_kind;

        if (!rule_view(entry, &premises, &left, &right) ||
                !pattern_app_is(left, "radix-digit:run", 6u, &left_arguments) ||
                !pattern_app(right, &right_label, &right_arguments) ||
                (!string_view_is(right_label, "radix-digit:run") &&
                    !string_view_is(right_label, "radix-digit:halted"))) {
            set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET,
                error_buf, error_buf_size, "target transition %u has an invalid boundary", index);
            return false;
        }
        rule_kind = classify_target_rule(premises, right);
        if (rule_kind == RADIX_DIGIT_TARGET_RULE_INVALID || rule_kinds[rule_kind]) {
            set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET,
                error_buf, error_buf_size,
                "target transition %u has an invalid or duplicate semantic role", index);
            return false;
        }
        if (!target_rule_wiring_exact(rule_kind, argument(entry, 1u), premises,
                left_arguments, right)) {
            set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET,
                error_buf, error_buf_size,
                "target transition %u has invalid state or evidence wiring", index);
            return false;
        }
        rule_kinds[rule_kind] = true;
        premise_len = cetta_op_lang_v1_field_len(premises);
        for (premise_index = 0u; premise_index < premise_len; ++premise_index) {
            const CettaOpLangV1SExpr *premise =
                cetta_op_lang_v1_field_entry(premises, premise_index);
            if (relation_query_is(premise, "RadixDigitExecuteInstruction"))
                ++execute_count;
            else if (relation_query_is(premise, "RadixDigitFetch"))
                ++fetch_count;
            else if (relation_query_is(premise, "RadixDigitConsumeFuel"))
                ++consume_count;
            else if (relation_query_is(premise, "RadixDigitMissingProgramCounter"))
                ++missing_count;
            else if (relation_query_is(premise, "RadixDigitFuelExhaustedFault"))
                ++fuel_count;
        }
    }
    if (execute_count != 5u || fetch_count != 5u || consume_count != 6u ||
            missing_count != 1u || fuel_count != 1u) {
        set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET,
            error_buf, error_buf_size,
            "target transition capabilities are incomplete (%u,%u,%u,%u,%u)",
            execute_count, fetch_count, consume_count, missing_count, fuel_count);
        return false;
    }
    for (index = 0u; index < RADIX_DIGIT_TARGET_RULE_KIND_COUNT; ++index) {
        if (!rule_kinds[index]) {
            set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET,
                error_buf, error_buf_size,
                "target is missing RadixDigit transition semantic role %u", index);
            return false;
        }
    }
    return cetta_radix_digit_v1_target_profile_valid(profile);
}

static const AddEntry *find_add(
    const SourceProfile *profile,
    uint32_t first,
    uint32_t second) {
    uint32_t index;
    for (index = 0u; index < profile->addition_len; ++index)
        if (profile->additions[index].first == first &&
                profile->additions[index].second == second)
            return &profile->additions[index];
    return NULL;
}

static const SuccEntry *find_succ(
    const SourceProfile *profile,
    uint32_t input) {
    uint32_t index;
    for (index = 0u; index < profile->successor_len; ++index)
        if (profile->successors[index].input == input)
            return &profile->successors[index];
    return NULL;
}

static const DigitEntry *find_multiplication(
    const SourceProfile *profile,
    uint32_t digit) {
    uint32_t index;
    for (index = 0u; index < profile->multiplication_len; ++index)
        if (profile->multiplications[index].digit == digit)
            return &profile->multiplications[index];
    return NULL;
}

static const ProductEntry *find_product(
    const SourceProfile *profile,
    uint32_t first,
    uint32_t second) {
    uint32_t index;
    for (index = 0u; index < profile->product_len; ++index)
        if (profile->products[index].first == first &&
                profile->products[index].second == second)
            return &profile->products[index];
    return NULL;
}

static bool append_origin(
    uint32_t *origins,
    uint32_t *origin_len,
    uint32_t origin) {
    if (*origin_len >= 32u)
        return false;
    origins[(*origin_len)++] = origin;
    return true;
}

static bool append_origins(
    uint32_t *origins,
    uint32_t *origin_len,
    const uint32_t *source,
    uint32_t source_len) {
    uint32_t index;
    for (index = 0u; index < source_len; ++index)
        if (!append_origin(origins, origin_len, source[index]))
            return false;
    return true;
}

static bool build_addition_table(
    CettaWaltersZantemaDaRadixDigitV1Table *table,
    const SourceProfile *profile) {
    uint32_t row_len = profile->radix * profile->radix * 2u;
    uint32_t row_index = 0u;
    uint32_t first;
    uint32_t second;
    uint32_t carry;

    table->rows = calloc(row_len, sizeof(*table->rows));
    if (!table->rows)
        return false;
    table->row_len = row_len;
    for (first = 0u; first < profile->radix; ++first) {
        for (second = 0u; second < profile->radix; ++second) {
            const AddEntry *addition = find_add(profile, first, second);
            if (!addition)
                return false;
            for (carry = 0u; carry <= 1u; ++carry) {
                CettaWaltersZantemaDaRadixDigitV1TableRow *row = &table->rows[row_index++];
                row->input_len = 3u;
                row->inputs[0] = first;
                row->inputs[1] = second;
                row->inputs[2] = carry;
                row->output_len = 2u;
                row->source_rule_indices[row->source_rule_len++] = addition->rule_index;
                if (carry == 0u) {
                    row->outputs[0] = addition->output;
                    row->outputs[1] = addition->carry ? 1u : 0u;
                } else {
                    const SuccEntry *successor = find_succ(profile, addition->output);
                    if (!successor || (addition->carry && successor->carry))
                        return false;
                    row->outputs[0] = successor->output;
                    row->outputs[1] =
                        (addition->carry || successor->carry) ? 1u : 0u;
                    row->source_rule_indices[row->source_rule_len++] = successor->rule_index;
                }
            }
        }
    }
    return true;
}

static bool add_with_table(
    const CettaWaltersZantemaDaRadixDigitV1Table *table,
    const uint32_t *left,
    uint32_t left_len,
    const uint32_t *right,
    uint32_t right_len,
    uint32_t carry,
    VectorResult *result) {
    uint32_t left_index = 0u;
    uint32_t right_index = 0u;
    uint32_t step_limit = left_len + right_len + 2u;
    uint32_t steps = 0u;

    memset(result, 0, sizeof(*result));
    while ((left_index < left_len || right_index < right_len || carry != 0u) &&
            steps++ < step_limit) {
        uint32_t inputs[3];
        const CettaWaltersZantemaDaRadixDigitV1TableRow *row;
        if (result->digit_len >= WALTERS_ZANTEMA_DA_RADIX_DIGIT_MAX_VECTOR_DIGITS)
            return false;
        inputs[0] = left_index < left_len ? left[left_index++] : 0u;
        inputs[1] = right_index < right_len ? right[right_index++] : 0u;
        inputs[2] = carry;
        row = cetta_radix_digit_v1_find_row(table, inputs, 3u, NULL);
        if (!row || row->output_len != 2u ||
                !append_origins(result->origins, &result->origin_len,
                    row->source_rule_indices, row->source_rule_len))
            return false;
        result->digits[result->digit_len++] = row->outputs[0];
        carry = row->outputs[1];
    }
    return left_index == left_len && right_index == right_len && carry == 0u;
}

static bool build_multiplication_row(
    CettaWaltersZantemaDaRadixDigitV1TableRow *row,
    const SourceProfile *profile,
    const CettaWaltersZantemaDaRadixDigitV1Table *addition_table,
    uint32_t first,
    uint32_t second,
    uint32_t accumulated,
    uint32_t carry) {
    const DigitEntry *multiplication = find_multiplication(profile, first);
    const ProductEntry *product = find_product(profile, first, second);
    VectorResult with_accumulated;
    VectorResult with_carry;
    uint32_t single[1];

    if (!multiplication || !product)
        return false;
    single[0] = accumulated;
    if (!add_with_table(addition_table,
            product->digits, product->digit_len, single, 1u, 0u,
            &with_accumulated))
        return false;
    single[0] = carry;
    if (!add_with_table(addition_table,
            with_accumulated.digits, with_accumulated.digit_len,
            single, 1u, 0u, &with_carry) ||
            with_carry.digit_len > 2u)
        return false;
    memset(row, 0, sizeof(*row));
    row->input_len = 4u;
    row->inputs[0] = first;
    row->inputs[1] = second;
    row->inputs[2] = accumulated;
    row->inputs[3] = carry;
    row->output_len = 2u;
    row->outputs[0] = with_carry.digit_len > 0u ? with_carry.digits[0] : 0u;
    row->outputs[1] = with_carry.digit_len > 1u ? with_carry.digits[1] : 0u;
    if (row->outputs[0] >= profile->radix || row->outputs[1] >= profile->radix ||
            !append_origin(row->source_rule_indices, &row->source_rule_len,
                multiplication->rule_index) ||
            !append_origin(row->source_rule_indices, &row->source_rule_len,
                product->rule_index) ||
            !append_origins(row->source_rule_indices, &row->source_rule_len,
                with_accumulated.origins, with_accumulated.origin_len) ||
            !append_origins(row->source_rule_indices, &row->source_rule_len,
                with_carry.origins, with_carry.origin_len))
        return false;
    return true;
}

static bool build_multiplication_table(
    CettaWaltersZantemaDaRadixDigitV1Table *table,
    const SourceProfile *profile,
    const CettaWaltersZantemaDaRadixDigitV1Table *addition_table) {
    uint64_t row_len64 = (uint64_t)profile->radix * profile->radix *
        profile->radix * profile->radix;
    uint32_t row_index = 0u;
    uint32_t first;
    uint32_t second;
    uint32_t accumulated;
    uint32_t carry;

    if (row_len64 > UINT32_MAX)
        return false;
    table->row_len = (uint32_t)row_len64;
    table->rows = calloc(table->row_len, sizeof(*table->rows));
    if (!table->rows)
        return false;
    for (first = 0u; first < profile->radix; ++first)
        for (second = 0u; second < profile->radix; ++second)
            for (accumulated = 0u; accumulated < profile->radix; ++accumulated)
                for (carry = 0u; carry < profile->radix; ++carry)
                    if (!build_multiplication_row(&table->rows[row_index++],
                            profile, addition_table,
                            first, second, accumulated, carry))
                        return false;
    return true;
}

static bool program_allocate(CettaRadixDigitV1Program *program, uint32_t instruction_len) {
    program->instructions = calloc(instruction_len,
        sizeof(*program->instructions));
    if (!program->instructions)
        return false;
    program->instruction_len = instruction_len;
    return true;
}

static bool program_allocate_tables(CettaRadixDigitV1Program *program, uint32_t table_len) {
    program->tables = calloc(table_len, sizeof(*program->tables));
    if (!program->tables)
        return false;
    program->table_len = table_len;
    return true;
}

static void set_instruction(
    CettaRadixDigitV1Instruction *instruction,
    CettaRadixDigitV1InstructionKind kind,
    const CettaRadixDigitV1TargetProfile *profile,
    uint32_t table_index,
    uint32_t argument_len,
    ...) {
    va_list arguments;
    uint32_t index;
    memset(instruction, 0, sizeof(*instruction));
    instruction->constructor_term_index =
        profile->instruction_term_indices[kind];
    instruction->table_index = table_index;
    instruction->argument_len = argument_len;
    va_start(arguments, argument_len);
    for (index = 0u; index < argument_len; ++index)
        instruction->arguments[index] = va_arg(arguments, uint32_t);
    va_end(arguments);
}

static bool build_addition_program(
    CettaRadixDigitV1Program *program,
    const CettaRadixDigitV1TargetProfile *profile) {
    CettaRadixDigitV1Instruction *cells;
    if (!program_allocate(program, 17u))
        return false;
    cells = program->instructions;
    set_instruction(&cells[0], CETTA_RADIX_DIGIT_V1_LENGTH, profile, 0u, 3u, 0u, 1u, 1u);
    set_instruction(&cells[1], CETTA_RADIX_DIGIT_V1_LENGTH, profile, 0u, 3u, 1u, 2u, 2u);
    set_instruction(&cells[2], CETTA_RADIX_DIGIT_V1_SET, profile, 0u, 3u, 0u, 0u, 3u);
    set_instruction(&cells[3], CETTA_RADIX_DIGIT_V1_SET, profile, 0u, 3u, 5u, 0u, 4u);
    set_instruction(&cells[4], CETTA_RADIX_DIGIT_V1_BRANCH_LT, profile, 0u, 4u, 0u, 1u, 5u, 6u);
    set_instruction(&cells[5], CETTA_RADIX_DIGIT_V1_READ_OR_ZERO, profile, 0u, 4u, 0u, 0u, 3u, 7u);
    set_instruction(&cells[6], CETTA_RADIX_DIGIT_V1_SET, profile, 0u, 3u, 3u, 0u, 7u);
    set_instruction(&cells[7], CETTA_RADIX_DIGIT_V1_BRANCH_LT, profile, 0u, 4u, 0u, 2u, 8u, 9u);
    set_instruction(&cells[8], CETTA_RADIX_DIGIT_V1_READ_OR_ZERO, profile, 0u, 4u, 1u, 0u, 4u, 10u);
    set_instruction(&cells[9], CETTA_RADIX_DIGIT_V1_SET, profile, 0u, 3u, 4u, 0u, 10u);
    set_instruction(&cells[10], CETTA_RADIX_DIGIT_V1_BRANCH_LT, profile, 0u, 4u, 0u, 1u, 13u, 11u);
    set_instruction(&cells[11], CETTA_RADIX_DIGIT_V1_BRANCH_LT, profile, 0u, 4u, 0u, 2u, 13u, 12u);
    set_instruction(&cells[12], CETTA_RADIX_DIGIT_V1_BRANCH_EQ, profile, 0u, 4u, 5u, 0u, 16u, 13u);
    set_instruction(&cells[13], CETTA_RADIX_DIGIT_V1_LOOKUP, profile, 0u, 8u, 3u, 3u, 4u, 5u, 2u, 6u, 5u, 14u);
    set_instruction(&cells[14], CETTA_RADIX_DIGIT_V1_WRITE, profile, 0u, 4u, 2u, 0u, 6u, 15u);
    set_instruction(&cells[15], CETTA_RADIX_DIGIT_V1_INCREMENT, profile, 0u, 2u, 0u, 4u);
    set_instruction(&cells[16], CETTA_RADIX_DIGIT_V1_RETURN_BUFFER, profile, 0u, 1u, 2u);
    return cetta_radix_digit_v1_program_valid(profile, program);
}

static bool build_multiplication_program(
    CettaRadixDigitV1Program *program,
    const CettaRadixDigitV1TargetProfile *profile) {
    CettaRadixDigitV1Instruction *cells;
    if (!program_allocate(program, 20u))
        return false;
    cells = program->instructions;
    set_instruction(&cells[0], CETTA_RADIX_DIGIT_V1_LENGTH, profile, 0u, 3u, 0u, 1u, 1u);
    set_instruction(&cells[1], CETTA_RADIX_DIGIT_V1_LENGTH, profile, 0u, 3u, 1u, 2u, 2u);
    set_instruction(&cells[2], CETTA_RADIX_DIGIT_V1_SET, profile, 0u, 3u, 0u, 0u, 3u);
    set_instruction(&cells[3], CETTA_RADIX_DIGIT_V1_BRANCH_LT, profile, 0u, 4u, 0u, 1u, 4u, 19u);
    set_instruction(&cells[4], CETTA_RADIX_DIGIT_V1_READ_OR_ZERO, profile, 0u, 4u, 0u, 0u, 3u, 5u);
    set_instruction(&cells[5], CETTA_RADIX_DIGIT_V1_SET, profile, 0u, 3u, 4u, 0u, 6u);
    set_instruction(&cells[6], CETTA_RADIX_DIGIT_V1_COPY, profile, 0u, 3u, 0u, 6u, 7u);
    set_instruction(&cells[7], CETTA_RADIX_DIGIT_V1_SET, profile, 0u, 3u, 8u, 0u, 8u);
    set_instruction(&cells[8], CETTA_RADIX_DIGIT_V1_BRANCH_LT, profile, 0u, 4u, 4u, 2u, 9u, 15u);
    set_instruction(&cells[9], CETTA_RADIX_DIGIT_V1_READ_OR_ZERO, profile, 0u, 4u, 1u, 4u, 5u, 10u);
    set_instruction(&cells[10], CETTA_RADIX_DIGIT_V1_READ_OR_ZERO, profile, 0u, 4u, 2u, 6u, 7u, 11u);
    set_instruction(&cells[11], CETTA_RADIX_DIGIT_V1_LOOKUP, profile, 0u, 9u, 4u, 3u, 5u, 7u, 8u, 2u, 9u, 8u, 12u);
    set_instruction(&cells[12], CETTA_RADIX_DIGIT_V1_WRITE, profile, 0u, 4u, 2u, 6u, 9u, 13u);
    set_instruction(&cells[13], CETTA_RADIX_DIGIT_V1_INCREMENT, profile, 0u, 2u, 4u, 14u);
    set_instruction(&cells[14], CETTA_RADIX_DIGIT_V1_INCREMENT, profile, 0u, 2u, 6u, 8u);
    set_instruction(&cells[15], CETTA_RADIX_DIGIT_V1_BRANCH_EQ, profile, 0u, 4u, 8u, 0u, 18u, 16u);
    set_instruction(&cells[16], CETTA_RADIX_DIGIT_V1_SET, profile, 0u, 3u, 5u, 0u, 17u);
    set_instruction(&cells[17], CETTA_RADIX_DIGIT_V1_READ_OR_ZERO, profile, 0u, 4u, 2u, 6u, 7u, 11u);
    set_instruction(&cells[18], CETTA_RADIX_DIGIT_V1_INCREMENT, profile, 0u, 2u, 0u, 3u);
    set_instruction(&cells[19], CETTA_RADIX_DIGIT_V1_RETURN_BUFFER, profile, 0u, 1u, 2u);
    return cetta_radix_digit_v1_program_valid(profile, program);
}

void cetta_walters_zantema_da_radix_digit_v1_program_init(CettaWaltersZantemaDaRadixDigitV1Program *program) {
    if (program)
        memset(program, 0, sizeof(*program));
}

void cetta_walters_zantema_da_radix_digit_v1_program_free(CettaWaltersZantemaDaRadixDigitV1Program *program) {
    if (!program)
        return;
    cetta_radix_digit_v1_program_free(&program->addition_program);
    cetta_radix_digit_v1_program_free(&program->multiplication_program);
    cetta_walters_zantema_da_radix_digit_v1_program_init(program);
}

bool cetta_walters_zantema_da_radix_digit_v1_transform(
    CettaWaltersZantemaDaRadixDigitV1Program *out,
    const CettaOperationalLanguageDefV1 *source,
    const CettaOperationalLanguageDefV1 *target,
    CettaWaltersZantemaDaRadixDigitV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    SourceProfile profile;
    CettaWaltersZantemaDaRadixDigitV1Program candidate;

    if (!out || !source || !target) {
        set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_BAD_ARGUMENT,
            error_buf, error_buf_size, "null transform argument");
        return false;
    }
    source_profile_init(&profile);
    cetta_walters_zantema_da_radix_digit_v1_program_init(&candidate);
    if (!inspect_source(&profile, source, status, error_buf, error_buf_size) ||
            !inspect_target(&candidate.target_profile,
                target, status, error_buf, error_buf_size)) {
        source_profile_free(&profile);
        cetta_walters_zantema_da_radix_digit_v1_program_free(&candidate);
        return false;
    }
    candidate.radix = profile.radix;
    if (!program_allocate_tables(&candidate.addition_program, 1u) ||
            !program_allocate_tables(&candidate.multiplication_program, 1u) ||
            !build_addition_table(&candidate.addition_program.tables[0], &profile) ||
            !build_multiplication_table(&candidate.multiplication_program.tables[0],
                &profile, &candidate.addition_program.tables[0]) ||
            !build_addition_program(&candidate.addition_program,
                &candidate.target_profile) ||
            !build_multiplication_program(&candidate.multiplication_program,
                &candidate.target_profile)) {
        set_error(status, CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_INCONSISTENT_SOURCE,
            error_buf, error_buf_size,
            "supplied rules do not derive complete bounded RadixDigit tables and graphs");
        source_profile_free(&profile);
        cetta_walters_zantema_da_radix_digit_v1_program_free(&candidate);
        return false;
    }
    source_profile_free(&profile);
    cetta_walters_zantema_da_radix_digit_v1_program_free(out);
    *out = candidate;
    if (status)
        *status = CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_OK;
    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    return true;
}

const char *cetta_walters_zantema_da_radix_digit_v1_status_name(CettaWaltersZantemaDaRadixDigitV1Status status) {
    switch (status) {
        case CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_OK: return "ok";
        case CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_BAD_ARGUMENT: return "bad_argument";
        case CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_SOURCE: return "unsupported_source";
        case CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_INCONSISTENT_SOURCE: return "inconsistent_source";
        case CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET: return "unsupported_target";
        case CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_ALLOCATION_FAILURE: return "allocation_failure";
        case CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_INTERNAL_FAILURE: return "internal_failure";
    }
    return "unknown";
}
