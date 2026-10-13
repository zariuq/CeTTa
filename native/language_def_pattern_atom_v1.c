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

static const char *const pattern_collection_names[] = {
    "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec",
    "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashBag",
    "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashSet",
};

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

static bool pattern_atom_natural(const Atom *source, char **out,
                                  PatternAtomContextV1 *context) {
    char digits[32];
    const char *text = NULL;
    if (source && source->kind == ATOM_GROUNDED) {
        if (source->ground.gkind == GV_INT && source->ground.ival >= 0) {
            snprintf(digits, sizeof(digits), "%lld", (long long)source->ground.ival);
            text = digits;
        } else if (source->ground.gkind == GV_BIGINT) {
            text = atom_bigint_cstr(source);
        }
    }
    if (!text || !*text)
        return pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                                 "Pattern index or arity must be a natural number");
    for (const char *p = text; *p; ++p) {
        if (!pattern_atom_take_work(context, 0u)) return false;
        if (*p < '0' || *p > '9')
            return pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                                     "Pattern index or arity must be a natural number");
    }
    *out = cetta_bigint_canonicalize_owned(text);
    return *out != NULL || pattern_atom_fail(context,
        CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE, "Pattern natural allocation failed");
}

static bool pattern_atom_optional(const Atom *source, const char *none,
                                   const char *some, CettaLdOptionalTextV1 *out,
                                   PatternAtomContextV1 *context) {
    if (!pattern_atom_take_work(context, 0u)) return false;
    if (atom_is_symbol((Atom *)source, none)) return true;
    if (!pattern_atom_expr(source, some, 1u))
        return pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                                 "Malformed optional Pattern name");
    if (!pattern_atom_text(source->expr.elems[1], &out->value, context)) return false;
    out->present = true;
    return true;
}

static bool pattern_atom_names(const Atom *source, CettaLdTextListV1 *out,
                                PatternAtomContextV1 *context) {
    const Atom *cursor = source;
    uint32_t len = 0u;
    while (!atom_is_symbol((Atom *)cursor, "LNil")) {
        if (!pattern_atom_take_work(context, 0u)) return false;
        if (!pattern_atom_expr(cursor, "LCons", 2u))
            return pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                                     "Pattern names must be an LCons/LNil list");
        if (len == UINT32_MAX)
            return pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_RESOURCE_LIMIT,
                                     "Too many Pattern binder names");
        ++len;
        cursor = cursor->expr.elems[2];
    }
    if (len) {
        out->items = calloc(len, sizeof(*out->items));
        if (!out->items) return pattern_atom_fail(context,
            CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE, "Pattern names allocation failed");
    }
    for (cursor = source; out->len < len; cursor = cursor->expr.elems[2]) {
        if (!pattern_atom_text(cursor->expr.elems[1], &out->items[out->len], context)) return false;
        ++out->len;
    }
    return true;
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
    if (!out || !source)
        return pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                                 "Pattern child is missing");
    if (!pattern_atom_take_work(context, depth))
        return false;
    if (pattern_atom_expr(source, "Var", 1u)) {
        result.kind = CETTA_LD_PATTERN_BVAR_V1;
        if (!pattern_atom_natural(source->expr.elems[1], &result.as.bvar_decimal, context))
            goto fail;
    } else if (pattern_atom_expr(source, "FVar", 1u)) {
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
    } else if (pattern_atom_expr(source, "PLam", 2u)) {
        result.kind = CETTA_LD_PATTERN_LAMBDA_V1;
        result.as.lambda.body = calloc(1u, sizeof(*result.as.lambda.body));
        if (!result.as.lambda.body) goto allocation_fail;
        if (!pattern_atom_optional(source->expr.elems[1], "BNone", "BSome",
                &result.as.lambda.binder, context) ||
            !pattern_atom_decode_inner(result.as.lambda.body, source->expr.elems[2], context, depth + 1u))
            goto fail;
    } else if (pattern_atom_expr(source, "PMultiLam", 3u)) {
        result.kind = CETTA_LD_PATTERN_MULTI_LAMBDA_V1;
        result.as.multi_lambda.body = calloc(1u, sizeof(*result.as.multi_lambda.body));
        if (!result.as.multi_lambda.body) goto allocation_fail;
        if (!pattern_atom_natural(source->expr.elems[1], &result.as.multi_lambda.arity_decimal, context) ||
            !pattern_atom_names(source->expr.elems[2], &result.as.multi_lambda.binders, context) ||
            !pattern_atom_decode_inner(result.as.multi_lambda.body, source->expr.elems[3], context, depth + 1u))
            goto fail;
    } else if (pattern_atom_expr(source, "PSubst", 2u)) {
        result.kind = CETTA_LD_PATTERN_SUBST_V1;
        result.as.subst.body = calloc(1u, sizeof(*result.as.subst.body));
        result.as.subst.replacement = calloc(1u, sizeof(*result.as.subst.replacement));
        if (!result.as.subst.body || !result.as.subst.replacement) goto allocation_fail;
        if (!pattern_atom_decode_inner(result.as.subst.body, source->expr.elems[1], context, depth + 1u) ||
            !pattern_atom_decode_inner(result.as.subst.replacement, source->expr.elems[2], context, depth + 1u))
            goto fail;
    } else if (pattern_atom_expr(source, "PCollection", 3u)) {
        result.kind = CETTA_LD_PATTERN_COLLECTION_V1;
        const Atom *kind = source->expr.elems[1];
        size_t i = 0u;
        if (kind && kind->kind == ATOM_GROUNDED && kind->ground.gkind == GV_STRING && kind->ground.sval) {
            for (; i < 3u; ++i)
                if (!strcmp(kind->ground.sval, pattern_collection_names[i])) break;
        } else i = 3u;
        if (i == 3u) {
            (void)pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                                    "Unknown Pattern collection kind");
            goto fail;
        }
        result.as.collection.collection_type = (CettaLdCollectionTypeV1)i;
        if (!pattern_atom_list_decode(&result.as.collection.elements, source->expr.elems[2], context, depth + 1u) ||
            !pattern_atom_optional(source->expr.elems[3], "RNone", "RSome", &result.as.collection.rest, context))
            goto fail;
    } else {
        (void)pattern_atom_fail(
            context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
            "Unknown or malformed LanguageDef Pattern constructor");
        goto fail;
    }
    *out = result;
    return true;

