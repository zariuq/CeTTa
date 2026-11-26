#include "str_natives.h"

#include "native_sha256.h"

#include "string_ops.h"
#include "str_stream.h"
#include "utf8.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static Atom *str_expects(CettaStrRefusal *refusal, const char *expected) {
    refusal->expected = expected;
    return NULL;
}

static Atom *str_refuses(CettaStrRefusal *refusal, const char *reason,
                         size_t offset) {
    refusal->reason = reason;
    refusal->offset = offset;
    return NULL;
}

static bool str_text(Atom *arg, const char **bytes, size_t *len) {
    if (!arg)
        return false;
    if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_STRING) {
        *bytes = arg->ground.sval;
        *len = arg->ground.slen;
        return true;
    }
    if (arg->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr(arg);
        if (!name)
            return false;
        *bytes = name;
        *len = strlen(name);
        return true;
    }
    return false;
}

static bool str_string(Atom *arg, const char **bytes, size_t *len) {
    return arg && arg->kind == ATOM_GROUNDED &&
           arg->ground.gkind == GV_STRING && str_text(arg, bytes, len);
}

static bool str_offset(Atom *arg, size_t *out) {
    if (!arg || arg->kind != ATOM_GROUNDED || arg->ground.gkind != GV_INT ||
        arg->ground.ival < 0)
        return false;
    *out = (size_t)arg->ground.ival;
    return true;
}

static Atom *str_from_buffer(Arena *a, CettaByteBuffer *buffer) {
    Atom *result = atom_string_n(a, buffer->bytes ? buffer->bytes : "",
                                 buffer->len);
    cetta_byte_buffer_free(buffer);
    return result;
}

static void str_append(CettaByteBuffer *out, const char *bytes, size_t len) {
    if (!cetta_byte_buffer_append(out, bytes, len))
        cetta_oom(out->len + len + 1u);
}

static const char *str_memmem(const char *haystack, size_t haystack_len,
                              const char *needle, size_t needle_len) {
    if (needle_len == 0u)
        return haystack;
    if (needle_len > haystack_len)
        return NULL;
    for (size_t i = 0u; i + needle_len <= haystack_len; i++) {
        if (haystack[i] == needle[0] &&
            memcmp(haystack + i, needle, needle_len) == 0)
            return haystack + i;
    }
    return NULL;
}

/* ── bytes ─────────────────────────────────────────────────────────────── */

static Atom *str_byte_length(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    return atom_int(a, (int64_t)len);
}

/* The bytes from start up to end; an offset outside the text is refused. */
static Atom *str_byte_slice(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len, start, stop;
    if (!str_text(args[0], &text, &len) || !str_offset(args[1], &start) ||
        !str_offset(args[2], &stop))
        return str_expects(r, "expected text, non-negative start, non-negative end");
    if (start > stop || stop > len)
        return str_refuses(r, "StrSliceOutsideTextV1", len);
    return atom_string_n(a, text + start, stop - start);
}

static Atom *str_concat(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *lhs, *rhs;
    size_t lhs_len, rhs_len;
    if (!str_text(args[0], &lhs, &lhs_len) || !str_text(args[1], &rhs, &rhs_len))
        return str_expects(r, "expected two text arguments");
    CettaByteBuffer out;
    cetta_byte_buffer_init(&out);
    str_append(&out, lhs, lhs_len);
    str_append(&out, rhs, rhs_len);
    return str_from_buffer(a, &out);
}

static Atom *str_join(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *sep;
    size_t sep_len;
    Atom *const *items = NULL;
    CettaExprLen count = 0u;
    if (!str_text(args[0], &sep, &sep_len) ||
        !atom_sequence_view(args[1], &items, &count))
        return str_expects(r, "expected separator and expression of text");
    CettaByteBuffer out;
    cetta_byte_buffer_init(&out);
    for (CettaExprLen i = 0u; i < count; i++) {
        const char *piece;
        size_t piece_len;
        if (!str_text(items[i], &piece, &piece_len)) {
            cetta_byte_buffer_free(&out);
            return str_expects(r, "expected separator and expression of text");
        }
        if (i > 0u)
            str_append(&out, sep, sep_len);
        str_append(&out, piece, piece_len);
    }
    return str_from_buffer(a, &out);
}

static void str_push_piece(Atom ***pieces, uint32_t *count, uint32_t *cap,
                           Atom *piece) {
    if (*count >= *cap) {
        *cap = *cap ? *cap * 2u : 8u;
        *pieces = cetta_realloc(*pieces, sizeof(Atom *) * *cap);
    }
    (*pieces)[(*count)++] = piece;
}

static Atom *str_split(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *sep, *text;
    size_t sep_len, text_len;
    if (!str_text(args[0], &sep, &sep_len) || !str_text(args[1], &text, &text_len))
        return str_expects(r, "expected separator and text");
    if (sep_len == 0u)
        return str_expects(r, "separator must be non-empty");
    Atom **pieces = NULL;
    uint32_t count = 0u, cap = 0u;
    size_t start = 0u;
    for (;;) {
        const char *found = str_memmem(text + start, text_len - start, sep, sep_len);
        size_t stop = found ? (size_t)(found - text) : text_len;
        str_push_piece(&pieces, &count, &cap,
                       atom_string_n(a, text + start, stop - start));
        if (!found)
            break;
        start = stop + sep_len;
    }
    Atom *result = atom_expr(a, pieces, count);
    free(pieces);
    return result;
}

static Atom *str_split_whitespace(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    Atom **pieces = NULL;
    uint32_t count = 0u, cap = 0u;
    size_t i = 0u;
    while (i < len) {
        while (i < len && isspace((unsigned char)text[i]))
            i++;
        if (i >= len)
            break;
        size_t start = i;
        while (i < len && !isspace((unsigned char)text[i]))
            i++;
        str_push_piece(&pieces, &count, &cap, atom_string_n(a, text + start, i - start));
    }
    Atom *result = atom_expr(a, pieces, count);
    free(pieces);
    return result;
}

