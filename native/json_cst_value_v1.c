#include "json_cst_value_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_JSON_CST_VALUE_V1_MUTATION
#define CETTA_JSON_CST_VALUE_V1_MUTATION 0
#endif

typedef struct {
    const CettaJsonElaborationPlanV1 *plan;
    Arena *arena;
    uint32_t work;
    uint32_t work_limit;
    uint32_t depth_limit;
    CettaJsonCstValueV1Status status;
    char *error;
    size_t error_size;
} JsonElabV1;

typedef struct {
    Atom **items;
    uint32_t len;
    uint32_t cap;
} JsonAtomVecV1;

typedef struct {
    uint32_t *items;
    uint32_t len;
    uint32_t cap;
} JsonScalarVecV1;

typedef struct {
    char *bytes;
    uint32_t len;
    uint32_t cap;
} JsonByteVecV1;

static bool json_elab_error(JsonElabV1 *ctx,
                            CettaJsonCstValueV1Status status,
                            const char *format, ...) {
    if (ctx) {
        ctx->status = status;
        if (ctx->error && ctx->error_size > 0u) {
            va_list arguments;
            va_start(arguments, format);
            (void)vsnprintf(ctx->error, ctx->error_size,
                            format, arguments);
            va_end(arguments);
        }
    }
    return false;
}

static bool json_elab_work(JsonElabV1 *ctx, uint32_t amount) {
    if (!ctx || amount > ctx->work_limit - ctx->work) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT,
            "JSON CST elaboration work limit exceeded");
    }
    ctx->work += amount;
    return true;
}

static bool json_atom_vec_push(JsonElabV1 *ctx, JsonAtomVecV1 *vec,
                               Atom *value) {
    Atom **next;
    uint32_t next_cap;
    if (!vec || !value || !json_elab_work(ctx, 1u)) return false;
    if (vec->len == vec->cap) {
        next_cap = vec->cap ? vec->cap * 2u : 8u;
        if (next_cap < vec->cap) {
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT,
                "JSON value contains too many occurrences");
        }
        next = (Atom **)realloc(
            vec->items, (size_t)next_cap * sizeof(*vec->items));
        if (!next) {
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_ALLOCATION_FAILURE,
                "out of memory retaining JSON occurrences");
        }
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len++] = value;
    return true;
}

static bool json_scalar_vec_push(JsonElabV1 *ctx, JsonScalarVecV1 *vec,
                                 uint32_t value) {
    uint32_t *next;
    uint32_t next_cap;
    if (!vec || !json_elab_work(ctx, 1u)) return false;
    if (vec->len == vec->cap) {
        next_cap = vec->cap ? vec->cap * 2u : 16u;
        if (next_cap < vec->cap) {
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT,
                "JSON string contains too many scalars");
        }
        next = (uint32_t *)realloc(
            vec->items, (size_t)next_cap * sizeof(*vec->items));
        if (!next) {
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_ALLOCATION_FAILURE,
                "out of memory retaining JSON string scalars");
        }
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len++] = value;
    return true;
}

static bool json_byte_vec_push(JsonElabV1 *ctx, JsonByteVecV1 *vec,
                               char value) {
    char *next;
    uint32_t next_cap;
    if (!vec || !json_elab_work(ctx, 1u)) return false;
    if (vec->len == vec->cap) {
        next_cap = vec->cap ? vec->cap * 2u : 32u;
        if (next_cap < vec->cap) {
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT,
                "JSON number lexeme is too long");
        }
        next = (char *)realloc(vec->bytes, (size_t)next_cap);
        if (!next) {
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_ALLOCATION_FAILURE,
                "out of memory retaining JSON number lexeme");
        }
        vec->bytes = next;
        vec->cap = next_cap;
    }
    vec->bytes[vec->len++] = value;
    return true;
}

