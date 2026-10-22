#define _POSIX_C_SOURCE 200809L
#include "json_direct_v1.h"
#include "native/decimal_value_v1.h"
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
    bool ordinary, petta;
    CettaJsonRuntimeV1Status status;
    char *error;
    size_t error_size;
    unsigned lex;
    bool lex_active;
    uint64_t plain_loop_known;
    uint64_t plain_loop_value;
    Atom *heads[JD_HEAD_COUNT];
    AtomExprHead head_views[JD_HEAD_COUNT];
    bool head_view_ready[JD_HEAD_COUNT];
} JsonDirectV1;

enum { JD_VECTOR_INLINE_CAPACITY = 8 };

/* Container assembly is temporary parser state, not part of the resulting
 * Atom tree.  Keep the common small object/array entirely inside its movable
 * frame; only larger containers acquire a heap buffer.  Never retain a
 * pointer to `inline_items`, because continuation-frame growth may relocate
 * the enclosing vector. */
typedef struct {
    Atom **heap_items;
    size_t len, cap;
    Atom *inline_items[JD_VECTOR_INLINE_CAPACITY];
} JsonVectorV1;

static Atom **jd_vector_items(JsonVectorV1 *vector) {
    return vector->heap_items
        ? vector->heap_items : vector->inline_items;
}

static void jd_vector_free(JsonVectorV1 *vector) {
    free(vector->heap_items);
    vector->heap_items = NULL;
    vector->len = 0u;
    vector->cap = 0u;
}

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
    static const char *const ordinary[] = {NULL, NULL, NULL, NULL, "JsonArray", "JsonObject", "JsonPair", NULL, "JsonNull"};
    static const char *const canonical[] = {"cp", "JsonStringV1", "JsonNumberV1", "JsonBoolV1", "JsonArrayV1", "JsonObjectV1", "JsonMemberV1", "JsonSourceSpanV1", "JsonNullV1"};
    const char *name = (p->ordinary ? ordinary : canonical)[head];
    if (!name)
        return jd_error(p, CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT,
                        "an inspection-only JSON constructor");
    if (!p->heads[head]) p->heads[head] = atom_symbol(p->arena, name);
    return p->heads[head];
}

static Atom *jd_app(JsonDirectV1 *p, JsonHeadV1 head, Atom **args, size_t count) {
    Atom *items[5];
    items[0] = jd_head(p, head);
    if (!items[0])
        return NULL;
    if (!p->head_view_ready[head]) {
        atom_expr_head_init(&p->head_views[head], items[0]);
        p->head_view_ready[head] = true;
    }
    for (size_t i = 0; i < count; ++i) items[i + 1] = args[i];
    return atom_expr_headed(
        p->arena, &p->head_views[head], items, count + 1u);
}

static Atom *jd_one(JsonDirectV1 *p, JsonHeadV1 head, Atom *value) {
    return jd_app(p, head, &value, 1);
}

