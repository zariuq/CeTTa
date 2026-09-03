#include "structured_c_emitter_v1.h"
#include "structured_c_profile_v1.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    FILE *output;
    const CettaStructuredCProfileV1 *profile;
} EmitContext;

static void set_status(CettaStructuredCEmitStatusV1 *status,
                       CettaStructuredCEmitStatusV1 value) {
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

static bool emitf(FILE *output, const char *format, ...) {
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vfprintf(output, format, arguments);
    va_end(arguments);
    return result >= 0;
}

static bool text_equal(const CettaLdTextV1 *left,
                       const CettaLdTextV1 *right) {
    return left && right && left->len == right->len &&
        (left->len == 0u ||
         memcmp(left->bytes, right->bytes, left->len) == 0);
}

static bool pattern_is(const CettaLdPatternV1 *pattern,
                       const CettaLdTextV1 *head,
                       uint32_t arity) {
    return pattern && pattern->kind == CETTA_LD_PATTERN_APPLY_V1 &&
        text_equal(&pattern->as.apply.head, head) &&
        pattern->as.apply.arguments.len == arity;
}

static const CettaLdPatternV1 *argument(const CettaLdPatternV1 *pattern,
                                        uint32_t index) {
    if (!pattern || pattern->kind != CETTA_LD_PATTERN_APPLY_V1 ||
        index >= pattern->as.apply.arguments.len)
        return NULL;
    return &pattern->as.apply.arguments.items[index];
}

static const CettaLdTextV1 *wrapped_token(
    const CettaLdPatternV1 *pattern,
    const CettaLdTextV1 *wrapper) {
    const CettaLdPatternV1 *token;

    if (!pattern_is(pattern, wrapper, 1u))
        return NULL;
    token = argument(pattern, 0u);
    if (!token || token->kind != CETTA_LD_PATTERN_APPLY_V1 ||
        token->as.apply.arguments.len != 0u)
        return NULL;
    return &token->as.apply.head;
}

static bool identifier_valid(const CettaLdTextV1 *identifier) {
    uint32_t index;

    if (!identifier || identifier->len == 0u || !identifier->bytes)
        return false;
    if (!((identifier->bytes[0] >= 'A' && identifier->bytes[0] <= 'Z') ||
          (identifier->bytes[0] >= 'a' && identifier->bytes[0] <= 'z') ||
          identifier->bytes[0] == '_'))
        return false;
    for (index = 1u; index < identifier->len; index++) {
        uint8_t byte = identifier->bytes[index];
        if (!((byte >= 'A' && byte <= 'Z') ||
              (byte >= 'a' && byte <= 'z') ||
              (byte >= '0' && byte <= '9') || byte == '_'))
            return false;
    }
    return true;
}

static bool integer_token_valid(const CettaLdTextV1 *integer) {
    uint32_t index = 0u;

    if (!integer || integer->len == 0u || !integer->bytes)
        return false;
    if (integer->bytes[0] == '-') {
        if (integer->len == 1u)
            return false;
        index = 1u;
    }
    for (; index < integer->len; index++) {
        if (integer->bytes[index] < '0' || integer->bytes[index] > '9')
            return false;
    }
    return true;
}

static bool include_valid(const char *include) {
    const unsigned char *cursor = (const unsigned char *)include;

    if (!include || !*include)
        return false;
    for (; *cursor; cursor++) {
        if (*cursor == '"' || *cursor == '\\' || *cursor == '\n' ||
            *cursor == '\r' || *cursor < 0x20u)
            return false;
    }
    return true;
}

static bool emit_indent(EmitContext *context, uint32_t depth) {
    uint32_t index;

    for (index = 0u; index < depth; index++) {
        if (!emitf(context->output, "    "))
            return false;
    }
    return true;
}

static bool emit_identifier(EmitContext *context,
                            const CettaLdPatternV1 *pattern) {
    const CettaLdTextV1 *name = wrapped_token(
        pattern, context->profile->identifier);

    return identifier_valid(name) &&
        emitf(context->output, "%.*s", (int)name->len,
              (const char *)name->bytes);
}

static bool emit_named(EmitContext *context,
                       const CettaLdPatternV1 *pattern,
                       const CettaLdTextV1 *wrapper) {
    const CettaLdTextV1 *name = wrapped_token(pattern, wrapper);

    return identifier_valid(name) &&
        emitf(context->output, "%.*s", (int)name->len,
              (const char *)name->bytes);
}

static bool emit_type(EmitContext *context,
                      const CettaLdPatternV1 *type) {
    if (pattern_is(type, context->profile->type_named, 1u))
        return emit_identifier(context, argument(type, 0u));
    if (pattern_is(type, context->profile->type_const, 1u))
        return emitf(context->output, "const ") &&
            emit_type(context, argument(type, 0u));
    if (pattern_is(type, context->profile->type_pointer, 1u))
        return emit_type(context, argument(type, 0u)) &&
            emitf(context->output, " *");
    return false;
}

static bool emit_value(EmitContext *context,
                       const CettaLdPatternV1 *value) {
    const CettaLdPatternV1 *token;
    const CettaLdTextV1 *text;

    if (pattern_is(value, context->profile->value_symbol, 1u))
        return emit_identifier(context, argument(value, 0u));
    if (!pattern_is(value, context->profile->value_integer, 1u))
        return false;
    token = argument(value, 0u);
    if (!token || token->kind != CETTA_LD_PATTERN_APPLY_V1 ||
        token->as.apply.arguments.len != 0u)
        return false;
    text = &token->as.apply.head;
    return integer_token_valid(text) &&
        emitf(context->output, "%.*s", (int)text->len,
              (const char *)text->bytes);
}

static bool emit_expression(EmitContext *context,
                            const CettaLdPatternV1 *expression);

static bool emit_expressions(EmitContext *context,
                             const CettaLdPatternV1 *expressions) {
    const CettaLdPatternV1 *cursor = expressions;
    bool first = true;

    while (pattern_is(cursor, context->profile->expressions_cons, 2u)) {
        if ((!first && !emitf(context->output, ", ")) ||
            !emit_expression(context, argument(cursor, 0u)))
            return false;
        first = false;
        cursor = argument(cursor, 1u);
    }
    return pattern_is(cursor, context->profile->expressions_nil, 0u);
}

static bool emit_expression(EmitContext *context,
                            const CettaLdPatternV1 *expression) {
    if (pattern_is(expression, context->profile->expression_variable, 1u))
        return emit_identifier(context, argument(expression, 0u));
    if (pattern_is(expression, context->profile->expression_constant, 1u))
        return emit_value(context, argument(expression, 0u));
    if (pattern_is(expression, context->profile->expression_call, 2u))
        return emit_named(context, argument(expression, 0u),
                          context->profile->external_name) &&
            emitf(context->output, "(") &&
            emit_expressions(context, argument(expression, 1u)) &&
            emitf(context->output, ")");
    return false;
}

static bool emit_statements(EmitContext *context,
                            const CettaLdPatternV1 *statements,
                            uint32_t depth);

static bool emit_statement(EmitContext *context,
                           const CettaLdPatternV1 *statement,
                           uint32_t depth) {
    if (!emit_indent(context, depth))
        return false;
    if (pattern_is(statement, context->profile->assign, 2u))
        return emit_identifier(context, argument(statement, 0u)) &&
            emitf(context->output, " = ") &&
            emit_expression(context, argument(statement, 1u)) &&
            emitf(context->output, ";\n");
    if (pattern_is(statement, context->profile->declare, 3u))
        return emit_type(context, argument(statement, 1u)) &&
            emitf(context->output, " ") &&
            emit_identifier(context, argument(statement, 0u)) &&
            emitf(context->output, " = ") &&
            emit_expression(context, argument(statement, 2u)) &&
            emitf(context->output, ";\n");
    if (pattern_is(statement, context->profile->effect, 1u))
        return emit_expression(context, argument(statement, 0u)) &&
            emitf(context->output, ";\n");
    if (pattern_is(statement, context->profile->return_statement, 1u))
        return emitf(context->output, "return ") &&
            emit_expression(context, argument(statement, 0u)) &&
            emitf(context->output, ";\n");
    if (pattern_is(statement, context->profile->if_statement, 3u))
        return emitf(context->output, "if (") &&
            emit_expression(context, argument(statement, 0u)) &&
            emitf(context->output, ") {\n") &&
            emit_statements(context, argument(statement, 1u), depth + 1u) &&
            emit_indent(context, depth) && emitf(context->output, "} else {\n") &&
            emit_statements(context, argument(statement, 2u), depth + 1u) &&
            emit_indent(context, depth) && emitf(context->output, "}\n");
    if (pattern_is(statement, context->profile->while_statement, 2u))
        return emitf(context->output, "while (") &&
            emit_expression(context, argument(statement, 0u)) &&
            emitf(context->output, ") {\n") &&
            emit_statements(context, argument(statement, 1u), depth + 1u) &&
            emit_indent(context, depth) && emitf(context->output, "}\n");
    if (pattern_is(statement, context->profile->switch_statement, 3u)) {
        const CettaLdPatternV1 *cases = argument(statement, 1u);
        if (!emitf(context->output, "switch (") ||
            !emit_expression(context, argument(statement, 0u)) ||
            !emitf(context->output, ") {\n"))
            return false;
        while (pattern_is(cases, context->profile->cases_cons, 2u)) {
            const CettaLdPatternV1 *case_pattern = argument(cases, 0u);
            if (!pattern_is(case_pattern, context->profile->case_statement, 2u) ||
                !emit_indent(context, depth) ||
                !emitf(context->output, "case ") ||
                !emit_value(context, argument(case_pattern, 0u)) ||
                !emitf(context->output, ":\n") ||
                !emit_statements(context, argument(case_pattern, 1u),
                                 depth + 1u) ||
                !emit_indent(context, depth + 1u) ||
                !emitf(context->output, "break;\n"))
                return false;
            cases = argument(cases, 1u);
        }
        if (!pattern_is(cases, context->profile->cases_nil, 0u) ||
            !emit_indent(context, depth) ||
            !emitf(context->output, "default:\n") ||
            !emit_statements(context, argument(statement, 2u), depth + 1u) ||
            !emit_indent(context, depth + 1u) ||
            !emitf(context->output, "break;\n") ||
            !emit_indent(context, depth) || !emitf(context->output, "}\n"))
            return false;
        return true;
    }
    return false;
}

static bool emit_statements(EmitContext *context,
                            const CettaLdPatternV1 *statements,
                            uint32_t depth) {
    const CettaLdPatternV1 *cursor = statements;

    while (pattern_is(cursor, context->profile->statements_cons, 2u)) {
        if (!emit_statement(context, argument(cursor, 0u), depth))
            return false;
        cursor = argument(cursor, 1u);
    }
    return pattern_is(cursor, context->profile->statements_nil, 0u);
}

static bool emit_parameters(EmitContext *context,
                            const CettaLdPatternV1 *parameters) {
    const CettaLdPatternV1 *cursor = parameters;
    bool first = true;

    while (pattern_is(cursor, context->profile->parameters_cons, 2u)) {
        const CettaLdPatternV1 *parameter = argument(cursor, 0u);
        if (!pattern_is(parameter, context->profile->parameter, 2u) ||
            (!first && !emitf(context->output, ",\n    ")) ||
            !emit_type(context, argument(parameter, 1u)) ||
            !emitf(context->output, " ") ||
            !emit_identifier(context, argument(parameter, 0u)))
            return false;
        first = false;
        cursor = argument(cursor, 1u);
    }
    if (!pattern_is(cursor, context->profile->parameters_nil, 0u))
        return false;
    return first ? emitf(context->output, "void") : true;
}

static bool emit_external(EmitContext *context,
                          const CettaLdPatternV1 *external) {
    if (!pattern_is(external, context->profile->external_function, 3u))
        return false;
    return emitf(context->output, "extern ") &&
        emit_type(context, argument(external, 1u)) &&
        emitf(context->output, " ") &&
        emit_named(context, argument(external, 0u),
                   context->profile->external_name) &&
        emitf(context->output, "(\n    ") &&
        emit_parameters(context, argument(external, 2u)) &&
        emitf(context->output, ");\n\n");
}

static bool emit_externals(EmitContext *context,
                           const CettaLdPatternV1 *externals) {
    const CettaLdPatternV1 *cursor = externals;

    while (pattern_is(cursor, context->profile->external_functions_cons, 2u)) {
        if (!emit_external(context, argument(cursor, 0u)))
            return false;
        cursor = argument(cursor, 1u);
    }
    return pattern_is(cursor, context->profile->external_functions_nil, 0u);
}

static bool emit_function(EmitContext *context,
                          const CettaLdPatternV1 *function) {
    if (!pattern_is(function, context->profile->function, 4u))
        return false;
    return emit_type(context, argument(function, 1u)) &&
        emitf(context->output, " ") &&
        emit_named(context, argument(function, 0u),
                   context->profile->function_name) &&
        emitf(context->output, "(\n    ") &&
        emit_parameters(context, argument(function, 2u)) &&
        emitf(context->output, ") {\n") &&
        emit_statements(context, argument(function, 3u), 1u) &&
        emitf(context->output, "}\n\n");
}

static bool emit_functions(EmitContext *context,
                           const CettaLdPatternV1 *functions) {
    const CettaLdPatternV1 *cursor = functions;

    while (pattern_is(cursor, context->profile->functions_cons, 2u)) {
        if (!emit_function(context, argument(cursor, 0u)))
            return false;
        cursor = argument(cursor, 1u);
    }
    return pattern_is(cursor, context->profile->functions_nil, 0u);
}

bool cetta_structured_c_emit_v1(
    FILE *output,
    const CettaLanguageDefCoreV1 *language,
    const CettaLdPatternV1 *program,
    const char *abi_include,
    CettaStructuredCEmitStatusV1 *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaStructuredCProfileV1 profile;
    EmitContext context;

    set_status(status, CETTA_STRUCTURED_C_EMIT_OK_V1);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!output || !language || !program || !include_valid(abi_include)) {
        set_status(status, CETTA_STRUCTURED_C_EMIT_BAD_ARGUMENT_V1);
        set_error(error_buf, error_buf_size,
                  "StructuredC emission requires language, program, and ABI include");
        return false;
    }
    if (!cetta_structured_c_profile_v1_admit(language, &profile)) {
        set_status(status, CETTA_STRUCTURED_C_EMIT_UNSUPPORTED_LANGUAGE_V1);
        set_error(error_buf, error_buf_size,
                  "language is not the supported StructuredC presentation");
        return false;
    }
    if (!pattern_is(program, profile.program, 2u)) {
        set_status(status, CETTA_STRUCTURED_C_EMIT_UNSUPPORTED_PROGRAM_V1);
        set_error(error_buf, error_buf_size,
                  "input is not a StructuredC Program Pattern");
        return false;
    }
    context.output = output;
    context.profile = &profile;
    if (!emitf(output, "#include \"%s\"\n\n", abi_include) ||
        !emit_externals(&context, argument(program, 0u)) ||
        !emit_functions(&context, argument(program, 1u))) {
        set_status(status, ferror(output)
            ? CETTA_STRUCTURED_C_EMIT_IO_FAILURE_V1
            : CETTA_STRUCTURED_C_EMIT_UNSUPPORTED_PROGRAM_V1);
        set_error(error_buf, error_buf_size,
                  ferror(output) ? "I/O failure while emitting StructuredC"
                                 : "program uses an unsupported StructuredC form");
        return false;
    }
    if (fflush(output) != 0 || ferror(output)) {
        set_status(status, CETTA_STRUCTURED_C_EMIT_IO_FAILURE_V1);
        set_error(error_buf, error_buf_size,
                  "I/O failure while completing StructuredC emission");
        return false;
    }
    return true;
}

const char *cetta_structured_c_emit_status_v1_name(
    CettaStructuredCEmitStatusV1 status) {
    switch (status) {
    case CETTA_STRUCTURED_C_EMIT_OK_V1:
        return "ok";
    case CETTA_STRUCTURED_C_EMIT_BAD_ARGUMENT_V1:
        return "bad_argument";
    case CETTA_STRUCTURED_C_EMIT_UNSUPPORTED_LANGUAGE_V1:
        return "unsupported_language";
    case CETTA_STRUCTURED_C_EMIT_UNSUPPORTED_PROGRAM_V1:
        return "unsupported_program";
    case CETTA_STRUCTURED_C_EMIT_IO_FAILURE_V1:
        return "io_failure";
    }
    return "unknown";
}