static bool json_cst_view(Atom *term, const char **label,
                          uint32_t *start, uint32_t *stop,
                          Atom ***children, uint32_t *child_len) {
    Atom *head;
    Atom *name;
    Atom *start_term;
    Atom *stop_term;
    if (!term || term->kind != ATOM_EXPR || term->expr.len < 4u)
        return false;
    head = term->expr.elems[0];
    name = term->expr.elems[1];
    start_term = term->expr.elems[2];
    stop_term = term->expr.elems[3];
    if (!atom_is_symbol(head, "CstRuleV1") || !name ||
        name->kind != ATOM_GROUNDED || name->ground.gkind != GV_STRING ||
        !name->ground.sval || !start_term || !stop_term ||
        start_term->kind != ATOM_GROUNDED ||
        start_term->ground.gkind != GV_INT ||
        stop_term->kind != ATOM_GROUNDED ||
        stop_term->ground.gkind != GV_INT ||
        start_term->ground.ival < 0 || stop_term->ground.ival < 0 ||
        start_term->ground.ival > UINT32_MAX ||
        stop_term->ground.ival > UINT32_MAX ||
        start_term->ground.ival > stop_term->ground.ival) {
        return false;
    }
    if (label) *label = name->ground.sval;
    if (start) *start = (uint32_t)start_term->ground.ival;
    if (stop) *stop = (uint32_t)stop_term->ground.ival;
    if (children) *children = term->expr.elems + 4u;
    if (child_len) *child_len = (uint32_t)term->expr.len - 4u;
    return true;
}

static bool json_cst_is(JsonElabV1 *ctx, Atom *term,
                        CettaJsonElaborationOpV1 expected,
                        Atom ***children) {
    const char *actual = NULL;
    uint32_t child_len = 0u;
    const CettaJsonElaborationPlanEntryV1 *entry;
    if (!ctx || !ctx->plan ||
        !json_cst_view(term, &actual, NULL, NULL, children, &child_len))
        return false;
    entry = cetta_json_elaboration_plan_v1_find(ctx->plan, actual);
    return entry && entry->op == expected && child_len == entry->child_len;
}

static bool json_cst_is_spanned(JsonElabV1 *ctx, Atom *term,
                                CettaJsonElaborationOpV1 expected,
                                uint32_t *start, uint32_t *stop,
                                Atom ***children) {
    const char *actual = NULL;
    uint32_t child_len = 0u;
    const CettaJsonElaborationPlanEntryV1 *entry;
    if (!ctx || !ctx->plan ||
        !json_cst_view(term, &actual, start, stop, children, &child_len))
        return false;
    entry = cetta_json_elaboration_plan_v1_find(ctx->plan, actual);
    return entry && entry->op == expected && child_len == entry->child_len;
}

static bool json_lexical_scalar(JsonElabV1 *ctx, Atom *term,
                                uint32_t *out) {
    const char *label = NULL;
    Atom **children = NULL;
    uint32_t child_len = 0u;
    Atom *cp;
    Atom *value;
    if (!json_elab_work(ctx, 1u) ||
        !json_cst_view(
            term, &label, NULL, NULL, &children, &child_len)) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
            "expected a JSON lexical scalar CST node");
    }
    {
        const CettaJsonElaborationPlanEntryV1 *entry =
            cetta_json_elaboration_plan_v1_find(ctx->plan, label);
        if (!entry || entry->op != CETTA_JSON_ELAB_LEXICAL_SCALAR_V1 ||
            child_len != entry->child_len) {
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                "expected an admitted JSON lexical scalar CST node");
        }
    }
    cp = children[0];
    if (!cp || cp->kind != ATOM_EXPR || cp->expr.len != 2u ||
        !atom_is_symbol(cp->expr.elems[0], "cp")) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
            "JSON lexical node lacks a codepoint value");
    }
    value = cp->expr.elems[1];
    if (!value || value->kind != ATOM_GROUNDED ||
        value->ground.gkind != GV_INT || value->ground.ival < 0 ||
        value->ground.ival > 0x10ffff ||
        (value->ground.ival >= 0xd800 && value->ground.ival <= 0xdfff)) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
            "JSON lexical node carries a non-scalar codepoint");
    }
    *out = (uint32_t)value->ground.ival;
    return true;
}

static Atom *json_app1(Arena *arena, const char *head, Atom *argument) {
    Atom *items[2] = {atom_symbol(arena, head), argument};
    return atom_expr(arena, items, 2u);
}

