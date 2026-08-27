#include "external_call_to_structured_c_transform_v1.h"
#include "structured_c_profile_v1.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const generated_function_names
    [CETTA_EXACT_ARITHMETIC_OP_COUNT_V1] = {
        "cetta_generated_exact_integer_add_v1",
        "cetta_generated_exact_integer_sub_v1",
        "cetta_generated_exact_integer_mul_v1",
        "cetta_generated_exact_integer_tquot_v1",
        "cetta_generated_exact_integer_fquot_v1",
        "cetta_generated_exact_integer_trem_v1",
        "cetta_generated_exact_integer_frem_v1",
};

static void set_status(CettaExternalCallStructuredCTransformStatusV1 *status,
                       CettaExternalCallStructuredCTransformStatusV1 value) {
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

static bool text_copy(CettaLdTextV1 *out, const CettaLdTextV1 *source) {
    uint8_t *bytes = NULL;

    if (!out || !source || (source->len > 0u && !source->bytes))
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

static bool make_wrapped_token(CettaLdPatternV1 *out,
                               const CettaLdTextV1 *wrapper,
                               const char *token) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, wrapper, 1u) ||
        !make_apply_c(&result.as.apply.arguments.items[0], token, 0u)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool make_wrapped_text(CettaLdPatternV1 *out,
                              const CettaLdTextV1 *wrapper,
                              const CettaLdTextV1 *token) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, wrapper, 1u) ||
        !make_apply(&result.as.apply.arguments.items[0], token, 0u)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool make_named_type(CettaLdPatternV1 *out,
                            const CettaStructuredCProfileV1 *profile,
                            const char *name) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->type_named, 1u) ||
        !make_wrapped_token(&result.as.apply.arguments.items[0],
                            profile->identifier, name)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool wrap_unary(CettaLdPatternV1 *out,
                       const CettaLdTextV1 *head,
                       CettaLdPatternV1 *child) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, head, 1u))
        return false;
    move_pattern(&result.as.apply.arguments.items[0], child);
    move_pattern(out, &result);
    return true;
}

static bool make_pointer_type(CettaLdPatternV1 *out,
                              const CettaStructuredCProfileV1 *profile,
                              const char *name,
                              bool is_const) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_named_type(&result, profile, name) ||
        (is_const && !wrap_unary(&result, profile->type_const, &result)) ||
        !wrap_unary(&result, profile->type_pointer, &result)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool prepend(CettaLdPatternV1 *list,
                    const CettaLdTextV1 *cons_head,
                    CettaLdPatternV1 *element) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, cons_head, 2u))
        return false;
    move_pattern(&result.as.apply.arguments.items[0], element);
    move_pattern(&result.as.apply.arguments.items[1], list);
    move_pattern(list, &result);
    return true;
}

static bool make_expression_variable(CettaLdPatternV1 *out,
                                     const CettaStructuredCProfileV1 *profile,
                                     const char *name) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->expression_variable, 1u) ||
        !make_wrapped_token(&result.as.apply.arguments.items[0],
                            profile->identifier, name)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool make_expression_symbol(CettaLdPatternV1 *out,
                                   const CettaStructuredCProfileV1 *profile,
                                   const char *name) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->expression_constant, 1u) ||
        !make_apply(&result.as.apply.arguments.items[0], profile->value_symbol,
                    1u) ||
        !make_wrapped_token(
            &result.as.apply.arguments.items[0].as.apply.arguments.items[0],
            profile->identifier, name)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool make_expression_u32(CettaLdPatternV1 *out,
                                const CettaStructuredCProfileV1 *profile,
                                uint32_t value) {
    CettaLdPatternV1 result;
    char decimal[16];

    (void)snprintf(decimal, sizeof(decimal), "%u", value);
    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->expression_constant, 1u) ||
        !make_apply(&result.as.apply.arguments.items[0], profile->value_integer,
                    1u) ||
        !make_apply_c(
            &result.as.apply.arguments.items[0].as.apply.arguments.items[0],
            decimal, 0u)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool make_call(CettaLdPatternV1 *out,
                      const CettaStructuredCProfileV1 *profile,
                      const char *name,
                      CettaLdPatternV1 *arguments) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->expression_call, 2u) ||
        !make_wrapped_token(&result.as.apply.arguments.items[0],
                            profile->external_name, name)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(&result.as.apply.arguments.items[1], arguments);
    move_pattern(out, &result);
    return true;
}