static bool jd_push(JsonDirectV1 *p, JsonVectorV1 *v, Atom *value) {
    if (!value) return false;
    if (!v->heap_items && v->len < JD_VECTOR_INLINE_CAPACITY) {
        v->inline_items[v->len++] = value;
        return true;
    }
    if (!v->heap_items || v->len == v->cap) {
        size_t cap = v->cap
            ? v->cap * 2u : JD_VECTOR_INLINE_CAPACITY * 2u;
        if (cap < v->cap || cap > UINT32_MAX || cap > SIZE_MAX / sizeof(Atom *)) {
            jd_error(p, CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT, "a smaller JSON container");
            return false;
        }
        Atom **items = v->heap_items
            ? realloc(v->heap_items, cap * sizeof(*items))
            : malloc(cap * sizeof(*items));
        if (!items) {
            jd_error(p, CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE, "memory for JSON values");
            return false;
        }
        if (!v->heap_items && v->len > 0u)
            memcpy(items, v->inline_items, v->len * sizeof(*items));
        v->heap_items = items; v->cap = cap;
    }
    v->heap_items[v->len++] = value;
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

static bool jd_word_has_zero(uint64_t value) {
    const uint64_t ones = UINT64_C(0x0101010101010101);
    const uint64_t highs = UINT64_C(0x8080808080808080);
    return ((value - ones) & ~value & highs) != 0u;
}

static bool jd_word_has_control(uint64_t value) {
    const uint64_t threshold = UINT64_C(0x2020202020202020);
    const uint64_t highs = UINT64_C(0x8080808080808080);
    return ((value - threshold) & ~value & highs) != 0u;
}

/* The compiler has already checked JsonUnescaped against RFC 8259.  Confirm
 * that this generated DFA state loops on that ASCII class, then advance a
 * plain span wordwise without replaying the same transition per byte.  If a
 * future grammar produces a different automaton shape, retain the exact DFA
 * walk instead of assuming the optimization. */
static bool jd_plain_ascii_self_loop(JsonDirectV1 *p) {
    unsigned state = p->lex;
    uint64_t bit = state < 64u ? UINT64_C(1) << state : 0u;
    if (bit && (p->plain_loop_known & bit))
        return (p->plain_loop_value & bit) != 0u;
    bool loops = true;
    for (uint32_t cp = 32u; cp < 128u; cp++) {
        if (cp == '"' || cp == '\\')
            continue;
        if (jd_lex_step(state, cp) != state) {
            loops = false;
            break;
        }
    }
    if (bit) {
        p->plain_loop_known |= bit;
        if (loops)
            p->plain_loop_value |= bit;
    }
    return loops;
}

static bool jd_plain_ascii_span(JsonDirectV1 *p) {
    bool self_loop = jd_plain_ascii_self_loop(p);
    /* A minimal DFA may distinguish the empty-body state from the
     * nonempty-body loop. Consume that one semantic transition, then test
     * the stable state before entering the wordwise loop. */
    if (!self_loop && p->at < p->len && p->bytes[p->at] >= 32u &&
        p->bytes[p->at] < 128u && p->bytes[p->at] != '"' &&
        p->bytes[p->at] != '\\') {
        if (!jd_lex_advance(p, p->bytes[p->at]))
            return false;
        p->at++;
        p->scalar++;
        self_loop = jd_plain_ascii_self_loop(p);
    }
    if (self_loop) {
        const uint64_t highs = UINT64_C(0x8080808080808080);
        const uint64_t quotes = UINT64_C(0x2222222222222222);
        const uint64_t slashes = UINT64_C(0x5c5c5c5c5c5c5c5c);
        while (p->len - p->at >= sizeof(uint64_t)) {
            uint64_t word = 0u;
            memcpy(&word, p->bytes + p->at, sizeof(word));
            if ((word & highs) != 0u ||
                jd_word_has_control(word) ||
                jd_word_has_zero(word ^ quotes) ||
                jd_word_has_zero(word ^ slashes)) {
                break;
            }
            p->at += sizeof(word);
            p->scalar += sizeof(word);
        }
    }
    while (p->at < p->len && p->bytes[p->at] >= 32u &&
           p->bytes[p->at] < 128u && p->bytes[p->at] != '"' &&
           p->bytes[p->at] != '\\') {
        if (!self_loop && !jd_lex_advance(p, p->bytes[p->at]))
            return false;
        p->at++;
        p->scalar++;
    }
    return true;
}

static Atom *jd_string(JsonDirectV1 *p) {
    p->lex_active = true; p->lex = JD_LEX_START;
    if (!jd_take(p, '"')) return jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "a quoted string");
    size_t begin = p->at, written = 0, cap = 0;
    uint8_t *decoded = NULL;
    JsonVectorV1 scalars = {0};
    Atom *result = NULL;
    if (p->ordinary) {
        /* The common unescaped ASCII span is validated and copied once. */
        if (!jd_plain_ascii_span(p)) goto done;
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
        if (!p->ordinary) {
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
    if (p->ordinary) {
        Atom *text = atom_string_n(p->arena, (const char *)(decoded ? decoded : p->bytes + begin), written);
        result = text;
    } else result = jd_one(p, JD_STRING,
                           atom_expr(p->arena,
                                     jd_vector_items(&scalars), scalars.len));
    goto done;
escape_error:
    jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "a JSON escape"); goto done;
surrogate_error:
    jd_error(p, CETTA_JSON_RUNTIME_V1_INVALID_UNICODE_ESCAPE, "a paired Unicode surrogate escape"); goto done;
allocation:
    jd_error(p, CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE, "memory for a JSON string");
done:
    p->lex_active = false;
    free(decoded); jd_vector_free(&scalars); return result;
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
    size_t length = p->at - start;
    if (!p->ordinary) {
        return jd_one(p, JD_NUMBER,
                      atom_string_n(p->arena,
                          (const char *)p->bytes + start, length));
    }

    bool real = false;
    for (size_t index = 0u; index < length; index++) {
        uint8_t byte = p->bytes[start + index];
        if (byte == '.' || byte == 'e' || byte == 'E') {
            real = true;
            break;
        }
    }
    if (!real) {
        bool negative = length && p->bytes[start] == '-';
        size_t index = negative ? 1u : 0u;
        uint64_t magnitude = 0u;
        uint64_t limit = negative
            ? (uint64_t)INT64_MAX + UINT64_C(1)
            : (uint64_t)INT64_MAX;
        bool fits = true;
        for (; index < length; index++) {
            uint64_t digit = (uint64_t)(p->bytes[start + index] - '0');
            if (fits && magnitude > (limit - digit) / UINT64_C(10))
                fits = false;
            else if (fits)
                magnitude = magnitude * UINT64_C(10) + digit;
        }
        if (fits) {
            int64_t value = negative
                ? magnitude == (uint64_t)INT64_MAX + UINT64_C(1)
                    ? INT64_MIN : -(int64_t)magnitude
                : (int64_t)magnitude;
            return atom_int(p->arena, value);
        }
    }

    char *text = malloc(length + 1u);
    if (!text)
        return jd_error(p, CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE,
                        "memory for a JSON number");
    memcpy(text, p->bytes + start, length);
    text[length] = '\0';
    Atom *result = NULL;
    if (real) {
        double value = 0.0;
        if (cetta_decimal_real_nearest_v1(text, &value))
            result = atom_float(p->arena, value);
    } else {
        result = atom_bigint(p->arena, text);
    }
    free(text);
    if (!result)
        return jd_error(p, CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE,
                        "a native JSON number");
    return result;
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

typedef enum {
    JD_LOOK_ERROR,
    JD_LOOK_NULL,
    JD_LOOK_FALSE,
    JD_LOOK_TRUE,
    JD_LOOK_STRING,
    JD_LOOK_NUMBER,
    JD_LOOK_ARRAY,
    JD_LOOK_OBJECT
} JsonLookaheadV1;
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
                if (p->ordinary) {
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
                           atom_expr(p->arena,
                                     jd_vector_items(&frame->values),
                                     frame->values.len));
            jd_vector_free(&frame->values); len--;
        }
next_member:;
        JsonFrameV1 *frame = &frames[len-1];
        frame->member_start = p->scalar;
        if (frame->object) {
            frame->key = jd_string(p);
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
    for (size_t i = 0; i < len; i++)
        jd_vector_free(&frames[i].values);
    free(frames); return NULL;
}

bool cetta_json_direct_v1_parse(Arena *arena, const uint8_t *bytes, size_t len,
    bool ordinary, bool petta, uint32_t depth_limit, Atom **out,
    CettaJsonRuntimeV1Status *status, char *error, size_t error_size) {
    if (error && error_size) error[0] = 0;
    if (status) *status = CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
    if (!arena || !bytes || !out || len > UINT32_MAX) return false;
    ArenaMark mark = arena_mark(arena);
    JsonDirectV1 p = {.arena=arena, .bytes=bytes, .len=len, .depth_limit=depth_limit,
                     .ordinary=ordinary, .petta=petta, .error=error, .error_size=error_size};
    jd_ws(&p);
    Atom *value = jd_value(&p);
    jd_ws(&p);
    if (value && p.at != len) value = jd_error(&p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "end of JSON text");
    if (status) *status = p.status;
    if (!value) { arena_reset(arena, mark); return false; }
    *out = value; return true;
}

typedef struct {
    bool object;
    bool selected_here;
    bool selected_key_seen;
    size_t value_start;
    size_t active_depth;
    size_t next_index;
} JsonProjectFrameV1;

static size_t jd_utf8_encode(uint32_t cp, uint8_t bytes[4]) {
    if (cp < 0x80u) {
        bytes[0] = (uint8_t)cp;
        return 1u;
    }
    if (cp < 0x800u) {
        bytes[0] = (uint8_t)(0xc0u | (cp >> 6));
        bytes[1] = (uint8_t)(0x80u | (cp & 63u));
        return 2u;
    }
    if (cp < 0x10000u) {
        bytes[0] = (uint8_t)(0xe0u | (cp >> 12));
        bytes[1] = (uint8_t)(0x80u | ((cp >> 6) & 63u));
        bytes[2] = (uint8_t)(0x80u | (cp & 63u));
        return 3u;
    }
    bytes[0] = (uint8_t)(0xf0u | (cp >> 18));
    bytes[1] = (uint8_t)(0x80u | ((cp >> 12) & 63u));
    bytes[2] = (uint8_t)(0x80u | ((cp >> 6) & 63u));
    bytes[3] = (uint8_t)(0x80u | (cp & 63u));
    return 4u;
}

/* Scan and decode a JSON string without constructing either a String Atom or
 * one scalar Atom per character.  When wanted is non-NULL, compare its
 * decoded UTF-8 bytes on the fly. */
static bool jd_project_string(JsonDirectV1 *p,
    const uint8_t *wanted, size_t wanted_len, bool *matches) {
    size_t compared = 0u;
    bool equal = wanted != NULL;
    p->lex_active = true;
    p->lex = JD_LEX_START;
    if (!jd_take(p, '"')) {
        jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
                 "a quoted string");
        goto failed;
    }
    /* Unselected ASCII payloads are the common configuration/API case.
     * Validate the generated lexical state in one tight byte loop, without
     * scalar decoding or comparison work. */
    if (!wanted) {
        if (!jd_plain_ascii_span(p))
            goto failed;
    }
    while (p->at < p->len && p->bytes[p->at] != '"') {
        uint32_t cp = 0u;
        if (jd_take(p, '\\')) {
            if (p->at == p->len)
                goto escape_error;
            uint8_t escaped = p->bytes[p->at];
            if (!jd_lex_advance(p, escaped))
                goto failed;
            p->at++;
            p->scalar++;
            switch (escaped) {
            case '"': case '\\': case '/': cp = escaped; break;
            case 'b': cp = 8u; break;
            case 'f': cp = 12u; break;
            case 'n': cp = 10u; break;
            case 'r': cp = 13u; break;
            case 't': cp = 9u; break;
            case 'u':
                if (!jd_hex(p, &cp))
                    goto failed;
                if (cp >= 0xd800u && cp <= 0xdbffu) {
                    uint32_t low = 0u;
                    if (!jd_take(p, '\\') || !jd_take(p, 'u') ||
                        !jd_hex(p, &low) ||
                        low < 0xdc00u || low > 0xdfffu) {
                        goto surrogate_error;
                    }
                    cp = 0x10000u + ((cp - 0xd800u) << 10) +
                         low - 0xdc00u;
                } else if (cp >= 0xdc00u && cp <= 0xdfffu) {
                    goto surrogate_error;
                }
                break;
            default:
                goto escape_error;
            }
        } else {
            if (p->bytes[p->at] < 32u) {
                jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
                         "an escaped control character");
                goto failed;
            }
            if (!jd_scalar(p, &cp))
                goto failed;
        }
        if (equal) {
            uint8_t encoded[4];
            size_t width = jd_utf8_encode(cp, encoded);
            if (width > wanted_len - compared ||
                memcmp(wanted + compared, encoded, width) != 0) {
                equal = false;
            } else {
                compared += width;
            }
        }
    }
    if (!jd_take(p, '"')) {
        jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
                 "a closing string quote");
        goto failed;
    }
    if (!(jd_lex_accept[p->lex] & 4u)) {
        jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
                 "a complete string token");
        goto failed;
    }
    p->lex_active = false;
    if (matches)
        *matches = equal && compared == wanted_len;
    return true;