static Atom *json_app2(Arena *arena, const char *head,
                       Atom *first, Atom *second) {
    Atom *items[3] = {atom_symbol(arena, head), first, second};
    return atom_expr(arena, items, 3u);
}

static Atom *json_app4(Arena *arena, const char *head,
                       Atom *first, Atom *second, Atom *third,
                       Atom *fourth) {
    Atom *items[5] = {
        atom_symbol(arena, head), first, second, third, fourth};
    return atom_expr(arena, items, 5u);
}

static const char *json_target_name(
    JsonElabV1 *ctx, CettaJsonTargetConstructorV1 constructor) {
    const char *name = ctx
        ? cetta_json_elaboration_plan_v1_target_name(ctx->plan, constructor)
        : NULL;
    if (!name) {
        json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                        "JSON target constructor is absent from the admitted plan");
    }
    return name;
}

static bool json_whitespace(JsonElabV1 *ctx, Atom *term, uint32_t depth) {
    Atom **children = NULL;
    uint32_t scalar;
    if (depth > ctx->depth_limit) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT,
            "JSON whitespace nesting limit exceeded");
    }
    if (!json_elab_work(ctx, 1u)) return false;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_WS_EMPTY_V1, NULL))
        return true;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_WS_CONS_V1, &children) ||
        !json_lexical_scalar(ctx, children[0], &scalar) ||
        (scalar != 0x20u && scalar != 0x09u &&
         scalar != 0x0au && scalar != 0x0du)) {
        if (ctx->status == CETTA_JSON_CST_VALUE_V1_OK) {
            json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                            "malformed JSON whitespace CST");
        }
        return false;
    }
    return json_whitespace(ctx, children[1], depth + 1u);
}

static Atom *json_scalar_list(JsonElabV1 *ctx,
                              const JsonScalarVecV1 *scalars) {
    Atom **items;
    Atom *list;
    uint32_t index;
    items = (Atom **)calloc(
        scalars->len ? scalars->len : 1u, sizeof(*items));
    if (!items) {
        json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_ALLOCATION_FAILURE,
                        "out of memory materializing JSON scalar list");
        return NULL;
    }
    for (index = 0u; index < scalars->len; index++) {
        Atom *cp_items[2] = {
            atom_symbol(ctx->arena, "cp"),
            atom_int(ctx->arena, (int64_t)scalars->items[index]),
        };
        items[index] = atom_expr(ctx->arena, cp_items, 2u);
    }
    list = atom_expr(ctx->arena, items, scalars->len);
    free(items);
    return list;
}

static bool json_string_chars(JsonElabV1 *ctx, Atom *term,
                              JsonScalarVecV1 *scalars,
                              uint32_t *pending_high,
                              uint32_t depth);

