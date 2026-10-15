#include "json_value_v1.h"

#include "lib_parse_native_grammar.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Arena *arena;
    CettaJsonNumberValidatorV1 number_validator;
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

static bool json_value_work(JsonValueCtxV1 *ctx, uint32_t amount) {
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
        if (!json_value_work(ctx, 1u) ||
            !json_expr_is(cp, "cp", 2u) ||
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

static Atom *json_to_legacy_scalar(JsonValueCtxV1 *ctx, Atom *value) {
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
        return json_app1(ctx->arena, "JsonBool",
                         atom_bool(ctx->arena, boolean_value));
    }
    if (json_expr_is(value, "JsonNumberV1", 2u)) {
        Atom *number = value->expr.elems[1];
        if (!number || number->kind != ATOM_GROUNDED ||
            number->ground.gkind != GV_STRING || !number->ground.sval) {
            json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                             "JsonNumberV1 expects an exact text lexeme");
            return NULL;
        }
        return json_app1(ctx->arena, "JsonNumber",
                         atom_string_n(ctx->arena, number->ground.sval,
                                       number->ground.slen));
    }
    if (json_expr_is(value, "JsonStringV1", 2u)) {
        JsonBytesV1 utf8 = {.limit = CETTA_STRING_BYTES_MAX, .ctx = ctx};
        Atom *result = NULL;
        if (json_canonical_string_utf8(ctx, value, &utf8)) {
            result = json_app1(ctx->arena, "JsonString",
                               atom_string_n(ctx->arena,
                                   (const char *)utf8.bytes, utf8.len));
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
                         "out of memory decoding legacy JSON string");
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
    if (!atom) return NULL;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING)
        return atom->ground.sval;
    if (atom->kind == ATOM_SYMBOL) return atom_name_cstr(atom);
    return NULL;
}

/* The caller has already checked that atom is text. */
static size_t json_text_len(Atom *atom, const char *text) {
    return atom->kind == ATOM_GROUNDED ? atom_string_len(atom) : strlen(text);
}

static Atom *json_from_legacy_scalar(JsonValueCtxV1 *ctx, Atom *value) {
    if (atom_is_symbol(value, "JsonNull"))
        return atom_symbol(ctx->arena, "JsonNullV1");
    if (json_expr_is(value, "JsonBool", 2u)) {
        Atom *boolean = value->expr.elems[1];
        bool boolean_value;
        if (!json_bool_view(boolean, &boolean_value)) {
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                    "JsonBool expects True or False"), NULL;
        }
        return json_app1(ctx->arena, "JsonBoolV1",
                         atom_bool(ctx->arena, boolean_value));
    }
    if (json_expr_is(value, "JsonString", 2u)) {
        const char *text = json_text(value->expr.elems[1]);
        if (!text) {
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                    "JsonString expects text"), NULL;
        }
        return json_scalar_list_from_utf8(
            ctx, text, json_text_len(value->expr.elems[1], text));
    }
    if (json_expr_is(value, "JsonNumber", 2u)) {
        const char *text = json_text(value->expr.elems[1]);
        if (!text) {
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                    "JsonNumber expects an exact text lexeme"),
                NULL;
        }
        return json_app1(ctx->arena, "JsonNumberV1",
                         atom_string_n(ctx->arena, text,
                             json_text_len(value->expr.elems[1], text)));
    }

    json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                     "malformed legacy JSON value");
    return NULL;
}

typedef struct {
    Atom *items;
    Atom **converted;
    Atom *key;
    CettaExprIndex index;
    bool object;
} JsonConvertFrameV1;

static Atom *json_convert_child(JsonValueCtxV1 *ctx, JsonConvertFrameV1 *frame,
                                bool to_legacy) {
    Atom *child = frame->items->expr.elems[frame->index];
    if (!frame->object) return child;
    if (to_legacy) {
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
        if (key && (frame->key = json_scalar_list_from_utf8(
                ctx, key, json_text_len(child->expr.elems[1], key))))
            return child->expr.elems[2];
    }
    if (ctx->status == CETTA_JSON_VALUE_V1_OK)
        json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "malformed JSON object member");
    return NULL;
}