escape_error:
    jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED, "a JSON escape");
    goto failed;
surrogate_error:
    jd_error(p, CETTA_JSON_RUNTIME_V1_INVALID_UNICODE_ESCAPE,
             "a paired Unicode surrogate escape");
failed:
    p->lex_active = false;
    if (matches)
        *matches = false;
    return false;
}

static bool jd_project_number(JsonDirectV1 *p) {
    unsigned state = JD_LEX_START;
    while (p->at < p->len) {
        unsigned next = jd_lex_step(state, p->bytes[p->at]);
        if (next == UINT32_MAX)
            break;
        state = next;
        p->at++;
        p->scalar++;
    }
    if (jd_lex_accept[state] & 2u)
        return true;
    jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
             "a JSON number digit");
    return false;
}

static bool jd_project_scalar(JsonDirectV1 *p, JsonLookaheadV1 lookahead) {
    switch (lookahead) {
    case JD_LOOK_NULL:
        p->at += 4u; p->scalar += 4u; return true;
    case JD_LOOK_FALSE:
        p->at += 5u; p->scalar += 5u; return true;
    case JD_LOOK_TRUE:
        p->at += 4u; p->scalar += 4u; return true;
    case JD_LOOK_STRING:
        return jd_project_string(p, NULL, 0u, NULL);
    case JD_LOOK_NUMBER:
        return jd_project_number(p);
    default:
        jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
                 "a scalar JSON value");
        return false;
    }
}

