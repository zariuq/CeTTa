#include "language_def_pattern_atom_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t depth_limit;
    uint64_t remaining_work;
    CettaLdPatternAtomV1Status status;
    char *error_buf;
    size_t error_buf_size;
} PatternAtomContextV1;

static void pattern_atom_set_error(PatternAtomContextV1 *context,
                                   const char *format, ...) {
    va_list arguments;
    if (!context || !context->error_buf || context->error_buf_size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(context->error_buf, context->error_buf_size,
                    format, arguments);
    va_end(arguments);
}

static bool pattern_atom_fail(PatternAtomContextV1 *context,
                              CettaLdPatternAtomV1Status status,
                              const char *message) {
    if (context) {
        context->status = status;
        pattern_atom_set_error(context, "%s", message);
    }
    return false;
}

static bool pattern_atom_take_work(PatternAtomContextV1 *context,
                                   uint32_t depth) {
    if (!context)
        return false;
    if (depth > context->depth_limit)
        return pattern_atom_fail(
            context, CETTA_LD_PATTERN_ATOM_V1_RESOURCE_LIMIT,
            "Pattern wire exceeds the configured depth limit");
    if (context->remaining_work == 0u)
        return pattern_atom_fail(
            context, CETTA_LD_PATTERN_ATOM_V1_RESOURCE_LIMIT,
            "Pattern wire exhausted the configured work limit");
    context->remaining_work--;
    return true;
}

static bool pattern_atom_text(const Atom *source, CettaLdTextV1 *out,
                              PatternAtomContextV1 *context) {
    const char *text = NULL;
    size_t len;
    CettaLdTextV1 result = {0};

    if (!source || !out)
        return pattern_atom_fail(
            context, CETTA_LD_PATTERN_ATOM_V1_BAD_ARGUMENT,
            "Pattern text decode received an invalid argument");
    if (source->kind == ATOM_GROUNDED &&
        source->ground.gkind == GV_STRING) {
        text = source->ground.sval;
    } else if (source->kind == ATOM_SYMBOL) {
        text = atom_name_cstr((Atom *)source);
    }
    if (!text)
        return pattern_atom_fail(
            context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
            "Pattern name must be a String or symbol");
    len = strlen(text);
    if (len > UINT32_MAX)
        return pattern_atom_fail(
            context, CETTA_LD_PATTERN_ATOM_V1_RESOURCE_LIMIT,
            "Pattern name exceeds the wire length limit");
    if (len > 0u) {
        result.bytes = malloc(len);
        if (!result.bytes)
            return pattern_atom_fail(
                context, CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE,
                "Pattern name allocation failed");
        memcpy(result.bytes, text, len);
    }
    result.len = (uint32_t)len;
    *out = result;
    return true;
}

static bool pattern_atom_expr(const Atom *source, const char *head,
                              CettaExprLen arity) {
    return source && source->kind == ATOM_EXPR &&
        source->expr.len == arity + 1u && source->expr.elems &&
        atom_is_symbol(source->expr.elems[0], head);
}

static bool pattern_atom_decode_inner(CettaLdPatternV1 *out,
                                      const Atom *source,
                                      PatternAtomContextV1 *context,
                                      uint32_t depth);

static bool pattern_atom_list_decode(CettaLdPatternListV1 *out,
                                     const Atom *source,
                                     PatternAtomContextV1 *context,
                                     uint32_t depth) {
    CettaLdPatternListV1 result = {0};
    const Atom *cursor = source;
    uint32_t len = 0u;
    uint32_t index;

    while (!atom_is_symbol((Atom *)cursor, "LNil")) {
        if (!pattern_atom_take_work(context, depth) ||
            !pattern_atom_expr(cursor, "LCons", 2u))
            goto malformed;
        if (len == UINT32_MAX)
            return pattern_atom_fail(
                context, CETTA_LD_PATTERN_ATOM_V1_RESOURCE_LIMIT,
                "Pattern list exceeds the wire length limit");
        len++;
        cursor = cursor->expr.elems[2];
    }
    if (len > 0u) {
        result.items = calloc(len, sizeof(*result.items));
        if (!result.items)
            return pattern_atom_fail(
                context, CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE,
                "Pattern list allocation failed");
    }
    result.len = len;
    cursor = source;
    for (index = 0u; index < len; index++) {
        if (!pattern_atom_decode_inner(
                &result.items[index], cursor->expr.elems[1],
                context, depth + 1u))
            goto fail;
        cursor = cursor->expr.elems[2];
    }
    *out = result;
    return true;

malformed:
    if (context && context->status == CETTA_LD_PATTERN_ATOM_V1_OK)
        (void)pattern_atom_fail(
            context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
            "Pattern list must be an LCons/LNil spine");
fail:
    for (index = 0u; index < result.len; index++)
        cetta_ld_pattern_v1_free(&result.items[index]);
    free(result.items);
    return false;
}

static bool pattern_atom_decode_inner(CettaLdPatternV1 *out,
                                      const Atom *source,
                                      PatternAtomContextV1 *context,
                                      uint32_t depth) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!out || !source || !pattern_atom_take_work(context, depth))
        return false;
    if (pattern_atom_expr(source, "FVar", 1u)) {
        result.kind = CETTA_LD_PATTERN_FVAR_V1;
        if (!pattern_atom_text(source->expr.elems[1], &result.as.fvar,
                               context))
            goto fail;
    } else if (pattern_atom_expr(source, "PApp", 2u)) {
        result.kind = CETTA_LD_PATTERN_APPLY_V1;
        if (!pattern_atom_text(source->expr.elems[1],
                               &result.as.apply.head, context) ||
            !pattern_atom_list_decode(
                &result.as.apply.arguments, source->expr.elems[2],
                context, depth + 1u))
            goto fail;
    } else {
        (void)pattern_atom_fail(
            context, CETTA_LD_PATTERN_ATOM_V1_UNSUPPORTED_PROFILE,
            "contextual runner accepts only PApp/FVar Pattern wires");
        goto fail;
    }
    *out = result;
    return true;