allocation_fail:
    (void)pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE,
                            "Pattern child allocation failed");
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

static Atom *pattern_atom_encode_text(Arena *arena, const CettaLdTextV1 *source,
                                      PatternAtomContextV1 *context) {
    if (!source || (source->len && (!source->bytes || memchr(source->bytes, 0, source->len)))) {
        (void)pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                                "Pattern text is not representable by an Atom string");
        return NULL;
    }
    char *text = malloc((size_t)source->len + 1u);
    if (!text) {
        (void)pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE,
                                "Pattern text encoding allocation failed");
        return NULL;
    }
    if (source->len) memcpy(text, source->bytes, source->len);
    text[source->len] = '\0';
    Atom *result = atom_string(arena, text);
    free(text);
    return result;
}

static Atom *pattern_atom_encode_natural(Arena *arena, const char *digits,
                                         PatternAtomContextV1 *context) {
    if (!digits || !*digits) goto malformed;
    for (const char *p = digits; *p; ++p) {
        if (!pattern_atom_take_work(context, 0u)) return NULL;
        if (*p < '0' || *p > '9') goto malformed;
    }
    return atom_bigint(arena, digits);
malformed:
    (void)pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                            "Malformed Pattern natural number");
    return NULL;
}

static Atom *pattern_atom_encode_optional(Arena *arena, const CettaLdOptionalTextV1 *source,
                                          const char *none, const char *some,
                                          PatternAtomContextV1 *context) {
    if (!pattern_atom_take_work(context, 0u)) return NULL;
    if (!source->present) return atom_symbol(arena, none);
    Atom *text = pattern_atom_encode_text(arena, &source->value, context);
    return text ? atom_expr2(arena, atom_symbol(arena, some), text) : NULL;
}

static Atom *pattern_atom_encode_inner(Arena *, const CettaLdPatternV1 *, PatternAtomContextV1 *, uint32_t);

static Atom *pattern_atom_encode_list(Arena *arena, const CettaLdPatternListV1 *source,
                                      PatternAtomContextV1 *context, uint32_t depth) {
    if (source->len && !source->items) {
        (void)pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                                "Pattern list has no elements");
        return NULL;
    }
    Atom *tail = atom_symbol(arena, "LNil");
    for (uint32_t i = source->len; i > 0u; --i) {
        Atom *item = pattern_atom_encode_inner(arena, &source->items[i - 1u], context, depth + 1u);
        if (!item) return NULL;
        tail = atom_expr3(arena, atom_symbol(arena, "LCons"), item, tail);
    }
    return tail;
}

