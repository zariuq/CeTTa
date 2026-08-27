#include "language_def_premise_free_rewriter_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CETTA_LD_PFR_V1_MAX_DEPTH = 4096u
};

typedef struct {
    const uint8_t *label;
    uint32_t label_len;
    uint32_t arity;
} CettaLdPfrV1Constructor;

typedef struct {
    const uint8_t *name;
    uint32_t name_len;
    const CettaOpLangV1SExpr **variables;
    uint32_t variable_len;
    const CettaOpLangV1SExpr *left;
    const CettaOpLangV1SExpr *right;
} CettaLdPfrV1Rule;

typedef struct {
    const CettaOpLangV1SExpr **values;
    bool *bound;
    uint32_t len;
} CettaLdPfrV1Bindings;

typedef enum {
    CETTA_LD_PFR_V1_NO_STEP = 0,
    CETTA_LD_PFR_V1_DID_STEP,
    CETTA_LD_PFR_V1_STEP_ALLOCATION_FAILURE,
    CETTA_LD_PFR_V1_STEP_DEPTH_LIMIT
} CettaLdPfrV1StepStatus;

static void pfr_set_status(CettaLdPfrV1Status *status,
                           CettaLdPfrV1Status value) {
    if (status)
        *status = value;
}

static void pfr_set_error(char *error_buf, size_t error_buf_size,
                          const char *message) {
    if (!error_buf || error_buf_size == 0u)
        return;
    if (!message)
        message = "";
    (void)snprintf(error_buf, error_buf_size, "%s", message);
}

static bool pfr_bytes_equal(const uint8_t *left, uint32_t left_len,
                            const uint8_t *right, uint32_t right_len) {
    return left_len == right_len &&
        (left_len == 0u ||
         (left && right && memcmp(left, right, left_len) == 0));
}

static bool pfr_bytes_equal_cstr(const uint8_t *left, uint32_t left_len,
                                 const char *right) {
    size_t right_len;

    if (!right)
        return false;
    right_len = strlen(right);
    return right_len <= UINT32_MAX &&
        pfr_bytes_equal(left, left_len,
                        (const uint8_t *)right, (uint32_t)right_len);
}

static bool pfr_list(const CettaOpLangV1SExpr *list, uint32_t *len) {
    const CettaOpLangV1SExpr *cursor = list;
    uint32_t count = 0u;

    while (cursor &&
           cetta_op_lang_v1_application_is(cursor, "LCons", 2u)) {
        if (count == UINT32_MAX)
            return false;
        count++;
        cursor = cursor->as.application.arguments[1];
    }
    if (!cetta_op_lang_v1_symbol_is(cursor, "LNil"))
        return false;
    if (len)
        *len = count;
    return true;
}

static const CettaOpLangV1SExpr *pfr_list_entry(
    const CettaOpLangV1SExpr *list, uint32_t index) {
    return cetta_op_lang_v1_field_entry(list, index);
}

static bool pfr_string(const CettaOpLangV1SExpr *expression,
                       const uint8_t **bytes, uint32_t *len) {
    if (!expression ||
        expression->kind != CETTA_OP_LANG_V1_SEXPR_STRING ||
        (expression->as.string.len != 0u &&
         !expression->as.string.bytes)) {
        return false;
    }
    if (bytes)
        *bytes = expression->as.string.bytes;
    if (len)
        *len = expression->as.string.len;
    return true;
}

static CettaLdPfrV1Constructor *pfr_constructors(
    const CettaLdPfrV1Program *program) {
    return program ? (CettaLdPfrV1Constructor *)program->constructors : NULL;
}

static CettaLdPfrV1Rule *pfr_rules(const CettaLdPfrV1Program *program) {
    return program ? (CettaLdPfrV1Rule *)program->rules : NULL;
}

static void pfr_rule_free(CettaLdPfrV1Rule *rule) {
    if (!rule)
        return;
    free(rule->variables);
    memset(rule, 0, sizeof(*rule));
}

void cetta_ld_pfr_v1_program_init(CettaLdPfrV1Program *program) {
    if (program)
        memset(program, 0, sizeof(*program));
}

void cetta_ld_pfr_v1_program_free(CettaLdPfrV1Program *program) {
    CettaLdPfrV1Rule *rules;
    uint32_t index;

    if (!program)
        return;
    rules = pfr_rules(program);
    for (index = 0u; index < program->rule_len; index++)
        pfr_rule_free(&rules[index]);
    free(rules);
    free(program->constructors);
    memset(program, 0, sizeof(*program));
}

static int pfr_constructor_index(const CettaLdPfrV1Program *program,
                                 const uint8_t *label,
                                 uint32_t label_len) {
    CettaLdPfrV1Constructor *constructors = pfr_constructors(program);
    uint32_t index;

    for (index = 0u; index < program->constructor_len; index++) {
        if (pfr_bytes_equal(constructors[index].label,
                            constructors[index].label_len,
                            label, label_len)) {
            return (int)index;
        }
    }
    return -1;
}

