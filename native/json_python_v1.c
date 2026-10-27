#define _POSIX_C_SOURCE 200809L
#include "json_python_v1.h"
#include "native/decimal_value_v1.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Adapted from CPython 3.12's C JSON scanner (Modules/_json.c:
 * scanstring_unicode, _match_number_unicode, scan_once_unicode and the
 * object and array parsers) and from json/decoder.py (JSONDecoder.decode,
 * JSONDecodeError), checked against CPython 3.12.13:
 *   Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010,
 *   2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020, 2021, 2022, 2023
 *   Python Software Foundation; All Rights Reserved.
 * Used under the PSF License Version 2, in docs/licenses/cpython.txt.
 *
 * It keeps CPython's states, checks and messages. Changes from CPython:
 * - written in C over the text's code points, decoded from UTF-8 with lone
 *   surrogates as surrogatepass writes them, so positions are Python string
 *   indices;
 * - containers use an explicit stack, where CPython recurses and raises
 *   RecursionError near its recursion limit;
 * - values are CeTTa atoms, and a repeated object key keeps its first
 *   position with the last value, as dict(pairs) does, through a hash index;
 * - errors are returned as data (JSONDecodeError's msg, pos, lineno and
 *   colno; int()'s ValueError past 4300 digits) rather than raised;
 * - only json.loads's defaults: strict, with no hooks. */

enum { PY_INT_MAX_STR_DIGITS = 4300 };

typedef struct {
    uint8_t *bytes;
    size_t len, cap;
} PyBuffer;

static bool py_reserve(PyBuffer *buffer, size_t extra) {
    if (buffer->cap - buffer->len >= extra) return true;
    size_t next = buffer->cap ? buffer->cap : 64u;
    while (next - buffer->len < extra) {
        if (next > SIZE_MAX / 2u) return false;
        next *= 2u;
    }
    uint8_t *grown = realloc(buffer->bytes, next);
    if (!grown) return false;
    buffer->bytes = grown;
    buffer->cap = next;
    return true;
}

/* UTF-8, and a surrogate as the three bytes surrogatepass writes. */
static bool py_append_code(PyBuffer *buffer, uint32_t code) {
    if (!py_reserve(buffer, 4u)) return false;
    uint8_t *out = buffer->bytes + buffer->len;
    if (code < 0x80u) { out[0] = (uint8_t)code; buffer->len += 1u; }
    else if (code < 0x800u) {
        out[0] = (uint8_t)(0xc0u | (code >> 6u)); out[1] = (uint8_t)(0x80u | (code & 0x3fu));
        buffer->len += 2u;
    } else if (code < 0x10000u) {
        out[0] = (uint8_t)(0xe0u | (code >> 12u)); out[1] = (uint8_t)(0x80u | ((code >> 6u) & 0x3fu));
        out[2] = (uint8_t)(0x80u | (code & 0x3fu));
        buffer->len += 3u;
    } else {
        out[0] = (uint8_t)(0xf0u | (code >> 18u)); out[1] = (uint8_t)(0x80u | ((code >> 12u) & 0x3fu));
        out[2] = (uint8_t)(0x80u | ((code >> 6u) & 0x3fu)); out[3] = (uint8_t)(0x80u | (code & 0x3fu));
        buffer->len += 4u;
    }
    return true;
}

typedef struct {
    Atom *key, *value;
} PyPair;

typedef struct {
    bool object;
    Atom **items;
    size_t len, cap;
    PyPair *pairs;
    size_t pair_len, pair_cap;
    size_t *slots;      /* pair index + 1, or 0 */
    size_t slot_cap;
    Atom *key;
} PyFrame;

typedef struct {
    Arena *arena;
    const uint32_t *s;
    size_t n;
    bool petta;
    CettaJsonPythonV1Error *error;
    PyBuffer text;
    PyFrame *frames;
    size_t depth, frame_cap;
} PyParser;

static bool py_decode_error(PyParser *p, const char *msg, size_t pos) {
    p->error->status = CETTA_JSON_PYTHON_V1_DECODE_ERROR;
    p->error->msg = msg;
    p->error->pos = pos;
    return false;
}

static bool py_allocation(PyParser *p) {
    p->error->status = CETTA_JSON_PYTHON_V1_ALLOCATION_FAILURE;
    p->error->msg = "out of memory";
    return false;
}

static bool py_ws(uint32_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static size_t py_skip_ws(PyParser *p, size_t idx) {
    while (idx < p->n && py_ws(p->s[idx])) idx++;
    return idx;
}

static int py_hex(uint32_t c) {
    return c >= '0' && c <= '9' ? (int)(c - '0') : c >= 'a' && c <= 'f' ? (int)(c - 'a' + 10)
         : c >= 'A' && c <= 'F' ? (int)(c - 'A' + 10) : -1;
}

/* scanstring_unicode: `end` is the index just after the opening quote. */
static Atom *py_scanstring(PyParser *p, size_t end, size_t *next_end) {
    const uint32_t *s = p->s;
    size_t len = p->n, begin = end - 1u;
    p->text.len = 0u;
    for (;;) {
        size_t next;
        uint32_t c = 0u;
        for (next = end; next < len; next++) {
            c = s[next];
            if (c == '"' || c == '\\') break;
            if (c <= 0x1fu) {
                py_decode_error(p, "Invalid control character at", next);
                return NULL;
            }
        }
        if (next >= len) {
            py_decode_error(p, "Unterminated string starting at", begin);
            return NULL;
        }
        for (size_t k = end; k < next; k++)
            if (!py_append_code(&p->text, s[k])) { py_allocation(p); return NULL; }
        next++;
        if (c == '"') { end = next; break; }
        if (next == len) {
            py_decode_error(p, "Unterminated string starting at", begin);
            return NULL;
        }
        c = s[next];
        if (c != 'u') {
            end = next + 1u;
            switch (c) {
            case '"': case '\\': case '/': break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            default: c = 0u;
            }
            if (c == 0u) {
                py_decode_error(p, "Invalid \\escape", end - 2u);
                return NULL;
            }
        } else {
            c = 0u;
            next++;
            end = next + 4u;
            if (end >= len) {
                py_decode_error(p, "Invalid \\uXXXX escape", next - 1u);
                return NULL;
            }
            for (; next < end; next++) {
                int digit = py_hex(s[next]);
                if (digit < 0) {
                    py_decode_error(p, "Invalid \\uXXXX escape", end - 5u);
                    return NULL;
                }
                c = (c << 4u) | (uint32_t)digit;
            }
            /* As in CPython, the two index increments happen even when the
             * second test fails; `end` alone decides where scanning resumes. */
            if (c >= 0xd800u && c <= 0xdbffu && end + 6u < len &&
                s[next++] == '\\' && s[next++] == 'u') {
                uint32_t c2 = 0u;
                end += 6u;
                for (; next < end; next++) {
                    int digit = py_hex(s[next]);
                    if (digit < 0) {
                        py_decode_error(p, "Invalid \\uXXXX escape", end - 5u);
                        return NULL;
                    }
                    c2 = (c2 << 4u) | (uint32_t)digit;
                }
                if (c2 >= 0xdc00u && c2 <= 0xdfffu)
                    c = 0x10000u + (((c & 0x3ffu) << 10u) | (c2 & 0x3ffu));
                else
                    end -= 6u;
            }
        }
        if (!py_append_code(&p->text, c)) { py_allocation(p); return NULL; }
    }
    *next_end = end;
    Atom *result = atom_string_n(p->arena, (const char *)(p->text.bytes ? p->text.bytes : (uint8_t *)""),
                                 p->text.len);
    if (!result) py_allocation(p);
    return result;
}

static bool py_digit(uint32_t c) {
    return c >= '0' && c <= '9';
}

/* _match_number_unicode. False with *stop set means StopIteration(start). */
static Atom *py_number(PyParser *p, size_t start, size_t *next, bool *stop) {
    const uint32_t *s = p->s;
    size_t end_idx = p->n - 1u, idx = start;
    bool is_float = false, negative = false;
    if (s[idx] == '-') {
        negative = true;
        idx++;
        if (idx > end_idx) { *stop = true; return NULL; }
    }
    if (s[idx] >= '1' && s[idx] <= '9') {
        idx++;
        while (idx <= end_idx && py_digit(s[idx])) idx++;
    } else if (s[idx] == '0') {
        idx++;
    } else {
        *stop = true;
        return NULL;
    }
    if (idx < end_idx && s[idx] == '.' && py_digit(s[idx + 1u])) {
        is_float = true;
        idx += 2u;
        while (idx <= end_idx && py_digit(s[idx])) idx++;
    }
    if (idx < end_idx && (s[idx] == 'e' || s[idx] == 'E')) {
        size_t e_start = idx;
        idx++;
        if (idx < end_idx && (s[idx] == '-' || s[idx] == '+')) idx++;
        while (idx <= end_idx && py_digit(s[idx])) idx++;
        if (py_digit(s[idx - 1u])) is_float = true;
        else idx = e_start;
    }
    size_t length = idx - start;
    char *text = malloc(length + 1u);
    if (!text) { py_allocation(p); return NULL; }
    for (size_t k = 0u; k < length; k++) text[k] = (char)s[start + k];
    text[length] = '\0';
    Atom *result = NULL;
    if (is_float) {
        double value = 0.0;
        if (!cetta_decimal_nearest_validated_v1(text, &value)) {
            free(text);
            py_decode_error(p, "Expecting value", start);
            return NULL;
        }
        result = atom_float(p->arena, value);
    } else {
        size_t digits = length - (negative ? 1u : 0u);
        if (digits > PY_INT_MAX_STR_DIGITS) {
            free(text);
            p->error->status = CETTA_JSON_PYTHON_V1_VALUE_ERROR;
            p->error->msg = "Exceeds the limit for integer string conversion";
            p->error->digits = digits;
            return NULL;
        }
        if (digits <= 18u) {
            long long value = strtoll(text, NULL, 10);
            result = atom_int(p->arena, (int64_t)value);
        } else {
            result = atom_bigint(p->arena, text);
        }
    }
    free(text);
    if (!result) py_allocation(p);
    *next = idx;
    return result;
}

static bool py_matches(PyParser *p, size_t at, const char *word) {
    for (size_t k = 0u; word[k]; k++)
        if (at + k >= p->n || p->s[at + k] != (uint32_t)(unsigned char)word[k]) return false;
    return true;
}

static Atom *py_literal(PyParser *p, const char *name) {
    if (!strcmp(name, "true") || !strcmp(name, "false"))
        return p->petta ? atom_symbol(p->arena, name) : atom_bool(p->arena, name[0] == 't');
    return atom_symbol(p->arena, name);
}

static void py_frame_free(PyFrame *frame) {
    free(frame->items);
    free(frame->pairs);
    free(frame->slots);
    memset(frame, 0, sizeof(*frame));
}

static uint64_t py_key_hash(Atom *key) {
    const uint8_t *bytes = (const uint8_t *)key->ground.sval;
    size_t len = atom_string_len(key);
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t k = 0u; k < len; k++) { hash ^= bytes[k]; hash *= UINT64_C(1099511628211); }
    return hash;
}

static bool py_key_equal(Atom *left, Atom *right) {
    size_t len = atom_string_len(left);
    return len == atom_string_len(right) && !memcmp(left->ground.sval, right->ground.sval, len);
}

static bool py_rehash(PyFrame *frame, size_t capacity) {
    size_t *slots = calloc(capacity, sizeof(*slots));
    if (!slots) return false;
    for (size_t i = 0u; i < frame->pair_len; i++) {
        size_t at = (size_t)(py_key_hash(frame->pairs[i].key) & (capacity - 1u));
        while (slots[at]) at = (at + 1u) & (capacity - 1u);
        slots[at] = i + 1u;
    }
    free(frame->slots);
    frame->slots = slots;
    frame->slot_cap = capacity;
    return true;
}

/* PyDict_SetItem: a repeated key keeps its first position and takes the
 * new value. */
static bool py_object_set(PyFrame *frame, Atom *key, Atom *value) {
    if ((frame->pair_len + 1u) * 2u > frame->slot_cap &&
        !py_rehash(frame, frame->slot_cap ? frame->slot_cap * 2u : 16u)) return false;
    size_t at = (size_t)(py_key_hash(key) & (frame->slot_cap - 1u));
    while (frame->slots[at]) {
        PyPair *pair = &frame->pairs[frame->slots[at] - 1u];
        if (py_key_equal(pair->key, key)) { pair->value = value; return true; }
        at = (at + 1u) & (frame->slot_cap - 1u);
    }
    if (frame->pair_len == frame->pair_cap) {
        size_t next = frame->pair_cap ? frame->pair_cap * 2u : 8u;
        PyPair *grown = realloc(frame->pairs, next * sizeof(*grown));
        if (!grown) return false;
        frame->pairs = grown;
        frame->pair_cap = next;
    }
    frame->pairs[frame->pair_len++] = (PyPair){key, value};
    frame->slots[at] = frame->pair_len;
    return true;
}

static bool py_array_push(PyFrame *frame, Atom *value) {
    if (frame->len == frame->cap) {
        size_t next = frame->cap ? frame->cap * 2u : 8u;
        Atom **grown = realloc(frame->items, next * sizeof(*grown));
        if (!grown) return false;
        frame->items = grown;
        frame->cap = next;
    }
    frame->items[frame->len++] = value;
    return true;
}

static Atom *py_close(PyParser *p, PyFrame *frame) {
    Atom *result = NULL;
    if (frame->object) {
        Atom **members = malloc((frame->pair_len ? frame->pair_len : 1u) * sizeof(*members));
        if (!members) { py_allocation(p); return NULL; }
        Atom *head = atom_symbol(p->arena, "JsonPair");
        for (size_t i = 0u; i < frame->pair_len; i++)
            members[i] = atom_expr3(p->arena, head, frame->pairs[i].key, frame->pairs[i].value);
        result = atom_expr2(p->arena, atom_symbol(p->arena, "JsonObject"),
                            atom_expr(p->arena, members, frame->pair_len));
        free(members);
    } else {
        result = atom_expr2(p->arena, atom_symbol(p->arena, "JsonArray"),
                            atom_expr(p->arena, frame->items, frame->len));
    }
    if (!result) py_allocation(p);
    return result;
}

static PyFrame *py_push_frame(PyParser *p, bool object) {
    if (p->depth == p->frame_cap) {
        size_t next = p->frame_cap ? p->frame_cap * 2u : 16u;
        PyFrame *grown = realloc(p->frames, next * sizeof(*grown));
        if (!grown) { py_allocation(p); return NULL; }
        p->frames = grown;
        p->frame_cap = next;
    }
    PyFrame *frame = &p->frames[p->depth++];
    memset(frame, 0, sizeof(*frame));
    frame->object = object;
    return frame;
}

/* Read an object key, the ':' and the whitespace after it, as
 * _parse_object_unicode does before each value. */
static bool py_object_key(PyParser *p, PyFrame *frame, size_t *idx_ptr) {
    size_t idx = *idx_ptr, end_idx = p->n - 1u, next;
    if (idx > end_idx || p->n == 0u || p->s[idx] != '"')
        return py_decode_error(p, "Expecting property name enclosed in double quotes", idx);
    Atom *key = py_scanstring(p, idx + 1u, &next);
    if (!key) return false;
    idx = py_skip_ws(p, next);
    if (idx > end_idx || p->s[idx] != ':')
        return py_decode_error(p, "Expecting ':' delimiter", idx);
    frame->key = key;
    *idx_ptr = py_skip_ws(p, idx + 1u);
    return true;
}

/* scan_once_unicode with the container parsers unrolled onto a stack.
 * A false result with *stop set is StopIteration(*stop_at). */
static Atom *py_scan(PyParser *p, size_t idx, size_t *next_out, bool *stop, size_t *stop_at) {
    size_t base = p->depth;
    Atom *value = NULL;
    for (;;) {
        /* Begin a value at idx. */
        if (idx >= p->n) { *stop = true; *stop_at = idx; goto failed; }
        uint32_t c = p->s[idx];
        size_t length = p->n, next = idx;
        value = NULL;
        switch (c) {
        case '"':
            value = py_scanstring(p, idx + 1u, &next);
            if (!value) goto failed;
            break;
        case '{': {
            size_t at = py_skip_ws(p, idx + 1u);
            if (at < length && p->s[at] == '}') {
                PyFrame empty = {.object = true};
                value = py_close(p, &empty);
                if (!value) goto failed;
                next = at + 1u;
                break;
            }
            PyFrame *frame = py_push_frame(p, true);
            if (!frame || !py_object_key(p, frame, &at)) goto failed;
            idx = at;
            continue;
        }
        case '[': {
            size_t at = py_skip_ws(p, idx + 1u);
            if (at < length && p->s[at] == ']') {
                PyFrame empty = {.object = false};
                value = py_close(p, &empty);
                if (!value) goto failed;
                next = at + 1u;
                break;
            }
            if (!py_push_frame(p, false)) goto failed;
            idx = at;
            continue;
        }
        case 'n':
            if (idx + 3u < length && py_matches(p, idx, "null")) {
                value = py_literal(p, "JsonNull"); next = idx + 4u;
            }
            break;
        case 't':
            if (idx + 3u < length && py_matches(p, idx, "true")) {
                value = py_literal(p, "true"); next = idx + 4u;
            }
            break;
        case 'f':
            if (idx + 4u < length && py_matches(p, idx, "false")) {
                value = py_literal(p, "false"); next = idx + 5u;
            }
            break;
        case 'N':
            if (idx + 2u < length && py_matches(p, idx, "NaN")) {
                value = atom_float(p->arena, __builtin_nan("")); next = idx + 3u;
            }
            break;
        case 'I':
            if (idx + 7u < length && py_matches(p, idx, "Infinity")) {
                value = atom_float(p->arena, __builtin_inf()); next = idx + 8u;
            }
            break;
        case '-':
            if (idx + 8u < length && py_matches(p, idx, "-Infinity")) {
                value = atom_float(p->arena, -__builtin_inf()); next = idx + 9u;
            }
            break;
        default:
            break;
        }
        if (!value) {
            if (p->error->status != CETTA_JSON_PYTHON_V1_OK) goto failed;
            value = py_number(p, idx, &next, stop);
            if (!value) {
                if (*stop) *stop_at = idx;
                goto failed;
            }
        }
        /* A value is complete at next; feed it to its container. */
        for (;;) {
            if (p->depth == base) {
                *next_out = next;
                return value;
            }
            PyFrame *frame = &p->frames[p->depth - 1u];
            size_t end_idx = p->n - 1u;
            if (frame->object ? !py_object_set(frame, frame->key, value) : !py_array_push(frame, value)) {
                py_allocation(p);
                goto failed;
            }
            idx = py_skip_ws(p, next);
            uint32_t close = frame->object ? '}' : ']';
            if (idx <= end_idx && p->s[idx] == close) {
                value = py_close(p, frame);
                py_frame_free(frame);
                p->depth--;
                if (!value) goto failed;
                next = idx + 1u;
                continue;
            }
            if (idx > end_idx || p->s[idx] != ',') {
                py_decode_error(p, "Expecting ',' delimiter", idx);
                goto failed;
            }
            idx = py_skip_ws(p, idx + 1u);
            if (frame->object && !py_object_key(p, frame, &idx)) goto failed;
            break;
        }
    }
failed:
    while (p->depth > base) py_frame_free(&p->frames[--p->depth]);
    return NULL;
}

bool cetta_json_python_v1_parse(Arena *arena, const uint8_t *bytes, size_t len,
                                bool petta_booleans, Atom **out,
                                CettaJsonPythonV1Error *error) {
    memset(error, 0, sizeof(*error));
    uint32_t *codes = malloc((len ? len : 1u) * sizeof(*codes));
    if (!codes) {
        error->status = CETTA_JSON_PYTHON_V1_ALLOCATION_FAILURE;
        error->msg = "out of memory";
        return false;
    }
    size_t n = 0u;
    for (size_t at = 0u; at < len;) {
        uint32_t width;
        if (!cetta_utf8_decode_surrogatepass(bytes, len, at, &codes[n], &width)) {
            free(codes);
            error->status = CETTA_JSON_PYTHON_V1_INVALID_TEXT;
            error->msg = "the bytes are not UTF-8, even with surrogatepass";
            error->byte_offset = at;
            return false;
        }
        at += width;
        n++;
    }
    PyParser p = {.arena = arena, .s = codes, .n = n, .petta = petta_booleans, .error = error};
    bool ok = false;
    if (n > 0u && codes[0] == 0xfeffu) {
        py_decode_error(&p, "Unexpected UTF-8 BOM (decode using utf-8-sig)", 0u);
    } else {
        size_t idx = py_skip_ws(&p, 0u), next = 0u, stop_at = 0u;
        bool stop = false;
        Atom *value = py_scan(&p, idx, &next, &stop, &stop_at);
        if (!value) {
            if (stop) py_decode_error(&p, "Expecting value", stop_at);
        } else {
            size_t end = py_skip_ws(&p, next);
            if (end != n) py_decode_error(&p, "Extra data", end);
            else { *out = value; ok = true; }
        }
    }
    if (error->status == CETTA_JSON_PYTHON_V1_DECODE_ERROR) {
        size_t lines = 0u, last = SIZE_MAX;
        for (size_t k = 0u; k < error->pos && k < n; k++)
            if (codes[k] == '\n') { lines++; last = k; }
        error->lineno = lines + 1u;
        error->colno = last == SIZE_MAX ? error->pos + 1u : error->pos - last;
    }
    free(p.text.bytes);
    free(p.frames);
    free(codes);
    return ok;
}