static bool make_call_text(CettaLdPatternV1 *out,
                           const CettaStructuredCProfileV1 *profile,
                           const CettaLdTextV1 *name,
                           CettaLdPatternV1 *arguments) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->expression_call, 2u) ||
        !make_wrapped_text(&result.as.apply.arguments.items[0],
                           profile->external_name, name)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(&result.as.apply.arguments.items[1], arguments);
    move_pattern(out, &result);
    return true;
}

static bool empty_expressions(CettaLdPatternV1 *out,
                              const CettaStructuredCProfileV1 *profile) {
    return make_apply(out, profile->expressions_nil, 0u);
}

static bool prepend_variable_argument(CettaLdPatternV1 *arguments,
                                      const CettaStructuredCProfileV1 *profile,
                                      const char *name) {
    CettaLdPatternV1 expression;

    cetta_ld_pattern_v1_init(&expression);
    if (!make_expression_variable(&expression, profile, name) ||
        !prepend(arguments, profile->expressions_cons, &expression)) {
        cetta_ld_pattern_v1_free(&expression);
        return false;
    }
    return true;
}

static bool prepend_symbol_argument(CettaLdPatternV1 *arguments,
                                    const CettaStructuredCProfileV1 *profile,
                                    const char *name) {
    CettaLdPatternV1 expression;

    cetta_ld_pattern_v1_init(&expression);
    if (!make_expression_symbol(&expression, profile, name) ||
        !prepend(arguments, profile->expressions_cons, &expression)) {
        cetta_ld_pattern_v1_free(&expression);
        return false;
    }
    return true;
}

static bool prepend_u32_argument(CettaLdPatternV1 *arguments,
                                 const CettaStructuredCProfileV1 *profile,
                                 uint32_t value) {
    CettaLdPatternV1 expression;

    cetta_ld_pattern_v1_init(&expression);
    if (!make_expression_u32(&expression, profile, value) ||
        !prepend(arguments, profile->expressions_cons, &expression)) {
        cetta_ld_pattern_v1_free(&expression);
        return false;
    }
    return true;
}

static bool make_effect(CettaLdPatternV1 *out,
                        const CettaStructuredCProfileV1 *profile,
                        CettaLdPatternV1 *expression) {
    return wrap_unary(out, profile->effect, expression);
}

static bool make_return(CettaLdPatternV1 *out,
                        const CettaStructuredCProfileV1 *profile,
                        CettaLdPatternV1 *expression) {
    return wrap_unary(out, profile->return_statement, expression);
}

static bool make_helper_call(CettaLdPatternV1 *out,
                             const CettaStructuredCProfileV1 *profile,
                             const char *name,
                             const char *const *variables,
                             uint32_t variable_count) {
    CettaLdPatternV1 arguments;
    uint32_t index;

    cetta_ld_pattern_v1_init(&arguments);
    if (!empty_expressions(&arguments, profile))
        return false;
    for (index = variable_count; index > 0u; index--) {
        if (!prepend_variable_argument(&arguments, profile,
                                       variables[index - 1u])) {
            cetta_ld_pattern_v1_free(&arguments);
            return false;
        }
    }
    if (!make_call(out, profile, name, &arguments)) {
        cetta_ld_pattern_v1_free(&arguments);
        return false;
    }
    return true;
}

