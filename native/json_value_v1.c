#include "json_value_v1.h"
#include "json_direct_v1.h"

#include "native/decimal_value_v1.h"
#include "lib_parse_native_grammar.h"
#include "utf8.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Arena *arena;
    CettaJsonNumberValidatorV1 number_validator;
    bool ordinary_copy;
    bool legacy_tags;
    /* Emit what Python's json.dumps writes: its float spellings and
     * separators always, ASCII escapes and key order per option. */
    bool python;
    bool ascii;
    bool sort_keys;
    const char *item_separator;
    size_t item_separator_len;
    const char *key_separator;
    size_t key_separator_len;
    /* A line break before each member and closing bracket, then per nesting
     * level indent_text, or indent_len spaces when indent_text is NULL. */
    bool indent;
    const char *indent_text;
    size_t indent_len;
    uint64_t work;
    uint32_t work_limit;
    uint32_t depth_limit;
    CettaJsonValueV1Status status;
    char *error;
    size_t error_size;
} JsonValueCtxV1;

typedef struct {
    uint8_t *bytes;
    size_t len;
    size_t cap;
    size_t limit;
    JsonValueCtxV1 *ctx;
} JsonBytesV1;

static bool json_value_error(JsonValueCtxV1 *ctx,
                             CettaJsonValueV1Status status,
                             const char *format, ...) {
    if (ctx) {
        ctx->status = status;
        if (ctx->error && ctx->error_size > 0u) {
            va_list arguments;
            va_start(arguments, format);
            (void)vsnprintf(ctx->error, ctx->error_size, format, arguments);
            va_end(arguments);
        }
    }
    return false;
}

static bool json_value_work(JsonValueCtxV1 *ctx, uint64_t amount) {
    if (!ctx || amount > UINT64_MAX - ctx->work ||
        (ctx->work_limit && (ctx->work > ctx->work_limit || amount > ctx->work_limit - ctx->work))) {
        return json_value_error(ctx, CETTA_JSON_VALUE_V1_RESOURCE_LIMIT,
                                "JSON value work limit exceeded");
    }
    ctx->work += amount;
    return true;
}

static bool json_expr_is(Atom *atom, const char *head, CettaExprLen len) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == len &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static bool json_bool_view(Atom *atom, bool *value_out) {
    if (!atom || !value_out) return false;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_BOOL) {
        *value_out = atom->ground.bval;
        return true;
    }
    if (atom_is_symbol(atom, "True")) {
        *value_out = true;
        return true;
    }
    if (atom_is_symbol(atom, "False")) {
        *value_out = false;
        return true;
    }
    /* PeTTa spells the booleans true and false. */
    if (atom_is_symbol(atom, "true")) {
        *value_out = true;
        return true;
    }
    if (atom_is_symbol(atom, "false")) {
        *value_out = false;
        return true;
    }
    return false;
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

static bool json_member_span_valid(Atom *span) {
    Atom *start;
    Atom *stop;
    if (atom_is_symbol(span, "JsonNoSourceSpanV1"))
        return true;
    if (!json_expr_is(span, "JsonSourceSpanV1", 3u))
        return false;
    start = span->expr.elems[1];
    stop = span->expr.elems[2];
    return start && stop &&
        start->kind == ATOM_GROUNDED && start->ground.gkind == GV_INT &&
        stop->kind == ATOM_GROUNDED && stop->ground.gkind == GV_INT &&
        start->ground.ival >= 0 &&
        start->ground.ival <= stop->ground.ival;
}

/* Source locations are proof/provenance observations, not JSON data.  A
 * stringify round trip must preserve every JSON constructor and occurrence,
 * while the newly parsed source is expected to carry different spans. */
static bool json_value_equal_ignoring_source_spans(
    JsonValueCtxV1 *ctx, Atom *left, Atom *right, uint32_t depth) {
    typedef struct { Atom *left, *right; size_t depth; } Pair;
    Pair local[16], *pairs = local;
    size_t len = 1u, cap = 16u;
    bool ok = false;
    pairs[0] = (Pair){left, right, depth};
    while (len) {
        Pair pair = pairs[--len];
        left = pair.left; right = pair.right;
        if (!left || !right || (ctx->depth_limit && pair.depth > ctx->depth_limit) ||
            !json_value_work(ctx, 1u)) goto done;
        CettaExprLen count;
        if (json_expr_is(left, "JsonMemberV1", 5u) && json_expr_is(right, "JsonMemberV1", 5u)) {
            if (!json_member_span_valid(left->expr.elems[4]) || !json_member_span_valid(right->expr.elems[4])) goto done;
            count = 4u;
        } else {
            if (left->kind != right->kind || left->kind != ATOM_EXPR || left->expr.len != right->expr.len) {
                if (!atom_eq(left, right)) goto done;
                continue;
            }
            count = left->expr.len;
        }
        if (count > SIZE_MAX - len) goto allocation;
        size_t needed = len + count;
        if (needed > cap) {
            size_t next = cap;
            while (next < needed) {
                if (next > SIZE_MAX / 2u) goto allocation;
                next *= 2u;
            }
            if (next > SIZE_MAX / sizeof(*pairs)) goto allocation;
            Pair *grown = pairs == local ? malloc(next * sizeof(*pairs)) : realloc(pairs, next * sizeof(*pairs));
            if (!grown) goto allocation;
            if (pairs == local) memcpy(grown, local, len * sizeof(*pairs));
            pairs = grown; cap = next;
        }
        for (CettaExprIndex i = count; i > 0u; i--)
            pairs[len++] = (Pair){left->expr.elems[i-1u], right->expr.elems[i-1u], pair.depth + 1u};
    }
    ok = true;
    goto done;
allocation:
    json_value_error(ctx, CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE, "out of memory comparing JSON values");
done:
    if (pairs != local) free(pairs);
    return ok;
}

static bool json_scalar_list_view(JsonValueCtxV1 *ctx, Atom *string,
                                  Atom **list_out) {
    Atom *list;
    CettaExprIndex index;
    if (!json_expr_is(string, "JsonStringV1", 2u) ||
        !(list = string->expr.elems[1]) || list->kind != ATOM_EXPR) {
        return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                "expected (JsonStringV1 scalar-list)");
    }
    for (index = 0u; index < list->expr.len; index++) {
        Atom *cp = list->expr.elems[index];
        Atom *value;
        if (!json_value_work(ctx, 1u)) return false;
        if (!json_expr_is(cp, "cp", 2u) ||
            !(value = cp->expr.elems[1]) || value->kind != ATOM_GROUNDED ||
            value->ground.gkind != GV_INT || value->ground.ival < 0 ||
            value->ground.ival > 0x10ffff ||
            (value->ground.ival >= 0xd800 && value->ground.ival <= 0xdfff)) {
            return json_value_error(
                ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                "JsonStringV1 contains a non-Unicode-scalar value");
        }
    }
    *list_out = list;
    return true;
}

static bool json_utf8_append_scalar(JsonBytesV1 *out, uint32_t scalar);

static bool json_bytes_reserve(JsonBytesV1 *out, size_t extra) {
    uint8_t *next;
    size_t needed;
    size_t cap;
    if (!out || extra > out->limit - out->len) {
        return json_value_error(out ? out->ctx : NULL,
                                CETTA_JSON_VALUE_V1_RESOURCE_LIMIT,
                                "JSON output byte limit exceeded");
    }
    needed = out->len + extra;
    if (needed <= out->cap) return true;
    cap = out->cap ? out->cap : 128u;
    while (cap < needed) {
        if (cap > out->limit / 2u) {
            cap = out->limit;
            break;
        }
        cap *= 2u;
    }
    if (cap < needed) {
        return json_value_error(out->ctx, CETTA_JSON_VALUE_V1_RESOURCE_LIMIT,
                                "JSON output byte limit exceeded");
    }
    next = (uint8_t *)realloc(out->bytes, cap ? cap : 1u);
    if (!next) {
        return json_value_error(out->ctx,
                                CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE,
                                "out of memory emitting JSON");
    }
    out->bytes = next;
    out->cap = cap;
    return true;
}