static bool jd_project_path_key(Atom *component,
    const uint8_t **bytes, size_t *len) {
    if (!component || component->kind != ATOM_GROUNDED ||
        component->ground.gkind != GV_STRING || !bytes || !len) {
        return false;
    }
    *bytes = (const uint8_t *)component->ground.sval;
    *len = atom_string_len(component);
    return true;
}

static bool jd_project_path_index(Atom *component, size_t *index) {
    if (!component || component->kind != ATOM_GROUNDED ||
        component->ground.gkind != GV_INT ||
        component->ground.ival < 0 || !index ||
        (uint64_t)component->ground.ival > (uint64_t)SIZE_MAX) {
        return false;
    }
    *index = (size_t)component->ground.ival;
    return true;
}

#define JD_PROJECT_INACTIVE SIZE_MAX

/* One validating scan locates the selected value span.  The selected subtree
 * is parsed only after the complete outer document succeeds, so malformed
 * unselected siblings cannot be hidden and no partial result is published. */
static bool jd_project_span(JsonDirectV1 *p, Atom *path,
    bool *found, size_t *selected_start, size_t *selected_end) {
    JsonProjectFrameV1 *frames = NULL;
    size_t frame_len = 0u, frame_cap = 0u;
    size_t path_len = path->expr.len;
    size_t active_depth = 0u;
    size_t current_start = 0u;
    bool current_selected = false;
    bool completed_selected = false;
    size_t completed_start = 0u;

begin_value:
    if (p->depth_limit && frame_len >= p->depth_limit) {
        jd_error(p, CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT,
                 "a shallower JSON value");
        goto failed;
    }
    current_start = p->at;
    current_selected = active_depth != JD_PROJECT_INACTIVE &&
                       active_depth == path_len && !*found;
    {
        JsonLookaheadV1 lookahead = jd_value_lookahead(p);
        if (lookahead == JD_LOOK_ERROR) {
            jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
                     "a JSON value");
            goto failed;
        }
        if (lookahead != JD_LOOK_ARRAY && lookahead != JD_LOOK_OBJECT) {
            if (!jd_project_scalar(p, lookahead))
                goto failed;
            completed_selected = current_selected;
            completed_start = current_start;
            goto complete_value;
        }
        if (frame_len == frame_cap) {
            size_t next = frame_cap ? frame_cap * 2u : 16u;
            if (next < frame_cap ||
                next > SIZE_MAX / sizeof(*frames)) {
                jd_error(p, CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT,
                         "a smaller JSON container");
                goto failed;
            }
            JsonProjectFrameV1 *grown =
                realloc(frames, next * sizeof(*frames));
            if (!grown) {
                jd_error(p, CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE,
                         "memory for JSON projection continuations");
                goto failed;
            }
            frames = grown;
            frame_cap = next;
        }
        frames[frame_len++] = (JsonProjectFrameV1){
            .object = lookahead == JD_LOOK_OBJECT,
            .selected_here = current_selected,
            .value_start = current_start,
            .active_depth = active_depth,
        };
        p->at++;
        p->scalar++;
        jd_ws(p);
        JsonProjectFrameV1 *frame = &frames[frame_len - 1u];
        if (jd_take(p, frame->object ? JD_OBJECT_CLOSE : JD_ARRAY_CLOSE)) {
            completed_selected = frame->selected_here;
            completed_start = frame->value_start;
            frame_len--;
            goto complete_value;
        }
    }