static bool json_string_char(JsonElabV1 *ctx, Atom *term,
                             JsonScalarVecV1 *scalars,
                             uint32_t *pending_high,
                             uint32_t depth) {
    Atom **children = NULL;
    uint32_t scalar;
    if (depth > ctx->depth_limit) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT,
            "JSON semantic nesting limit exceeded");
    }
    if (!json_elab_work(ctx, 1u)) return false;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_STRING_CHAR_PLAIN_V1,
                    &children)) {
        if (*pending_high != UINT32_MAX) {
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_INVALID_UNICODE_ESCAPE,
                "high surrogate is not followed by a low surrogate escape");
        }
        return json_lexical_scalar(ctx, children[0], &scalar) &&
            json_scalar_vec_push(ctx, scalars, scalar);
    }
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_STRING_CHAR_ESCAPE_V1,
                     &children)) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
            "expected a JSON string-character CST node");
    }
    term = children[0];
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_ESCAPE_SIMPLE_V1,
                    &children)) {
        uint32_t decoded;
        if (*pending_high != UINT32_MAX) {
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_INVALID_UNICODE_ESCAPE,
                "high surrogate is not followed by a low surrogate escape");
        }
        if (!json_lexical_scalar(ctx, children[0], &scalar)) return false;
        switch (scalar) {
        case '"': decoded = '"'; break;
        case '\\': decoded = '\\'; break;
        case '/': decoded = '/'; break;
        case 'b': decoded = 8u; break;
        case 'f': decoded = 12u; break;
        case 'n': decoded = 10u; break;
        case 'r': decoded = 13u; break;
        case 't': decoded = 9u; break;
        default:
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                "unknown JSON simple escape code");
        }
        return json_scalar_vec_push(ctx, scalars, decoded);
    }
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_ESCAPE_UNICODE_V1,
                    &children)) {
        uint32_t unit = 0u;
        uint32_t index;
        for (index = 0u; index < 4u; index++) {
            uint32_t digit;
            if (!json_lexical_scalar(ctx, children[index], &scalar))
                return false;
            if (scalar >= '0' && scalar <= '9') digit = scalar - '0';
            else if (scalar >= 'a' && scalar <= 'f')
                digit = scalar - 'a' + 10u;
            else if (scalar >= 'A' && scalar <= 'F')
                digit = scalar - 'A' + 10u;
            else {
                return json_elab_error(
                    ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                    "non-hex scalar in JSON Unicode escape CST");
            }
            unit = (unit << 4u) | digit;
        }
        if (unit >= 0xd800u && unit <= 0xdbffu) {
            if (*pending_high != UINT32_MAX) {
                return json_elab_error(
                    ctx, CETTA_JSON_CST_VALUE_V1_INVALID_UNICODE_ESCAPE,
                    "two high surrogates occur without a low surrogate");
            }
            *pending_high = unit;
            return true;
        }
        if (unit >= 0xdc00u && unit <= 0xdfffu) {
            uint32_t combined;
            if (*pending_high == UINT32_MAX) {
                return json_elab_error(
                    ctx, CETTA_JSON_CST_VALUE_V1_INVALID_UNICODE_ESCAPE,
                    "low surrogate occurs without a high surrogate");
            }
            combined = 0x10000u +
                ((*pending_high - 0xd800u) << 10u) + (unit - 0xdc00u);
            *pending_high = UINT32_MAX;
            return json_scalar_vec_push(ctx, scalars, combined);
        }
        if (*pending_high != UINT32_MAX) {
            return json_elab_error(
                ctx, CETTA_JSON_CST_VALUE_V1_INVALID_UNICODE_ESCAPE,
                "high surrogate is not followed by a low surrogate escape");
        }
        return json_scalar_vec_push(ctx, scalars, unit);
    }
    return json_elab_error(
        ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
        "expected a JSON escape CST node");
}

static bool json_string_chars(JsonElabV1 *ctx, Atom *term,
                              JsonScalarVecV1 *scalars,
                              uint32_t *pending_high,
                              uint32_t depth) {
    Atom **children = NULL;
    if (depth > ctx->depth_limit) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT,
            "JSON string nesting limit exceeded");
    }
    if (!json_elab_work(ctx, 1u)) return false;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_STRING_CHARS_EMPTY_V1,
                    NULL))
        return true;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_STRING_CHARS_CONS_V1,
                     &children)) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
            "expected a JSON string-character list CST node");
    }
    return json_string_char(
               ctx, children[0], scalars, pending_high, depth + 1u) &&
        json_string_chars(
               ctx, children[1], scalars, pending_high, depth + 1u);
}

static Atom *json_string_value(JsonElabV1 *ctx, Atom *term,
                               uint32_t depth) {
    JsonScalarVecV1 scalars = {0};
    Atom **children = NULL;
    Atom *list;
    Atom *value = NULL;
    uint32_t pending_high = UINT32_MAX;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_STRING_V1, &children) ||
        !json_string_chars(
            ctx, children[0], &scalars, &pending_high, depth + 1u)) {
        goto done;
    }
    if (pending_high != UINT32_MAX) {
        json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_INVALID_UNICODE_ESCAPE,
            "JSON string ends with an unpaired high surrogate");
        goto done;
    }
    list = json_scalar_list(ctx, &scalars);
    if (list) {
        const char *name = json_target_name(ctx, CETTA_JSON_TARGET_STRING_V1);
        if (name) value = json_app1(ctx->arena, name, list);
    }