static int pfr_rule_variable_index(const CettaLdPfrV1Rule *rule,
                                   const uint8_t *name,
                                   uint32_t name_len) {
    uint32_t index;

    if (!rule)
        return -1;
    for (index = 0u; index < rule->variable_len; index++) {
        const CettaOpLangV1SExpr *variable = rule->variables[index];
        if (variable &&
            variable->kind == CETTA_OP_LANG_V1_SEXPR_STRING &&
            pfr_bytes_equal(variable->as.string.bytes,
                            variable->as.string.len,
                            name, name_len)) {
            return (int)index;
        }
    }
    return -1;
}

static bool pfr_validate_term_parameter(
    const CettaOpLangV1SExpr *parameter) {
    const CettaOpLangV1SExpr *name;
    const CettaOpLangV1SExpr *type;

    if (!cetta_op_lang_v1_application_is(parameter, "TermSimple", 2u))
        return false;
    name = parameter->as.application.arguments[0];
    type = parameter->as.application.arguments[1];
    return pfr_string(name, NULL, NULL) &&
        cetta_op_lang_v1_application_is(type, "TBase", 1u) &&
        pfr_string(type->as.application.arguments[0], NULL, NULL);
}

static bool pfr_compile_constructors(
    CettaLdPfrV1Program *candidate,
    const CettaOperationalLanguageDefV1 *language,
    CettaLdPfrV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaLdPfrV1Constructor *constructors;
    uint32_t term_len;
    uint32_t index;

    if (!pfr_list(language->terms_field, &term_len)) {
        pfr_set_status(status, CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
        pfr_set_error(error_buf, error_buf_size,
                      "constructor field is not a proper list");
        return false;
    }
    constructors = term_len
        ? calloc(term_len, sizeof(*constructors)) : NULL;
    if (term_len && !constructors) {
        pfr_set_status(status, CETTA_LD_PFR_V1_ALLOCATION_FAILURE);
        pfr_set_error(error_buf, error_buf_size,
                      "constructor table allocation failed");
        return false;
    }
    candidate->constructors = constructors;
    candidate->constructor_len = term_len;

    for (index = 0u; index < term_len; index++) {
        const CettaOpLangV1SExpr *entry =
            pfr_list_entry(language->terms_field, index);
        const CettaOpLangV1SExpr *parameters;
        uint32_t parameter_len;
        uint32_t parameter_index;

        if (!cetta_op_lang_v1_application_is(entry, "GrammarRule", 5u) ||
            !pfr_string(entry->as.application.arguments[0],
                        &constructors[index].label,
                        &constructors[index].label_len) ||
            !pfr_string(entry->as.application.arguments[1], NULL, NULL)) {
            pfr_set_status(status,
                           CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
            pfr_set_error(error_buf, error_buf_size,
                          "malformed GrammarRule constructor declaration");
            return false;
        }
        if (pfr_constructor_index(candidate, constructors[index].label,
                                  constructors[index].label_len) !=
                (int)index) {
            pfr_set_status(status,
                           CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
            pfr_set_error(error_buf, error_buf_size,
                          "duplicate constructor label");
            return false;
        }
        parameters = entry->as.application.arguments[2];
        if (!pfr_list(parameters, &parameter_len)) {
            pfr_set_status(status,
                           CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
            pfr_set_error(error_buf, error_buf_size,
                          "constructor parameter field is not a proper list");
            return false;
        }
        constructors[index].arity = parameter_len;
        for (parameter_index = 0u;
             parameter_index < parameter_len;
             parameter_index++) {
            if (!pfr_validate_term_parameter(
                    pfr_list_entry(parameters, parameter_index))) {
                pfr_set_status(status,
                               CETTA_LD_PFR_V1_UNSUPPORTED_PRESENTATION);
                pfr_set_error(
                    error_buf, error_buf_size,
                    "premise-free profile supports only TermSimple parameters");
                return false;
            }
        }
    }
    return true;
}

static bool pfr_compile_rule_variables(CettaLdPfrV1Rule *rule,
                                       const CettaOpLangV1SExpr *context,
                                       CettaLdPfrV1Status *status,
                                       char *error_buf,
                                       size_t error_buf_size) {
    uint32_t variable_len;
    uint32_t index;
    uint32_t prior;

    if (!pfr_list(context, &variable_len)) {
        pfr_set_status(status, CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
        pfr_set_error(error_buf, error_buf_size,
                      "rewrite type context is not a proper list");
        return false;
    }
    rule->variables = variable_len
        ? calloc(variable_len, sizeof(*rule->variables)) : NULL;
    if (variable_len && !rule->variables) {
        pfr_set_status(status, CETTA_LD_PFR_V1_ALLOCATION_FAILURE);
        pfr_set_error(error_buf, error_buf_size,
                      "rewrite variable table allocation failed");
        return false;
    }
    rule->variable_len = variable_len;
    for (index = 0u; index < variable_len; index++) {
        const CettaOpLangV1SExpr *binding =
            pfr_list_entry(context, index);
        const CettaOpLangV1SExpr *name;
        const CettaOpLangV1SExpr *type;

        if (!cetta_op_lang_v1_application_is(binding, "TypeBinding", 2u)) {
            pfr_set_status(status,
                           CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
            pfr_set_error(error_buf, error_buf_size,
                          "malformed TypeBinding in rewrite context");
            return false;
        }
        name = binding->as.application.arguments[0];
        type = binding->as.application.arguments[1];
        if (!pfr_string(name, NULL, NULL) ||
            !cetta_op_lang_v1_application_is(type, "TBase", 1u) ||
            !pfr_string(type->as.application.arguments[0], NULL, NULL)) {
            pfr_set_status(status,
                           CETTA_LD_PFR_V1_UNSUPPORTED_PRESENTATION);
            pfr_set_error(
                error_buf, error_buf_size,
                "premise-free profile supports only named TBase variables");
            return false;
        }
        for (prior = 0u; prior < index; prior++) {
            if (cetta_op_lang_v1_string_is(
                    name,
                    rule->variables[prior]->as.string.bytes,
                    rule->variables[prior]->as.string.len)) {
                pfr_set_status(status,
                               CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
                pfr_set_error(error_buf, error_buf_size,
                              "duplicate rewrite variable");
                return false;
            }
        }
        rule->variables[index] = name;
    }
    return true;
}

static bool pfr_validate_pattern(
    const CettaLdPfrV1Program *program,
    const CettaLdPfrV1Rule *rule,
    const CettaOpLangV1SExpr *pattern,
    bool *seen,
    bool require_seen,
    uint32_t depth,
    CettaLdPfrV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    const CettaOpLangV1SExpr *label;
    const CettaOpLangV1SExpr *arguments;
    const uint8_t *label_bytes;
    uint32_t label_len;
    uint32_t argument_len;
    uint32_t index;
    int constructor_index;

    if (depth > CETTA_LD_PFR_V1_MAX_DEPTH) {
        pfr_set_status(status, CETTA_LD_PFR_V1_DEPTH_LIMIT);
        pfr_set_error(error_buf, error_buf_size,
                      "rewrite pattern exceeds profile depth limit");
        return false;
    }
    if (cetta_op_lang_v1_application_is(pattern, "FVar", 1u)) {
        const CettaOpLangV1SExpr *name =
            pattern->as.application.arguments[0];
        int variable_index;

        if (!pfr_string(name, &label_bytes, &label_len)) {
            pfr_set_status(status,
                           CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
            pfr_set_error(error_buf, error_buf_size,
                          "FVar must carry one string name");
            return false;
        }
        variable_index = pfr_rule_variable_index(
            rule, label_bytes, label_len);
        if (variable_index < 0 ||
            (require_seen && !seen[variable_index])) {
            pfr_set_status(status,
                           CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
            pfr_set_error(error_buf, error_buf_size,
                          require_seen
                              ? "rewrite result contains an unbound variable"
                              : "pattern variable is absent from its type context");
            return false;
        }
        seen[variable_index] = true;
        return true;
    }
    if (!cetta_op_lang_v1_application_is(pattern, "PApp", 2u)) {
        pfr_set_status(status, CETTA_LD_PFR_V1_UNSUPPORTED_PRESENTATION);
        pfr_set_error(
            error_buf, error_buf_size,
            "premise-free profile supports only PApp and FVar patterns");
        return false;
    }
    label = pattern->as.application.arguments[0];
    arguments = pattern->as.application.arguments[1];
    if (!pfr_string(label, &label_bytes, &label_len) ||
        !pfr_list(arguments, &argument_len)) {
        pfr_set_status(status, CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
        pfr_set_error(error_buf, error_buf_size,
                      "malformed PApp constructor pattern");
        return false;
    }
    constructor_index = pfr_constructor_index(
        program, label_bytes, label_len);
    if (constructor_index < 0 ||
        pfr_constructors(program)[constructor_index].arity != argument_len) {
        pfr_set_status(status, CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
        pfr_set_error(error_buf, error_buf_size,
                      "pattern uses an undeclared constructor or wrong arity");
        return false;
    }
    for (index = 0u; index < argument_len; index++) {
        if (!pfr_validate_pattern(
                program, rule, pfr_list_entry(arguments, index), seen,
                require_seen, depth + 1u, status,
                error_buf, error_buf_size)) {
            return false;
        }
    }
    return true;
}

static bool pfr_compile_rules(
    CettaLdPfrV1Program *candidate,
    const CettaOperationalLanguageDefV1 *language,
    CettaLdPfrV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaLdPfrV1Rule *rules;
    uint32_t rule_len;
    uint32_t index;

    if (!cetta_op_lang_v1_symbol_is(language->equations_field, "LNil")) {
        pfr_set_status(status, CETTA_LD_PFR_V1_UNSUPPORTED_PRESENTATION);
        pfr_set_error(error_buf, error_buf_size,
                      "premise-free rewrite profile does not execute equations");
        return false;
    }
    if (!pfr_list(language->rewrites_field, &rule_len)) {
        pfr_set_status(status, CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
        pfr_set_error(error_buf, error_buf_size,
                      "rewrite field is not a proper list");
        return false;
    }
    rules = rule_len ? calloc(rule_len, sizeof(*rules)) : NULL;
    if (rule_len && !rules) {
        pfr_set_status(status, CETTA_LD_PFR_V1_ALLOCATION_FAILURE);
        pfr_set_error(error_buf, error_buf_size,
                      "rewrite table allocation failed");
        return false;
    }
    candidate->rules = rules;
    candidate->rule_len = rule_len;

    for (index = 0u; index < rule_len; index++) {
        const CettaOpLangV1SExpr *entry =
            pfr_list_entry(language->rewrites_field, index);
        const CettaOpLangV1SExpr *name;
        bool *seen;
        uint32_t prior;

        if (!cetta_op_lang_v1_application_is(entry, "RewriteRule", 5u)) {
            pfr_set_status(status,
                           CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
            pfr_set_error(error_buf, error_buf_size,
                          "malformed RewriteRule declaration");
            return false;
        }
        name = entry->as.application.arguments[0];
        if (!pfr_string(name, &rules[index].name,
                        &rules[index].name_len)) {
            pfr_set_status(status,
                           CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
            pfr_set_error(error_buf, error_buf_size,
                          "rewrite rule name must be a string");
            return false;
        }
        for (prior = 0u; prior < index; prior++) {
            if (pfr_bytes_equal(rules[prior].name, rules[prior].name_len,
                                rules[index].name, rules[index].name_len)) {
                pfr_set_status(status,
                               CETTA_LD_PFR_V1_MALFORMED_PRESENTATION);
                pfr_set_error(error_buf, error_buf_size,
                              "duplicate rewrite rule name");
                return false;
            }
        }
        if (!pfr_compile_rule_variables(
                &rules[index], entry->as.application.arguments[1],
                status, error_buf, error_buf_size)) {
            return false;
        }
        if (!cetta_op_lang_v1_symbol_is(
                entry->as.application.arguments[2], "LNil")) {
            pfr_set_status(status,
                           CETTA_LD_PFR_V1_UNSUPPORTED_PRESENTATION);
            pfr_set_error(error_buf, error_buf_size,
                          "rewrite premises are outside the premise-free profile");
            return false;
        }
        rules[index].left = entry->as.application.arguments[3];
        rules[index].right = entry->as.application.arguments[4];
        seen = rules[index].variable_len
            ? calloc(rules[index].variable_len, sizeof(*seen)) : NULL;
        if (rules[index].variable_len && !seen) {
            pfr_set_status(status, CETTA_LD_PFR_V1_ALLOCATION_FAILURE);
            pfr_set_error(error_buf, error_buf_size,
                          "rewrite validation allocation failed");
            return false;
        }
        if (!pfr_validate_pattern(
                candidate, &rules[index], rules[index].left,
                seen, false, 0u, status, error_buf, error_buf_size) ||
            !pfr_validate_pattern(
                candidate, &rules[index], rules[index].right,
                seen, true, 0u, status, error_buf, error_buf_size)) {
            free(seen);
            return false;
        }
        free(seen);
    }
    return true;
}

bool cetta_ld_pfr_v1_compile(
    CettaLdPfrV1Program *out,
    const CettaOperationalLanguageDefV1 *language,
    CettaLdPfrV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaLdPfrV1Program candidate;

    pfr_set_status(status, CETTA_LD_PFR_V1_OK);
    pfr_set_error(error_buf, error_buf_size, "");
    if (!out || !language || !language->root) {
        pfr_set_status(status, CETTA_LD_PFR_V1_BAD_ARGUMENT);
        pfr_set_error(error_buf, error_buf_size,
                      "bad premise-free compiler argument");
        return false;
    }
    cetta_ld_pfr_v1_program_init(&candidate);
    if (!pfr_compile_constructors(
            &candidate, language, status, error_buf, error_buf_size) ||
        !pfr_compile_rules(
            &candidate, language, status, error_buf, error_buf_size)) {
        cetta_ld_pfr_v1_program_free(&candidate);
        return false;
    }
    cetta_ld_pfr_v1_program_free(out);
    *out = candidate;
    pfr_set_status(status, CETTA_LD_PFR_V1_OK);
    return true;
}

static void pfr_sexpr_free(CettaOpLangV1SExpr *expression) {
    uint32_t index;

    if (!expression)
        return;
    switch (expression->kind) {
    case CETTA_OP_LANG_V1_SEXPR_SYMBOL:
        free(expression->as.symbol);
        break;
    case CETTA_OP_LANG_V1_SEXPR_STRING:
        free(expression->as.string.bytes);
        break;
    case CETTA_OP_LANG_V1_SEXPR_NATURAL:
        free(expression->as.natural);
        break;
    case CETTA_OP_LANG_V1_SEXPR_APPLICATION:
        free(expression->as.application.head);
        for (index = 0u;
             index < expression->as.application.argument_len;
             index++) {
            pfr_sexpr_free(expression->as.application.arguments[index]);
        }
        free(expression->as.application.arguments);
        break;
    }
    free(expression);
}

static char *pfr_dup_cstr(const char *text) {
    size_t len;
    char *copy;

    if (!text)
        return NULL;
    len = strlen(text);
    copy = malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

static char *pfr_dup_bytes_cstr(const uint8_t *bytes, uint32_t len) {
    char *copy;

    if (len && !bytes)
        return NULL;
    copy = malloc((size_t)len + 1u);
    if (!copy)
        return NULL;
    if (len)
        memcpy(copy, bytes, len);
    copy[len] = '\0';
    return copy;
}

static CettaOpLangV1SExpr *pfr_node_alloc(CettaOpLangV1SExprKind kind) {
    CettaOpLangV1SExpr *node = calloc(1u, sizeof(*node));

    if (node)
        node->kind = kind;
    return node;
}

static CettaOpLangV1SExpr *pfr_clone(
    const CettaOpLangV1SExpr *expression,
    uint32_t depth) {
    CettaOpLangV1SExpr *copy;
    uint32_t index;

    if (!expression || depth > CETTA_LD_PFR_V1_MAX_DEPTH)
        return NULL;
    copy = pfr_node_alloc(expression->kind);
    if (!copy)
        return NULL;
    switch (expression->kind) {
    case CETTA_OP_LANG_V1_SEXPR_SYMBOL:
        copy->as.symbol = pfr_dup_cstr(expression->as.symbol);
        if (!copy->as.symbol)
            goto fail;
        break;
    case CETTA_OP_LANG_V1_SEXPR_STRING:
        if (expression->as.string.len) {
            copy->as.string.bytes =
                malloc(expression->as.string.len);
            if (!copy->as.string.bytes)
                goto fail;
            memcpy(copy->as.string.bytes,
                   expression->as.string.bytes,
                   expression->as.string.len);
        }
        copy->as.string.len = expression->as.string.len;
        break;
    case CETTA_OP_LANG_V1_SEXPR_NATURAL:
        copy->as.natural = pfr_dup_cstr(expression->as.natural);
        if (!copy->as.natural)
            goto fail;
        break;
    case CETTA_OP_LANG_V1_SEXPR_APPLICATION:
        copy->as.application.head =
            pfr_dup_cstr(expression->as.application.head);
        if (!copy->as.application.head)
            goto fail;
        copy->as.application.argument_len =
            expression->as.application.argument_len;
        if (copy->as.application.argument_len) {
            copy->as.application.arguments = calloc(
                copy->as.application.argument_len,
                sizeof(*copy->as.application.arguments));
            if (!copy->as.application.arguments)
                goto fail;
        }
        for (index = 0u;
             index < copy->as.application.argument_len;
             index++) {
            copy->as.application.arguments[index] = pfr_clone(
                expression->as.application.arguments[index], depth + 1u);
            if (!copy->as.application.arguments[index])
                goto fail;
        }
        break;
    }
    return copy;

fail:
    pfr_sexpr_free(copy);
    return NULL;
}

bool cetta_ld_pfr_v1_term_equal(
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right) {
    uint32_t index;

    if (left == right)
        return true;
    if (!left || !right || left->kind != right->kind)
        return false;
    switch (left->kind) {
    case CETTA_OP_LANG_V1_SEXPR_SYMBOL:
        return left->as.symbol && right->as.symbol &&
            strcmp(left->as.symbol, right->as.symbol) == 0;
    case CETTA_OP_LANG_V1_SEXPR_STRING:
        return pfr_bytes_equal(left->as.string.bytes,
                               left->as.string.len,
                               right->as.string.bytes,
                               right->as.string.len);
    case CETTA_OP_LANG_V1_SEXPR_NATURAL:
        return left->as.natural && right->as.natural &&
            strcmp(left->as.natural, right->as.natural) == 0;
    case CETTA_OP_LANG_V1_SEXPR_APPLICATION:
        if (!left->as.application.head ||
            !right->as.application.head ||
            strcmp(left->as.application.head,
                   right->as.application.head) != 0 ||
            left->as.application.argument_len !=
                right->as.application.argument_len) {
            return false;
        }
        for (index = 0u;
             index < left->as.application.argument_len;
             index++) {
            if (!cetta_ld_pfr_v1_term_equal(
                    left->as.application.arguments[index],
                    right->as.application.arguments[index])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool pfr_validate_closed_term(
    const CettaLdPfrV1Program *program,
    const CettaOpLangV1SExpr *term,
    uint32_t depth,
    CettaLdPfrV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    int constructor_index;
    uint32_t index;

    if (depth > CETTA_LD_PFR_V1_MAX_DEPTH) {
        pfr_set_status(status, CETTA_LD_PFR_V1_DEPTH_LIMIT);
        pfr_set_error(error_buf, error_buf_size,
                      "input term exceeds profile depth limit");
        return false;
    }
    if (!term ||
        term->kind != CETTA_OP_LANG_V1_SEXPR_APPLICATION ||
        !term->as.application.head) {
        pfr_set_status(status, CETTA_LD_PFR_V1_MALFORMED_TERM);
        pfr_set_error(error_buf, error_buf_size,
                      "input must be a closed constructor application");
        return false;
    }
    constructor_index = pfr_constructor_index(
        program, (const uint8_t *)term->as.application.head,
        (uint32_t)strlen(term->as.application.head));
    if (constructor_index < 0 ||
        pfr_constructors(program)[constructor_index].arity !=
            term->as.application.argument_len) {
        pfr_set_status(status, CETTA_LD_PFR_V1_MALFORMED_TERM);
        pfr_set_error(error_buf, error_buf_size,
                      "input uses an undeclared constructor or wrong arity");
        return false;
    }
    for (index = 0u;
         index < term->as.application.argument_len;
         index++) {
        if (!pfr_validate_closed_term(
                program, term->as.application.arguments[index], depth + 1u,
                status, error_buf, error_buf_size)) {
            return false;
        }
    }
    return true;
}

static bool pfr_bindings_init(CettaLdPfrV1Bindings *bindings,
                              uint32_t len) {
    if (!bindings)
        return false;
    memset(bindings, 0, sizeof(*bindings));
    bindings->values = len ? calloc(len, sizeof(*bindings->values)) : NULL;
    bindings->bound = len ? calloc(len, sizeof(*bindings->bound)) : NULL;
    if (len && (!bindings->values || !bindings->bound)) {
        free(bindings->values);
        free(bindings->bound);
        memset(bindings, 0, sizeof(*bindings));
        return false;
    }
    bindings->len = len;
    return true;
}

static void pfr_bindings_free(CettaLdPfrV1Bindings *bindings) {
    if (!bindings)
        return;
    free(bindings->values);
    free(bindings->bound);
    memset(bindings, 0, sizeof(*bindings));
}

static bool pfr_match(const CettaLdPfrV1Rule *rule,
                      const CettaOpLangV1SExpr *pattern,
                      const CettaOpLangV1SExpr *term,
                      CettaLdPfrV1Bindings *bindings,
                      uint32_t depth) {
    const CettaOpLangV1SExpr *label;
    const CettaOpLangV1SExpr *arguments;
    uint32_t argument_len;
    uint32_t index;

    if (!rule || !pattern || !term || !bindings ||
        depth > CETTA_LD_PFR_V1_MAX_DEPTH) {
        return false;
    }
    if (cetta_op_lang_v1_application_is(pattern, "FVar", 1u)) {
        const CettaOpLangV1SExpr *name =
            pattern->as.application.arguments[0];
        int variable_index = pfr_rule_variable_index(
            rule, name->as.string.bytes, name->as.string.len);

        if (variable_index < 0)
            return false;
        if (bindings->bound[variable_index]) {
            return cetta_ld_pfr_v1_term_equal(
                bindings->values[variable_index], term);
        }
        bindings->values[variable_index] = term;
        bindings->bound[variable_index] = true;
        return true;
    }
    if (!cetta_op_lang_v1_application_is(pattern, "PApp", 2u) ||
        term->kind != CETTA_OP_LANG_V1_SEXPR_APPLICATION ||
        !term->as.application.head) {
        return false;
    }
    label = pattern->as.application.arguments[0];
    arguments = pattern->as.application.arguments[1];
    if (!pfr_string(label, NULL, NULL) ||
        !pfr_list(arguments, &argument_len) ||
        !pfr_bytes_equal_cstr(label->as.string.bytes,
                              label->as.string.len,
                              term->as.application.head) ||
        argument_len != term->as.application.argument_len) {
        return false;
    }
    for (index = 0u; index < argument_len; index++) {
        if (!pfr_match(rule, pfr_list_entry(arguments, index),
                       term->as.application.arguments[index],
                       bindings, depth + 1u)) {
            return false;
        }
    }
    return true;
}

static CettaOpLangV1SExpr *pfr_instantiate(
    const CettaLdPfrV1Rule *rule,
    const CettaOpLangV1SExpr *pattern,
    const CettaLdPfrV1Bindings *bindings,
    uint32_t depth) {
    CettaOpLangV1SExpr *result;
    const CettaOpLangV1SExpr *label;
    const CettaOpLangV1SExpr *arguments;
    uint32_t argument_len;
    uint32_t index;

    if (!rule || !pattern || !bindings ||
        depth > CETTA_LD_PFR_V1_MAX_DEPTH) {
        return NULL;
    }
    if (cetta_op_lang_v1_application_is(pattern, "FVar", 1u)) {
        const CettaOpLangV1SExpr *name =
            pattern->as.application.arguments[0];
        int variable_index = pfr_rule_variable_index(
            rule, name->as.string.bytes, name->as.string.len);

        if (variable_index < 0 || !bindings->bound[variable_index])
            return NULL;
        return pfr_clone(bindings->values[variable_index], depth + 1u);
    }
    if (!cetta_op_lang_v1_application_is(pattern, "PApp", 2u))
        return NULL;
    label = pattern->as.application.arguments[0];
    arguments = pattern->as.application.arguments[1];
    if (!pfr_string(label, NULL, NULL) ||
        !pfr_list(arguments, &argument_len)) {
        return NULL;
    }
    result = pfr_node_alloc(CETTA_OP_LANG_V1_SEXPR_APPLICATION);
    if (!result)
        return NULL;
    result->as.application.head = pfr_dup_bytes_cstr(
        label->as.string.bytes, label->as.string.len);
    if (!result->as.application.head)
        goto fail;
    result->as.application.argument_len = argument_len;
    if (argument_len) {
        result->as.application.arguments = calloc(
            argument_len, sizeof(*result->as.application.arguments));
        if (!result->as.application.arguments)
            goto fail;
    }
    for (index = 0u; index < argument_len; index++) {
        result->as.application.arguments[index] = pfr_instantiate(
            rule, pfr_list_entry(arguments, index), bindings, depth + 1u);
        if (!result->as.application.arguments[index])
            goto fail;
    }
    return result;

fail:
    pfr_sexpr_free(result);
    return NULL;
}

static CettaLdPfrV1StepStatus pfr_one_step(
    const CettaLdPfrV1Program *program,
    const CettaOpLangV1SExpr *term,
    uint32_t depth,
    CettaOpLangV1SExpr **next,
    uint32_t *rule_index) {
    CettaLdPfrV1Rule *rules = pfr_rules(program);
    uint32_t index;

    if (depth > CETTA_LD_PFR_V1_MAX_DEPTH)
        return CETTA_LD_PFR_V1_STEP_DEPTH_LIMIT;
    for (index = 0u; index < program->rule_len; index++) {
        CettaLdPfrV1Bindings bindings;
        bool matched;

        if (!pfr_bindings_init(&bindings, rules[index].variable_len))
            return CETTA_LD_PFR_V1_STEP_ALLOCATION_FAILURE;
        matched = pfr_match(&rules[index], rules[index].left,
                            term, &bindings, depth);
        if (matched) {
            *next = pfr_instantiate(
                &rules[index], rules[index].right, &bindings, depth);
            pfr_bindings_free(&bindings);
            if (!*next)
                return CETTA_LD_PFR_V1_STEP_ALLOCATION_FAILURE;
            *rule_index = index;
            return CETTA_LD_PFR_V1_DID_STEP;
        }
        pfr_bindings_free(&bindings);
    }
    if (term->kind == CETTA_OP_LANG_V1_SEXPR_APPLICATION) {
        for (index = 0u;
             index < term->as.application.argument_len;
             index++) {
            CettaOpLangV1SExpr *child_next = NULL;
            CettaLdPfrV1StepStatus child_status = pfr_one_step(
                program, term->as.application.arguments[index], depth + 1u,
                &child_next, rule_index);

            if (child_status == CETTA_LD_PFR_V1_DID_STEP) {
                CettaOpLangV1SExpr *rebuilt = pfr_clone(term, depth);
                if (!rebuilt) {
                    pfr_sexpr_free(child_next);
                    return CETTA_LD_PFR_V1_STEP_ALLOCATION_FAILURE;
                }
                pfr_sexpr_free(rebuilt->as.application.arguments[index]);
                rebuilt->as.application.arguments[index] = child_next;
                *next = rebuilt;
                return CETTA_LD_PFR_V1_DID_STEP;
            }
            if (child_status != CETTA_LD_PFR_V1_NO_STEP)
                return child_status;
        }
    }
    return CETTA_LD_PFR_V1_NO_STEP;
}

void cetta_ld_pfr_v1_result_init(CettaLdPfrV1Result *result) {
    if (result)
        memset(result, 0, sizeof(*result));
}

void cetta_ld_pfr_v1_result_free(CettaLdPfrV1Result *result) {
    if (!result)
        return;
    pfr_sexpr_free(result->normal_form);
    free(result->rule_indices);
    memset(result, 0, sizeof(*result));
}

static bool pfr_trace_push(CettaLdPfrV1Result *result,
                           uint32_t *capacity,
                           uint32_t rule_index) {
    uint32_t next_capacity;
    uint32_t *next;

    if (result->rule_len == *capacity) {
        next_capacity = *capacity ? *capacity * 2u : 16u;
        if (next_capacity < *capacity)
            return false;
        next = realloc(result->rule_indices,
                       sizeof(*result->rule_indices) * next_capacity);
        if (!next)
            return false;
        result->rule_indices = next;
        *capacity = next_capacity;
    }
    result->rule_indices[result->rule_len++] = rule_index;
    return true;
}

bool cetta_ld_pfr_v1_normalize(
    const CettaLdPfrV1Program *program,
    const CettaOpLangV1SExpr *term,
    uint32_t step_limit,
    CettaLdPfrV1Result *out,
    CettaLdPfrV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaLdPfrV1Result candidate;
    uint32_t trace_capacity = 0u;

    pfr_set_status(status, CETTA_LD_PFR_V1_OK);
    pfr_set_error(error_buf, error_buf_size, "");
    if (!program || !out || !term ||
        (program->constructor_len && !program->constructors) ||
        (program->rule_len && !program->rules)) {
        pfr_set_status(status, CETTA_LD_PFR_V1_BAD_ARGUMENT);
        pfr_set_error(error_buf, error_buf_size,
                      "bad premise-free normalization argument");
        return false;
    }
    if (!pfr_validate_closed_term(
            program, term, 0u, status, error_buf, error_buf_size)) {
        return false;
    }
    cetta_ld_pfr_v1_result_init(&candidate);
    candidate.normal_form = pfr_clone(term, 0u);
    if (!candidate.normal_form) {
        pfr_set_status(status, CETTA_LD_PFR_V1_ALLOCATION_FAILURE);
        pfr_set_error(error_buf, error_buf_size,
                      "normalization input copy failed");
        return false;
    }

    for (;;) {
        CettaOpLangV1SExpr *next = NULL;
        uint32_t rule_index = 0u;
        CettaLdPfrV1StepStatus step_status = pfr_one_step(
            program, candidate.normal_form, 0u, &next, &rule_index);

        if (step_status == CETTA_LD_PFR_V1_NO_STEP)
            break;
        if (step_status == CETTA_LD_PFR_V1_STEP_DEPTH_LIMIT) {
            pfr_set_status(status, CETTA_LD_PFR_V1_DEPTH_LIMIT);
            pfr_set_error(error_buf, error_buf_size,
                          "contextual traversal exceeded profile depth limit");
            goto fail;
        }
        if (step_status == CETTA_LD_PFR_V1_STEP_ALLOCATION_FAILURE) {
            pfr_set_status(status, CETTA_LD_PFR_V1_ALLOCATION_FAILURE);
            pfr_set_error(error_buf, error_buf_size,
                          "contextual rewrite allocation failed");
            goto fail;
        }
        if (candidate.rule_len == step_limit) {
            pfr_sexpr_free(next);
            pfr_set_status(status, CETTA_LD_PFR_V1_STEP_LIMIT);
            pfr_set_error(error_buf, error_buf_size,
                          "normalization requires more rewrite steps");
            goto fail;
        }
        if (!pfr_trace_push(&candidate, &trace_capacity, rule_index)) {
            pfr_sexpr_free(next);
            pfr_set_status(status, CETTA_LD_PFR_V1_ALLOCATION_FAILURE);
            pfr_set_error(error_buf, error_buf_size,
                          "rewrite receipt allocation failed");
            goto fail;
        }
        pfr_sexpr_free(candidate.normal_form);
        candidate.normal_form = next;
    }

    cetta_ld_pfr_v1_result_free(out);
    *out = candidate;
    pfr_set_status(status, CETTA_LD_PFR_V1_OK);
    return true;

fail:
    cetta_ld_pfr_v1_result_free(&candidate);
    return false;
}

bool cetta_ld_pfr_v1_rule_name(
    const CettaLdPfrV1Program *program,
    uint32_t rule_index,
    const uint8_t **name_bytes,
    uint32_t *name_len) {
    CettaLdPfrV1Rule *rules = pfr_rules(program);

    if (!program || !rules || rule_index >= program->rule_len)
        return false;
    if (name_bytes)
        *name_bytes = rules[rule_index].name;
    if (name_len)
        *name_len = rules[rule_index].name_len;
    return true;
}

const char *cetta_ld_pfr_v1_status_name(CettaLdPfrV1Status status) {
    switch (status) {
    case CETTA_LD_PFR_V1_OK:
        return "ok";
    case CETTA_LD_PFR_V1_BAD_ARGUMENT:
        return "bad-argument";
    case CETTA_LD_PFR_V1_MALFORMED_PRESENTATION:
        return "malformed-presentation";
    case CETTA_LD_PFR_V1_UNSUPPORTED_PRESENTATION:
        return "unsupported-presentation";
    case CETTA_LD_PFR_V1_MALFORMED_TERM:
        return "malformed-term";
    case CETTA_LD_PFR_V1_STEP_LIMIT:
        return "step-limit";
    case CETTA_LD_PFR_V1_DEPTH_LIMIT:
        return "depth-limit";
    case CETTA_LD_PFR_V1_ALLOCATION_FAILURE:
        return "allocation-failure";
    case CETTA_LD_PFR_V1_INTERNAL_FAILURE:
        return "internal-failure";
    }
    return "unknown";
}