prepare_child:;
    {
        JsonProjectFrameV1 *frame = &frames[frame_len - 1u];
        active_depth = JD_PROJECT_INACTIVE;
        if (frame->object) {
            const uint8_t *wanted = NULL;
            size_t wanted_len = 0u;
            bool interested = frame->active_depth != JD_PROJECT_INACTIVE &&
                frame->active_depth < path_len &&
                !frame->selected_key_seen &&
                jd_project_path_key(
                    path->expr.elems[frame->active_depth],
                    &wanted, &wanted_len);
            bool matches = false;
            if (!jd_project_string(
                    p, interested ? wanted : NULL,
                    interested ? wanted_len : 0u, &matches)) {
                goto failed;
            }
            if (interested && matches) {
                frame->selected_key_seen = true;
                active_depth = frame->active_depth + 1u;
            }
            jd_ws(p);
            if (!jd_take(p, JD_MEMBER_COLON)) {
                jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
                         "':' after an object key");
                goto failed;
            }
            jd_ws(p);
        } else {
            size_t wanted_index = 0u;
            bool interested = frame->active_depth != JD_PROJECT_INACTIVE &&
                frame->active_depth < path_len &&
                jd_project_path_index(
                    path->expr.elems[frame->active_depth],
                    &wanted_index);
            if (interested && frame->next_index == wanted_index)
                active_depth = frame->active_depth + 1u;
            frame->next_index++;
        }
    }
    goto begin_value;