done:
    free(scalars.items);
    return value;
}

static bool json_number_digits(JsonElabV1 *ctx, Atom *term,
                               JsonByteVecV1 *bytes, uint32_t depth) {
    Atom **children = NULL;
    uint32_t scalar;
    if (depth > ctx->depth_limit) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT,
            "JSON number nesting limit exceeded");
    }
    if (!json_elab_work(ctx, 1u)) return false;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_DIGITS_EMPTY_V1, NULL))
        return true;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_DIGITS_CONS_V1,
                     &children) ||
        !json_lexical_scalar(ctx, children[0], &scalar) ||
        scalar < '0' || scalar > '9' ||
        !json_byte_vec_push(ctx, bytes, (char)scalar)) {
        if (ctx->status == CETTA_JSON_CST_VALUE_V1_OK) {
            json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                            "malformed JSON digit-list CST");
        }
        return false;
    }
    return json_number_digits(ctx, children[1], bytes, depth + 1u);
}

static bool json_number_integer(JsonElabV1 *ctx, Atom *term,
                                JsonByteVecV1 *bytes, uint32_t depth) {
    Atom **children = NULL;
    uint32_t scalar;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_INT_ZERO_V1, NULL))
        return json_byte_vec_push(ctx, bytes, '0');
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_INT_NONZERO_V1,
                     &children) ||
        !json_lexical_scalar(ctx, children[0], &scalar) ||
        scalar < '1' || scalar > '9' ||
        !json_byte_vec_push(ctx, bytes, (char)scalar)) {
        if (ctx->status == CETTA_JSON_CST_VALUE_V1_OK) {
            json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                            "malformed JSON integer CST");
        }
        return false;
    }
    return json_number_digits(ctx, children[1], bytes, depth + 1u);
}

static bool json_number_fraction(JsonElabV1 *ctx, Atom *term,
                                 JsonByteVecV1 *bytes, uint32_t depth) {
    Atom **children = NULL;
    uint32_t scalar;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_FRAC_NONE_V1, NULL))
        return true;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_FRAC_SOME_V1, &children) ||
        !json_cst_is(ctx, children[0], CETTA_JSON_ELAB_FRAC_V1,
                     &children) ||
        !json_byte_vec_push(ctx, bytes, '.') ||
        !json_lexical_scalar(ctx, children[0], &scalar) ||
        scalar < '0' || scalar > '9' ||
        !json_byte_vec_push(ctx, bytes, (char)scalar)) {
        if (ctx->status == CETTA_JSON_CST_VALUE_V1_OK) {
            json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                            "malformed JSON fraction CST");
        }
        return false;
    }
    return json_number_digits(ctx, children[1], bytes, depth + 1u);
}

static bool json_number_exponent(JsonElabV1 *ctx, Atom *term,
                                 JsonByteVecV1 *bytes, uint32_t depth) {
    Atom **children = NULL;
    Atom **exp_children = NULL;
    Atom **sign_children = NULL;
    uint32_t scalar;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_EXP_NONE_V1, NULL))
        return true;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_EXP_SOME_V1, &children) ||
        !json_cst_is(ctx, children[0], CETTA_JSON_ELAB_EXP_V1,
                     &exp_children) ||
        !json_lexical_scalar(ctx, exp_children[0], &scalar) ||
        (scalar != 'e' && scalar != 'E') ||
        !json_byte_vec_push(ctx, bytes, (char)scalar)) {
        if (ctx->status == CETTA_JSON_CST_VALUE_V1_OK) {
            json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                            "malformed JSON exponent CST");
        }
        return false;
    }
    if (json_cst_is(ctx, exp_children[1], CETTA_JSON_ELAB_SIGN_SOME_V1,
                    &sign_children)) {
        if (!json_lexical_scalar(ctx, sign_children[0], &scalar) ||
            (scalar != '+' && scalar != '-') ||
            !json_byte_vec_push(ctx, bytes, (char)scalar)) {
            return false;
        }
    } else if (!json_cst_is(ctx, exp_children[1],
                            CETTA_JSON_ELAB_SIGN_NONE_V1, NULL)) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
            "malformed JSON exponent sign CST");
    }
    if (!json_lexical_scalar(ctx, exp_children[2], &scalar) ||
        scalar < '0' || scalar > '9' ||
        !json_byte_vec_push(ctx, bytes, (char)scalar)) {
        return false;
    }
    return json_number_digits(ctx, exp_children[3], bytes, depth + 1u);
}

