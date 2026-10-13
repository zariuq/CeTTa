#define _POSIX_C_SOURCE 200809L
#include "json_direct_v1.h"
#include "symbol.h"
#include <stdlib.h>
#include <string.h>
#include "runtime/generated/json/lexical_v1.generated.h"
#include "runtime/generated/json/predictive_v1.generated.h"

typedef enum {
    JD_CP, JD_STRING, JD_NUMBER, JD_BOOL, JD_ARRAY, JD_OBJECT, JD_MEMBER, JD_SPAN,
    JD_NULL, JD_HEAD_COUNT
} JsonHeadV1;

typedef struct {
    Arena *arena;
    const uint8_t *bytes;
    size_t len, at, scalar;
    uint32_t depth_limit;
    bool legacy, petta;
    CettaJsonRuntimeV1Status status;
    char *error;
    size_t error_size;
    unsigned lex;
    bool lex_active;
    Atom *heads[JD_HEAD_COUNT];
} JsonDirectV1;

typedef struct { Atom **items; size_t len, cap; } JsonVectorV1;

static Atom *jd_error(JsonDirectV1 *p, CettaJsonRuntimeV1Status status,
                       const char *expected) {
    p->status = status;
    if (p->error && p->error_size)
        snprintf(p->error, p->error_size, "JSON byte %zu: expected %s", p->at, expected);
    return NULL;
}

static bool jd_lex_advance(JsonDirectV1 *p, uint32_t cp) {
    if (!p->lex_active) return true;
    unsigned next = jd_lex_step(p->lex, cp);
    if (next == UINT32_MAX) {
        jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "a character admitted by the lexical grammar");
        return false;
    }
    p->lex = next; return true;
}

static Atom *jd_head(JsonDirectV1 *p, JsonHeadV1 head) {
    static const char *const legacy[] = {"cp", "JsonString", "JsonNumber", "JsonBool", "JsonArray", "JsonObject", "JsonPair", "JsonSourceSpanV1", "JsonNull"};
    static const char *const canonical[] = {"cp", "JsonStringV1", "JsonNumberV1", "JsonBoolV1", "JsonArrayV1", "JsonObjectV1", "JsonMemberV1", "JsonSourceSpanV1", "JsonNullV1"};
    if (!p->heads[head]) p->heads[head] = atom_symbol(p->arena, (p->legacy ? legacy : canonical)[head]);
    return p->heads[head];
}

static Atom *jd_app(JsonDirectV1 *p, JsonHeadV1 head, Atom **args, size_t count) {
    Atom *items[5];
    items[0] = jd_head(p, head);
    for (size_t i = 0; i < count; ++i) items[i + 1] = args[i];
    return atom_expr(p->arena, items, count + 1);
}

static Atom *jd_one(JsonDirectV1 *p, JsonHeadV1 head, Atom *value) {
    return jd_app(p, head, &value, 1);
}

static bool jd_push(JsonDirectV1 *p, JsonVectorV1 *v, Atom *value) {
    if (!value) return false;
    if (v->len == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 8;
        if (cap < v->cap || cap > UINT32_MAX || cap > SIZE_MAX / sizeof(Atom *)) {
            jd_error(p, CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT, "a smaller JSON container");
            return false;
        }
        Atom **items = realloc(v->items, cap * sizeof(*items));
        if (!items) {
            jd_error(p, CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE, "memory for JSON values");
            return false;
        }
        v->items = items; v->cap = cap;
    }
    v->items[v->len++] = value;
    return true;
}

static void jd_ws(JsonDirectV1 *p) {
    while (p->at < p->len && (p->bytes[p->at] == ' ' || p->bytes[p->at] == '\t' ||
           p->bytes[p->at] == '\n' || p->bytes[p->at] == '\r')) {
        p->at++; p->scalar++;
    }
}

static bool jd_take(JsonDirectV1 *p, uint8_t byte) {
    if (p->at == p->len || p->bytes[p->at] != byte) return false;
    if (!jd_lex_advance(p, byte)) return false;
    p->at++; p->scalar++;
    return true;
}

/* Strict Unicode scalars: reject overlong encodings, surrogate UTF-8 and
 * out-of-range code points instead of silently replacing them. */
