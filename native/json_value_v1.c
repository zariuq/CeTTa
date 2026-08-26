#include "json_value_v1.h"

#include "lib_parse_native_grammar.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Arena *arena;
    uint32_t work;
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
    if (!ctx || amount > ctx->work_limit - ctx->work) {
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

static Atom *json_app3(Arena *arena, const char *head,
                       Atom *first, Atom *second, Atom *third) {
    Atom *items[4] = {atom_symbol(arena, head), first, second, third};
    return atom_expr(arena, items, 4u);
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
                                       JsonBytesV1 *out,
                                       bool reject_nul) {
    Atom *list;
    CettaExprIndex index;
    if (!json_scalar_list_view(ctx, string, &list)) return false;
    for (index = 0u; index < list->expr.len; index++) {
        uint32_t scalar = (uint32_t)
            list->expr.elems[index]->expr.elems[1]->ground.ival;
        if (reject_nul && scalar == 0u) {
            return json_value_error(
                ctx, CETTA_JSON_VALUE_V1_UNREPRESENTABLE_LEGACY_STRING,
                "legacy grounded strings cannot retain U+0000");
        }
        if (!json_utf8_append_scalar(out, scalar)) return false;
    }
    return true;
}

static Atom *json_to_legacy(JsonValueCtxV1 *ctx, Atom *value,
                            uint32_t depth) {
    CettaExprIndex index;
    if (!value || depth > ctx->depth_limit || !json_value_work(ctx, 1u)) {
        if (ctx->status == CETTA_JSON_VALUE_V1_OK) {
            json_value_error(ctx, CETTA_JSON_VALUE_V1_RESOURCE_LIMIT,
                             "JSON value nesting limit exceeded");
        }
        return NULL;
    }
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
                         atom_string(ctx->arena, number->ground.sval));
    }
    if (json_expr_is(value, "JsonStringV1", 2u)) {
        JsonBytesV1 utf8 = {.limit = SIZE_MAX, .ctx = ctx};
        Atom *result = NULL;
        if (json_canonical_string_utf8(ctx, value, &utf8, true) &&
            json_bytes_char(&utf8, 0u)) {
            result = json_app1(ctx->arena, "JsonString",
                               atom_string(ctx->arena,
                                           (const char *)utf8.bytes));
        }
        free(utf8.bytes);
        return result;
    }
    if (json_expr_is(value, "JsonArrayV1", 2u)) {
        Atom *items = value->expr.elems[1];
        Atom **converted;
        Atom *list;
        if (!items || items->kind != ATOM_EXPR) goto malformed;
        converted = (Atom **)calloc(items->expr.len ? items->expr.len : 1u,
                                    sizeof(*converted));
        if (!converted) goto allocation;
        for (index = 0u; index < items->expr.len; index++) {
            converted[index] = json_to_legacy(
                ctx, items->expr.elems[index], depth + 1u);
            if (!converted[index]) {
                free(converted);
                return NULL;
            }
        }
        list = atom_expr(ctx->arena, converted, items->expr.len);
        free(converted);
        return json_app1(ctx->arena, "JsonArray", list);
    }
    if (json_expr_is(value, "JsonObjectV1", 2u)) {
        Atom *members = value->expr.elems[1];
        Atom **pairs;
        Atom *list;
        if (!members || members->kind != ATOM_EXPR) goto malformed;
        pairs = (Atom **)calloc(members->expr.len ? members->expr.len : 1u,
                               sizeof(*pairs));
        if (!pairs) goto allocation;
        for (index = 0u; index < members->expr.len; index++) {
            Atom *member = members->expr.elems[index];
            JsonBytesV1 utf8 = {.limit = SIZE_MAX, .ctx = ctx};
            Atom *member_value;
            if (!json_expr_is(member, "JsonMemberV1", 4u) ||
                !member->expr.elems[1] ||
                member->expr.elems[1]->kind != ATOM_GROUNDED ||
                member->expr.elems[1]->ground.gkind != GV_INT ||
                member->expr.elems[1]->ground.ival != (int64_t)index ||
                !json_canonical_string_utf8(
                    ctx, member->expr.elems[2], &utf8, true) ||
                !json_bytes_char(&utf8, 0u) ||
                !(member_value = json_to_legacy(
                      ctx, member->expr.elems[3], depth + 1u))) {
                free(utf8.bytes);
                free(pairs);
                if (ctx->status == CETTA_JSON_VALUE_V1_OK) goto malformed;
                return NULL;
            }
            pairs[index] = json_app2(
                ctx->arena, "JsonPair",
                atom_string(ctx->arena, (const char *)utf8.bytes),
                member_value);
            free(utf8.bytes);
        }
        list = atom_expr(ctx->arena, pairs, members->expr.len);
        free(pairs);
        return json_app1(ctx->arena, "JsonObject", list);
    }