static Atom *json_number_value(JsonElabV1 *ctx, Atom *term,
                               uint32_t depth) {
    JsonByteVecV1 bytes = {0};
    Atom **children = NULL;
    Atom *value = NULL;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_NUMBER_V1, &children)) {
        json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                        "expected a JSON number CST node");
        goto done;
    }
    if (json_cst_is(ctx, children[0], CETTA_JSON_ELAB_MINUS_SOME_V1,
                    NULL)) {
        if (!json_byte_vec_push(ctx, &bytes, '-')) goto done;
    } else if (!json_cst_is(ctx, children[0],
                            CETTA_JSON_ELAB_MINUS_NONE_V1, NULL)) {
        json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                        "malformed JSON minus CST");
        goto done;
    }
    if (!json_number_integer(ctx, children[1], &bytes, depth + 1u) ||
        !json_number_fraction(ctx, children[2], &bytes, depth + 1u) ||
        !json_number_exponent(ctx, children[3], &bytes, depth + 1u) ||
        !json_byte_vec_push(ctx, &bytes, '\0')) {
        goto done;
    }
    {
        const char *name = json_target_name(ctx, CETTA_JSON_TARGET_NUMBER_V1);
        if (name) {
            value = json_app1(ctx->arena, name,
                              atom_string(ctx->arena, bytes.bytes));
        }
    }

done:
    free(bytes.bytes);
    return value;
}

static Atom *json_value(JsonElabV1 *ctx, Atom *term, uint32_t depth);

static Atom *json_member(JsonElabV1 *ctx, Atom *term,
                         uint32_t occurrence, uint32_t depth) {
    Atom **children = NULL;
    Atom *key;
    Atom *value;
    uint32_t start = 0u;
    uint32_t stop = 0u;
    if (!json_cst_is_spanned(
            ctx, term, CETTA_JSON_ELAB_MEMBER_V1,
            &start, &stop, &children)) {
        json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                        "expected a JSON member CST node");
        return NULL;
    }
    key = json_string_value(ctx, children[0], depth + 1u);
    value = key && json_whitespace(ctx, children[1], depth + 1u) &&
            json_whitespace(ctx, children[2], depth + 1u)
        ? json_value(ctx, children[3], depth + 1u)
        : NULL;
    if (CETTA_JSON_CST_VALUE_V1_MUTATION == 1 && occurrence == 0u)
        occurrence = 1u;
    if (key && value) {
        const char *member_name =
            json_target_name(ctx, CETTA_JSON_TARGET_MEMBER_V1);
        const char *span_name =
            json_target_name(ctx, CETTA_JSON_TARGET_SOURCE_SPAN_V1);
        if (member_name && span_name) {
            return json_app4(
                ctx->arena, member_name,
                atom_int(ctx->arena, (int64_t)occurrence), key, value,
                json_app2(ctx->arena, span_name,
                          atom_int(ctx->arena, (int64_t)start),
                          atom_int(ctx->arena, (int64_t)stop)));
        }
    }
    return NULL;
}

static bool json_member_tail(JsonElabV1 *ctx, Atom *term,
                             JsonAtomVecV1 *members, uint32_t depth) {
    Atom **children = NULL;
    Atom *member;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_MEMBER_TAIL_EMPTY_V1,
                    NULL))
        return true;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_MEMBER_TAIL_CONS_V1,
                     &children)) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
            "expected a JSON member-tail CST node");
    }
    member = json_whitespace(ctx, children[0], depth + 1u) &&
             json_whitespace(ctx, children[1], depth + 1u)
        ? json_member(ctx, children[2], members->len, depth + 1u)
        : NULL;
    return member && json_atom_vec_push(ctx, members, member) &&
        json_member_tail(ctx, children[3], members, depth + 1u);
}