complete_value:
    if (completed_selected && !*found) {
        *found = true;
        *selected_start = completed_start;
        *selected_end = p->at;
    }
    for (;;) {
        if (frame_len == 0u) {
            free(frames);
            return true;
        }
        JsonProjectFrameV1 *frame = &frames[frame_len - 1u];
        jd_ws(p);
        if (jd_take(p, frame->object ? JD_OBJECT_CLOSE : JD_ARRAY_CLOSE)) {
            completed_selected = frame->selected_here;
            completed_start = frame->value_start;
            frame_len--;
            if (completed_selected && !*found) {
                *found = true;
                *selected_start = completed_start;
                *selected_end = p->at;
            }
            continue;
        }
        if (!jd_take(p, frame->object
                ? JD_MEMBER_COMMA : JD_ELEMENT_COMMA)) {
            jd_error(p, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
                     frame->object ? "',' or '}'" : "',' or ']'");
            goto failed;
        }
        jd_ws(p);
        goto prepare_child;
    }

failed:
    free(frames);
    return false;
}

bool cetta_json_direct_v1_project(Arena *arena,
    const uint8_t *bytes, size_t len, Atom *path,
    bool ordinary, bool petta, uint32_t depth_limit,
    Atom **out, bool *found, CettaJsonRuntimeV1Status *status,
    char *error, size_t error_size) {
    if (error && error_size)
        error[0] = 0;
    if (status)
        *status = CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
    if (out)
        *out = NULL;
    if (found)
        *found = false;
    if (!arena || !bytes || !path || path->kind != ATOM_EXPR ||
        !out || !found || len > UINT32_MAX) {
        return false;
    }
    for (CettaExprIndex i = 0u; i < path->expr.len; i++) {
        size_t ignored = 0u;
        const uint8_t *ignored_bytes = NULL;
        size_t ignored_len = 0u;
        if (!jd_project_path_key(
                path->expr.elems[i], &ignored_bytes, &ignored_len) &&
            !jd_project_path_index(path->expr.elems[i], &ignored)) {
            if (error && error_size) {
                snprintf(error, error_size,
                         "JSON path component %u is not text or a nonnegative integer",
                         (unsigned)i);
            }
            return false;
        }
    }
    if (path->expr.len == 0u) {
        bool ok = cetta_json_direct_v1_parse(
            arena, bytes, len, ordinary, petta, depth_limit,
            out, status, error, error_size);
        *found = ok;
        return ok;
    }

    ArenaMark mark = arena_mark(arena);
    JsonDirectV1 parser = {
        .arena = arena,
        .bytes = bytes,
        .len = len,
        .depth_limit = depth_limit,
        .ordinary = ordinary,
        .petta = petta,
        .error = error,
        .error_size = error_size,
    };
    size_t selected_start = 0u, selected_end = 0u;
    jd_ws(&parser);
    bool ok = jd_project_span(
        &parser, path, found, &selected_start, &selected_end);
    jd_ws(&parser);
    if (ok && parser.at != len) {
        jd_error(&parser, CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
                 "end of JSON text");
        ok = false;
    }
    if (!ok) {
        if (status)
            *status = parser.status;
        arena_reset(arena, mark);
        *found = false;
        return false;
    }
    if (!*found) {
        if (status)
            *status = CETTA_JSON_RUNTIME_V1_OK;
        return true;
    }
    CettaJsonRuntimeV1Status selected_status = CETTA_JSON_RUNTIME_V1_OK;
    if (!cetta_json_direct_v1_parse(
            arena, bytes + selected_start, selected_end - selected_start,
            ordinary, petta, depth_limit, out, &selected_status,
            error, error_size)) {
        if (status)
            *status = selected_status;
        arena_reset(arena, mark);
        *found = false;
        return false;
    }
    if (status)
        *status = CETTA_JSON_RUNTIME_V1_OK;
    return true;
}