static Atom *json_convert_value(JsonValueCtxV1 *ctx, Atom *value, bool to_legacy) {
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
        bool object = json_expr_is(current, to_legacy ? "JsonObjectV1" : "JsonObject", 2u);
        if (object || json_expr_is(current, to_legacy ? "JsonArrayV1" : "JsonArray", 2u)) {
            Atom *items = current->expr.elems[1];
            if (!items || items->kind != ATOM_EXPR) {
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
            Atom **converted = items->expr.len ? calloc(items->expr.len, sizeof(*converted)) : NULL;
            if (items->expr.len && !converted) goto allocation;
            frames[len++] = (JsonConvertFrameV1){.items = items, .converted = converted, .object = object};
            if (!items->expr.len) goto closed;
            current = json_convert_child(ctx, &frames[len-1u], to_legacy);
            if (!current) goto failed;
            continue;
        }
        current = to_legacy ? json_to_legacy_scalar(ctx, current) : json_from_legacy_scalar(ctx, current);
        if (!current) goto failed;
        for (;;) {
            if (!len) { free(frames); return current; }
            JsonConvertFrameV1 *frame = &frames[len-1u];
            if (frame->object) current = to_legacy
                ? json_app2(ctx->arena, "JsonPair", frame->key, current)
                : json_app4(ctx->arena, "JsonMemberV1", atom_int(ctx->arena, frame->index),
                            frame->key, current, atom_symbol(ctx->arena, "JsonNoSourceSpanV1"));
            frame->converted[frame->index++] = current;
            if (frame->index < frame->items->expr.len) {
                current = json_convert_child(ctx, frame, to_legacy);
                if (!current) goto failed;
                break;
            }
closed:
            frame = &frames[len-1u];
            current = json_app1(ctx->arena, frame->object ? (to_legacy ? "JsonObject" : "JsonObjectV1") :
                               (to_legacy ? "JsonArray" : "JsonArrayV1"),
                               atom_expr(ctx->arena, frame->converted, frame->items->expr.len));
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
                         bool to_legacy) {
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
    result = json_convert_value(&ctx, value, to_legacy);
    if (!result) {
        arena_reset(arena, mark);
        if (status) *status = ctx.status;
        return false;
    }
    *out = result;
    if (status) *status = CETTA_JSON_VALUE_V1_OK;
    return true;
}

bool cetta_json_value_v1_to_legacy(
    Arena *arena, Atom *canonical, uint32_t work_limit,
    uint32_t depth_limit, Atom **out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    return json_convert(arena, canonical, work_limit, depth_limit,
                        out, status, error_buf, error_buf_size, true);
}

bool cetta_json_value_v1_from_legacy(
    Arena *arena, Atom *legacy, uint32_t work_limit,
    uint32_t depth_limit, Atom **out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    return json_convert(arena, legacy, work_limit, depth_limit,
                        out, status, error_buf, error_buf_size, false);
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
            } else if (!json_utf8_append_scalar(out, scalar)) {
                return false;
            }
            break;
        }
    }
    return json_bytes_char(out, '"');
}

static bool json_emit_scalar(JsonValueCtxV1 *ctx, Atom *value, JsonBytesV1 *out) {
    if (atom_is_symbol(value, "JsonNullV1"))
        return json_bytes_append(out, "null", 4u);
    if (json_expr_is(value, "JsonBoolV1", 2u)) {
        bool boolean_value;
        if (json_bool_view(value->expr.elems[1], &boolean_value)) {
            return boolean_value
                ? json_bytes_append(out, "true", 4u)
                : json_bytes_append(out, "false", 5u);
        }
        return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                "JsonBoolV1 expects True or False");
    }
    if (json_expr_is(value, "JsonStringV1", 2u))
        return json_emit_string(ctx, value, out);
    if (json_expr_is(value, "JsonNumberV1", 2u)) {
        Atom *number = value->expr.elems[1];
        if (!number || number->kind != ATOM_GROUNDED ||
            number->ground.gkind != GV_STRING || !number->ground.sval) {
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                    "JsonNumberV1 expects exact text");
        }
        /* Reference emission is checked by the full runtime roundtrip.
         * Fast emission supplies its lexical interpretation explicitly. */
        if (ctx->number_validator && !ctx->number_validator(
                (const uint8_t *)number->ground.sval, number->ground.slen))
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT,
                                    "JSON syntax rejected by authored grammar");
        return json_bytes_append(out, number->ground.sval,
                                 number->ground.slen);
    }
    return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "malformed canonical JSON value");
}

typedef struct { Atom *items; CettaExprIndex index; bool object; } JsonEmitFrameV1;