static bool json_members(JsonElabV1 *ctx, Atom *term,
                         JsonAtomVecV1 *members, uint32_t depth) {
    Atom **children = NULL;
    Atom *member;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_MEMBERS_NONE_V1, NULL))
        return true;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_MEMBERS_SOME_V1,
                     &children) ||
        !json_cst_is(ctx, children[0], CETTA_JSON_ELAB_MEMBERS_V1,
                     &children)) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
            "expected a JSON members CST node");
    }
    member = json_member(ctx, children[0], 0u, depth + 1u);
    return member && json_atom_vec_push(ctx, members, member) &&
        json_member_tail(ctx, children[1], members, depth + 1u);
}

static Atom *json_object_value(JsonElabV1 *ctx, Atom *term,
                               uint32_t depth) {
    JsonAtomVecV1 members = {0};
    Atom **children = NULL;
    Atom *list;
    Atom *value = NULL;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_OBJECT_V1, &children) ||
        !json_whitespace(ctx, children[0], depth + 1u) ||
        !json_whitespace(ctx, children[2], depth + 1u) ||
        !json_members(ctx, children[1], &members, depth + 1u)) {
        goto done;
    }
    list = atom_expr(ctx->arena, members.items, members.len);
    {
        const char *name = json_target_name(ctx, CETTA_JSON_TARGET_OBJECT_V1);
        if (name) value = json_app1(ctx->arena, name, list);
    }

done:
    free(members.items);
    return value;
}

static bool json_element_tail(JsonElabV1 *ctx, Atom *term,
                              JsonAtomVecV1 *values, uint32_t depth) {
    Atom **children = NULL;
    Atom *value;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_ELEMENT_TAIL_EMPTY_V1,
                    NULL))
        return true;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_ELEMENT_TAIL_CONS_V1,
                     &children)) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
            "expected a JSON element-tail CST node");
    }
    value = json_whitespace(ctx, children[0], depth + 1u) &&
            json_whitespace(ctx, children[1], depth + 1u)
        ? json_value(ctx, children[2], depth + 1u)
        : NULL;
    return value && json_atom_vec_push(ctx, values, value) &&
        json_element_tail(ctx, children[3], values, depth + 1u);
}

static bool json_elements(JsonElabV1 *ctx, Atom *term,
                          JsonAtomVecV1 *values, uint32_t depth) {
    Atom **children = NULL;
    Atom *value;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_ELEMENTS_NONE_V1, NULL))
        return true;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_ELEMENTS_SOME_V1,
                     &children) ||
        !json_cst_is(ctx, children[0], CETTA_JSON_ELAB_ELEMENTS_V1,
                     &children)) {
        return json_elab_error(
            ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
            "expected a JSON elements CST node");
    }
    value = json_value(ctx, children[0], depth + 1u);
    return value && json_atom_vec_push(ctx, values, value) &&
        json_element_tail(ctx, children[1], values, depth + 1u);
}

static Atom *json_array_value(JsonElabV1 *ctx, Atom *term,
                              uint32_t depth) {
    JsonAtomVecV1 values = {0};
    Atom **children = NULL;
    Atom *list;
    Atom *value = NULL;
    if (!json_cst_is(ctx, term, CETTA_JSON_ELAB_ARRAY_V1, &children) ||
        !json_whitespace(ctx, children[0], depth + 1u) ||
        !json_whitespace(ctx, children[2], depth + 1u) ||
        !json_elements(ctx, children[1], &values, depth + 1u)) {
        goto done;
    }
    list = atom_expr(ctx->arena, values.items, values.len);
    {
        const char *name = json_target_name(ctx, CETTA_JSON_TARGET_ARRAY_V1);
        if (name) value = json_app1(ctx->arena, name, list);
    }

done:
    free(values.items);
    return value;
}