static bool jd_scalar_shape(JsonDirectV1 *p,const size_t *shape,size_t rank,CettaJsonScalarKindV1 kind,
    CettaJsonScalarConsumerV1 consume,void *context) {
    size_t *counts=rank ? calloc(rank,sizeof(*counts)) : NULL;
    if(rank && !counts) return jd_error(p,CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE,"numeric array counters"),false;
    size_t depth=0,ordinal=0;
    jd_ws(p);
    if(rank && !jd_take(p,JD_ARRAY_OPEN)) goto shape_error;
    for(;;) {
        jd_ws(p);
        if(rank && counts[depth]==shape[depth]) {
            if(!jd_take(p,JD_ARRAY_CLOSE)) goto shape_error;
            if(!depth) break;
            depth--; counts[depth]++; continue;
        }
        if(rank && counts[depth]) {
            if(!jd_take(p,JD_ELEMENT_COMMA)) goto shape_error;
            jd_ws(p);
        }
        if(rank && depth+1<rank) {
            if(!jd_take(p,JD_ARRAY_OPEN)) goto shape_error;
            counts[++depth]=0; continue;
        }
        JsonLookaheadV1 look=jd_value_lookahead(p);
        if(kind==CETTA_JSON_SCALAR_NUMBER_V1 ? look!=JD_LOOK_NUMBER : (look!=JD_LOOK_TRUE && look!=JD_LOOK_FALSE)) goto shape_error;
        size_t start=p->at;
        if(!(kind==CETTA_JSON_SCALAR_NUMBER_V1 ? jd_project_number(p) : jd_project_scalar(p,look))) { free(counts); return false; }
        if(consume && !consume(context,p->bytes+start,p->at-start,ordinal)) {
            free(counts); return jd_error(p,CETTA_JSON_RUNTIME_V1_MALFORMED_VALUE,"an admitted numeric sink value"),false;
        }
        if(ordinal==SIZE_MAX) { free(counts); return jd_error(p,CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT,"a representable numeric array size"),false; }
        ordinal++;
        if(!rank) break;
        counts[depth]++;
    }
    free(counts); jd_ws(p);
    if(p->at==p->len) return true;
    return jd_error(p,CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT,"end of the selected numeric array"),false;
shape_error:
    free(counts);
    return jd_error(p,CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT,"the declared rectangular numeric array shape"),false;
}