malformed:
    json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                     "malformed canonical JSON value");
    return NULL;
allocation:
    json_value_error(ctx, CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE,
                     "out of memory converting JSON value");
    return NULL;
}

static Atom *json_scalar_list_from_utf8(JsonValueCtxV1 *ctx,
                                        const char *text) {
    CettaLpNativeUtf8ScalarBuffer scalars;
    Atom **items = NULL;
    Atom *list = NULL;
    Atom *result = NULL;
    uint32_t index;
    char error[256] = {0};
    cetta_lp_native_utf8_scalar_buffer_init(&scalars);
    if (!cetta_lp_native_utf8_scalar_buffer_decode(
            &scalars, (const uint8_t *)text, strlen(text),
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

static Atom *json_from_legacy(JsonValueCtxV1 *ctx, Atom *value,
                              uint32_t depth) {
    CettaExprIndex index;
    if (!value || depth > ctx->depth_limit || !json_value_work(ctx, 1u)) {
        if (ctx->status == CETTA_JSON_VALUE_V1_OK) {
            json_value_error(ctx, CETTA_JSON_VALUE_V1_RESOURCE_LIMIT,
                             "JSON value nesting limit exceeded");
        }
        return NULL;
    }
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
        return json_scalar_list_from_utf8(ctx, text);
    }
    if (json_expr_is(value, "JsonNumber", 2u)) {
        const char *text = json_text(value->expr.elems[1]);
        if (!text) {
            return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                                    "JsonNumber expects an exact text lexeme"),
                NULL;
        }
        return json_app1(ctx->arena, "JsonNumberV1",
                         atom_string(ctx->arena, text));
    }
    if (json_expr_is(value, "JsonArray", 2u)) {
        Atom *items = value->expr.elems[1];
        Atom **converted;
        Atom *list;
        if (!items || items->kind != ATOM_EXPR) goto malformed;
        converted = (Atom **)calloc(items->expr.len ? items->expr.len : 1u,
                                    sizeof(*converted));
        if (!converted) goto allocation;
        for (index = 0u; index < items->expr.len; index++) {
            converted[index] = json_from_legacy(
                ctx, items->expr.elems[index], depth + 1u);
            if (!converted[index]) {
                free(converted);
                return NULL;
            }
        }
        list = atom_expr(ctx->arena, converted, items->expr.len);
        free(converted);
        return json_app1(ctx->arena, "JsonArrayV1", list);
    }
    if (json_expr_is(value, "JsonObject", 2u)) {
        Atom *pairs = value->expr.elems[1];
        Atom **members;
        Atom *list;
        if (!pairs || pairs->kind != ATOM_EXPR) goto malformed;
        members = (Atom **)calloc(pairs->expr.len ? pairs->expr.len : 1u,
                                 sizeof(*members));
        if (!members) goto allocation;
        for (index = 0u; index < pairs->expr.len; index++) {
            Atom *pair = pairs->expr.elems[index];
            const char *key;
            Atom *key_value;
            Atom *member_value;
            if (!json_expr_is(pair, "JsonPair", 3u) ||
                !(key = json_text(pair->expr.elems[1])) ||
                !(key_value = json_scalar_list_from_utf8(ctx, key)) ||
                !(member_value = json_from_legacy(
                      ctx, pair->expr.elems[2], depth + 1u))) {
                free(members);
                if (ctx->status == CETTA_JSON_VALUE_V1_OK) goto malformed;
                return NULL;
            }
            members[index] = json_app3(
                ctx->arena, "JsonMemberV1",
                atom_int(ctx->arena, (int64_t)index),
                key_value, member_value);
        }
        list = atom_expr(ctx->arena, members, pairs->expr.len);
        free(members);
        return json_app1(ctx->arena, "JsonObjectV1", list);
    }

malformed:
    json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                     "malformed legacy JSON value");
    return NULL;
allocation:
    json_value_error(ctx, CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE,
                     "out of memory converting JSON value");
    return NULL;
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
    if (!arena || !value || !out || work_limit == 0u || depth_limit == 0u) {
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
    result = to_legacy
        ? json_to_legacy(&ctx, value, 1u)
        : json_from_legacy(&ctx, value, 1u);
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

static bool json_emit_value(JsonValueCtxV1 *ctx, Atom *value,
                            JsonBytesV1 *out, uint32_t depth) {
    CettaExprIndex index;
    if (!value || depth > ctx->depth_limit || !json_value_work(ctx, 1u)) {
        if (ctx->status == CETTA_JSON_VALUE_V1_OK) {
            json_value_error(ctx, CETTA_JSON_VALUE_V1_RESOURCE_LIMIT,
                             "JSON value nesting limit exceeded");
        }
        return false;
    }
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
        return json_bytes_append(out, number->ground.sval,
                                 strlen(number->ground.sval));
    }
    if (json_expr_is(value, "JsonArrayV1", 2u)) {
        Atom *items = value->expr.elems[1];
        if (!items || items->kind != ATOM_EXPR ||
            !json_bytes_char(out, '[')) goto malformed;
        for (index = 0u; index < items->expr.len; index++) {
            if ((index > 0u && !json_bytes_char(out, ',')) ||
                !json_emit_value(ctx, items->expr.elems[index],
                                 out, depth + 1u)) return false;
        }
        return json_bytes_char(out, ']');
    }
    if (json_expr_is(value, "JsonObjectV1", 2u)) {
        Atom *members = value->expr.elems[1];
        if (!members || members->kind != ATOM_EXPR ||
            !json_bytes_char(out, '{')) goto malformed;
        for (index = 0u; index < members->expr.len; index++) {
            Atom *member = members->expr.elems[index];
            if (!json_expr_is(member, "JsonMemberV1", 4u) ||
                !member->expr.elems[1] ||
                member->expr.elems[1]->kind != ATOM_GROUNDED ||
                member->expr.elems[1]->ground.gkind != GV_INT ||
                member->expr.elems[1]->ground.ival != (int64_t)index ||
                (index > 0u && !json_bytes_char(out, ',')) ||
                !json_emit_string(ctx, member->expr.elems[2], out) ||
                !json_bytes_char(out, ':') ||
                !json_emit_value(ctx, member->expr.elems[3],
                                 out, depth + 1u)) {
                if (ctx->status == CETTA_JSON_VALUE_V1_OK) goto malformed;
                return false;
            }
        }
        return json_bytes_char(out, '}');
    }
malformed:
    return json_value_error(ctx, CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
                            "malformed canonical JSON value");
}

bool cetta_json_value_v1_stringify(
    const CettaJsonRuntimeV1 *runtime, Atom *value, bool legacy_input,
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
    if (!runtime || !value || !bytes_out || !byte_len_out ||
        work_limit == 0u || depth_limit == 0u || byte_limit == 0u) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "bad JSON stringify arguments");
        }
        return false;
    }
    arena_init(&normalized_arena);
    arena_init(&reparsed_arena);
    if (legacy_input && !cetta_json_value_v1_from_legacy(
            &normalized_arena, value, work_limit, depth_limit,
            &canonical, &convert_status, error_buf, error_buf_size)) {
        if (status) *status = convert_status;
        goto done;
    }
    ctx = (JsonValueCtxV1){
        .work_limit = work_limit,
        .depth_limit = depth_limit,
        .status = CETTA_JSON_VALUE_V1_OK,
        .error = error_buf,
        .error_size = error_buf_size,
    };
    bytes = (JsonBytesV1){.limit = byte_limit, .ctx = &ctx};
    if (!json_emit_value(&ctx, canonical, &bytes, 1u)) {
        if (status) *status = ctx.status;
        goto done_bytes;
    }
    cetta_json_runtime_v1_default_limits(&limits);
    limits.recognizer_work_limit = work_limit;
    limits.elaboration_work_limit = work_limit;
    limits.value_depth_limit = depth_limit;
    if (!cetta_json_runtime_v1_parse(
            runtime, &reparsed_arena, bytes.bytes, bytes.len,
            &limits, &reparsed, &parse_status,
            error_buf, error_buf_size) || !atom_eq(canonical, reparsed)) {
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

const char *cetta_json_value_v1_status_name(CettaJsonValueV1Status status) {
    switch (status) {
    case CETTA_JSON_VALUE_V1_OK: return "ok";
    case CETTA_JSON_VALUE_V1_BAD_ARGUMENT: return "bad-argument";
    case CETTA_JSON_VALUE_V1_MALFORMED_VALUE: return "malformed-value";
    case CETTA_JSON_VALUE_V1_INVALID_UTF8: return "invalid-utf8";
    case CETTA_JSON_VALUE_V1_UNREPRESENTABLE_LEGACY_STRING:
        return "unrepresentable-legacy-string";
    case CETTA_JSON_VALUE_V1_RESOURCE_LIMIT: return "resource-limit";
    case CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE:
        return "allocation-failure";
    case CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT:
        return "roundtrip-disagreement";
    }
    return "unknown";
}