static Atom *str_find(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *haystack, *needle;
    size_t haystack_len, needle_len;
    if (!str_text(args[0], &haystack, &haystack_len) ||
        !str_text(args[1], &needle, &needle_len))
        return str_expects(r, "expected text and search text");
    const char *found = str_memmem(haystack, haystack_len, needle, needle_len);
    if (!found)
        return atom_expr2(a, atom_symbol(a, "TextNotFound"), args[1]);
    return atom_int(a, (int64_t)(found - haystack));
}

static Atom *str_starts_with(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text, *prefix;
    size_t text_len, prefix_len;
    if (!str_text(args[0], &text, &text_len) || !str_text(args[1], &prefix, &prefix_len))
        return str_expects(r, "expected text and prefix");
    return prefix_len <= text_len && memcmp(text, prefix, prefix_len) == 0
        ? atom_true(a) : atom_false(a);
}

static Atom *str_ends_with(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text, *suffix;
    size_t text_len, suffix_len;
    if (!str_text(args[0], &text, &text_len) || !str_text(args[1], &suffix, &suffix_len))
        return str_expects(r, "expected text and suffix");
    return suffix_len <= text_len &&
           memcmp(text + text_len - suffix_len, suffix, suffix_len) == 0
        ? atom_true(a) : atom_false(a);
}

static Atom *str_trim(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    size_t start = 0u, stop = len;
    while (start < len && isspace((unsigned char)text[start]))
        start++;
    while (stop > start && isspace((unsigned char)text[stop - 1u]))
        stop--;
    return atom_string_n(a, text + start, stop - start);
}

/* -1, 0 or 1 by byte order, a proper prefix first. */
static Atom *str_compare(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *left, *right;
    size_t left_len, right_len;
    if (!str_text(args[0], &left, &left_len) || !str_text(args[1], &right, &right_len))
        return str_expects(r, "expected two text arguments");
    return atom_int(a, cetta_bytes_compare(left, left_len, right, right_len));
}

/* ── codepoints ────────────────────────────────────────────────────────── */