static Atom *pattern_atom_encode_inner(Arena *arena,
                                       const CettaLdPatternV1 *source,
                                       PatternAtomContextV1 *context,
                                       uint32_t depth) {
    if (!arena || !source) {
        (void)pattern_atom_fail(context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
                                "Pattern child is missing");
        return NULL;
    }
    if (!pattern_atom_take_work(context, depth))
        return NULL;
    if (source->kind == CETTA_LD_PATTERN_BVAR_V1) {
        Atom *index = pattern_atom_encode_natural(arena, source->as.bvar_decimal, context);
        return index ? atom_expr2(arena, atom_symbol(arena, "Var"), index) : NULL;
    }
    if (source->kind == CETTA_LD_PATTERN_FVAR_V1) {
        Atom *name = pattern_atom_encode_text(arena, &source->as.fvar, context);
        return name ? atom_expr2(arena, atom_symbol(arena, "FVar"), name) : NULL;
    }
    if (source->kind == CETTA_LD_PATTERN_APPLY_V1) {
        Atom *head = pattern_atom_encode_text(arena, &source->as.apply.head, context);
        Atom *tail = head ? pattern_atom_encode_list(arena, &source->as.apply.arguments, context, depth) : NULL;
        return tail ? atom_expr3(arena, atom_symbol(arena, "PApp"), head, tail) : NULL;
    }
    if (source->kind == CETTA_LD_PATTERN_LAMBDA_V1) {
        Atom *binder = pattern_atom_encode_optional(arena, &source->as.lambda.binder, "BNone", "BSome", context);
        Atom *body = binder ? pattern_atom_encode_inner(arena, source->as.lambda.body, context, depth + 1u) : NULL;
        return body ? atom_expr3(arena, atom_symbol(arena, "PLam"), binder, body) : NULL;
    }
    if (source->kind == CETTA_LD_PATTERN_MULTI_LAMBDA_V1) {
        const CettaLdTextListV1 *names = &source->as.multi_lambda.binders;
        if (names->len && !names->items) goto malformed;
        Atom *arity = pattern_atom_encode_natural(arena, source->as.multi_lambda.arity_decimal, context);
        if (!arity) return NULL;
        Atom *tail = atom_symbol(arena, "LNil");
        for (uint32_t i = names->len; i > 0u; --i) {
            if (!pattern_atom_take_work(context, depth)) return NULL;
            Atom *name = pattern_atom_encode_text(arena, &names->items[i - 1u], context);
            if (!name) return NULL;
            tail = atom_expr3(arena, atom_symbol(arena, "LCons"), name, tail);
        }
        Atom *body = pattern_atom_encode_inner(arena, source->as.multi_lambda.body, context, depth + 1u);
        Atom *parts[] = {atom_symbol(arena, "PMultiLam"), arity, tail, body};
        return body ? atom_expr(arena, parts, 4u) : NULL;
    }
    if (source->kind == CETTA_LD_PATTERN_SUBST_V1) {
        Atom *body = pattern_atom_encode_inner(arena, source->as.subst.body, context, depth + 1u);
        Atom *replacement = body ? pattern_atom_encode_inner(arena, source->as.subst.replacement, context, depth + 1u) : NULL;
        return replacement ? atom_expr3(arena, atom_symbol(arena, "PSubst"), body, replacement) : NULL;
    }
    if (source->kind == CETTA_LD_PATTERN_COLLECTION_V1) {
        unsigned kind = source->as.collection.collection_type;
        if (kind >= 3u) goto malformed;
        Atom *elements = pattern_atom_encode_list(arena, &source->as.collection.elements, context, depth);
        Atom *rest = elements ? pattern_atom_encode_optional(arena, &source->as.collection.rest, "RNone", "RSome", context) : NULL;
        Atom *parts[] = {atom_symbol(arena, "PCollection"), atom_string(arena, pattern_collection_names[kind]), elements, rest};
        return rest ? atom_expr(arena, parts, 4u) : NULL;
    }
malformed:
    (void)pattern_atom_fail(
        context, CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
        "Malformed or unknown LanguageDef Pattern value");
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
    if (!result && context.status == CETTA_LD_PATTERN_ATOM_V1_OK)
        (void)pattern_atom_fail(&context, CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE,
                                "Pattern Atom encoding allocation failed");
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