static bool make_finish_return(CettaLdPatternV1 *out,
                               const CettaStructuredCProfileV1 *profile,
                               const char *outcome) {
    CettaLdPatternV1 arguments;
    CettaLdPatternV1 expression;

    cetta_ld_pattern_v1_init(&arguments);
    cetta_ld_pattern_v1_init(&expression);
    if (!empty_expressions(&arguments, profile) ||
        !prepend_symbol_argument(&arguments, profile, outcome) ||
        !prepend_variable_argument(&arguments, profile, "receipt") ||
        !make_call(&expression, profile,
                   "cetta_external_call_generated_finish_v1", &arguments) ||
        !make_return(out, profile, &expression)) {
        cetta_ld_pattern_v1_free(&arguments);
        cetta_ld_pattern_v1_free(&expression);
        return false;
    }
    return true;
}

static bool make_record_step(CettaLdPatternV1 *out,
                             const CettaStructuredCProfileV1 *profile,
                             uint32_t pc) {
    CettaLdPatternV1 arguments;
    CettaLdPatternV1 expression;

    cetta_ld_pattern_v1_init(&arguments);
    cetta_ld_pattern_v1_init(&expression);
    if (!empty_expressions(&arguments, profile) ||
        !prepend_u32_argument(&arguments, profile, pc) ||
        !prepend_variable_argument(&arguments, profile, "receipt") ||
        !make_call(&expression, profile,
                   "cetta_external_call_generated_record_step_v1", &arguments) ||
        !make_effect(out, profile, &expression)) {
        cetta_ld_pattern_v1_free(&arguments);
        cetta_ld_pattern_v1_free(&expression);
        return false;
    }
    return true;
}

static bool make_record_external(CettaLdPatternV1 *out,
                                 const CettaStructuredCProfileV1 *profile,
                                 uint32_t pc) {
    CettaLdPatternV1 arguments;
    CettaLdPatternV1 expression;

    cetta_ld_pattern_v1_init(&arguments);
    cetta_ld_pattern_v1_init(&expression);
    if (!empty_expressions(&arguments, profile) ||
        !prepend_variable_argument(&arguments, profile, "external_status") ||
        !prepend_u32_argument(&arguments, profile, 0u) ||
        !prepend_u32_argument(&arguments, profile, pc) ||
        !prepend_variable_argument(&arguments, profile, "receipt") ||
        !make_call(&expression, profile,
                   "cetta_external_call_generated_record_external_v1",
                   &arguments) ||
        !make_effect(out, profile, &expression)) {
        cetta_ld_pattern_v1_free(&arguments);
        cetta_ld_pattern_v1_free(&expression);
        return false;
    }
    return true;
}

static bool make_status_case(CettaLdPatternV1 *out,
                             const CettaStructuredCProfileV1 *profile,
                             const char *external_status,
                             const char *outcome,
                             uint32_t pc) {
    CettaLdPatternV1 body;
    CettaLdPatternV1 statement;
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&body);
    cetta_ld_pattern_v1_init(&statement);
    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&body, profile->statements_nil, 0u) ||
        !make_finish_return(&statement, profile, outcome) ||
        !prepend(&body, profile->statements_cons, &statement) ||
        !make_record_step(&statement, profile, pc) ||
        !prepend(&body, profile->statements_cons, &statement) ||
        !make_apply(&result, profile->case_statement, 2u) ||
        !make_apply(&result.as.apply.arguments.items[0], profile->value_symbol,
                    1u) ||
        !make_wrapped_token(
            &result.as.apply.arguments.items[0].as.apply.arguments.items[0],
            profile->identifier, external_status)) {
        cetta_ld_pattern_v1_free(&body);
        cetta_ld_pattern_v1_free(&statement);
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(&result.as.apply.arguments.items[1], &body);
    move_pattern(out, &result);
    return true;
}