static Atom *json_value(JsonElabV1 *ctx, Atom *term, uint32_t depth) {
    Atom **children = NULL;
    if (depth > ctx->depth_limit) {
        json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT,
                        "JSON semantic nesting limit exceeded");
        return NULL;
    }
    if (!json_elab_work(ctx, 1u)) return NULL;
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_VALUE_NULL_V1, NULL)) {
        const char *name = json_target_name(ctx, CETTA_JSON_TARGET_NULL_V1);
        return name ? atom_symbol(ctx->arena, name) : NULL;
    }
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_VALUE_FALSE_V1, NULL)) {
        const char *name = json_target_name(ctx, CETTA_JSON_TARGET_BOOL_V1);
        return name
            ? json_app1(ctx->arena, name, atom_false(ctx->arena))
            : NULL;
    }
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_VALUE_TRUE_V1, NULL)) {
        const char *name = json_target_name(ctx, CETTA_JSON_TARGET_BOOL_V1);
        return name
            ? json_app1(ctx->arena, name, atom_true(ctx->arena))
            : NULL;
    }
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_VALUE_OBJECT_V1, &children))
        return json_object_value(ctx, children[0], depth + 1u);
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_VALUE_ARRAY_V1, &children))
        return json_array_value(ctx, children[0], depth + 1u);
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_VALUE_NUMBER_V1, &children))
        return json_number_value(ctx, children[0], depth + 1u);
    if (json_cst_is(ctx, term, CETTA_JSON_ELAB_VALUE_STRING_V1, &children))
        return json_string_value(ctx, children[0], depth + 1u);
    json_elab_error(ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                    "expected a JSON value CST node");
    return NULL;
}

bool cetta_json_cst_value_v1_elaborate(
    const CettaJsonElaborationPlanV1 *plan,
    Arena *arena,
    Atom *json_text_cst,
    uint32_t work_limit,
    uint32_t depth_limit,
    Atom **out,
    CettaJsonCstValueV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    JsonElabV1 ctx;
    ArenaMark mark;
    Atom **children = NULL;
    Atom *value = NULL;
    if (error_buf && error_buf_size > 0u) error_buf[0] = '\0';
    if (status) *status = CETTA_JSON_CST_VALUE_V1_BAD_ARGUMENT;
    if (!plan || !arena || !json_text_cst || !out || work_limit == 0u ||
        depth_limit == 0u) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "bad JSON CST elaboration arguments");
        }
        return false;
    }
    mark = arena_mark(arena);
    ctx = (JsonElabV1){
        .plan = plan,
        .arena = arena,
        .work = 0u,
        .work_limit = work_limit,
        .depth_limit = depth_limit,
        .status = CETTA_JSON_CST_VALUE_V1_OK,
        .error = error_buf,
        .error_size = error_buf_size,
    };
    if (!json_cst_is(&ctx, json_text_cst, CETTA_JSON_ELAB_TEXT_V1,
                     &children) ||
        !json_whitespace(&ctx, children[0], 1u) ||
        !json_whitespace(&ctx, children[2], 1u)) {
        if (ctx.status == CETTA_JSON_CST_VALUE_V1_OK) {
            json_elab_error(&ctx, CETTA_JSON_CST_VALUE_V1_MALFORMED_CST,
                            "expected the authored json:text CST root");
        }
    } else {
        value = json_value(&ctx, children[1], 1u);
    }
    if (!value) {
        arena_reset(arena, mark);
        if (status) *status = ctx.status;
        return false;
    }
    *out = value;
    if (status) *status = CETTA_JSON_CST_VALUE_V1_OK;
    return true;
}

const char *cetta_json_cst_value_v1_status_name(
    CettaJsonCstValueV1Status status) {
    switch (status) {
    case CETTA_JSON_CST_VALUE_V1_OK: return "ok";
    case CETTA_JSON_CST_VALUE_V1_BAD_ARGUMENT: return "bad-argument";
    case CETTA_JSON_CST_VALUE_V1_MALFORMED_CST: return "malformed-cst";
    case CETTA_JSON_CST_VALUE_V1_INVALID_UNICODE_ESCAPE:
        return "invalid-unicode-escape";
    case CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT: return "resource-limit";
    case CETTA_JSON_CST_VALUE_V1_ALLOCATION_FAILURE:
        return "allocation-failure";
    }
    return "unknown";
}