fail:
    cetta_ld_pattern_v1_free(&result);
    return false;
}

bool cetta_ld_pattern_atom_v1_decode(
    CettaLdPatternV1 *out, const Atom *source, uint32_t depth_limit,
    uint64_t work_limit, CettaLdPatternAtomV1Status *status,
    char *error_buf, size_t error_buf_size) {
    PatternAtomContextV1 context = {
        depth_limit, work_limit, CETTA_LD_PATTERN_ATOM_V1_OK,
        error_buf, error_buf_size};
    CettaLdPatternV1 result;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out || !source || work_limit == 0u) {
        context.status = CETTA_LD_PATTERN_ATOM_V1_BAD_ARGUMENT;
        pattern_atom_set_error(&context,
                               "invalid Pattern wire decode request");
        if (status)
            *status = context.status;
        return false;
    }
    cetta_ld_pattern_v1_init(&result);
    if (!pattern_atom_decode_inner(&result, source, &context, 0u)) {
        if (status)
            *status = context.status;
        return false;
    }
    cetta_ld_pattern_v1_free(out);
    *out = result;
    if (status)
        *status = CETTA_LD_PATTERN_ATOM_V1_OK;
    return true;
}

static Atom *pattern_atom_encode_inner(Arena *arena,
                                       const CettaLdPatternV1 *source,
                                       PatternAtomContextV1 *context,
                                       uint32_t depth) {
    Atom *arguments[2];

    if (!arena || !source || !pattern_atom_take_work(context, depth))
        return NULL;
    if (source->kind == CETTA_LD_PATTERN_FVAR_V1) {
        char *name = malloc((size_t)source->as.fvar.len + 1u);
        Atom *result;
        if (!name) {
            (void)pattern_atom_fail(
                context, CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE,
                "Pattern name encoding allocation failed");
            return NULL;
        }
        if (source->as.fvar.len > 0u && !source->as.fvar.bytes) {
            free(name);
            (void)pattern_atom_fail(
                context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                "Pattern name has no bytes");
            return NULL;
        }
        if (source->as.fvar.len > 0u)
            memcpy(name, source->as.fvar.bytes, source->as.fvar.len);
        name[source->as.fvar.len] = '\0';
        result = atom_expr2(arena, atom_symbol(arena, "FVar"),
                            atom_string(arena, name));
        free(name);
        return result;
    }
    if (source->kind == CETTA_LD_PATTERN_APPLY_V1) {
        Atom *tail = atom_symbol(arena, "LNil");
        char *head = malloc((size_t)source->as.apply.head.len + 1u);
        uint32_t index;
        if (!head) {
            (void)pattern_atom_fail(
                context, CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE,
                "Pattern head encoding allocation failed");
            return NULL;
        }
        if (source->as.apply.head.len > 0u &&
            !source->as.apply.head.bytes) {
            free(head);
            (void)pattern_atom_fail(
                context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                "Pattern head has no bytes");
            return NULL;
        }
        if (source->as.apply.head.len > 0u)
            memcpy(head, source->as.apply.head.bytes,
                   source->as.apply.head.len);
        head[source->as.apply.head.len] = '\0';
        for (index = source->as.apply.arguments.len; index > 0u; index--) {
            Atom *item = pattern_atom_encode_inner(
                arena, &source->as.apply.arguments.items[index - 1u],
                context, depth + 1u);
            if (!item) {
                free(head);
                return NULL;
            }
            tail = atom_expr3(arena, atom_symbol(arena, "LCons"),
                              item, tail);
        }
        arguments[0] = atom_string(arena, head);
        arguments[1] = tail;
        free(head);
        return atom_expr3(arena, atom_symbol(arena, "PApp"),
                          arguments[0], arguments[1]);
    }
    (void)pattern_atom_fail(
        context, CETTA_LD_PATTERN_ATOM_V1_UNSUPPORTED_PROFILE,
        "contextual runner produced a Pattern outside PApp/FVar");
    return NULL;
}