static bool json_bytes_append(JsonBytesV1 *out,
                              const void *bytes, size_t len) {
    if (!json_bytes_reserve(out, len)) return false;
    if (len > 0u) memcpy(out->bytes + out->len, bytes, len);
    out->len += len;
    return true;
}

static bool json_bytes_char(JsonBytesV1 *out, uint8_t byte) {
    return json_bytes_append(out, &byte, 1u);
}

static bool json_utf8_append_scalar(JsonBytesV1 *out, uint32_t scalar) {
    uint8_t bytes[4];
    size_t len;
    if (scalar <= 0x7fu) {
        bytes[0] = (uint8_t)scalar;
        len = 1u;
    } else if (scalar <= 0x7ffu) {
        bytes[0] = (uint8_t)(0xc0u | (scalar >> 6u));
        bytes[1] = (uint8_t)(0x80u | (scalar & 0x3fu));
        len = 2u;
    } else if (scalar <= 0xffffu &&
               !(scalar >= 0xd800u && scalar <= 0xdfffu)) {
        bytes[0] = (uint8_t)(0xe0u | (scalar >> 12u));
        bytes[1] = (uint8_t)(0x80u | ((scalar >> 6u) & 0x3fu));
        bytes[2] = (uint8_t)(0x80u | (scalar & 0x3fu));
        len = 3u;
    } else if (scalar <= 0x10ffffu) {
        bytes[0] = (uint8_t)(0xf0u | (scalar >> 18u));
        bytes[1] = (uint8_t)(0x80u | ((scalar >> 12u) & 0x3fu));
        bytes[2] = (uint8_t)(0x80u | ((scalar >> 6u) & 0x3fu));
        bytes[3] = (uint8_t)(0x80u | (scalar & 0x3fu));
        len = 4u;
    } else {
        return json_value_error(out->ctx,
                                CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                "non-Unicode scalar in JSON value");
    }
    return json_bytes_append(out, bytes, len);
}

static bool json_canonical_string_utf8(JsonValueCtxV1 *ctx, Atom *string,
                                       JsonBytesV1 *out) {
    Atom *list;
    CettaExprIndex index;
    if (!json_scalar_list_view(ctx, string, &list)) return false;
    for (index = 0u; index < list->expr.len; index++) {
        uint32_t scalar = (uint32_t)
            list->expr.elems[index]->expr.elems[1]->ground.ival;
        if (!json_utf8_append_scalar(out, scalar)) return false;
    }
    return true;
}

static Atom *json_ordinary_number_from_text(
    JsonValueCtxV1 *ctx, Atom *text_atom) {
    if (!text_atom || text_atom->kind != ATOM_GROUNDED ||
        text_atom->ground.gkind != GV_STRING || !text_atom->ground.sval) {
        json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                         "JsonNumberV1 expects an exact text lexeme");
        return NULL;
    }
    const char *text = text_atom->ground.sval;
    size_t length = atom_string_len(text_atom);
    if (!cetta_json_direct_v1_number_valid(
            (const uint8_t *)text, length)) {
        json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                         "JsonNumberV1 contains an invalid number lexeme");
        return NULL;
    }
    bool real = memchr(text, '.', length) || memchr(text, 'e', length) ||
                memchr(text, 'E', length);
    if (!real)
        return atom_bigint(ctx->arena, text);
    double number = 0.0;
    if (!cetta_decimal_real_nearest_v1(text, &number)) {
        json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                         "JsonNumberV1 cannot be projected to a native number");
        return NULL;
    }
    return atom_float(ctx->arena, number);
}

static Atom *json_to_ordinary_scalar(JsonValueCtxV1 *ctx, Atom *value) {
    if (atom_is_symbol(value, "JsonNullV1"))
        return atom_symbol(ctx->arena, "JsonNull");
    if (json_expr_is(value, "JsonBoolV1", 2u)) {
        Atom *boolean = value->expr.elems[1];
        bool boolean_value;
        if (!json_bool_view(boolean, &boolean_value)) {
            json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                             "JsonBoolV1 expects True or False");
            return NULL;
        }
        Atom *b = atom_bool(ctx->arena, boolean_value);
        return ctx->legacy_tags ? json_app1(ctx->arena, "JsonBool", b) : b;
    }
    if (json_expr_is(value, "JsonNumberV1", 2u)) {
        if (ctx->legacy_tags) {
            Atom *number = value->expr.elems[1];
            if (!number || number->kind != ATOM_GROUNDED ||
                number->ground.gkind != GV_STRING || !number->ground.sval) {
                json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                 "JsonNumberV1 expects an exact text lexeme");
                return NULL;
            }
            return json_app1(ctx->arena, "JsonNumber",
                             atom_string_n(ctx->arena, number->ground.sval,
                                           atom_string_len(number)));
        }
        return json_ordinary_number_from_text(ctx, value->expr.elems[1]);
    }
    if (json_expr_is(value, "JsonStringV1", 2u)) {
        JsonBytesV1 utf8 = {.limit = CETTA_STRING_BYTES_MAX, .ctx = ctx};
        Atom *result = NULL;
        if (json_canonical_string_utf8(ctx, value, &utf8)) {
            result = atom_string_n(ctx->arena,
                                   (const char *)utf8.bytes, utf8.len);
            if (ctx->legacy_tags && result)
                result = json_app1(ctx->arena, "JsonString", result);
        }
        free(utf8.bytes);
        return result;
    }

    json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                     "malformed canonical JSON value");
    return NULL;
}

static Atom *json_scalar_list_from_utf8(JsonValueCtxV1 *ctx,
                                        const char *text, size_t text_len) {
    CettaLpNativeUtf8ScalarBuffer scalars;
    Atom **items = NULL;
    Atom *list = NULL;
    Atom *result = NULL;
    uint32_t index;
    char error[256] = {0};
    cetta_lp_native_utf8_scalar_buffer_init(&scalars);
    if (!cetta_lp_native_utf8_scalar_buffer_decode(
            &scalars, (const uint8_t *)text, text_len,
            error, sizeof(error))) {
        json_value_error(ctx, CETTA_JSON_VALUE_V1_INVALID_UTF8,
                         "%s", error[0] ? error : "invalid UTF-8 string");
        goto done;
    }
    items = (Atom **)calloc(scalars.view.scalar_len
                                ? scalars.view.scalar_len : 1u,
                            sizeof(*items));
    if (!items) {
        json_value_error(ctx, CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE,
                         "out of memory decoding an inspected JSON string");
        goto done;
    }
    for (index = 0u; index < scalars.view.scalar_len; index++) {
        Atom *cp_items[2] = {
            atom_symbol(ctx->arena, "cp"),
            atom_int(ctx->arena,
                     (int64_t)cetta_lp_native_utf8_scalar_view_scalar_at(
                         &scalars.view, index)),
        };
        items[index] = atom_expr(ctx->arena, cp_items, 2u);
    }
    list = atom_expr(ctx->arena, items, scalars.view.scalar_len);
    result = json_app1(ctx->arena, "JsonStringV1", list);

done:
    free(items);
    cetta_lp_native_utf8_scalar_buffer_free(&scalars);
    return result;
}

static const char *json_text(Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_STRING ? atom->ground.sval : NULL;
}

/* The caller has already checked that atom is text. */
static size_t json_text_len(Atom *atom, const char *text) {
    return atom->kind == ATOM_GROUNDED ? atom_string_len(atom) : strlen(text);
}