static bool make_switch(CettaLdPatternV1 *out,
                        const CettaStructuredCProfileV1 *profile,
                        uint32_t value_pc) {
    static const char *const external_statuses[] = {
        "CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1",
        "CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_LANGUAGE_FAULT_V1",
        "CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_ENGINE_FAULT_V1",
        "CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_RESOURCE_FAULT_V1"};
    static const char *const outcomes[] = {
        "CETTA_EXTERNAL_CALL_GENERATED_VALUE_V1",
        "CETTA_EXTERNAL_CALL_GENERATED_LANGUAGE_FAULT_V1",
        "CETTA_EXTERNAL_CALL_GENERATED_ENGINE_FAULT_V1",
        "CETTA_EXTERNAL_CALL_GENERATED_RESOURCE_FAULT_V1"};
    CettaLdPatternV1 cases;
    CettaLdPatternV1 item;
    CettaLdPatternV1 default_body;
    CettaLdPatternV1 result;
    uint32_t index;

    cetta_ld_pattern_v1_init(&cases);
    cetta_ld_pattern_v1_init(&item);
    cetta_ld_pattern_v1_init(&default_body);
    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&cases, profile->cases_nil, 0u))
        return false;
    for (index = 4u; index > 0u; index--) {
        uint32_t current = index - 1u;
        if (!make_status_case(&item, profile, external_statuses[current],
                              outcomes[current], value_pc + current) ||
            !prepend(&cases, profile->cases_cons, &item))
            goto fail;
    }
    if (!make_apply(&default_body, profile->statements_nil, 0u) ||
        !make_finish_return(&item, profile,
            "CETTA_EXTERNAL_CALL_GENERATED_ENGINE_FAULT_V1") ||
        !prepend(&default_body, profile->statements_cons, &item))
        goto fail;
    {
        const char *const variables[] = {"receipt"};
        CettaLdPatternV1 expression;
        cetta_ld_pattern_v1_init(&expression);
        if (!make_helper_call(&expression, profile,
                "cetta_external_call_generated_mark_incomplete_v1",
                variables, 1u) ||
            !make_effect(&item, profile, &expression) ||
            !prepend(&default_body, profile->statements_cons, &item)) {
            cetta_ld_pattern_v1_free(&expression);
            goto fail;
        }
    }
    if (!make_apply(&result, profile->switch_statement, 3u) ||
        !make_expression_variable(&result.as.apply.arguments.items[0], profile,
                                  "external_status"))
        goto fail;
    move_pattern(&result.as.apply.arguments.items[1], &cases);
    move_pattern(&result.as.apply.arguments.items[2], &default_body);
    move_pattern(out, &result);
    return true;
fail:
    cetta_ld_pattern_v1_free(&cases);
    cetta_ld_pattern_v1_free(&item);
    cetta_ld_pattern_v1_free(&default_body);
    cetta_ld_pattern_v1_free(&result);
    return false;
}

static bool make_guard(CettaLdPatternV1 *out,
                       const CettaStructuredCProfileV1 *profile) {
    CettaLdPatternV1 then_body;
    CettaLdPatternV1 else_body;
    CettaLdPatternV1 statement;
    CettaLdPatternV1 condition;
    CettaLdPatternV1 arguments;
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&then_body);
    cetta_ld_pattern_v1_init(&else_body);
    cetta_ld_pattern_v1_init(&statement);
    cetta_ld_pattern_v1_init(&condition);
    cetta_ld_pattern_v1_init(&arguments);
    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&then_body, profile->statements_nil, 0u) ||
        !make_finish_return(&statement, profile,
            "CETTA_EXTERNAL_CALL_GENERATED_DECLINED_V1") ||
        !prepend(&then_body, profile->statements_cons, &statement) ||
        !make_record_step(&statement, profile, 1u) ||
        !prepend(&then_body, profile->statements_cons, &statement) ||
        !make_apply(&else_body, profile->statements_nil, 0u) ||
        !empty_expressions(&arguments, profile) ||
        !prepend_variable_argument(&arguments, profile, "second") ||
        !make_call(&condition, profile,
            "cetta_external_call_exact_integer_is_zero_v1", &arguments) ||
        !make_apply(&result, profile->if_statement, 3u))
        goto fail;
    move_pattern(&result.as.apply.arguments.items[0], &condition);
    move_pattern(&result.as.apply.arguments.items[1], &then_body);
    move_pattern(&result.as.apply.arguments.items[2], &else_body);
    move_pattern(out, &result);
    return true;