static Atom *json_emit_child(JsonValueCtxV1 *ctx, JsonEmitFrameV1 *frame, JsonBytesV1 *out) {
    Atom *child = frame->items->expr.elems[frame->index];
    if (frame->index && !json_bytes_char(out, ',')) return NULL;
    if (!frame->object) return child;
    if (json_expr_is(child, "JsonMemberV1", 5u) &&
        child->expr.elems[1] && child->expr.elems[1]->kind == ATOM_GROUNDED &&
        child->expr.elems[1]->ground.gkind == GV_INT &&
        child->expr.elems[1]->ground.ival == (int64_t)frame->index &&
        json_member_span_valid(child->expr.elems[4]) &&
        json_emit_string(ctx, child->expr.elems[2], out) && json_bytes_char(out, ':'))
        return child->expr.elems[3];
    if (ctx->status == CETTA_JSON_VALUE_V1_OK)
        json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "malformed canonical JSON value");
    return NULL;
}

static bool json_emit_value(JsonValueCtxV1 *ctx, Atom *value, JsonBytesV1 *out) {
    JsonEmitFrameV1 *frames = NULL;
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
        bool object = json_expr_is(current, "JsonObjectV1", 2u);
        if (object || json_expr_is(current, "JsonArrayV1", 2u)) {
            Atom *items = current->expr.elems[1];
            if (!items || items->kind != ATOM_EXPR) {
                json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE, "malformed canonical JSON value");
                goto failed;
            }
            if (!json_bytes_char(out, object ? '{' : '[')) goto failed;
            if (len == cap) {
                size_t next = cap ? cap * 2u : 16u;
                if (next < cap || next > SIZE_MAX / sizeof(*frames)) goto allocation;
                JsonEmitFrameV1 *grown = realloc(frames, next * sizeof(*frames));
                if (!grown) goto allocation;
                frames = grown; cap = next;
            }
            frames[len++] = (JsonEmitFrameV1){.items = items, .object = object};
            if (!items->expr.len) goto closed;
            current = json_emit_child(ctx, &frames[len-1u], out);
            if (!current) goto failed;
            continue;
        }
        if (!json_emit_scalar(ctx, current, out)) goto failed;
        while (len) {
            JsonEmitFrameV1 *frame = &frames[len-1u];
            if (++frame->index < frame->items->expr.len) {
                current = json_emit_child(ctx, frame, out);
                if (!current) goto failed;
                break;
            }
closed:
            frame = &frames[len-1u];
            if (!json_bytes_char(out, frame->object ? '}' : ']')) goto failed;
            len--;
        }
        if (!len) { free(frames); return true; }
    }
allocation:
    json_value_error(ctx, CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE, "out of memory emitting JSON continuations");
failed:
    free(frames); return false;
}

static bool json_stringify_impl(
    const CettaJsonRuntimeV1 *runtime, CettaJsonNumberValidatorV1 number_validator,
    Atom *value, bool legacy_input,
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
    if (legacy_input && !cetta_json_value_v1_from_legacy(
            &normalized_arena, value, work_limit, depth_limit,
            &canonical, &convert_status, error_buf, error_buf_size)) {
        if (status) *status = convert_status;
        goto done;
    }
    ctx = (JsonValueCtxV1){
        .number_validator = number_validator,
        .work_limit = work_limit,
        .depth_limit = depth_limit,
        .status = CETTA_JSON_VALUE_V1_OK,
        .error = error_buf,
        .error_size = error_buf_size,
    };
    bytes = (JsonBytesV1){.limit = byte_limit ? byte_limit : CETTA_STRING_BYTES_MAX, .ctx = &ctx};
    if (!json_emit_value(&ctx, canonical, &bytes)) {
        if (status) *status = ctx.status;
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
    const CettaJsonRuntimeV1 *runtime, Atom *value, bool legacy_input,
    uint32_t work_limit, uint32_t depth_limit, size_t byte_limit,
    uint8_t **bytes_out, size_t *byte_len_out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    if (!runtime) {
        if (status) *status = CETTA_JSON_VALUE_V1_BAD_ARGUMENT;
        return false;
    }
    return json_stringify_impl(runtime, NULL, value, legacy_input, work_limit,
        depth_limit, byte_limit, bytes_out, byte_len_out, status, error_buf, error_buf_size);
}

bool cetta_json_value_v1_emit(CettaJsonNumberValidatorV1 number_validator,
    Atom *value, bool legacy_input,
    uint32_t work_limit, uint32_t depth_limit, size_t byte_limit,
    uint8_t **bytes_out, size_t *byte_len_out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size) {
    return json_stringify_impl(NULL, number_validator, value, legacy_input, work_limit,
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
    }
    return "unknown";
}