/* A text carrier is inspected in place. Scalar syntax is an explicit
 * conversion, not a prerequisite for validation, lookup or emission. */
static bool json_utf8_text(JsonValueCtxV1 *ctx, const char *text, size_t len,
                           size_t *scalar_count) {
    size_t at = 0u, count = 0u;
    while (at < len) {
        if ((uint8_t)text[at] < 0x80u) { at++; count++; continue; }
        uint32_t scalar, width;
        /* Python text may hold lone surrogates, encoded as surrogatepass
         * encodes them; json.dumps escapes them under ensure_ascii. */
        bool decoded = ctx->python
            ? cetta_utf8_decode_surrogatepass((const uint8_t *)text, len, at, &scalar, &width)
            : cetta_utf8_decode_one((const uint8_t *)text, len, at, &scalar, &width);
        if (!decoded)
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_INVALID_UTF8,
                                    "invalid UTF-8 at byte offset %zu", at);
        at += width; count++;
    }
    if (scalar_count) *scalar_count = count;
    return true;
}

static Atom *json_from_ordinary_scalar(JsonValueCtxV1 *ctx, Atom *value) {
    if (atom_is_symbol(value, "JsonNull"))
        return ctx->arena ? atom_symbol(ctx->arena, ctx->ordinary_copy ? "JsonNull" : "JsonNullV1") : value;
    bool boolean_value = false;
    if (json_bool_view(value, &boolean_value) ||
        (json_expr_is(value, "JsonBool", 2u) &&
         json_bool_view(value->expr.elems[1], &boolean_value))) {
        if (!ctx->arena)
            return value;
        return ctx->ordinary_copy
            ? atom_bool(ctx->arena, boolean_value)
            : json_app1(ctx->arena, "JsonBoolV1",
                        atom_bool(ctx->arena, boolean_value));
    }
    if (json_expr_is(value, "JsonBool", 2u)) {
        return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                "JsonBool expects True or False"), NULL;
    }
    bool tagged_string = json_expr_is(value, "JsonString", 2u);
    Atom *str_atom = tagged_string ? value->expr.elems[1] : value;
    /* The tagged compatibility view explicitly declares a text payload.
     * PeTTa foreign strings can be symbol spellings; bare ordinary symbols
     * remain invalid JSON values. */
    const char *string_text = tagged_string && str_atom && str_atom->kind == ATOM_SYMBOL
        ? atom_name_cstr(str_atom) : json_text(str_atom);
    if (string_text) {
        const char *text = string_text;
        size_t text_len = json_text_len(str_atom, text);
        if (!ctx->arena)
            return json_utf8_text(ctx, text, text_len, NULL)
                ? value : NULL;
        if (ctx->ordinary_copy) {
            return json_utf8_text(ctx, text, text_len, NULL)
                ? atom_string_n(ctx->arena, text, text_len) : NULL;
        }
        return json_scalar_list_from_utf8(ctx, text, text_len);
    }
    if (json_expr_is(value, "JsonString", 2u)) {
        return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                "JsonString expects text"), NULL;
    }
    if (json_expr_is(value, "JsonNumber", 2u)) {
        Atom *num_atom = value->expr.elems[1];
        const char *text = json_text(num_atom);
        if (!text) {
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                    "JsonNumber expects an exact text lexeme"), NULL;
        }
        size_t text_len = json_text_len(num_atom, text);
        if (!cetta_json_direct_v1_number_valid((const uint8_t *)text, text_len)) {
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT,
                                    "JSON syntax rejected by authored grammar"), NULL;
        }
        if (!ctx->arena)
            return value;
        if (ctx->ordinary_copy)
            return json_ordinary_number_from_text(ctx, num_atom);
        return json_app1(ctx->arena, "JsonNumberV1",
                         atom_string_n(ctx->arena, text, text_len));
    }
    if (value->kind == ATOM_GROUNDED &&
        (value->ground.gkind == GV_INT ||
         value->ground.gkind == GV_BIGINT ||
         value->ground.gkind == GV_FLOAT)) {
        if (value->ground.gkind == GV_FLOAT && !isfinite(value->ground.fval) &&
            !ctx->python) {
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                    "ordinary JSON numbers must be finite"), NULL;
        }
        if (!ctx->arena)
            return value;
        if (ctx->ordinary_copy)
            return atom_deep_copy(ctx->arena, value);
        char *number_text = NULL;
        bool heap_text = false;
        if (value->ground.gkind == GV_FLOAT) {
            if (!cetta_decimal_real_from_double_v1(
                    value->ground.fval, &number_text)) {
                return json_value_error(
                    ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                    "ordinary JSON float has no finite decimal spelling"), NULL;
            }
            heap_text = true;
        } else {
            number_text = atom_to_parseable_string(ctx->arena, value);
        }
        Atom *result = number_text
            ? json_app1(ctx->arena, "JsonNumberV1",
                        atom_string(ctx->arena, number_text))
            : NULL;
        if (heap_text)
            free(number_text);
        if (!result)
            json_value_error(ctx, CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE,
                             "out of memory converting a JSON number");
        return result;
    }

    json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                     "malformed ordinary JSON value");
    return NULL;
}

typedef struct {
    Atom *source;
    Atom *const *items;
    CettaExprLen count;
    Atom **converted;
    Atom *key;
    CettaExprIndex index;
    bool object;
} JsonConvertFrameV1;

static Atom *json_convert_child(JsonValueCtxV1 *ctx, JsonConvertFrameV1 *frame,
                                bool to_ordinary) {
    Atom *child = frame->items[frame->index];
    if (!frame->object) return child;
    if (to_ordinary) {
        JsonBytesV1 utf8 = {.limit = CETTA_STRING_BYTES_MAX, .ctx = ctx};
        bool ok = json_expr_is(child, "JsonMemberV1", 5u) &&
            child->expr.elems[1] && child->expr.elems[1]->kind == ATOM_GROUNDED &&
            child->expr.elems[1]->ground.gkind == GV_INT &&
            child->expr.elems[1]->ground.ival == (int64_t)frame->index &&
            json_member_span_valid(child->expr.elems[4]) &&
            json_canonical_string_utf8(ctx, child->expr.elems[2], &utf8);
        if (ok) frame->key = atom_string_n(ctx->arena, (const char *)utf8.bytes, utf8.len);
        free(utf8.bytes);
        if (ok) return child->expr.elems[3];
    } else if (json_expr_is(child, "JsonPair", 3u)) {
        const char *key = json_text(child->expr.elems[1]);
        if (key && !ctx->arena)
            return json_utf8_text(ctx, key, json_text_len(child->expr.elems[1], key), NULL)
                ? child->expr.elems[2] : NULL;
        if (key && ctx->ordinary_copy) {
            size_t len = json_text_len(child->expr.elems[1], key);
            if (!json_utf8_text(ctx, key, len, NULL)) return NULL;
            frame->key = atom_string_n(ctx->arena, key, len);
            return child->expr.elems[2];
        }
        if (key && (frame->key = json_scalar_list_from_utf8(
                ctx, key, json_text_len(child->expr.elems[1], key))))
            return child->expr.elems[2];
    }
    if (ctx->status == CETTA_JSON_VALUE_V1_OK)
        json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "malformed JSON object member");
    return NULL;
}