static bool jd_scalar(JsonDirectV1 *p, uint32_t *out) {
    if (p->at == p->len) return false;
    uint8_t first = p->bytes[p->at];
    size_t n = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
               first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    uint32_t cp = first & (n == 1 ? 0x7f : n == 2 ? 0x1f : n == 3 ? 0xf : 7);
    if (!n || n > p->len - p->at) goto invalid;
    for (size_t i = 1; i < n; i++) {
        uint8_t next = p->bytes[p->at + i];
        if ((next & 0xc0) != 0x80) goto invalid;
        cp = (cp << 6) | (next & 0x3f);
    }
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
        (n == 4 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        goto invalid;
    if (!jd_lex_advance(p, cp)) return false;
    p->at += n; p->scalar++; *out = cp;
    return true;
invalid:
    jd_error(p, CETTA_JSON_RUNTIME_V1_INVALID_UTF8, "a UTF-8 Unicode scalar");
    return false;
}

static bool jd_hex(JsonDirectV1 *p, uint32_t *out) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; i++) {
        if (p->at == p->len) goto invalid;
        uint8_t c = p->bytes[p->at];
        int digit = c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) goto invalid;
        if (!jd_lex_advance(p, c)) return false;
        value = (value << 4) | (uint32_t)digit; p->at++; p->scalar++;
    }
    *out = value; return true;
invalid:
    jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "four hexadecimal escape digits");
    return false;
}

static Atom *jd_string(JsonDirectV1 *p, bool key) {
    p->lex_active = true; p->lex = JD_LEX_START;
    if (!jd_take(p, '"')) return jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "a quoted string");
    size_t begin = p->at, written = 0, cap = 0;
    uint8_t *decoded = NULL;
    JsonVectorV1 scalars = {0};
    Atom *result = NULL;
    if (p->legacy) {
        /* The common unescaped ASCII span is validated and copied once. */
        while (p->at < p->len && p->bytes[p->at] >= 32 && p->bytes[p->at] < 128 &&
               p->bytes[p->at] != '"' && p->bytes[p->at] != '\\') {
            if (!jd_lex_advance(p, p->bytes[p->at])) goto done;
            p->at++; p->scalar++;
        }
        written = p->at - begin;
        if (p->at < p->len && p->bytes[p->at] != '"') {
            cap = written < 60 ? 64 : written + 4;
            decoded = malloc(cap);
            if (!decoded) goto allocation;
            memcpy(decoded, p->bytes + begin, written);
        }
    }
    while (p->at < p->len && p->bytes[p->at] != '"') {
        uint32_t cp;
        if (jd_take(p, '\\')) {
            if (p->at == p->len) goto escape_error;
            uint8_t c = p->bytes[p->at];
            if (!jd_lex_advance(p, c)) goto done;
            p->at++; p->scalar++;
            switch (c) {
            case '"': case '\\': case '/': cp = c; break;
            case 'b': cp = 8; break;
            case 'f': cp = 12; break;
            case 'n': cp = 10; break;
            case 'r': cp = 13; break;
            case 't': cp = 9; break;
            case 'u':
                if (!jd_hex(p, &cp)) goto done;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    uint32_t low;
                    if (!jd_take(p, '\\') || !jd_take(p, 'u') || !jd_hex(p, &low) ||
                        low < 0xdc00 || low > 0xdfff) goto surrogate_error;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) goto surrogate_error;
                break;
            default: goto escape_error;
            }
        } else {
            if (p->bytes[p->at] < 32) {
                jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "an escaped control character");
                goto done;
            }
            if (!jd_scalar(p, &cp)) goto done;
        }
        if (!p->legacy) {
            if (!jd_push(p, &scalars, jd_one(p, JD_CP, atom_int(p->arena, cp)))) goto done;
        } else {
            if (cap - written < 4) {
                size_t next = cap ? cap * 2 : 64;
                if (next < cap) goto allocation;
                uint8_t *grown = realloc(decoded, next);
                if (!grown) goto allocation;
                decoded = grown; cap = next;
            }
            if (cp < 0x80) decoded[written++] = cp;
            else if (cp < 0x800) {
                decoded[written++] = 0xc0 | (cp >> 6); decoded[written++] = 0x80 | (cp & 63);
            } else if (cp < 0x10000) {
                decoded[written++] = 0xe0 | (cp >> 12);
                decoded[written++] = 0x80 | ((cp >> 6) & 63); decoded[written++] = 0x80 | (cp & 63);
            } else {
                decoded[written++] = 0xf0 | (cp >> 18);
                decoded[written++] = 0x80 | ((cp >> 12) & 63);
                decoded[written++] = 0x80 | ((cp >> 6) & 63); decoded[written++] = 0x80 | (cp & 63);
            }
        }
    }
    if (!jd_take(p, '"')) {
        jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "a closing string quote"); goto done;
    }
    if (!(jd_lex_accept[p->lex] & 4u)) {
        jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "a complete string token"); goto done;
    }
    if (p->legacy) {
        Atom *text = atom_string_n(p->arena, (const char *)(decoded ? decoded : p->bytes + begin), written);
        result = key ? text : jd_one(p, JD_STRING, text);
    } else result = jd_one(p, JD_STRING, atom_expr(p->arena, scalars.items, scalars.len));
    goto done;