fail:
    cetta_ld_pattern_v1_free(&then_body);
    cetta_ld_pattern_v1_free(&else_body);
    cetta_ld_pattern_v1_free(&statement);
    cetta_ld_pattern_v1_free(&condition);
    cetta_ld_pattern_v1_free(&arguments);
    cetta_ld_pattern_v1_free(&result);
    return false;
}

static bool make_parameter(CettaLdPatternV1 *out,
                           const CettaStructuredCProfileV1 *profile,
                           const char *name,
                           const char *type_name,
                           bool pointer,
                           bool is_const) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&result, profile->parameter, 2u) ||
        !make_wrapped_token(&result.as.apply.arguments.items[0],
                            profile->identifier, name) ||
        !(pointer
            ? make_pointer_type(&result.as.apply.arguments.items[1], profile,
                                type_name, is_const)
            : make_named_type(&result.as.apply.arguments.items[1], profile,
                              type_name))) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    move_pattern(out, &result);
    return true;
}

static bool make_function_parameters(CettaLdPatternV1 *out,
                                     const CettaStructuredCProfileV1 *profile) {
    static const struct {
        const char *name;
        const char *type;
        bool is_const;
    } parameters[] = {
        {"first", "CettaExternalCallExactIntegerV1", true},
        {"second", "CettaExternalCallExactIntegerV1", true},
        {"output", "CettaExternalCallExactIntegerV1", false},
        {"receipt", "CettaExternalCallGeneratedReceiptV1", false},
    };
    CettaLdPatternV1 list;
    CettaLdPatternV1 parameter;
    uint32_t index;

    cetta_ld_pattern_v1_init(&list);
    cetta_ld_pattern_v1_init(&parameter);
    if (!make_apply(&list, profile->parameters_nil, 0u))
        return false;
    for (index = 4u; index > 0u; index--) {
        uint32_t current = index - 1u;
        if (!make_parameter(&parameter, profile, parameters[current].name,
                            parameters[current].type, true,
                            parameters[current].is_const) ||
            !prepend(&list, profile->parameters_cons, &parameter)) {
            cetta_ld_pattern_v1_free(&list);
            cetta_ld_pattern_v1_free(&parameter);
            return false;
        }
    }
    move_pattern(out, &list);
    return true;
}

static bool make_external_parameters(CettaLdPatternV1 *out,
                                     const CettaStructuredCProfileV1 *profile) {
    CettaLdPatternV1 list;
    CettaLdPatternV1 parameter;

    cetta_ld_pattern_v1_init(&list);
    cetta_ld_pattern_v1_init(&parameter);
    if (!make_apply(&list, profile->parameters_nil, 0u) ||
        !make_parameter(&parameter, profile, "output",
            "CettaExternalCallExactIntegerV1", true, false) ||
        !prepend(&list, profile->parameters_cons, &parameter) ||
        !make_parameter(&parameter, profile, "second",
            "CettaExternalCallExactIntegerV1", true, true) ||
        !prepend(&list, profile->parameters_cons, &parameter) ||
        !make_parameter(&parameter, profile, "first",
            "CettaExternalCallExactIntegerV1", true, true) ||
        !prepend(&list, profile->parameters_cons, &parameter)) {
        cetta_ld_pattern_v1_free(&list);
        cetta_ld_pattern_v1_free(&parameter);
        return false;
    }
    move_pattern(out, &list);
    return true;
}