static Atom *json_convert_value(JsonValueCtxV1 *ctx, Atom *value, bool to_ordinary) {
    JsonConvertFrameV1 *frames = NULL;
    size_t len = 0u, cap = 0u;
    Atom *current = value;
    for (;;) {
        if (!current) {
            json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "missing JSON value");
            goto failed;
        }
        if (ctx->depth_limit && len >= ctx->depth_limit) {
            json_value_error(ctx, CETTA_JSON_VALUE_V1_RESOURCE_LIMIT, "JSON value nesting limit exceeded");
            goto failed;
        }
        if (!json_value_work(ctx, 1u)) goto failed;
        bool object = json_expr_is(current, to_ordinary ? "JsonObjectV1" : "JsonObject", 2u);
        if (object || json_expr_is(current, to_ordinary ? "JsonArrayV1" : "JsonArray", 2u)) {
            Atom *const *items;
            CettaExprLen count;
            if (!atom_sequence_view(current->expr.elems[1], &items, &count)) {
                json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "malformed JSON container");
                goto failed;
            }
            if (len == cap) {
                size_t next = cap ? cap * 2u : 16u;
                if (next < cap || next > SIZE_MAX / sizeof(*frames)) goto allocation;
                JsonConvertFrameV1 *grown = realloc(frames, next * sizeof(*frames));
                if (!grown) goto allocation;
                frames = grown; cap = next;
            }
            Atom **converted = ctx->arena && count ? calloc(count, sizeof(*converted)) : NULL;
            if (ctx->arena && count && !converted) goto allocation;
            frames[len++] = (JsonConvertFrameV1){.source = current, .items = items, .count = count,
                .converted = converted, .object = object};
            if (!count) goto closed;
            current = json_convert_child(ctx, &frames[len-1u], to_ordinary);
            if (!current) goto failed;
            continue;
        }
        current = to_ordinary ? json_to_ordinary_scalar(ctx, current) : json_from_ordinary_scalar(ctx, current);
        if (!current) goto failed;
        for (;;) {
            if (!len) { free(frames); return current; }
            JsonConvertFrameV1 *frame = &frames[len-1u];
            if (ctx->arena && frame->object) current = to_ordinary || ctx->ordinary_copy
                ? json_app2(ctx->arena, "JsonPair", frame->key, current)
                : json_app4(ctx->arena, "JsonMemberV1", atom_int(ctx->arena, frame->index),
                            frame->key, current, atom_symbol(ctx->arena, "JsonNoSourceSpanV1"));
            if (ctx->arena) frame->converted[frame->index] = current;
            frame->index++;
            if (frame->index < frame->count) {
                current = json_convert_child(ctx, frame, to_ordinary);
                if (!current) goto failed;
                break;
            }
closed:
            frame = &frames[len-1u];
            current = ctx->arena ? json_app1(ctx->arena, frame->object ? (to_ordinary || ctx->ordinary_copy ? "JsonObject" : "JsonObjectV1") :
                               (to_ordinary || ctx->ordinary_copy ? "JsonArray" : "JsonArrayV1"),
                               atom_expr(ctx->arena, frame->converted, frame->count)) : frame->source;
            free(frame->converted); len--;
        }
    }
allocation:
    json_value_error(ctx, CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE, "out of memory converting JSON value");
failed:
    for (size_t i = 0u; i < len; i++) free(frames[i].converted);
    free(frames); return NULL;
}

static bool json_convert(Arena *arena, Atom *value,
                         uint32_t work_limit, uint32_t depth_limit,
                         Atom **out, CettaJsonValueV1Status *status,
                         char *error_buf, size_t error_buf_size,
                         bool to_ordinary) {
    JsonValueCtxV1 ctx;
    ArenaMark mark;
    Atom *result;
    if (error_buf && error_buf_size > 0u) error_buf[0] = '\0';
    if (status) *status = CETTA_JSON_VALUE_V1_BAD_ARGUMENT;
    if (!arena || !value || !out) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "bad JSON value conversion arguments");
        }
        return false;
    }
    mark = arena_mark(arena);
    ctx = (JsonValueCtxV1){
        .arena = arena,
        .work_limit = work_limit,
        .depth_limit = depth_limit,
        .status = CETTA_JSON_VALUE_V1_OK,
        .error = error_buf,
        .error_size = error_buf_size,
    };
    result = json_convert_value(&ctx, value, to_ordinary);
    if (!result) {
        arena_reset(arena, mark);
        if (status) *status = ctx.status;
        return false;
    }
    *out = result;
    if (status) *status = CETTA_JSON_VALUE_V1_OK;
    return true;
}

bool cetta_json_value_v1_to_ordinary(
    Arena *arena, Atom *canonical, uint32_t work_limit,
    uint32_t depth_limit, Atom **out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    return json_convert(arena, canonical, work_limit, depth_limit,
                        out, status, error_buf, error_buf_size, true);
}

bool cetta_json_value_v1_to_legacy(
    Arena *arena, Atom *canonical, uint32_t work_limit,
    uint32_t depth_limit, Atom **out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    JsonValueCtxV1 ctx;
    ArenaMark mark;
    Atom *result;
    if (error_buf && error_buf_size > 0u) error_buf[0] = '\0';
    if (status) *status = CETTA_JSON_VALUE_V1_BAD_ARGUMENT;
    if (!arena || !canonical || !out) {
        if (error_buf && error_buf_size > 0u)
            (void)snprintf(error_buf, error_buf_size, "bad JSON value conversion arguments");
        return false;
    }
    mark = arena_mark(arena);
    ctx = (JsonValueCtxV1){
        .arena = arena,
        .legacy_tags = true,
        .work_limit = work_limit,
        .depth_limit = depth_limit,
        .status = CETTA_JSON_VALUE_V1_OK,
        .error = error_buf,
        .error_size = error_buf_size,
    };
    result = json_convert_value(&ctx, canonical, true);
    if (!result) {
        arena_reset(arena, mark);
        if (status) *status = ctx.status;
        return false;
    }
    *out = result;
    if (status) *status = CETTA_JSON_VALUE_V1_OK;
    return true;
}

bool cetta_json_value_v1_from_ordinary(
    Arena *arena, Atom *ordinary, uint32_t work_limit,
    uint32_t depth_limit, Atom **out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    return json_convert(arena, ordinary, work_limit, depth_limit,
                        out, status, error_buf, error_buf_size, false);
}

static bool json_validate_ordinary(Atom *value, bool python,
    uint32_t work_limit, uint32_t depth_limit, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    if (error_buf && error_buf_size) error_buf[0] = '\0';
    if (!value) {
        if (status) *status = CETTA_JSON_VALUE_V1_BAD_ARGUMENT;
        if (error_buf && error_buf_size)
            (void)snprintf(error_buf, error_buf_size, "bad JSON value conversion arguments");
        return false;
    }
    JsonValueCtxV1 ctx = {.python = python, .work_limit = work_limit, .depth_limit = depth_limit,
        .status = CETTA_JSON_VALUE_V1_OK, .error = error_buf, .error_size = error_buf_size};
    bool ok = json_convert_value(&ctx, value, false) != NULL;
    if (status) *status = ctx.status;
    return ok;
}

bool cetta_json_value_v1_validate_ordinary(Atom *value,
    uint32_t work_limit, uint32_t depth_limit, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    return json_validate_ordinary(value, false, work_limit, depth_limit, status,
                                  error_buf, error_buf_size);
}

bool cetta_json_value_v1_normalize_ordinary(Arena *arena, Atom *value, Atom **out,
    CettaJsonValueV1Status *status, char *error_buf, size_t error_buf_size) {
    if (status) *status = CETTA_JSON_VALUE_V1_BAD_ARGUMENT;
    if (!arena || !value || !out) return false;
    if (error_buf && error_buf_size) error_buf[0] = '\0';
    ArenaMark mark = arena_mark(arena);
    JsonValueCtxV1 ctx = {.arena = arena, .ordinary_copy = true,
        .status = CETTA_JSON_VALUE_V1_OK, .error = error_buf, .error_size = error_buf_size};
    Atom *result = json_convert_value(&ctx, value, false);
    if (status) *status = ctx.status;
    if (!result) { arena_reset(arena, mark); return false; }
    *out = result;
    return true;
}