escape_error:
    jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "a JSON escape"); goto done;
surrogate_error:
    jd_error(p, CETTA_JSON_RUNTIME_V1_INVALID_UNICODE_ESCAPE, "a paired Unicode surrogate escape"); goto done;
allocation:
    jd_error(p, CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE, "memory for a JSON string");
done:
    p->lex_active = false;
    free(decoded); free(scalars.items); return result;
}

static Atom *jd_number(JsonDirectV1 *p) {
    size_t start = p->at;
    unsigned state = JD_LEX_START;
    while (p->at < p->len) {
        unsigned next = jd_lex_step(state, p->bytes[p->at]);
        if (next == UINT32_MAX) break;
        state = next; p->at++; p->scalar++;
    }
    if (!(jd_lex_accept[state] & 2u)) goto invalid;
    return jd_one(p, JD_NUMBER,
                  atom_string_n(p->arena, (const char *)p->bytes + start, p->at - start));
invalid:
    return jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "a JSON number digit");
}

bool cetta_json_direct_v1_number_valid(const uint8_t *bytes, size_t len) {
    unsigned state = JD_LEX_START;
    for (size_t at = 0; at < len; at++) {
        state = jd_lex_step(state, bytes[at]);
        if (state == UINT32_MAX) return false;
    }
    return !!(jd_lex_accept[state] & 2u);
}

typedef enum { JD_START_ERROR, JD_START_SCALAR, JD_START_ARRAY, JD_START_OBJECT } JsonStartV1;
#include "runtime/generated/json/predictive_v1.generated.inc"

typedef struct {
    JsonVectorV1 values;
    Atom *key;
    size_t member_start;
    bool object;
} JsonFrameV1;

/* The continuation is an explicit heap zipper. Closing a container feeds its
 * value straight to its parent; neither parsing nor value construction uses
 * the C call stack. An optional caller budget is separate from the grammar. */