static Atom *str_codepoint_count(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len, count = 0u, bad = 0u;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    if (!cetta_utf8_count(text, len, &count, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    return atom_int(a, (int64_t)count);
}

/* The codepoints of a text as a list value of one-codepoint strings, so that
 * [$c | $rest] matches over its characters. */
static Atom *str_codepoints(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len, count = 0u, bad = 0u;
    CettaUtf8Unit *units = NULL;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    if (!cetta_utf8_units(text, len, &units, &count, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    Atom **items = arena_alloc(a, sizeof(Atom *) * (count ? count : 1u));
    for (size_t k = 0u; k < count; k++)
        items[k] = atom_string_n(a, text + units[k].offset, units[k].width);
    free(units);
    return atom_list(a, items, (CettaExprLen)count);
}

/* The scalar values of a text's codepoints, as integers: what codepoints
 * gives as one-codepoint strings. */
static Atom *str_codepoint_values(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len, count = 0u, bad = 0u;
    CettaUtf8Unit *units = NULL;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    if (!cetta_utf8_units(text, len, &units, &count, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    Atom **items = arena_alloc(a, sizeof(Atom *) * (count ? count : 1u));
    for (size_t k = 0u; k < count; k++)
        items[k] = atom_int(a, (int64_t)units[k].scalar);
    free(units);
    return atom_list(a, items, (CettaExprLen)count);
}

/* An unsigned 64-bit integer argument: a machine integer from 0, or a big one
 * below 2^64. */
static bool str_u64_arg(Atom *arg, uint64_t *out) {
    if (!arg || arg->kind != ATOM_GROUNDED)
        return false;
    if (arg->ground.gkind == GV_INT) {
        if (arg->ground.ival < 0)
            return false;
        *out = (uint64_t)arg->ground.ival;
        return true;
    }
    if (arg->ground.gkind != GV_BIGINT)
        return false;
    mpz_t z;
    mpz_init(z);
    bool ok = atom_bigint_get_mpz(arg, z) && mpz_sgn(z) >= 0 && mpz_sizeinbase(z, 2) <= 64u;
    if (ok) {
        uint64_t v = 0u;
        size_t words = 0u;
        mpz_export(&v, &words, -1, sizeof(v), 0, 0, z);
        *out = words ? v : 0u;
    }
    mpz_clear(z);
    return ok;
}

#define STR_SIP_ROTATE(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))
#define STR_SIP_HALF_ROUND(a, b, c, d, s, t) \
    a += b; c += d;                          \
    b = STR_SIP_ROTATE(b, s) ^ a;            \
    d = STR_SIP_ROTATE(d, t) ^ c;            \
    a = STR_SIP_ROTATE(a, 32);
#define STR_SIP_ROUND(v0, v1, v2, v3)                \
    STR_SIP_HALF_ROUND(v0, v1, v2, v3, 13, 16);      \
    STR_SIP_HALF_ROUND(v2, v1, v0, v3, 17, 21);

/* SipHash-1-3 with the four lanes xor'ed at the end (CPython's siphash13). */
static uint64_t str_siphash13_bytes(uint64_t k0, uint64_t k1, const uint8_t *in, size_t len) {
    uint64_t b = (uint64_t)len << 56;
    uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;
    uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;
    uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;
    uint64_t v3 = k1 ^ 0x7465646279746573ULL;
    while (len >= 8u) {
        uint64_t mi = 0u;
        for (size_t k = 0u; k < 8u; k++)
            mi |= (uint64_t)in[k] << (8u * k);
        in += 8u;
        len -= 8u;
        v3 ^= mi;
        STR_SIP_ROUND(v0, v1, v2, v3);
        v0 ^= mi;
    }
    uint64_t t = 0u;
    for (size_t k = 0u; k < len; k++)
        t |= (uint64_t)in[k] << (8u * k);
    b |= t;
    v3 ^= b;
    STR_SIP_ROUND(v0, v1, v2, v3);
    v0 ^= b;
    v2 ^= 0xff;
    STR_SIP_ROUND(v0, v1, v2, v3);
    STR_SIP_ROUND(v0, v1, v2, v3);
    STR_SIP_ROUND(v0, v1, v2, v3);
    return (v0 ^ v1) ^ (v2 ^ v3);
}

/* SipHash-1-3 (CPython's variant) under the key (k0, k1) of the text's code
 * points written in the narrowest fixed width that holds them all (1, 2 or 4
 * bytes, little-endian): the hash CPython gives a str under that key.  An
 * integer from 0 to 2^64 - 1; the empty text hashes to 0. */
static Atom *str_siphash13(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    uint64_t k0 = 0u, k1 = 0u;
    const char *text;
    size_t len, count = 0u, bad = 0u;
    CettaUtf8Unit *units = NULL;
    if (!str_u64_arg(args[0], &k0) || !str_u64_arg(args[1], &k1))
        return str_expects(r, "expected two keys from 0 to 2^64 - 1 and a text");
    if (!str_text(args[2], &text, &len))
        return str_expects(r, "expected two keys from 0 to 2^64 - 1 and a text");
    if (!cetta_utf8_units(text, len, &units, &count, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    if (count == 0u) {
        free(units);
        return atom_int(a, 0);
    }
    uint32_t max = 0u;
    for (size_t k = 0u; k < count; k++)
        if (units[k].scalar > max)
            max = units[k].scalar;
    size_t width = max < 0x100u ? 1u : max < 0x10000u ? 2u : 4u;
    uint8_t *buf = malloc(count * width);
    if (!buf) {
        free(units);
        return str_expects(r, "expected a text that fits in memory");
    }
    for (size_t k = 0u; k < count; k++)
        for (size_t j = 0u; j < width; j++)
            buf[k * width + j] = (uint8_t)(units[k].scalar >> (8u * j));
    uint64_t h = str_siphash13_bytes(k0, k1, buf, count * width);
    free(buf);
    free(units);
    mpz_t z;
    mpz_init(z);
    mpz_import(z, 1, -1, sizeof(h), 0, 0, &h);
    return atom_bigint_take_mpz(a, z);
}

/* The bytes of a text, as integers from 0 to 255.  A byte operation: any
 * bytes, well-formed UTF-8 or not. */
static Atom *str_byte_values(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    Atom **items = arena_alloc(a, sizeof(Atom *) * (len ? len : 1u));
    for (size_t k = 0u; k < len; k++)
        items[k] = atom_int(a, (int64_t)(unsigned char)text[k]);
    return atom_list(a, items, (CettaExprLen)len);
}

/* One string from a list or expression of codepoints, each a one-codepoint
 * string or an integer scalar value, in one allocation. */
static Atom *str_from_codepoints(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    Atom *const *items = NULL;
    CettaExprLen count = 0u;
    if (!atom_sequence_view(args[0], &items, &count))
        return str_expects(r, "expected a list of codepoints");
    CettaByteBuffer out;
    cetta_byte_buffer_init(&out);
    for (CettaExprLen k = 0u; k < count; k++) {
        Atom *item = items[k];
        size_t one = 0u;
        if (item && item->kind == ATOM_GROUNDED && item->ground.gkind == GV_INT &&
            item->ground.ival >= 0 && item->ground.ival <= 0x10ffff &&
            cetta_utf8_scalar_valid((uint32_t)item->ground.ival)) {
            uint8_t encoded[4];
            size_t width = cetta_utf8_encode((uint32_t)item->ground.ival, encoded);
            str_append(&out, (const char *)encoded, width);
        } else if (item && item->kind == ATOM_GROUNDED &&
                   item->ground.gkind == GV_STRING &&
                   cetta_utf8_count(item->ground.sval, item->ground.slen, &one, NULL) &&
                   one == 1u) {
            str_append(&out, item->ground.sval, item->ground.slen);
        } else {
            cetta_byte_buffer_free(&out);
            return str_refuses(r, "StrNotACodepointV1", (size_t)k);
        }
    }
    return str_from_buffer(a, &out);
}

/* (str:from-bytes VALUES): the string of these byte values, 0 to 255, whatever
 * they encode (the bytes of a language's binary data). */
static Atom *str_from_bytes(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    Atom *const *items = NULL;
    CettaExprLen count = 0u;
    if (!atom_sequence_view(args[0], &items, &count))
        return str_expects(r, "expected a list of byte values");
    CettaByteBuffer out;
    cetta_byte_buffer_init(&out);
    for (CettaExprLen k = 0u; k < count; k++) {
        Atom *item = items[k];
        if (!item || item->kind != ATOM_GROUNDED || item->ground.gkind != GV_INT ||
            item->ground.ival < 0 || item->ground.ival > 255) {
            cetta_byte_buffer_free(&out);
            return str_refuses(r, "StrNotAByteV1", (size_t)k);
        }
        char byte = (char)(uint8_t)item->ground.ival;
        str_append(&out, &byte, 1u);
    }
    return str_from_buffer(a, &out);
}

/* One string from code points up to U+10FFFF, surrogates included, each in
 * UTF-8's bit pattern (generalized UTF-8): the text of a language whose
 * strings are code point sequences, as Python's str is.  It is UTF-8 exactly
 * when it holds no surrogate; a surrogate pair stays two code points, so the
 * string of two surrogates is not the string of the character they pair to. */
static Atom *str_from_codepoints_generalized(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    Atom *const *items = NULL;
    CettaExprLen count = 0u;
    if (!atom_sequence_view(args[0], &items, &count))
        return str_expects(r, "expected a list of codepoints");
    CettaByteBuffer out;
    cetta_byte_buffer_init(&out);
    for (CettaExprLen k = 0u; k < count; k++) {
        Atom *item = items[k];
        if (!(item && item->kind == ATOM_GROUNDED && item->ground.gkind == GV_INT &&
              item->ground.ival >= 0 && item->ground.ival <= 0x10ffff)) {
            cetta_byte_buffer_free(&out);
            return str_refuses(r, "StrNotACodepointV1", (size_t)k);
        }
        uint8_t encoded[4];
        size_t width = cetta_utf8_encode((uint32_t)item->ground.ival, encoded);
        str_append(&out, (const char *)encoded, width);
    }
    return str_from_buffer(a, &out);
}

/* ── spelling ──────────────────────────────────────────────────────────── */

static Atom *str_spelled(Arena *a, Atom *text_arg, const CettaCharClass *initial,
                         const CettaCharClass *rest, CettaStrRefusal *r) {
    const char *text;
    size_t len, bad = 0u;
    bool holds = false;
    if (!str_text(text_arg, &text, &len))
        return str_expects(r, "expected text argument");
    if (!cetta_utf8_spelled(text, len, initial, rest, &holds, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    return holds ? atom_true(a) : atom_false(a);
}

static bool str_class(Atom *arg, CettaCharClass *out) {
    return arg && arg->kind == ATOM_SYMBOL &&
           cetta_char_class_from_name(atom_name_cstr(arg), out);
}

/* Whether every codepoint of a text is in a class; the empty text is. */
static Atom *str_all_in(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    CettaCharClass cls;
    if (!str_class(args[0], &cls))
        return str_expects(r, "expected a character class and text");
    return str_spelled(a, args[1], NULL, &cls, r);
}

/* Whether the first codepoint of a text is in a class; the empty text has
 * none. */
static Atom *str_initial_in(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    CettaCharClass cls;
    if (!str_class(args[0], &cls))
        return str_expects(r, "expected a character class and text");
    return str_spelled(a, args[1], &cls, NULL, r);
}

static const CettaCharClass str_class_alnum = CETTA_CHAR_CLASS_ALNUM;
static const CettaCharClass str_class_lower = CETTA_CHAR_CLASS_LOWER;
static const CettaCharClass str_class_upper = CETTA_CHAR_CLASS_UPPER;
static const CettaCharClass str_class_word = CETTA_CHAR_CLASS_WORD;

static Atom *str_alphanumeric(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_spelled(a, args[0], NULL, &str_class_alnum, r);
}

static Atom *str_lower_initial(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_spelled(a, args[0], &str_class_lower, NULL, r);
}

static Atom *str_upper_initial(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_spelled(a, args[0], &str_class_upper, NULL, r);
}

/* A lower word is a lower-case letter followed by word characters, and an
 * upper word an upper-case one: the spellings of TPTP's lower_word and
 * upper_word. */
static Atom *str_lower_word(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_spelled(a, args[0], &str_class_lower, &str_class_word, r);
}

static Atom *str_upper_word(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_spelled(a, args[0], &str_class_upper, &str_class_word, r);
}

/* ── conversions ───────────────────────────────────────────────────────── */

/* A symbol spelled by a string: nonempty UTF-8 without NUL. */
static Atom *str_to_symbol(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len, bad = 0u;
    if (!str_string(args[0], &text, &len))
        return str_expects(r, "expected a string");
    const char *nul = memchr(text, '\0', len);
    if (len == 0u)
        return str_refuses(r, "StrEmptySymbolV1", 0u);
    if (nul)
        return str_refuses(r, "StrNulInSymbolV1", (size_t)(nul - text));
    if (!cetta_utf8_count(text, len, NULL, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    return atom_symbol(a, text);
}

static Atom *str_from_symbol(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    if (!args[0] || args[0]->kind != ATOM_SYMBOL)
        return str_expects(r, "expected a symbol");
    const char *name = atom_name_cstr(args[0]);
    return atom_string(a, name ? name : "");
}

/* A number from its whole decimal spelling: an integer of any size, or a
 * float with a point or an exponent, rounded to the nearest double.  Any
 * other text, trailing text, and a float beyond the finite doubles are
 * refused. */
static Atom *str_to_number(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len;
    if (!str_string(args[0], &text, &len))
        return str_expects(r, "expected a string");
    size_t i = 0u;
    bool negative = i < len && text[i] == '-';
    if (i < len && (negative || text[i] == '+'))
        i++;
    size_t digits_start = i;
    uint64_t magnitude = 0u;
    uint64_t limit = negative
        ? (uint64_t)INT64_MAX + UINT64_C(1)
        : (uint64_t)INT64_MAX;
    bool fits_int64 = true;
    while (i < len && text[i] >= '0' && text[i] <= '9') {
        uint64_t digit = (uint64_t)(text[i] - '0');
        if (fits_int64) {
            if (magnitude > (limit - digit) / UINT64_C(10))
                fits_int64 = false;
            else
                magnitude = magnitude * UINT64_C(10) + digit;
        }
        i++;
    }
    if (i > digits_start && i == len) {
        if (fits_int64) {
            int64_t value = negative
                ? magnitude == (uint64_t)INT64_MAX + UINT64_C(1)
                    ? INT64_MIN : -(int64_t)magnitude
                : (int64_t)magnitude;
            return atom_int(a, value);
        }
        char *copy = arena_alloc(a, len + 1u);
        memcpy(copy, text, len);
        copy[len] = '\0';
        return atom_bigint(a, copy[0] == '+' ? copy + 1 : copy);
    }
    bool saw_digit = i > digits_start;
    if (i < len && text[i] == '.') {
        i++;
        while (i < len && text[i] >= '0' && text[i] <= '9') {
            i++;
            saw_digit = true;
        }
    }
    if (saw_digit && i < len && (text[i] == 'e' || text[i] == 'E')) {
        size_t exponent = i + 1u;
        if (exponent < len && (text[exponent] == '-' || text[exponent] == '+'))
            exponent++;
        size_t exponent_digits = exponent;
        while (exponent < len && text[exponent] >= '0' && text[exponent] <= '9')
            exponent++;
        if (exponent > exponent_digits)
            i = exponent;
    }
    if (!saw_digit || i != len)
        return str_refuses(r, "StrNotANumberV1", i);
    char *copy = arena_alloc(a, len + 1u);
    memcpy(copy, text, len);
    copy[len] = '\0';
    double value = strtod(copy, NULL);
    if (isinf(value))
        return str_refuses(r, "StrNumberOutOfRangeV1", 0u);
    return atom_float(a, value);
}

static Atom *str_from_number(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    if (!args[0] || args[0]->kind != ATOM_GROUNDED ||
        (args[0]->ground.gkind != GV_INT && args[0]->ground.gkind != GV_FLOAT &&
         args[0]->ground.gkind != GV_BIGINT && args[0]->ground.gkind != GV_RATIONAL))
        return str_expects(r, "expected a number");
    return atom_string(a, atom_to_parseable_string(a, args[0]));
}

/* ── sets ──────────────────────────────────────────────────────────────── */

static int str_atom_order(const void *left, const void *right) {
    return atom_string_compare(*(Atom *const *)left, *(Atom *const *)right);
}

/* A string set built once: its distinct members in byte order, a value that
 * can be bound and shared; membership searches it without rebuilding it. */
static Atom *str_set(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    Atom *const *items = NULL;
    CettaExprLen count = 0u;
    if (!atom_sequence_view(args[0], &items, &count))
        return str_expects(r, "expected a list of strings");
    Atom **members = arena_alloc(a, sizeof(Atom *) * (count ? count : 1u));
    for (CettaExprLen k = 0u; k < count; k++) {
        if (!items[k] || items[k]->kind != ATOM_GROUNDED ||
            items[k]->ground.gkind != GV_STRING)
            return str_expects(r, "expected a list of strings");
        members[k] = items[k];
    }
    qsort(members, count, sizeof(Atom *), str_atom_order);
    CettaExprLen distinct = 0u;
    for (CettaExprLen k = 0u; k < count; k++) {
        if (distinct == 0u || !atom_string_equal(members[distinct - 1u], members[k]))
            members[distinct++] = members[k];
    }
    return atom_expr2(a, atom_symbol(a, "StrSetV1"), atom_list(a, members, distinct));
}

static Atom *str_set_member(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len;
    Atom *const *members = NULL;
    CettaExprLen count = 0u;
    Atom *set = args[0];
    if (!set || set->kind != ATOM_EXPR || set->expr.len != 2u ||
        !atom_is_symbol(set->expr.elems[0], "StrSetV1") ||
        !atom_is_list(set->expr.elems[1]) ||
        !atom_sequence_view(set->expr.elems[1], &members, &count) ||
        !str_text(args[1], &text, &len))
        return str_expects(r, "expected a string set and text");
    CettaExprLen low = 0u, high = count;
    while (low < high) {
        CettaExprLen middle = low + (high - low) / 2u;
        Atom *member = members[middle];
        if (!member || member->kind != ATOM_GROUNDED || member->ground.gkind != GV_STRING)
            return str_expects(r, "expected a string set and text");
        int order = cetta_bytes_compare(member->ground.sval, member->ground.slen,
                                        text, len);
        if (order == 0)
            return atom_true(a);
        if (order < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return atom_false(a);
}

/* ── quoting ───────────────────────────────────────────────────────────── */

static Atom *str_quote(Arena *a, Atom *const *args, CettaStrRefusal *r, bool escape) {
    const char *text;
    size_t len, bad = 0u;
    CettaQuotingScheme scheme;
    if (!args[0] || args[0]->kind != ATOM_SYMBOL ||
        !cetta_quoting_scheme_from_name(atom_name_cstr(args[0]), &scheme) ||
        !str_string(args[1], &text, &len))
        return str_expects(r, "expected a quoting scheme and a string");
    CettaByteBuffer out;
    cetta_byte_buffer_init(&out);
    bool ok = escape ? cetta_string_escape(scheme, text, len, &out, &bad)
                     : cetta_string_unescape(scheme, text, len, &out, &bad);
    if (!ok) {
        cetta_byte_buffer_free(&out);
        return str_refuses(r, escape ? "StrUnrepresentableV1" : "StrInvalidEscapeV1", bad);
    }
    return str_from_buffer(a, &out);
}

static Atom *str_escape(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_quote(a, args, r, true);
}

static Atom *str_unescape(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_quote(a, args, r, false);
}

/* The SHA-256 digest of the text's bytes, as 64 lowercase hex digits. */
static Atom *str_sha256(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len;
    char digest[65];
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    cetta_native_sha256_hex((const uint8_t *)text, len, digest);
    return atom_string(a, digest);
}

/* Unicode character data, generated from CPython's database (Unicode
 * 15.1.0): a code point's properties as one bit set (its general category,
 * then derived properties), and its full case mappings. */
typedef struct {
    uint32_t first;
    uint64_t props;
} CettaUnicodeRange;

typedef struct {
    uint32_t cp;
    uint16_t at;
    uint8_t len;
} CettaUnicodeMapping;

#include "generated/unicode_tables_v1.generated.h"

#define UNICODE_COUNT(table) (sizeof(table) / sizeof((table)[0]))

static uint64_t unicode_props(uint32_t cp) {
    size_t lo = 0u, hi = UNICODE_COUNT(cetta_unicode_ranges);
    while (hi - lo > 1u) {
        size_t mid = lo + (hi - lo) / 2u;
        if (cetta_unicode_ranges[mid].first <= cp)
            lo = mid;
        else
            hi = mid;
    }
    return cetta_unicode_ranges[lo].props;
}

static const CettaUnicodeMapping *unicode_mapping(const CettaUnicodeMapping *table,
                                                  size_t n, uint32_t cp) {
    size_t lo = 0u, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (table[mid].cp < cp)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo < n && table[lo].cp == cp ? &table[lo] : NULL;
}

/* The Final_Sigma condition (Unicode 3.13, table 3-17) of the capital sigma
 * at K: a cased letter before it, past case-ignorable ones, and none after. */
static bool unicode_final_sigma(const CettaUtf8Unit *s, size_t n, size_t k) {
    size_t j;
    uint64_t p = 0u;
    for (j = k; j > 0u; j--) {
        p = unicode_props(s[j - 1u].scalar);
        if (!(p & CETTA_UNICODE_CASE_IGNORABLE))
            break;
    }
    if (j == 0u || !(p & CETTA_UNICODE_CASED))
        return false;
    for (j = k + 1u; j < n; j++) {
        p = unicode_props(s[j].scalar);
        if (!(p & CETTA_UNICODE_CASE_IGNORABLE))
            break;
    }
    return j == n || !(p & CETTA_UNICODE_CASED);
}

static void str_append_scalar(CettaByteBuffer *out, uint32_t scalar) {
    uint8_t encoded[4];
    size_t width = cetta_utf8_encode(scalar, encoded);
    str_append(out, (const char *)encoded, width);
}

/* (str:case-map KIND TEXT): TEXT with each code point's full case mapping of
 * KIND, "lower", "upper", "title" or "fold"; lowering, a capital sigma becomes
 * final or not by the Final_Sigma condition. */
static Atom *str_case_map(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *kind, *text;
    size_t kind_len, len, count = 0u, bad = 0u;
    if (!str_text(args[0], &kind, &kind_len) || !str_text(args[1], &text, &len))
        return str_expects(r, "expected a case kind and a text");
    const CettaUnicodeMapping *table;
    size_t n;
    bool lower = false;
    if (kind_len == 5u && memcmp(kind, "lower", 5u) == 0) {
        table = cetta_unicode_lower; n = UNICODE_COUNT(cetta_unicode_lower); lower = true;
    } else if (kind_len == 5u && memcmp(kind, "upper", 5u) == 0) {
        table = cetta_unicode_upper; n = UNICODE_COUNT(cetta_unicode_upper);
    } else if (kind_len == 5u && memcmp(kind, "title", 5u) == 0) {
        table = cetta_unicode_title; n = UNICODE_COUNT(cetta_unicode_title);
    } else if (kind_len == 4u && memcmp(kind, "fold", 4u) == 0) {
        table = cetta_unicode_fold; n = UNICODE_COUNT(cetta_unicode_fold);
    } else {
        return str_expects(r, "expected lower, upper, title or fold");
    }
    CettaUtf8Unit *units = NULL;
    if (!cetta_utf8_units(text, len, &units, &count, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    CettaByteBuffer out;
    cetta_byte_buffer_init(&out);
    for (size_t k = 0u; k < count; k++) {
        uint32_t cp = units[k].scalar;
        if (lower && cp == 0x3a3u) {
            str_append_scalar(&out, unicode_final_sigma(units, count, k) ? 0x3c2u : 0x3c3u);
            continue;
        }
        const CettaUnicodeMapping *m = unicode_mapping(table, n, cp);
        if (!m) {
            str_append(&out, text + units[k].offset, units[k].width);
            continue;
        }
        for (uint8_t i = 0u; i < m->len; i++)
            str_append_scalar(&out, cetta_unicode_pool[m->at + i]);
    }
    free(units);
    return str_from_buffer(a, &out);
}

/* (str:props-all MASK TEXT): whether every code point of TEXT has a property
 * of the bit set MASK; (str:props-any MASK TEXT): whether one has.  The bits
 * are the generated table's (CETTA_UNICODE_*). */
static Atom *str_props_test(Arena *a, Atom *const *args, CettaStrRefusal *r, bool all) {
    const char *text;
    size_t len, count = 0u, bad = 0u;
    if (!args[0] || args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_INT ||
        args[0]->ground.ival < 0 || !str_text(args[1], &text, &len))
        return str_expects(r, "expected a property mask and a text");
    uint64_t mask = (uint64_t)args[0]->ground.ival;
    CettaUtf8Unit *units = NULL;
    if (!cetta_utf8_units(text, len, &units, &count, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    bool result = all;
    for (size_t k = 0u; k < count; k++) {
        bool has = (unicode_props(units[k].scalar) & mask) != 0u;
        if (has != all) {
            result = !all;
            break;
        }
    }
    free(units);
    return result ? atom_true(a) : atom_false(a);
}

static Atom *str_props_all(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_props_test(a, args, r, true);
}

static Atom *str_props_any(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_props_test(a, args, r, false);
}

/* (str:code-props CODE-POINT): the code point's property bit set. */
static Atom *str_code_props(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    if (!args[0] || args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_INT ||
        args[0]->ground.ival < 0 || args[0]->ground.ival > 0x10ffff)
        return str_expects(r, "expected a code point");
    return atom_int(a, (int64_t)unicode_props((uint32_t)args[0]->ground.ival));
}

/* The SIDE argument of the trims: 1 the start, 2 the end, 3 both. */
static int str_side(Atom *arg) {
    const char *side;
    size_t len;
    if (!str_text(arg, &side, &len))
        return 0;
    if (len == 4u && memcmp(side, "left", 4u) == 0) return 1;
    if (len == 5u && memcmp(side, "right", 5u) == 0) return 2;
    if (len == 4u && memcmp(side, "both", 4u) == 0) return 3;
    return 0;
}

static bool str_unit_in(uint32_t scalar, const CettaUtf8Unit *set, size_t n) {
    for (size_t i = 0u; i < n; i++)
        if (set[i].scalar == scalar)
            return true;
    return false;
}

/* (str:trim-props MASK SIDE TEXT): TEXT without the code points with a
 * property of MASK at its start (SIDE "left"), its end ("right") or both;
 * (str:trim-chars CHARS SIDE TEXT): without the code points of CHARS. */
static Atom *str_trim_by(Arena *a, Atom *const *args, CettaStrRefusal *r, bool props) {
    const char *text, *chars = NULL;
    size_t len, chars_len = 0u, count = 0u, n_set = 0u, bad = 0u;
    uint64_t mask = 0u;
    int side = str_side(args[1]);
    if (props) {
        if (!args[0] || args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_INT ||
            args[0]->ground.ival < 0)
            side = 0;
        else
            mask = (uint64_t)args[0]->ground.ival;
    } else if (!str_text(args[0], &chars, &chars_len)) {
        side = 0;
    }
    if (!side || !str_text(args[2], &text, &len))
        return str_expects(r, props ? "expected a property mask, a side and a text"
                                    : "expected characters, a side and a text");
    CettaUtf8Unit *units = NULL, *set = NULL;
    if (!cetta_utf8_units(text, len, &units, &count, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    if (!props && !cetta_utf8_units(chars, chars_len, &set, &n_set, &bad)) {
        free(units);
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    }
    size_t lo = 0u, hi = count;
    if (side & 1)
        while (lo < hi && (props ? (unicode_props(units[lo].scalar) & mask) != 0u
                                 : str_unit_in(units[lo].scalar, set, n_set)))
            lo++;
    if (side & 2)
        while (hi > lo && (props ? (unicode_props(units[hi - 1u].scalar) & mask) != 0u
                                 : str_unit_in(units[hi - 1u].scalar, set, n_set)))
            hi--;
    size_t from = lo < count ? units[lo].offset : len;
    size_t to = hi > 0u && hi > lo ? units[hi - 1u].offset + units[hi - 1u].width : from;
    free(units);
    free(set);
    return atom_string_n(a, text + from, to - from);
}

static Atom *str_trim_props(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_trim_by(a, args, r, true);
}

static Atom *str_trim_chars(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_trim_by(a, args, r, false);
}

/* (str:split-props MASK MAXSPLIT TEXT): the runs of TEXT's code points
 * without a property of MASK, between runs with one; after MAXSPLIT splits (a
 * negative one: no limit) the rest is one item from its first code point
 * without the property.  (str:rsplit-props ...) splits from the end, the rest
 * the first item, to its last code point without the property. */
static Atom *str_split_props_by(Arena *a, Atom *const *args, CettaStrRefusal *r, bool from_end) {
    const char *text;
    size_t len, count = 0u, bad = 0u;
    if (!args[0] || args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_INT ||
        args[0]->ground.ival < 0 || !args[1] || args[1]->kind != ATOM_GROUNDED ||
        args[1]->ground.gkind != GV_INT || !str_text(args[2], &text, &len))
        return str_expects(r, "expected a property mask, a split count and a text");
    uint64_t mask = (uint64_t)args[0]->ground.ival;
    int64_t limit = args[1]->ground.ival;
    CettaUtf8Unit *units = NULL;
    if (!cetta_utf8_units(text, len, &units, &count, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    Atom **items = arena_alloc(a, sizeof(Atom *) * (count + 1u));
    size_t n = 0u;
#define STR_IN(k) ((unicode_props(units[(k)].scalar) & mask) != 0u)
    if (!from_end) {
        size_t i = 0u;
        int64_t splits = 0;
        while (i < count) {
            while (i < count && STR_IN(i)) i++;
            if (i == count) break;
            if (limit >= 0 && splits == limit) {
                items[n++] = atom_string_n(a, text + units[i].offset, len - units[i].offset);
                break;
            }
            size_t j = i;
            while (j < count && !STR_IN(j)) j++;
            size_t end = j < count ? units[j].offset : len;
            items[n++] = atom_string_n(a, text + units[i].offset, end - units[i].offset);
            splits++;
            i = j;
        }
    } else {
        size_t i = count;
        int64_t splits = 0;
        while (i > 0u) {
            while (i > 0u && STR_IN(i - 1u)) i--;
            if (i == 0u) break;
            size_t end = units[i - 1u].offset + units[i - 1u].width;
            if (limit >= 0 && splits == limit) {
                items[n++] = atom_string_n(a, text, end);
                break;
            }
            size_t j = i;
            while (j > 0u && !STR_IN(j - 1u)) j--;
            items[n++] = atom_string_n(a, text + units[j].offset, end - units[j].offset);
            splits++;
            i = j;
        }
        for (size_t k = 0u; k < n / 2u; k++) {
            Atom *t = items[k];
            items[k] = items[n - 1u - k];
            items[n - 1u - k] = t;
        }
    }
#undef STR_IN
    free(units);
    return atom_list(a, items, (CettaExprLen)n);
}

static Atom *str_split_props(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_split_props_by(a, args, r, false);
}

static Atom *str_rsplit_props(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_split_props_by(a, args, r, true);
}

/* (str:find-index SUB TEXT): the code point index of SUB's first occurrence
 * in TEXT, -1 when none; (str:rfind-index SUB TEXT): of its last, matches
 * that overlap counted. */
static Atom *str_find_index_by(Arena *a, Atom *const *args, CettaStrRefusal *r, bool last) {
    const char *sub, *text;
    size_t sub_len, len, count = 0u, bad = 0u;
    if (!str_text(args[0], &sub, &sub_len) || !str_text(args[1], &text, &len))
        return str_expects(r, "expected a substring and a text");
    size_t at = SIZE_MAX;
    if (sub_len <= len) {
        if (!last) {
            const char *hit = str_memmem(text, len, sub, sub_len);
            if (hit)
                at = (size_t)(hit - text);
        } else {
            for (size_t i = len - sub_len + 1u; i-- > 0u;) {
                if (memcmp(text + i, sub, sub_len) == 0) {
                    at = i;
                    break;
                }
            }
        }
    }
    if (at == SIZE_MAX)
        return atom_int(a, -1);
    if (!cetta_utf8_count(text, at, &count, &bad))
        return str_refuses(r, "StrMalformedUtf8V1", bad);
    return atom_int(a, (int64_t)count);
}

static Atom *str_find_index(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_find_index_by(a, args, r, false);
}

static Atom *str_rfind_index(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    return str_find_index_by(a, args, r, true);
}

static const CettaStrNative str_natives[] = {
    {"utf8-new", 1u, 0u, cetta_str_utf8_new, CETTA_STR_RESULT_STRUCTURE},
    {"utf8-feed", 3u, 1u << 0, cetta_str_utf8_feed, CETTA_STR_RESULT_STRUCTURE},
    {"line-segments", 2u, 0u, cetta_str_line_segments, CETTA_STR_RESULT_STRUCTURE},
    {"line-new", 0u, 0u, cetta_str_line_new, CETTA_STR_RESULT_STRUCTURE},
    {"collect-lines", 3u, (1u << 0) | (1u << 1), cetta_str_collect_lines, CETTA_STR_RESULT_STRUCTURE},
    {"feed-lines", 3u, 1u << 0, cetta_str_feed_lines, CETTA_STR_RESULT_STRUCTURE},
    {"advance-lines", 3u, 1u << 0, cetta_str_advance_lines, CETTA_STR_RESULT_STRUCTURE},
    {"find-ascii-uncased", 2u, 0u, cetta_str_find_ascii_uncased, CETTA_STR_RESULT_STRUCTURE},
    {"byte-length", 1u, 0u, str_byte_length, CETTA_STR_RESULT_ATOM},
    {"byte-slice", 3u, 0u, str_byte_slice, CETTA_STR_RESULT_ATOM},
    {"byte-values", 1u, 0u, str_byte_values, CETTA_STR_RESULT_STRUCTURE},
    {"siphash13", 3u, 0u, str_siphash13, CETTA_STR_RESULT_ATOM},
    {"concat", 2u, 0u, str_concat, CETTA_STR_RESULT_ATOM},
    {"join", 2u, 1u << 1, str_join, CETTA_STR_RESULT_ATOM},
    {"split", 2u, 0u, str_split, CETTA_STR_RESULT_STRUCTURE},
    {"split-whitespace", 1u, 0u, str_split_whitespace, CETTA_STR_RESULT_STRUCTURE},
    {"find", 2u, 0u, str_find, CETTA_STR_RESULT_STRUCTURE},
    {"starts-with", 2u, 0u, str_starts_with, CETTA_STR_RESULT_ATOM},
    {"ends-with", 2u, 0u, str_ends_with, CETTA_STR_RESULT_ATOM},
    {"trim", 1u, 0u, str_trim, CETTA_STR_RESULT_ATOM},
    {"compare", 2u, 0u, str_compare, CETTA_STR_RESULT_ATOM},
    {"codepoint-count", 1u, 0u, str_codepoint_count, CETTA_STR_RESULT_ATOM},
    {"codepoints", 1u, 0u, str_codepoints, CETTA_STR_RESULT_STRUCTURE},
    {"codepoint-values", 1u, 0u, str_codepoint_values, CETTA_STR_RESULT_STRUCTURE},
    {"from-codepoints", 1u, 1u << 0, str_from_codepoints, CETTA_STR_RESULT_ATOM},
    {"from-bytes", 1u, 1u << 0, str_from_bytes, CETTA_STR_RESULT_ATOM},
    {"from-codepoints-generalized", 1u, 1u << 0, str_from_codepoints_generalized, CETTA_STR_RESULT_ATOM},
    {"is-all-in", 2u, 0u, str_all_in, CETTA_STR_RESULT_ATOM},
    {"is-initial-in", 2u, 0u, str_initial_in, CETTA_STR_RESULT_ATOM},
    {"is-alphanumeric", 1u, 0u, str_alphanumeric, CETTA_STR_RESULT_ATOM},
    {"is-lower-initial", 1u, 0u, str_lower_initial, CETTA_STR_RESULT_ATOM},
    {"is-upper-initial", 1u, 0u, str_upper_initial, CETTA_STR_RESULT_ATOM},
    {"is-lower-word", 1u, 0u, str_lower_word, CETTA_STR_RESULT_ATOM},
    {"is-upper-word", 1u, 0u, str_upper_word, CETTA_STR_RESULT_ATOM},
    {"string->symbol", 1u, 0u, str_to_symbol, CETTA_STR_RESULT_ATOM},
    {"symbol->string", 1u, 0u, str_from_symbol, CETTA_STR_RESULT_ATOM},
    {"string->number", 1u, 0u, str_to_number, CETTA_STR_RESULT_ATOM},
    {"number->string", 1u, 0u, str_from_number, CETTA_STR_RESULT_ATOM},
    {"set", 1u, 1u << 0, str_set, CETTA_STR_RESULT_STRUCTURE},
    {"is-set-member", 2u, 0u, str_set_member, CETTA_STR_RESULT_ATOM},
    {"escape", 2u, 0u, str_escape, CETTA_STR_RESULT_ATOM},
    {"unescape", 2u, 0u, str_unescape, CETTA_STR_RESULT_ATOM},
    {"sha256", 1u, 0u, str_sha256, CETTA_STR_RESULT_ATOM},
    {"case-map", 2u, 0u, str_case_map, CETTA_STR_RESULT_ATOM},
    {"props-all", 2u, 0u, str_props_all, CETTA_STR_RESULT_ATOM},
    {"props-any", 2u, 0u, str_props_any, CETTA_STR_RESULT_ATOM},
    {"code-props", 1u, 0u, str_code_props, CETTA_STR_RESULT_ATOM},
    {"trim-props", 3u, 0u, str_trim_props, CETTA_STR_RESULT_ATOM},
    {"trim-chars", 3u, 0u, str_trim_chars, CETTA_STR_RESULT_ATOM},
    {"split-props", 3u, 0u, str_split_props, CETTA_STR_RESULT_STRUCTURE},
    {"rsplit-props", 3u, 0u, str_rsplit_props, CETTA_STR_RESULT_STRUCTURE},
    {"find-index", 2u, 0u, str_find_index, CETTA_STR_RESULT_ATOM},
    {"rfind-index", 2u, 0u, str_rfind_index, CETTA_STR_RESULT_ATOM},
};

const CettaStrNative *cetta_str_native(const char *name) {
    if (!name)
        return NULL;
    for (size_t i = 0u; i < sizeof(str_natives) / sizeof(str_natives[0]); i++) {
        if (strcmp(name, str_natives[i].name) == 0)
            return &str_natives[i];
    }
    return NULL;
}