/* Python's ensure_ascii spelling of one scalar outside printable ASCII:
 * lowercase \\uXXXX, with a surrogate pair above the Basic Multilingual Plane. */
static bool json_append_u_escape(JsonBytesV1 *out, uint32_t unit) {
    static const char hex[] = "0123456789abcdef";
    char escaped[6] = {'\\', 'u', hex[(unit >> 12u) & 15u], hex[(unit >> 8u) & 15u],
                       hex[(unit >> 4u) & 15u], hex[unit & 15u]};
    return json_bytes_append(out, escaped, sizeof(escaped));
}

static bool json_append_ascii_scalar(JsonBytesV1 *out, uint32_t scalar) {
    if (scalar < 0x10000u)
        return json_append_u_escape(out, scalar);
    scalar -= 0x10000u;
    return json_append_u_escape(out, 0xd800u | (scalar >> 10u)) &&
           json_append_u_escape(out, 0xdc00u | (scalar & 0x3ffu));
}

/* The shortest decimal digits that read back as the positive finite double
 * `magnitude`, as Python's repr chooses them, with the decimal point position
 * `decpt` (the value is 0.DIGITS times ten to the power decpt). At each length
 * the correctly rounded digits are tried first. Below a power of two the
 * rounding interval is half as wide, so there the next digit string above, or
 * below at a decade boundary, can be the shortest one instead. */
static bool json_shortest_digits(double magnitude, char *digits, size_t cap, int *decpt) {
    uint64_t scale = 1u;
    for (int precision = 1; precision <= 17; precision++, scale *= 10u) {
        char text[48];
        int written = snprintf(text, sizeof(text), "%.*e", precision - 1, magnitude);
        if (written <= 0 || (size_t)written >= sizeof(text)) return false;
        uint64_t mantissa = 0u;
        const char *at = text;
        for (; *at && *at != 'e'; at++)
            if (*at != '.') mantissa = mantissa * 10u + (uint64_t)(*at - '0');
        if (*at != 'e') return false;
        int exponent = atoi(at + 1);
        /* scale is 10^(precision-1): the smallest mantissa of this length. */
        uint64_t candidates[3];
        int exponents[3];
        size_t count = 0u;
        candidates[count] = mantissa; exponents[count++] = exponent;
        if (mantissa + 1u == scale * 10u) {
            candidates[count] = scale; exponents[count++] = exponent + 1;
        } else {
            candidates[count] = mantissa + 1u; exponents[count++] = exponent;
        }
        if (mantissa - 1u >= scale) {
            candidates[count] = mantissa - 1u; exponents[count++] = exponent;
        } else if (precision > 1 || mantissa > 1u) {
            candidates[count] = scale * 10u - 1u; exponents[count++] = exponent - 1;
        }
        for (size_t index = 0u; index < count; index++) {
            char spelled[48];
            int length = snprintf(spelled, sizeof(spelled), "%" PRIu64, candidates[index]);
            if (length <= 0 || (size_t)length >= sizeof(spelled)) return false;
            char reread[64];
            int reread_length = snprintf(reread, sizeof(reread), "%se%d", spelled,
                                         exponents[index] - (length - 1));
            if (reread_length <= 0 || (size_t)reread_length >= sizeof(reread)) return false;
            if (strtod(reread, NULL) != magnitude) continue;
            while (length > 1 && spelled[length - 1] == '0') spelled[--length] = '\0';
            if ((size_t)length >= cap) return false;
            memcpy(digits, spelled, (size_t)length + 1u);
            *decpt = exponents[index] + 1;
            return true;
        }
    }
    return false;
}

/* Python's float repr, as json.dumps writes it: fixed notation when
 * -4 < decpt <= 16, always with a fractional digit; otherwise one leading
 * digit and a signed exponent of at least two digits. Non-finite values take
 * json.dumps's NaN, Infinity and -Infinity. */
static bool json_append_python_float(JsonBytesV1 *out, double value) {
    if (isnan(value)) return json_bytes_append(out, "NaN", 3u);
    if (isinf(value))
        return value < 0.0 ? json_bytes_append(out, "-Infinity", 9u)
                           : json_bytes_append(out, "Infinity", 8u);
    if (signbit(value) && !json_bytes_char(out, '-')) return false;
    double magnitude = fabs(value);
    char digits[32];
    int decpt = 1;
    if (magnitude == 0.0) {
        memcpy(digits, "0", 2u);
    } else if (!json_shortest_digits(magnitude, digits, sizeof(digits), &decpt)) {
        return false;
    }
    int length = (int)strlen(digits);
    if (decpt <= -4 || decpt > 16) {
        char exponent[16];
        int written = snprintf(exponent, sizeof(exponent), "e%+03d", decpt - 1);
        if (written <= 0 || (size_t)written >= sizeof(exponent)) return false;
        return json_bytes_char(out, (uint8_t)digits[0]) &&
               (length == 1 || (json_bytes_char(out, '.') &&
                                json_bytes_append(out, digits + 1, (size_t)length - 1u))) &&
               json_bytes_append(out, exponent, (size_t)written);
    }
    if (decpt <= 0) {
        if (!json_bytes_append(out, "0.", 2u)) return false;
        for (int zero = 0; zero < -decpt; zero++)
            if (!json_bytes_char(out, '0')) return false;
        return json_bytes_append(out, digits, (size_t)length);
    }
    if (decpt >= length) {
        if (!json_bytes_append(out, digits, (size_t)length)) return false;
        for (int zero = length; zero < decpt; zero++)
            if (!json_bytes_char(out, '0')) return false;
        return json_bytes_append(out, ".0", 2u);
    }
    return json_bytes_append(out, digits, (size_t)decpt) && json_bytes_char(out, '.') &&
           json_bytes_append(out, digits + decpt, (size_t)(length - decpt));
}

static bool json_emit_string(JsonValueCtxV1 *ctx, Atom *string,
                             JsonBytesV1 *out) {
    static const uint8_t hex[] = "0123456789abcdef";
    Atom *list;
    CettaExprIndex index;
    if (!json_scalar_list_view(ctx, string, &list) ||
        !json_bytes_char(out, '"')) return false;
    for (index = 0u; index < list->expr.len; index++) {
        uint32_t scalar = (uint32_t)
            list->expr.elems[index]->expr.elems[1]->ground.ival;
        switch (scalar) {
        case '"': if (!json_bytes_append(out, "\\\"", 2u)) return false; break;
        case '\\': if (!json_bytes_append(out, "\\\\", 2u)) return false; break;
        case '\b': if (!json_bytes_append(out, "\\b", 2u)) return false; break;
        case '\f': if (!json_bytes_append(out, "\\f", 2u)) return false; break;
        case '\n': if (!json_bytes_append(out, "\\n", 2u)) return false; break;
        case '\r': if (!json_bytes_append(out, "\\r", 2u)) return false; break;
        case '\t': if (!json_bytes_append(out, "\\t", 2u)) return false; break;
        default:
            if (scalar < 0x20u) {
                uint8_t escape[6] = {'\\', 'u', '0', '0',
                    hex[(scalar >> 4u) & 0xfu], hex[scalar & 0xfu]};
                if (!json_bytes_append(out, escape, sizeof(escape)))
                    return false;
            } else if (ctx->ascii && scalar > 0x7eu) {
                if (!json_append_ascii_scalar(out, scalar)) return false;
            } else if (!json_utf8_append_scalar(out, scalar)) {
                return false;
            }
            break;
        }
    }
    return json_bytes_char(out, '"');
}