static Atom *jd_value(JsonDirectV1 *p) {
    JsonFrameV1 *frames = NULL;
    size_t len = 0, cap = 0;
    Atom *value = NULL;
    for (;;) {
        if (p->depth_limit && len >= p->depth_limit) {
            jd_error(p, CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT, "a shallower JSON value");
            goto failed;
        }
        JsonStartV1 start = jd_value_start(p, &value);
        if (start == JD_START_ERROR) goto failed;
        if (start != JD_START_SCALAR) {
            if (len == cap) {
                size_t next = cap ? cap * 2 : 16;
                if (next < cap || next > SIZE_MAX / sizeof(*frames)) goto allocation;
                JsonFrameV1 *grown = realloc(frames, next * sizeof(*frames));
                if (!grown) goto allocation;
                frames = grown; cap = next;
            }
            frames[len++] = (JsonFrameV1){.object = start == JD_START_OBJECT};
            p->at++; p->scalar++; jd_ws(p);
            JsonFrameV1 *frame = &frames[len-1];
            if (jd_take(p, frame->object ? JD_OBJECT_CLOSE : JD_ARRAY_CLOSE))
                goto closed;
            goto next_member;
        }
        for (;;) {
            if (!len) { free(frames); return value; }
            JsonFrameV1 *frame = &frames[len-1];
            if (frame->object) {
                if (p->legacy) {
                    Atom *args[] = {frame->key, value};
                    value = jd_app(p, JD_MEMBER, args, 2);
                } else {
                    Atom *span[] = {atom_int(p->arena, frame->member_start), atom_int(p->arena, p->scalar)};
                    Atom *args[] = {atom_int(p->arena, frame->values.len), frame->key, value,
                                   jd_app(p, JD_SPAN, span, 2)};
                    value = jd_app(p, JD_MEMBER, args, 4);
                }
            }
            if (!jd_push(p, &frame->values, value)) goto failed;
            jd_ws(p);
            if (!jd_take(p, frame->object ? JD_OBJECT_CLOSE : JD_ARRAY_CLOSE)) {
                if (!jd_take(p, frame->object ? JD_MEMBER_COMMA : JD_ELEMENT_COMMA)) {
                    jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
                             frame->object ? "',' or '}'" : "',' or ']'");
                    goto failed;
                }
                jd_ws(p);
                goto next_member;
            }
closed:
            frame = &frames[len-1];
            value = jd_one(p, frame->object ? JD_OBJECT : JD_ARRAY,
                           atom_expr(p->arena, frame->values.items, frame->values.len));
            free(frame->values.items); len--;
        }
next_member:;
        JsonFrameV1 *frame = &frames[len-1];
        frame->member_start = p->scalar;
        if (frame->object) {
            frame->key = jd_string(p, true);
            if (!frame->key) goto failed;
            jd_ws(p);
            if (!jd_take(p, JD_MEMBER_COLON)) {
                jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "':' after an object key");
                goto failed;
            }
            jd_ws(p);
        }
    }
allocation:
    jd_error(p, CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE, "memory for JSON continuations");
failed:
    for (size_t i = 0; i < len; i++) free(frames[i].values.items);
    free(frames); return NULL;
}

bool cetta_json_direct_v1_parse(Arena *arena, const uint8_t *bytes, size_t len,
    bool legacy, bool petta, uint32_t depth_limit, Atom **out,
    CettaJsonRuntimeV1Status *status, char *error, size_t error_size) {
    if (error && error_size) error[0] = 0;
    if (status) *status = CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
    if (!arena || !bytes || !out || len > UINT32_MAX) return false;
    ArenaMark mark = arena_mark(arena);
    JsonDirectV1 p = {.arena=arena, .bytes=bytes, .len=len, .depth_limit=depth_limit,
                     .legacy=legacy, .petta=petta, .error=error, .error_size=error_size};
    jd_ws(&p);
    Atom *value = jd_value(&p);
    jd_ws(&p);
    if (value && p.at != len) value = jd_error(&p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "end of JSON text");
    if (status) *status = p.status;
    if (!value) { arena_reset(arena, mark); return false; }
    *out = value; return true;
}

bool cetta_json_direct_v1_lines(FILE *input, bool legacy, bool petta,
    uint32_t depth_limit, CettaJsonLineConsumerV1 consume, void *context,
    size_t *failed_line, char *error, size_t error_size) {
    if (!input || !consume) return false;
    char *line = NULL;
    size_t cap = 0, number = 0;
    ssize_t len;
    bool ok = true;
    if (failed_line) *failed_line = 0;
    while ((len = getline(&line, &cap, input)) >= 0) {
        number++;
        size_t size = (size_t)len;
        while (size && (line[size-1] == '\n' || line[size-1] == '\r')) size--;
        if (!size) continue;
        Arena arena; arena_init(&arena);
        Atom *value = NULL;
        ok = cetta_json_direct_v1_parse(&arena, (uint8_t *)line, size, legacy, petta,
               depth_limit, &value, NULL, error, error_size) && consume(context, value, number);
        arena_free(&arena);
        if (!ok) { if (failed_line) *failed_line = number; break; }
    }
    if (ferror(input)) { ok = false; if (error && error_size) snprintf(error,error_size,"JSON stream read failed"); }
    free(line); return ok;
}