static bool make_function(CettaLdPatternV1 *out,
                          const CettaStructuredCProfileV1 *profile,
                          const CettaExactArithmeticExternalCallEntryV1 *entry,
                          const CettaExactArithmeticExternalCallProgramViewV1 *view) {
    CettaLdPatternV1 body;
    CettaLdPatternV1 statement;
    CettaLdPatternV1 expression;
    CettaLdPatternV1 arguments;
    CettaLdPatternV1 result;
    CettaLdPatternV1 parameters;
    uint32_t call_pc = view->guarded ? 2u : 0u;
    uint32_t value_pc = view->guarded ? 3u : 1u;

    cetta_ld_pattern_v1_init(&body);
    cetta_ld_pattern_v1_init(&statement);
    cetta_ld_pattern_v1_init(&expression);
    cetta_ld_pattern_v1_init(&arguments);
    cetta_ld_pattern_v1_init(&result);
    cetta_ld_pattern_v1_init(&parameters);
    if (!make_apply(&body, profile->statements_nil, 0u) ||
        !make_switch(&statement, profile, value_pc) ||
        !prepend(&body, profile->statements_cons, &statement) ||
        !make_record_external(&statement, profile, call_pc) ||
        !prepend(&body, profile->statements_cons, &statement) ||
        !empty_expressions(&arguments, profile) ||
        !prepend_variable_argument(&arguments, profile, "output") ||
        !prepend_variable_argument(&arguments, profile, "second") ||
        !prepend_variable_argument(&arguments, profile, "first") ||
        !make_call_text(&expression, profile, view->provider_link, &arguments) ||
        !make_apply(&statement, profile->declare, 3u) ||
        !make_wrapped_token(&statement.as.apply.arguments.items[0],
                            profile->identifier, "external_status") ||
        !make_named_type(&statement.as.apply.arguments.items[1], profile,
                         "CettaExternalCallGeneratedExternalV1"))
        goto fail;
    move_pattern(&statement.as.apply.arguments.items[2], &expression);
    if (!prepend(&body, profile->statements_cons, &statement) ||
        !make_record_step(&statement, profile, call_pc) ||
        !prepend(&body, profile->statements_cons, &statement))
        goto fail;
    if (view->guarded) {
        if (!make_guard(&statement, profile) ||
            !prepend(&body, profile->statements_cons, &statement) ||
            !make_record_step(&statement, profile, 0u) ||
            !prepend(&body, profile->statements_cons, &statement))
            goto fail;
    }
    {
        const char *const variables[] = {"receipt"};
        if (!make_helper_call(&expression, profile,
                "cetta_external_call_generated_begin_v1", variables, 1u) ||
            !make_effect(&statement, profile, &expression) ||
            !prepend(&body, profile->statements_cons, &statement))
            goto fail;
    }
    {
        const char *const variables[] = {"receipt"};
        CettaLdPatternV1 then_body;
        CettaLdPatternV1 else_body;
        cetta_ld_pattern_v1_init(&then_body);
        cetta_ld_pattern_v1_init(&else_body);
        if (!make_apply(&then_body, profile->statements_nil, 0u) ||
            !make_expression_symbol(&expression, profile,
                "CETTA_EXTERNAL_CALL_GENERATED_ENGINE_FAULT_V1") ||
            !make_return(&statement, profile, &expression) ||
            !prepend(&then_body, profile->statements_cons, &statement) ||
            !make_apply(&else_body, profile->statements_nil, 0u) ||
            !make_helper_call(&expression, profile,
                "cetta_external_call_generated_receipt_missing_v1",
                variables, 1u) ||
            !make_apply(&statement, profile->if_statement, 3u)) {
            cetta_ld_pattern_v1_free(&then_body);
            cetta_ld_pattern_v1_free(&else_body);
            goto fail;
        }
        move_pattern(&statement.as.apply.arguments.items[0], &expression);
        move_pattern(&statement.as.apply.arguments.items[1], &then_body);
        move_pattern(&statement.as.apply.arguments.items[2], &else_body);
        if (!prepend(&body, profile->statements_cons, &statement))
            goto fail;
    }
    if (!make_function_parameters(&parameters, profile) ||
        !make_apply(&result, profile->function, 4u) ||
        !make_wrapped_token(&result.as.apply.arguments.items[0],
                            profile->function_name,
                            generated_function_names[entry->operation]) ||
        !make_named_type(&result.as.apply.arguments.items[1], profile,
                         "CettaExternalCallGeneratedOutcomeV1"))
        goto fail;
    move_pattern(&result.as.apply.arguments.items[2], &parameters);
    move_pattern(&result.as.apply.arguments.items[3], &body);
    move_pattern(out, &result);
    return true;
fail:
    cetta_ld_pattern_v1_free(&body);
    cetta_ld_pattern_v1_free(&statement);
    cetta_ld_pattern_v1_free(&expression);
    cetta_ld_pattern_v1_free(&arguments);
    cetta_ld_pattern_v1_free(&result);
    cetta_ld_pattern_v1_free(&parameters);
    return false;
}