static bool json_emit_native_text(JsonValueCtxV1 *ctx, Atom *value, JsonBytesV1 *out,
                                  bool tagged_string) {
    const char *text = tagged_string && value && value->kind == ATOM_SYMBOL
        ? atom_name_cstr(value) : json_text(value);
    if (!text) return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                       "ordinary JSON strings must be native strings");
    size_t len = json_text_len(value, text), scalars;
    if (!json_utf8_text(ctx, text, len, &scalars) ||
        !json_value_work(ctx, scalars) || !json_bytes_char(out, '"')) return false;
    static const char hex[] = "0123456789abcdef";
    size_t begin = 0u;
    for (size_t at = 0u; at < len; at++) {
        uint8_t c = (uint8_t)text[at];
        if (c >= 0x20u && c != '"' && c != '\\' && (!ctx->ascii || c < 0x7fu)) continue;
        if (!json_bytes_append(out, text + begin, at - begin)) return false;
        if (ctx->ascii && c >= 0x7fu) {
            /* The text is valid UTF-8: the lead byte gives the sequence length. */
            size_t width = c < 0x80u ? 1u : c < 0xe0u ? 2u : c < 0xf0u ? 3u : 4u;
            uint32_t scalar = width == 1u ? c : width == 2u ? (c & 0x1fu)
                            : width == 3u ? (c & 0x0fu) : (c & 0x07u);
            for (size_t follow = 1u; follow < width; follow++)
                scalar = (scalar << 6u) | ((uint8_t)text[at + follow] & 0x3fu);
            if (!json_append_ascii_scalar(out, scalar)) return false;
            at += width - 1u;
            begin = at + 1u;
            continue;
        }
        const char *escape = NULL;
        switch (c) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        }
        if (escape) {
            if (!json_bytes_append(out, escape, 2u)) return false;
        } else {
            char escaped[] = {'\\', 'u', '0', '0', hex[c >> 4u], hex[c & 15u]};
            if (!json_bytes_append(out, escaped, sizeof(escaped))) return false;
        }
        begin = at + 1u;
    }
    return json_bytes_append(out, text + begin, len - begin) && json_bytes_char(out, '"');
}

/* Reference emission is checked by the full runtime roundtrip. Fast
 * emission supplies its lexical interpretation explicitly. */
static bool json_number_lexeme_valid(JsonValueCtxV1 *ctx, const char *bytes, size_t len) {
    return !ctx->number_validator ||
        ctx->number_validator((const uint8_t *)bytes, len);
}

static bool json_emit_scalar(JsonValueCtxV1 *ctx, Atom *value, JsonBytesV1 *out, bool ordinary) {
    if (atom_is_symbol(value, ordinary ? "JsonNull" : "JsonNullV1"))
        return json_bytes_append(out, "null", 4u);
    if (ordinary) {
        bool boolean_value;
        if (json_bool_view(value, &boolean_value) ||
            (json_expr_is(value, "JsonBool", 2u) &&
             json_bool_view(value->expr.elems[1], &boolean_value))) {
            return boolean_value
                ? json_bytes_append(out, "true", 4u)
                : json_bytes_append(out, "false", 5u);
        }
        if (json_expr_is(value, "JsonString", 2u))
            return json_emit_native_text(ctx, value->expr.elems[1], out, true);
        if (json_expr_is(value, "JsonNumber", 2u)) {
            Atom *number = value->expr.elems[1];
            const char *text = json_text(number);
            if (!text || number->kind != ATOM_GROUNDED ||
                number->ground.gkind != GV_STRING) {
                return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                        "JsonNumber expects exact text");
            }
            size_t len = json_text_len(number, text);
            if (!json_number_lexeme_valid(ctx, text, len))
                return json_value_error(ctx, CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT,
                                        "JSON syntax rejected by authored grammar");
            return json_bytes_append(out, text, len);
        }
        if (value && value->kind == ATOM_GROUNDED &&
            value->ground.gkind == GV_STRING)
            return json_emit_native_text(ctx, value, out, false);
        if (value && value->kind == ATOM_GROUNDED &&
            (value->ground.gkind == GV_INT ||
             value->ground.gkind == GV_BIGINT ||
             value->ground.gkind == GV_FLOAT)) {
            char *text = NULL;
            bool heap_text = false;
            if (value->ground.gkind == GV_FLOAT && ctx->python) {
                if (json_append_python_float(out, value->ground.fval)) return true;
                return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                        "invalid ordinary JSON number");
            }
            if (value->ground.gkind == GV_FLOAT) {
                if (!cetta_decimal_real_from_double_v1(
                        value->ground.fval, &text)) {
                    return json_value_error(
                        ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                        "ordinary JSON numbers must be finite");
                }
                heap_text = true;
            } else if (value->ground.gkind == GV_BIGINT) {
                text = (char *)atom_bigint_cstr(value);
                /* Python's int-to-text conversion refuses more than
                 * sys.get_int_max_str_digits() digits; json.dumps raises. */
                if (ctx->python && text &&
                    strlen(text) - (text[0] == '-' ? 1u : 0u) > 4300u)
                    return json_value_error(ctx, CETTA_JSON_VALUE_V1_PYTHON_VALUE_ERROR,
                        "Exceeds the limit (4300 digits) for integer string conversion; "
                        "use sys.set_int_max_str_digits() to increase the limit");
            } else {
                char integer[64];
                int written = snprintf(
                    integer, sizeof(integer), "%" PRId64,
                    value->ground.ival);
                if (written <= 0 || (size_t)written >= sizeof(integer))
                    return json_value_error(
                        ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                        "invalid ordinary JSON integer");
                return json_bytes_append(
                    out, integer, (size_t)written);
            }
            bool emitted = text && json_number_lexeme_valid(ctx, text, strlen(text)) &&
                           json_bytes_append(out, text, strlen(text));
            if (heap_text)
                free(text);
            if (emitted)
                return true;
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                    "invalid ordinary JSON number");
        }
        return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                "malformed ordinary JSON scalar");
    }
    if (json_expr_is(value, "JsonBoolV1", 2u)) {
        bool boolean_value;
        if (json_bool_view(value->expr.elems[1], &boolean_value))
            return boolean_value
                ? json_bytes_append(out, "true", 4u)
                : json_bytes_append(out, "false", 5u);
        return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                "JsonBoolV1 expects True or False");
    }
    if (json_expr_is(value, "JsonStringV1", 2u))
        return json_emit_string(ctx, value, out);
    if (json_expr_is(value, "JsonNumberV1", 2u)) {
        Atom *number = value->expr.elems[1];
        const char *text = json_text(number);
        if (!text || number->kind != ATOM_GROUNDED ||
            number->ground.gkind != GV_STRING) {
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                    "JsonNumberV1 expects exact text");
        }
        size_t len = json_text_len(number, text);
        if (!json_number_lexeme_valid(ctx, text, len))
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT,
                                    "JSON syntax rejected by authored grammar");
        return json_bytes_append(out, text, len);
    }
    return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "malformed canonical JSON value");
}

typedef struct {
    Atom *const *items;
    CettaExprLen count, index;
    bool object;
    Atom **sorted;   /* owned copy when keys are sorted */
} JsonEmitFrameV1;

typedef struct { Atom *member; size_t index; } JsonKeyedMemberV1;

static int json_member_key_order(const void *left_ptr, const void *right_ptr) {
    const JsonKeyedMemberV1 *left = left_ptr, *right = right_ptr;
    const char *a = json_text(left->member->expr.elems[1]);
    const char *b = json_text(right->member->expr.elems[1]);
    size_t a_len = a ? json_text_len(left->member->expr.elems[1], a) : 0u;
    size_t b_len = b ? json_text_len(right->member->expr.elems[1], b) : 0u;
    /* UTF-8 byte order is code point order, as Python sorts str keys. */
    int order = memcmp(a ? a : "", b ? b : "", a_len < b_len ? a_len : b_len);
    if (order) return order;
    if (a_len != b_len) return a_len < b_len ? -1 : 1;
    return left->index < right->index ? -1 : left->index > right->index ? 1 : 0;
}