bool cetta_json_direct_v1_scalar_array(const uint8_t *bytes,size_t len,Atom *path,
    const size_t *shape,size_t rank,CettaJsonScalarKindV1 kind,uint32_t depth_limit,CettaJsonScalarConsumerV1 consume,
    void *context,bool *found,CettaJsonRuntimeV1Status *status,char *error,size_t error_size) {
    if(found) *found=false;
    if(status) *status=CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
    if(!bytes || !path || path->kind!=ATOM_EXPR || !consume || !found || (rank && !shape) || rank>SIZE_MAX/sizeof(size_t) ||
        (kind!=CETTA_JSON_SCALAR_NUMBER_V1 && kind!=CETTA_JSON_SCALAR_BOOLEAN_V1)) return false;
    for(CettaExprIndex i=0;i<path->expr.len;i++) {
        const uint8_t *text; size_t n;
        if(!jd_project_path_key(path->expr.elems[i],&text,&n) && !jd_project_path_index(path->expr.elems[i],&n)) return false;
    }
    JsonDirectV1 p={.bytes=bytes,.len=len,.ordinary=true,.depth_limit=depth_limit,.error=error,.error_size=error_size};
    size_t start=0,end=0; jd_ws(&p);
    bool ok=jd_project_span(&p,path,found,&start,&end); jd_ws(&p);
    if(ok && p.at!=len) { jd_error(&p,CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,"end of JSON text"); ok=false; }
    if(ok && *found) {
        JsonDirectV1 selected={.bytes=bytes+start,.len=end-start,.ordinary=true,.error=error,.error_size=error_size};
        /* Shape admission is independent of conversion and has no callback. */
        ok=jd_scalar_shape(&selected,shape,rank,kind,NULL,NULL);
        if(ok) { selected.at=selected.scalar=0; ok=jd_scalar_shape(&selected,shape,rank,kind,consume,context); }
        if(!ok) p.status=selected.status;
    }
    if(!ok) *found=false;
    if(status) *status=ok ? CETTA_JSON_RUNTIME_V1_OK : p.status;
    return ok;
}

bool cetta_json_direct_v1_lines(FILE *input, bool ordinary, bool petta,
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
        ok = cetta_json_direct_v1_parse(&arena, (uint8_t *)line, size, ordinary, petta,
               depth_limit, &value, NULL, error, error_size) && consume(context, value, number);
        arena_free(&arena);
        if (!ok) { if (failed_line) *failed_line = number; break; }
    }
    if (ferror(input)) { ok = false; if (error && error_size) snprintf(error,error_size,"JSON stream read failed"); }
    free(line); return ok;
}