static bool make_external(CettaLdPatternV1 *out,
                          const CettaStructuredCProfileV1 *profile,
                          const CettaLdTextV1 *provider) {
    CettaLdPatternV1 result;
    CettaLdPatternV1 parameters;

    cetta_ld_pattern_v1_init(&result);
    cetta_ld_pattern_v1_init(&parameters);
    if (!make_external_parameters(&parameters, profile) ||
        !make_apply(&result, profile->external_function, 3u) ||
        !make_wrapped_text(&result.as.apply.arguments.items[0],
                           profile->external_name, provider) ||
        !make_named_type(&result.as.apply.arguments.items[1], profile,
                         "CettaExternalCallGeneratedExternalV1")) {
        cetta_ld_pattern_v1_free(&result);
        cetta_ld_pattern_v1_free(&parameters);
        return false;
    }
    move_pattern(&result.as.apply.arguments.items[2], &parameters);
    move_pattern(out, &result);
    return true;
}

static bool make_program(CettaLdPatternV1 *out,
                         const CettaStructuredCProfileV1 *profile,
                         const CettaLanguageDefCoreV1 *external_call_language,
                         const CettaExactArithmeticExternalCallTransformV1 *source) {
    CettaLdPatternV1 functions;
    CettaLdPatternV1 externals;
    CettaLdPatternV1 item;
    CettaLdPatternV1 result;
    uint32_t index;

    cetta_ld_pattern_v1_init(&functions);
    cetta_ld_pattern_v1_init(&externals);
    cetta_ld_pattern_v1_init(&item);
    cetta_ld_pattern_v1_init(&result);
    if (!make_apply(&functions, profile->functions_nil, 0u) ||
        !make_apply(&externals, profile->external_functions_nil, 0u))
        goto fail;
    for (index = source->entry_len; index > 0u; index--) {
        uint32_t current = index - 1u;
        CettaExactArithmeticExternalCallProgramViewV1 view;
        const CettaExactArithmeticExternalCallEntryV1 *entry =
            &source->entries[current];
        if (entry->operation != current ||
            !cetta_exact_arithmetic_external_call_program_v1_inspect(
                external_call_language, &entry->target_program, &view) ||
            !make_function(&item, profile, entry, &view) ||
            !prepend(&functions, profile->functions_cons, &item) ||
            !make_external(&item, profile, view.provider_link) ||
            !prepend(&externals, profile->external_functions_cons, &item))
            goto fail;
    }
    if (!make_apply(&result, profile->program, 2u))
        goto fail;
    move_pattern(&result.as.apply.arguments.items[0], &externals);
    move_pattern(&result.as.apply.arguments.items[1], &functions);
    move_pattern(out, &result);
    return true;
fail:
    cetta_ld_pattern_v1_free(&functions);
    cetta_ld_pattern_v1_free(&externals);
    cetta_ld_pattern_v1_free(&item);
    cetta_ld_pattern_v1_free(&result);
    return false;
}