/* Members ordered by key, or NULL with ctx carrying the failure. */
static Atom **json_sorted_members(JsonValueCtxV1 *ctx, Atom *const *items, CettaExprLen count) {
    JsonKeyedMemberV1 *keyed = malloc(sizeof(*keyed) * count);
    Atom **sorted = malloc(sizeof(*sorted) * count);
    if (!keyed || !sorted) {
        free(keyed); free(sorted);
        json_value_error(ctx, CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE, "out of memory sorting JSON keys");
        return NULL;
    }
    for (CettaExprLen index = 0u; index < count; index++) {
        if (!json_expr_is(items[index], "JsonPair", 3u) || !json_text(items[index]->expr.elems[1])) {
            free(keyed); free(sorted);
            json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "malformed ordinary JSON member");
            return NULL;
        }
        keyed[index] = (JsonKeyedMemberV1){.member = items[index], .index = index};
    }
    qsort(keyed, count, sizeof(*keyed), json_member_key_order);
    for (CettaExprLen index = 0u; index < count; index++) sorted[index] = keyed[index].member;
    free(keyed);
    return sorted;
}

/* The line is reserved whole, so an indent past the byte limit fails before
 * any of it is copied. */
static bool json_emit_indent(JsonValueCtxV1 *ctx, JsonBytesV1 *out, size_t depth) {
    static const char spaces[] = "                                ";
    size_t unit = ctx->indent_len;
    if (depth && unit > (SIZE_MAX - 1u) / depth)
        return json_value_error(ctx, CETTA_JSON_VALUE_V1_RESOURCE_LIMIT,
                                "JSON output byte limit exceeded");
    if (!json_bytes_reserve(out, 1u + depth * unit) || !json_bytes_char(out, '\n')) return false;
    for (size_t level = 0u; level < depth; level++) {
        if (ctx->indent_text) {
            if (!json_bytes_append(out, ctx->indent_text, unit)) return false;
            continue;
        }
        for (size_t left = unit; left; ) {
            size_t chunk = left < sizeof(spaces) - 1u ? left : sizeof(spaces) - 1u;
            if (!json_bytes_append(out, spaces, chunk)) return false;
            left -= chunk;
        }
    }
    return true;
}

static Atom *json_emit_child(JsonValueCtxV1 *ctx, JsonEmitFrameV1 *frame, JsonBytesV1 *out,
                             bool ordinary, size_t depth) {
    Atom *child = frame->items[frame->index];
    if (frame->index && !(ctx->python
            ? json_bytes_append(out, ctx->item_separator, ctx->item_separator_len)
            : json_bytes_char(out, ','))) return NULL;
    if (ctx->indent && !json_emit_indent(ctx, out, depth)) return NULL;
    if (!frame->object) return child;
    if (ordinary && json_expr_is(child, "JsonPair", 3u) &&
        json_emit_native_text(ctx, child->expr.elems[1], out, false) &&
        (ctx->python ? json_bytes_append(out, ctx->key_separator, ctx->key_separator_len)
                     : json_bytes_char(out, ':') && (!ctx->indent || json_bytes_char(out, ' '))))
        return child->expr.elems[2];
    if (json_expr_is(child, "JsonMemberV1", 5u) &&
        child->expr.elems[1] && child->expr.elems[1]->kind == ATOM_GROUNDED &&
        child->expr.elems[1]->ground.gkind == GV_INT &&
        child->expr.elems[1]->ground.ival == (int64_t)frame->index &&
        json_member_span_valid(child->expr.elems[4]) &&
        json_emit_string(ctx, child->expr.elems[2], out) &&
        (ctx->python ? json_bytes_append(out, ctx->key_separator, ctx->key_separator_len)
                     : json_bytes_char(out, ':') && (!ctx->indent || json_bytes_char(out, ' '))))
        return child->expr.elems[3];
    if (ctx->status == CETTA_JSON_VALUE_V1_OK)
        json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "malformed canonical JSON value");
    return NULL;
}

static bool json_emit_value(JsonValueCtxV1 *ctx, Atom *value, JsonBytesV1 *out, bool ordinary) {
    JsonEmitFrameV1 *frames = NULL;
    size_t len = 0u, cap = 0u;
    Atom *current = value;
    /* Where the chunk of the next scalar began. CPython's streaming encoder
     * yields a list's '[' or item separator glued to a scalar element, but a
     * dict's separators and a container's opening as chunks of their own; a
     * failing integer takes only its own chunk with it. */
    size_t glue = SIZE_MAX;
    for (;;) {
        if (!current) {
            json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "missing JSON value");
            goto failed;
        }
        if (ctx->depth_limit && len >= ctx->depth_limit) {
            json_value_error(ctx, CETTA_JSON_VALUE_V1_RESOURCE_LIMIT, "JSON value nesting limit exceeded");
            goto failed;
        }
        if (!json_value_work(ctx, 1u)) goto failed;
        bool object = json_expr_is(current, ordinary ? "JsonObject" : "JsonObjectV1", 2u);
        if (object || json_expr_is(current, ordinary ? "JsonArray" : "JsonArrayV1", 2u)) {
            Atom *const *items;
            CettaExprLen count;
            if (!atom_sequence_view(current->expr.elems[1], &items, &count)) {
                json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "malformed canonical JSON value");
                goto failed;
            }
            size_t open_at = out->len;
            if (!json_bytes_char(out, object ? '{' : '[')) goto failed;
            if (len == cap) {
                size_t next = cap ? cap * 2u : 16u;
                if (next < cap || next > SIZE_MAX / sizeof(*frames)) goto allocation;
                JsonEmitFrameV1 *grown = realloc(frames, next * sizeof(*frames));
                if (!grown) goto allocation;
                frames = grown; cap = next;
            }
            Atom **sorted = NULL;
            if (object && ctx->sort_keys && count > 1u) {
                if (!ordinary) {
                    json_value_error(ctx, CETTA_JSON_VALUE_V1_BAD_ARGUMENT,
                                     "sorted keys need ordinary JSON values");
                    goto failed;
                }
                sorted = json_sorted_members(ctx, items, count);
                if (!sorted) goto failed;
                items = sorted;
            }
            frames[len++] = (JsonEmitFrameV1){.items = items, .count = count, .object = object,
                                              .sorted = sorted};
            if (!count) goto closed;
            current = json_emit_child(ctx, &frames[len-1u], out, ordinary, len);
            glue = object ? SIZE_MAX : open_at;
            if (!current) goto failed;
            continue;
        }
        if (!json_emit_scalar(ctx, current, out, ordinary)) {
            if (ctx->status == CETTA_JSON_VALUE_V1_PYTHON_VALUE_ERROR && glue != SIZE_MAX)
                out->len = glue;
            goto failed;
        }
        while (len) {
            JsonEmitFrameV1 *frame = &frames[len-1u];
            if (++frame->index < frame->count) {
                size_t separator_at = out->len;
                current = json_emit_child(ctx, frame, out, ordinary, len);
                glue = frame->object ? SIZE_MAX : separator_at;
                if (!current) goto failed;
                break;
            }
closed:
            frame = &frames[len-1u];
            if (ctx->indent && frame->count && !json_emit_indent(ctx, out, len-1u)) goto failed;
            if (!json_bytes_char(out, frame->object ? '}' : ']')) goto failed;
            free(frame->sorted);
            len--;
        }
        if (!len) { free(frames); return true; }
    }
allocation:
    json_value_error(ctx, CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE, "out of memory emitting JSON continuations");
failed:
    for (size_t open = 0u; open < len; open++) free(frames[open].sorted);
    free(frames); return false;
}

