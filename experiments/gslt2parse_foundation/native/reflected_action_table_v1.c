#include "reflected_action_table_v1.h"

#include "parser_action_primitive_v1.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PP_REFLECTED_KIND_ATOM_V1 = 0,
    PP_REFLECTED_KIND_ATOMS_V1 = 1,
    PP_REFLECTED_KIND_TEXT_V1 = 2,
    PP_REFLECTED_KIND_INTEGER_V1 = 3,
    PP_REFLECTED_KIND_INTEGER_LEXEME_V1 = 4,
    PP_REFLECTED_KIND_RATIONAL_LEXEME_V1 = 5,
    PP_REFLECTED_KIND_COUNT_V1 = 6
} PPReflectedKindV1;

static bool reflected_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;
    if (buf && size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(buf, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool reflected_head(
    const Atom *term, const char *head, uint64_t arity) {
    return term && term->kind == ATOM_EXPR && term->expr.elems &&
        term->expr.len == arity + 1u &&
        atom_is_symbol(term->expr.elems[0], head);
}

static const char *reflected_string(const Atom *term) {
    if (!term || term->kind != ATOM_GROUNDED ||
        term->ground.gkind != GV_STRING || !term->ground.sval)
        return NULL;
    return term->ground.sval;
}

static bool reflected_u32(const Atom *term, uint32_t *out) {
    if (!term || !out || term->kind != ATOM_GROUNDED ||
        term->ground.gkind != GV_INT || term->ground.ival < 0 ||
        (uint64_t)term->ground.ival > UINT32_MAX)
        return false;
    *out = (uint32_t)term->ground.ival;
    return true;
}

static bool reflected_kind(const Atom *term, PPReflectedKindV1 *out) {
    uint32_t value;
    if (!reflected_u32(term, &value) || value >= PP_REFLECTED_KIND_COUNT_V1)
        return false;
    *out = (PPReflectedKindV1)value;
    return true;
}

static bool reflected_qindex(const Atom *term, uint32_t *out) {
    uint32_t value = 0u;
    while (reflected_head(term, "q-succ", 1u)) {
        if (value == UINT32_MAX)
            return false;
        value++;
        term = term->expr.elems[1];
    }
    if (!atom_is_symbol((Atom *)term, "q-zero"))
        return false;
    *out = value;
    return true;
}

static bool reflected_representation_embeds(
    PPReflectedKindV1 source, PPReflectedKindV1 target) {
    return target == PP_REFLECTED_KIND_ATOM_V1 || source == target;
}

static bool reflected_integer_lexeme(const Atom *term) {
    const char *text = reflected_string(term);
    return text && pp_action_primitive_v1_integer_decimal(text, strlen(text));
}

static bool reflected_rational_lexeme(const Atom *term) {
    const char *text = reflected_string(term);
    const char *slash;
    size_t length;
    size_t numerator_len;
    if (!text)
        return false;
    length = strlen(text);
    slash = strchr(text, '/');
    if (!slash || slash == text || slash[1] == '\0' || strchr(slash + 1, '/'))
        return false;
    numerator_len = (size_t)(slash - text);
    return pp_action_primitive_v1_integer_decimal(text, numerator_len) &&
        pp_action_primitive_v1_unsigned_decimal(
            slash + 1, length - numerator_len - 1u, true);
}

static bool reflected_constant_kind(
    const Atom *term, PPReflectedKindV1 kind) {
    if (!term)
        return false;
    switch (kind) {
    case PP_REFLECTED_KIND_ATOM_V1:
        return true;
    case PP_REFLECTED_KIND_ATOMS_V1:
        return term->kind == ATOM_EXPR;
    case PP_REFLECTED_KIND_TEXT_V1:
        return reflected_string(term) != NULL;
    case PP_REFLECTED_KIND_INTEGER_V1:
        return term->kind == ATOM_GROUNDED &&
            (term->ground.gkind == GV_INT || term->ground.gkind == GV_BIGINT);
    case PP_REFLECTED_KIND_INTEGER_LEXEME_V1:
        return reflected_integer_lexeme(term);
    case PP_REFLECTED_KIND_RATIONAL_LEXEME_V1:
        return reflected_rational_lexeme(term);
    case PP_REFLECTED_KIND_COUNT_V1:
        break;
    }
    return false;
}

static bool reflected_action_typed(
    const Atom *action,
    Atom *const *context,
    uint32_t context_len,
    PPReflectedKindV1 output,
    uint32_t depth);

static bool reflected_arguments_typed(
    const Atom *arguments,
    Atom *const *context,
    uint32_t context_len,
    const PPReflectedKindV1 *inputs,
    uint32_t input_len,
    uint32_t depth) {
    uint32_t index;
    if (!arguments || (!inputs && input_len != 0u) || depth == 0u)
        return false;
    for (index = 0u; index < input_len; index++) {
        if (!reflected_head(arguments, "pa-cons", 2u) ||
            !reflected_action_typed(
                arguments->expr.elems[1], context, context_len,
                inputs[index], depth - 1u)) {
            return false;
        }
        arguments = arguments->expr.elems[2];
    }
    return atom_is_symbol((Atom *)arguments, "pa-nil");
}

static bool reflected_primitive_signature(
    const Atom *operation,
    PPReflectedKindV1 output,
    const PPReflectedKindV1 **inputs,
    uint32_t *input_len) {
    static const PPReflectedKindV1 symbol[] = {
        PP_REFLECTED_KIND_TEXT_V1};
    static const PPReflectedKindV1 cons[] = {
        PP_REFLECTED_KIND_ATOM_V1, PP_REFLECTED_KIND_ATOMS_V1};
    static const PPReflectedKindV1 append[] = {
        PP_REFLECTED_KIND_ATOMS_V1, PP_REFLECTED_KIND_ATOMS_V1};
    static const PPReflectedKindV1 text_append[] = {
        PP_REFLECTED_KIND_TEXT_V1, PP_REFLECTED_KIND_TEXT_V1};
    static const PPReflectedKindV1 integer[] = {
        PP_REFLECTED_KIND_INTEGER_LEXEME_V1};
    static const PPReflectedKindV1 rational[] = {
        PP_REFLECTED_KIND_RATIONAL_LEXEME_V1};
    PPActionPrimitiveV1 primitive;
    uint32_t arity;
    if (!pp_action_primitive_v1_decode(operation, &primitive, &arity))
        return false;
    switch (primitive) {
    case PP_ACTION_PRIMITIVE_V1_SYMBOL:
        if (output != PP_REFLECTED_KIND_ATOM_V1) return false;
        *inputs = symbol;
        break;
    case PP_ACTION_PRIMITIVE_V1_CONS:
        if (output != PP_REFLECTED_KIND_ATOM_V1 &&
            output != PP_REFLECTED_KIND_ATOMS_V1) return false;
        *inputs = cons;
        break;
    case PP_ACTION_PRIMITIVE_V1_APPEND:
        if (output != PP_REFLECTED_KIND_ATOM_V1 &&
            output != PP_REFLECTED_KIND_ATOMS_V1) return false;
        *inputs = append;
        break;
    case PP_ACTION_PRIMITIVE_V1_TEXT_APPEND:
        if (output != PP_REFLECTED_KIND_ATOM_V1 &&
            output != PP_REFLECTED_KIND_TEXT_V1) return false;
        *inputs = text_append;
        break;
    case PP_ACTION_PRIMITIVE_V1_DECIMAL_INTEGER_VALUE:
        if (output != PP_REFLECTED_KIND_ATOM_V1 &&
            output != PP_REFLECTED_KIND_INTEGER_V1) return false;
        *inputs = integer;
        break;
    case PP_ACTION_PRIMITIVE_V1_DECIMAL_RATIONAL_COMPONENTS:
        if (output != PP_REFLECTED_KIND_ATOM_V1 &&
            output != PP_REFLECTED_KIND_ATOMS_V1) return false;
        *inputs = rational;
        break;
    }
    *input_len = arity;
    return true;
}

static bool reflected_action_typed(
    const Atom *action,
    Atom *const *context,
    uint32_t context_len,
    PPReflectedKindV1 output,
    uint32_t depth) {
    if (!action || !context || depth == 0u)
        return false;
    if (reflected_head(action, "pa-slot", 1u)) {
        uint32_t index;
        PPReflectedKindV1 actual;
        return reflected_qindex(action->expr.elems[1], &index) &&
            index < context_len && reflected_kind(context[index], &actual) &&
            reflected_representation_embeds(actual, output);
    }
    if (reflected_head(action, "pa-const", 1u))
        return reflected_constant_kind(action->expr.elems[1], output);
    if (reflected_head(action, "pa-apply", 2u)) {
        const Atom *arguments = action->expr.elems[2];
        uint32_t count = 0u;
        const Atom *cursor = arguments;
        PPReflectedKindV1 *inputs;
        bool ok;
        if (output != PP_REFLECTED_KIND_ATOM_V1 ||
            !action->expr.elems[1] ||
            action->expr.elems[1]->kind != ATOM_SYMBOL)
            return false;
        while (reflected_head(cursor, "pa-cons", 2u)) {
            if (count == UINT32_MAX)
                return false;
            count++;
            cursor = cursor->expr.elems[2];
        }
        if (!atom_is_symbol((Atom *)cursor, "pa-nil"))
            return false;
        inputs = malloc((count ? count : 1u) * sizeof(*inputs));
        if (!inputs)
            return false;
        for (uint32_t index = 0u; index < count; index++)
            inputs[index] = PP_REFLECTED_KIND_ATOM_V1;
        ok = reflected_arguments_typed(
            arguments, context, context_len, inputs, count, depth - 1u);
        free(inputs);
        return ok;
    }
    if (reflected_head(action, "pa-primitive", 2u)) {
        const PPReflectedKindV1 *inputs = NULL;
        uint32_t input_len = 0u;
        return reflected_primitive_signature(
                   action->expr.elems[1], output, &inputs, &input_len) &&
            reflected_arguments_typed(
                action->expr.elems[2], context, context_len,
                inputs, input_len, depth - 1u);
    }
    return false;
}

bool pp_reflected_action_table_v1_build(
    const PPTableSnapshotV1 *snapshot,
    const Atom *artifact,
    const char *artifact_digest,
    PPActionBytecodeV1Program *out,
    char *error_buf,
    size_t error_buf_size) {
    const char *syntax_digest;
    const char *snapshot_digest;
    const char *profile;
    const Atom *table;
    Atom **actions = NULL;
    uint32_t *arities = NULL;
    uint32_t declared_len;
    uint32_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!snapshot || !artifact || !out ||
        !reflected_head(artifact, "prepared-action-artifact-v1", 5u) ||
        !(syntax_digest = reflected_string(artifact->expr.elems[1])) ||
        !(snapshot_digest = reflected_string(artifact->expr.elems[2])) ||
        !(profile = reflected_string(artifact->expr.elems[3])) ||
        !reflected_u32(artifact->expr.elems[4], &declared_len) ||
        strcmp(syntax_digest, snapshot->syntax_digest) != 0 ||
        strcmp(snapshot_digest, snapshot->artifact_digest) != 0 ||
        strcmp(profile, snapshot->profile) != 0 ||
        declared_len != snapshot->slr.production_len) {
        return reflected_error(
            error_buf, error_buf_size,
            "reflected action artifact does not name this parser snapshot");
    }
    table = artifact->expr.elems[5];
    if (!table || table->kind != ATOM_EXPR || !table->expr.elems ||
        table->expr.len != (uint64_t)declared_len + 1u ||
        !atom_is_symbol(table->expr.elems[0], "reflected-action-table-v1")) {
        return reflected_error(
            error_buf, error_buf_size,
            "reflected action table has incomplete dense coverage");
    }
    actions = calloc(declared_len ? declared_len : 1u, sizeof(*actions));
    arities = calloc(declared_len ? declared_len : 1u, sizeof(*arities));
    if (!actions || !arities)
        goto done;
    for (index = 0u; index < declared_len; index++) {
        const Atom *row = table->expr.elems[index + 1u];
        const Atom *context;
        PPReflectedKindV1 output_kind;
        uint32_t production;
        uint64_t slot;
        if (!reflected_head(row, "reflected-action-row-v1", 4u) ||
            !reflected_u32(row->expr.elems[1], &production) ||
            production != index ||
            !(context = row->expr.elems[2]) || context->kind != ATOM_EXPR ||
            (context->expr.len > 0u && !context->expr.elems) ||
            context->expr.len != snapshot->slr.productions[index].rhs_len ||
            context->expr.len > UINT32_MAX ||
            !reflected_kind(row->expr.elems[3], &output_kind)) {
            reflected_error(
                error_buf, error_buf_size,
                "reflected action row identity or context is malformed");
            goto done;
        }
        for (slot = 0u; slot < context->expr.len; slot++) {
            PPReflectedKindV1 ignored;
            if (!reflected_kind(context->expr.elems[slot], &ignored)) {
                reflected_error(
                    error_buf, error_buf_size,
                    "reflected action context has an unknown sort");
                goto done;
            }
        }
        if (!reflected_action_typed(
                row->expr.elems[4], context->expr.elems,
                (uint32_t)context->expr.len, output_kind, 4096u)) {
            reflected_error(
                error_buf, error_buf_size,
                "reflected action row fails the independent sort checker");
            goto done;
        }
        actions[index] = row->expr.elems[4];
        arities[index] = (uint32_t)context->expr.len;
    }
    if (!pp_action_bytecode_v1_program_build_indexed(
            actions, arities, declared_len, snapshot->artifact_digest,
            PP_REFLECTED_ACTION_COMPILER_DIGEST_V1,
            artifact_digest, out,
            error_buf, error_buf_size)) {
        goto done;
    }
    ok = true;

done:
    free(actions);
    free(arities);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0')
        reflected_error(error_buf, error_buf_size,
                        "cannot allocate reflected action table");
    return ok;
}