static bool source_programs_are_supported(
    const CettaLanguageDefCoreV1 *external_call_language,
    const CettaExactArithmeticExternalCallTransformV1 *source) {
    uint32_t index;

    for (index = 0u; index < source->entry_len; index++) {
        CettaExactArithmeticExternalCallProgramViewV1 view;
        const CettaExactArithmeticExternalCallEntryV1 *entry =
            &source->entries[index];
        if (entry->operation != index ||
            !cetta_exact_arithmetic_external_call_program_v1_inspect(
                external_call_language, &entry->target_program, &view))
            return false;
    }
    return true;
}

void cetta_external_call_structured_c_transform_v1_init(
    CettaExternalCallStructuredCTransformV1 *transform) {
    if (!transform)
        return;
    cetta_ld_pattern_v1_init(&transform->target_program);
}

void cetta_external_call_structured_c_transform_v1_free(
    CettaExternalCallStructuredCTransformV1 *transform) {
    if (!transform)
        return;
    cetta_ld_pattern_v1_free(&transform->target_program);
}

bool cetta_external_call_to_structured_c_transform_v1(
    CettaExternalCallStructuredCTransformV1 *out,
    const CettaLanguageDefCoreV1 *external_call_language,
    const CettaLanguageDefCoreV1 *structured_c_language,
    const CettaExactArithmeticExternalCallTransformV1 *source,
    CettaExternalCallStructuredCTransformStatusV1 *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaStructuredCProfileV1 profile;
    CettaExternalCallStructuredCTransformV1 candidate;

    set_status(status, CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_OK_V1);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out || !external_call_language || !structured_c_language || !source ||
        source->entry_len != CETTA_EXACT_ARITHMETIC_OP_COUNT_V1) {
        set_status(status,
            CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_BAD_ARGUMENT_V1);
        set_error(error_buf, error_buf_size,
                  "lowering requires both target languages and a complete source transform");
        return false;
    }
    if (!cetta_structured_c_profile_v1_admit(
            structured_c_language, &profile)) {
        set_status(status,
            CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_UNSUPPORTED_TARGET_V1);
        set_error(error_buf, error_buf_size,
                  "target is not the supported StructuredC presentation");
        return false;
    }
    if (!source_programs_are_supported(external_call_language, source)) {
        set_status(status,
            CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_UNSUPPORTED_SOURCE_V1);
        set_error(error_buf, error_buf_size,
                  "source contains an unsupported ExternalCallMachine program");
        return false;
    }
    cetta_external_call_structured_c_transform_v1_init(&candidate);
    if (!make_program(&candidate.target_program, &profile,
                      external_call_language, source)) {
        set_status(status,
            CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_ALLOCATION_FAILURE_V1);
        set_error(error_buf, error_buf_size,
                  "could not allocate the StructuredC target program");
        cetta_external_call_structured_c_transform_v1_free(&candidate);
        return false;
    }
    cetta_external_call_structured_c_transform_v1_free(out);
    *out = candidate;
    return true;
}

const char *cetta_external_call_structured_c_transform_status_v1_name(
    CettaExternalCallStructuredCTransformStatusV1 status) {
    switch (status) {
    case CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_OK_V1:
        return "ok";
    case CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_BAD_ARGUMENT_V1:
        return "bad_argument";
    case CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_UNSUPPORTED_SOURCE_V1:
        return "unsupported_source";
    case CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_UNSUPPORTED_TARGET_V1:
        return "unsupported_target";
    case CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_ALLOCATION_FAILURE_V1:
        return "allocation_failure";
    }
    return "unknown";
}