static bool json_stringify_impl(
    const CettaJsonRuntimeV1 *runtime, CettaJsonNumberValidatorV1 number_validator,
    Atom *value, bool ordinary_input, bool pretty, const CettaJsonPythonOptionsV1 *python,
    uint32_t work_limit, uint32_t depth_limit, size_t byte_limit,
    uint8_t **bytes_out, size_t *byte_len_out,
    CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    Arena normalized_arena;
    Arena reparsed_arena;
    JsonValueCtxV1 ctx;
    JsonBytesV1 bytes;
    Atom *canonical = value;
    Atom *reparsed = NULL;
    CettaJsonRuntimeV1Limits limits;
    CettaJsonRuntimeV1Status parse_status;
    CettaJsonValueV1Status convert_status;
    bool ok = false;

    if (error_buf && error_buf_size > 0u) error_buf[0] = '\0';
    if (status) *status = CETTA_JSON_VALUE_V1_BAD_ARGUMENT;
    if ((!runtime && !number_validator) || !value || !bytes_out || !byte_len_out) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "bad JSON stringify arguments");
        }
        return false;
    }
    /* These values are borrowed only until emission finishes. Interning a
     * temporary conversion retains needless global roots and compares long,
     * similar prefixes at hash-table collisions. Keep it wholly local. */
    arena_init_detached(&normalized_arena);
    arena_init_detached(&reparsed_arena);
    bool fused_ordinary = ordinary_input && !runtime;
    if (fused_ordinary && !json_validate_ordinary(value, python != NULL, work_limit,
            depth_limit, &convert_status, error_buf, error_buf_size)) {
        if (status) *status = convert_status;
        goto done;
    }
    if (ordinary_input && !fused_ordinary && !cetta_json_value_v1_from_ordinary(
            &normalized_arena, value, work_limit, depth_limit,
            &canonical, &convert_status, error_buf, error_buf_size)) {
        if (status) *status = convert_status;
        goto done;
    }
    ctx = (JsonValueCtxV1){
        .number_validator = number_validator,
        .python = python != NULL,
        .ascii = python && python->ensure_ascii,
        .sort_keys = python && python->sort_keys,
        .item_separator = python ? python->item_separator : NULL,
        .item_separator_len = python ? python->item_separator_len : 0u,
        .key_separator = python ? python->key_separator : NULL,
        .key_separator_len = python ? python->key_separator_len : 0u,
        .work_limit = work_limit,
        .depth_limit = depth_limit,
        .status = CETTA_JSON_VALUE_V1_OK,
        .error = error_buf,
        .error_size = error_buf_size,
    };
    if (pretty) {
        ctx.indent = true;
        ctx.indent_len = 2u;
    } else if (python && python->indent) {
        ctx.indent = true;
        ctx.indent_text = python->indent_text;
        ctx.indent_len = python->indent_len;
    }
    bytes = (JsonBytesV1){.limit = byte_limit ? byte_limit : CETTA_STRING_BYTES_MAX, .ctx = &ctx};
    if (!json_emit_value(&ctx, canonical, &bytes, fused_ordinary)) {
        if (status) *status = ctx.status;
        if (ctx.status == CETTA_JSON_VALUE_V1_PYTHON_VALUE_ERROR) {
            *bytes_out = bytes.bytes;
            *byte_len_out = bytes.len;
            bytes.bytes = NULL;
        }
        goto done_bytes;
    }
    cetta_json_runtime_v1_default_limits(&limits);
    if (work_limit) {
        limits.recognizer_work_limit = work_limit;
        limits.elaboration_work_limit = work_limit;
    }
    if (depth_limit) limits.value_depth_limit = depth_limit;
    if (runtime && (!cetta_json_runtime_v1_parse(
            runtime, &reparsed_arena, bytes.bytes, bytes.len,
            &limits, &reparsed, &parse_status,
            error_buf, error_buf_size) ||
        !json_value_equal_ignoring_source_spans(
            &ctx, canonical, reparsed, 1u))) {
        if (status) *status = CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT;
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            (void)snprintf(
                error_buf, error_buf_size,
                "emitted JSON does not round-trip to the canonical value");
        }
        goto done_bytes;
    }
    *bytes_out = bytes.bytes;
    *byte_len_out = bytes.len;
    bytes.bytes = NULL;
    if (status) *status = CETTA_JSON_VALUE_V1_OK;
    ok = true;

done_bytes:
    free(bytes.bytes);
done:
    arena_free(&reparsed_arena);
    arena_free(&normalized_arena);
    return ok;
}

bool cetta_json_value_v1_stringify(
    const CettaJsonRuntimeV1 *runtime, Atom *value, bool ordinary_input,
    uint32_t work_limit, uint32_t depth_limit, size_t byte_limit,
    uint8_t **bytes_out, size_t *byte_len_out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    if (!runtime) {
        if (status) *status = CETTA_JSON_VALUE_V1_BAD_ARGUMENT;
        return false;
    }
    return json_stringify_impl(runtime, NULL, value, ordinary_input, false, NULL, work_limit,
        depth_limit, byte_limit, bytes_out, byte_len_out, status, error_buf, error_buf_size);
}

bool cetta_json_value_v1_emit(CettaJsonNumberValidatorV1 number_validator,
    Atom *value, bool ordinary_input,
    uint32_t work_limit, uint32_t depth_limit, size_t byte_limit,
    uint8_t **bytes_out, size_t *byte_len_out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    return json_stringify_impl(NULL, number_validator, value, ordinary_input, false, NULL, work_limit,
        depth_limit, byte_limit, bytes_out, byte_len_out, status, error_buf, error_buf_size);
}

bool cetta_json_value_v1_emit_pretty(CettaJsonNumberValidatorV1 number_validator,
    Atom *value, bool ordinary_input,
    uint32_t work_limit, uint32_t depth_limit, size_t byte_limit,
    uint8_t **bytes_out, size_t *byte_len_out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    return json_stringify_impl(NULL, number_validator, value, ordinary_input, true, NULL, work_limit,
        depth_limit, byte_limit, bytes_out, byte_len_out, status, error_buf, error_buf_size);
}

bool cetta_json_value_v1_emit_python(CettaJsonNumberValidatorV1 number_validator,
    Atom *value, bool ordinary_input, const CettaJsonPythonOptionsV1 *options,
    uint32_t work_limit, uint32_t depth_limit, size_t byte_limit,
    uint8_t **bytes_out, size_t *byte_len_out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    static const CettaJsonPythonOptionsV1 defaults = {
        .ensure_ascii = true, .sort_keys = false,
        .item_separator = ", ", .item_separator_len = 2u,
        .key_separator = ": ", .key_separator_len = 2u,
    };
    return json_stringify_impl(NULL, number_validator, value, ordinary_input, false,
        options ? options : &defaults, work_limit,
        depth_limit, byte_limit, bytes_out, byte_len_out, status, error_buf, error_buf_size);
}

const char *cetta_json_value_v1_status_name(CettaJsonValueV1Status status) {
    switch (status) {
    case CETTA_JSON_VALUE_V1_OK: return "ok";
    case CETTA_JSON_VALUE_V1_BAD_ARGUMENT: return "bad-argument";
    case CETTA_JSON_VALUE_V1_MALFORMED_VALUE: return "malformed-value";
    case CETTA_JSON_VALUE_V1_INVALID_UTF8: return "invalid-utf8";
    case CETTA_JSON_VALUE_V1_RESOURCE_LIMIT: return "resource-limit";
    case CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE:
        return "allocation-failure";
    case CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT:
        return "roundtrip-disagreement";
    case CETTA_JSON_VALUE_V1_PYTHON_VALUE_ERROR:
        return "python-value-error";
    }
    return "unknown";
}