Atom *cetta_ld_pattern_atom_v1_encode(
    Arena *arena, const CettaLdPatternV1 *source, uint32_t depth_limit,
    uint64_t work_limit, CettaLdPatternAtomV1Status *status,
    char *error_buf, size_t error_buf_size) {
    PatternAtomContextV1 context = {
        depth_limit, work_limit, CETTA_LD_PATTERN_ATOM_V1_OK,
        error_buf, error_buf_size};
    Atom *result;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!arena || !source || work_limit == 0u) {
        context.status = CETTA_LD_PATTERN_ATOM_V1_BAD_ARGUMENT;
        pattern_atom_set_error(&context,
                               "invalid Pattern wire encode request");
        if (status)
            *status = context.status;
        return NULL;
    }
    result = pattern_atom_encode_inner(arena, source, &context, 0u);
    if (status)
        *status = result ? CETTA_LD_PATTERN_ATOM_V1_OK : context.status;
    return result;
}

const char *cetta_ld_pattern_atom_v1_status_name(
    CettaLdPatternAtomV1Status status) {
    switch (status) {
    case CETTA_LD_PATTERN_ATOM_V1_OK:
        return "PatternAtomOK";
    case CETTA_LD_PATTERN_ATOM_V1_BAD_ARGUMENT:
        return "PatternAtomBadArgument";
    case CETTA_LD_PATTERN_ATOM_V1_MALFORMED:
        return "PatternAtomMalformed";
    case CETTA_LD_PATTERN_ATOM_V1_UNSUPPORTED_PROFILE:
        return "PatternAtomUnsupportedProfile";
    case CETTA_LD_PATTERN_ATOM_V1_RESOURCE_LIMIT:
        return "PatternAtomResourceLimit";
    case CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE:
        return "PatternAtomAllocationFailure";
    }
    return "PatternAtomUnknownStatus";
}
