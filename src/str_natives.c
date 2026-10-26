#define _GNU_SOURCE
#include "str_natives.h"

#include "native_sha256.h"

#include "string_ops.h"
#include "unicode_category15.h"
#include "python_lower15.h"
#include "str_stream.h"
#include "utf8.h"

#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
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

static pthread_once_t str_fixed_locale_once = PTHREAD_ONCE_INIT;
static locale_t str_fixed_locale;

static void str_fixed_locale_init(void) {
    str_fixed_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
}

/* Python's '%.*f' % (digits, value). printf in the C locale rounds the exact
 * binary value correctly, as Python's own conversion does; Python spells NaN
 * without a sign. */
static Atom *str_format_fixed(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    Atom *number = args[0], *count = args[1];
    double value;
    if (number->kind == ATOM_GROUNDED && number->ground.gkind == GV_FLOAT)
        value = number->ground.fval;
    else if (number->kind == ATOM_GROUNDED && number->ground.gkind == GV_INT)
        value = (double)number->ground.ival;
    else
        return str_expects(r, "expected a float or integer and a digit count");
    if (count->kind != ATOM_GROUNDED || count->ground.gkind != GV_INT ||
        count->ground.ival < 0 || count->ground.ival > INT_MAX)
        return str_expects(r, "expected a float or integer and a digit count");
    int digits = (int)count->ground.ival;
    if (isnan(value)) return atom_string(a, "nan");
    if (isinf(value)) return atom_string(a, value < 0 ? "-inf" : "inf");
    /* The shortest text '%.*f' can make has the sign of a negative value
     * (negative zero included), one integer digit, and the point with every
     * fraction digit. When even that cannot fit a string, refuse before the
     * length probe, which would generate all the digits first. */
    uint64_t shortest = (signbit(value) ? 1u : 0u) + 1u + (digits ? 1u + (uint64_t)digits : 0u);
    if (shortest > CETTA_STRING_BYTES_MAX)
        return str_expects(r, "expected a digit count whose text fits a string");
    pthread_once(&str_fixed_locale_once, str_fixed_locale_init);
    if (!str_fixed_locale) return str_expects(r, "expected a C numeric locale");
    locale_t previous = uselocale(str_fixed_locale);
    int length = snprintf(NULL, 0, "%.*f", digits, value);
    char *text = length >= 0 && (uint64_t)length <= CETTA_STRING_BYTES_MAX
        ? malloc((size_t)length + 1u) : NULL;
    if (text) snprintf(text, (size_t)length + 1u, "%.*f", digits, value);
    (void)uselocale(previous);
    if (!text) return str_expects(r, "expected a digit count whose text fits a string");
    Atom *result = atom_string_n(a, text, (size_t)length);
    free(text);
    return result;
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

/* The first malformed UTF-8 sequence of a text, as decoders that report
 * maximal subparts locate it: the offset where the sequence starts, the end of
 * its longest valid prefix, the byte at the start, and why it stops (an
 * invalid start byte, an invalid continuation byte, or the end of the data).
 * Overlong forms, surrogates and values above U+10FFFF stop at their second
 * byte. Well-formed text gives StrUtf8WellFormedV1. */
static Atom *str_utf8_error_at(Arena *a, size_t start, size_t end, uint8_t byte,
                               const char *reason) {
    Atom *items[5] = {atom_symbol(a, "StrUtf8ErrorV1"), atom_int(a, (int64_t)start),
                      atom_int(a, (int64_t)end), atom_int(a, (int64_t)byte),
                      atom_symbol(a, reason)};
    return atom_expr(a, items, 5u);
}

static Atom *str_utf8_error(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    const uint8_t *bytes = (const uint8_t *)text;
    size_t at = 0u;
    while (at < len) {
        uint8_t lead = bytes[at];
        if (lead < 0x80u) { at++; continue; }
        size_t need;
        uint8_t low = 0x80u, high = 0xbfu;
        if (lead >= 0xc2u && lead <= 0xdfu) need = 1u;
        else if (lead == 0xe0u) { need = 2u; low = 0xa0u; }
        else if ((lead >= 0xe1u && lead <= 0xecu) || lead == 0xeeu || lead == 0xefu) need = 2u;
        else if (lead == 0xedu) { need = 2u; high = 0x9fu; }
        else if (lead == 0xf0u) { need = 3u; low = 0x90u; }
        else if (lead >= 0xf1u && lead <= 0xf3u) need = 3u;
        else if (lead == 0xf4u) { need = 3u; high = 0x8fu; }
        else return str_utf8_error_at(a, at, at + 1u, lead, "invalid-start-byte");
        size_t next = at + 1u;
        for (size_t k = 0u; k < need; k++, next++) {
            if (next >= len)
                return str_utf8_error_at(a, at, next, lead, "unexpected-end-of-data");
            uint8_t follow = bytes[next];
            bool valid = k == 0u ? follow >= low && follow <= high
                                 : follow >= 0x80u && follow <= 0xbfu;
            if (!valid)
                return str_utf8_error_at(a, at, next, lead, "invalid-continuation-byte");
        }
        at = next;
    }
    return atom_symbol(a, "StrUtf8WellFormedV1");
}

/* The Unicode 15.0.0 General_Category of a code point, given as an integer
 * or as a text of one scalar, spelled as Python 3.12's unicodedata.category
 * spells it: "Lu", "Nd", "Cn". Surrogates, which no text holds, are Cs. */
static Atom *str_unicode_category(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len;
    uint32_t code, width;
    if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_INT) {
        if (args[0]->ground.ival < 0 || args[0]->ground.ival > 0x10ffff)
            return str_expects(r, "expected a code point from 0 to 1114111");
        code = (uint32_t)args[0]->ground.ival;
    } else if (!str_text(args[0], &text, &len) ||
               !cetta_utf8_decode_one((const uint8_t *)text, len, 0u, &code, &width) ||
               width != len) {
        return str_expects(r, "expected a code point or a text of one scalar");
    }
    size_t low = 0u, high = CETTA_UNICODE_CATEGORY15_RUNS;
    while (high - low > 1u) {
        size_t middle = low + (high - low) / 2u;
        if (cetta_unicode_category15_starts[middle] <= code) low = middle;
        else high = middle;
    }
    return atom_string(a, cetta_unicode_category15_names[cetta_unicode_category15_values[low]]);
}

/* The code points of a Python text, as integers. Its bytes are UTF-8
 * extended as the surrogatepass error handler extends it, so a lone
 * surrogate is one code point, as len() counts it; json:parse-python writes
 * strings that way. Other malformed bytes are refused. */
static Atom *str_python_codepoints(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len, count = 0u;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    for (size_t at = 0u; at < len; count++) {
        uint32_t code, width;
        if (!cetta_utf8_decode_surrogatepass((const uint8_t *)text, len, at, &code, &width))
            return str_refuses(r, "StrMalformedUtf8V1", at);
        at += width;
    }
    Atom **items = malloc((count ? count : 1u) * sizeof(*items));
    if (!items)
        return str_expects(r, "memory for the code points");
    size_t index = 0u;
    for (size_t at = 0u; at < len; index++) {
        uint32_t code = 0u, width = 1u;
        (void)cetta_utf8_decode_surrogatepass((const uint8_t *)text, len, at, &code, &width);
        items[index] = atom_int(a, (int64_t)code);
        at += width;
    }
    Atom *result = atom_expr(a, items, count);
    free(items);
    return result;
}

/* The Python text of a list of code points, surrogates included, written as
 * UTF-8 extended by surrogatepass. */
static Atom *str_from_python_codepoints(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    Atom *const *items = NULL;
    CettaExprLen count = 0u;
    if (!atom_sequence_view(args[0], &items, &count))
        return str_expects(r, "expected a list of codepoints");
    CettaByteBuffer out;
    cetta_byte_buffer_init(&out);
    for (CettaExprLen k = 0u; k < count; k++) {
        Atom *item = items[k];
        if (!item || item->kind != ATOM_GROUNDED || item->ground.gkind != GV_INT ||
            item->ground.ival < 0 || item->ground.ival > 0x10ffff) {
            cetta_byte_buffer_free(&out);
            return str_refuses(r, "StrNotACodepointV1", (size_t)k);
        }
        uint8_t encoded[4];
        size_t width = cetta_utf8_encode((uint32_t)item->ground.ival, encoded);
        str_append(&out, (const char *)encoded, width);
    }
    return str_from_buffer(a, &out);
}

static bool lower15_has(const uint32_t *starts, size_t count, uint32_t code) {
    size_t low = 0u, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (starts[middle] <= code) low = middle + 1u;
        else high = middle;
    }
    return low % 2u == 1u;
}

/* The full lowercase mapping of a code point other than U+03A3: its width,
 * and its code points in out (the code point itself when it has none). */
static uint8_t lower15_map(uint32_t code, uint32_t out[CETTA_PYTHON_LOWER15_WIDTH]) {
    size_t low = 0u, high = CETTA_PYTHON_LOWER15_MAPPINGS;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (cetta_python_lower15_from[middle] < code) low = middle + 1u;
        else high = middle;
    }
    if (low < CETTA_PYTHON_LOWER15_MAPPINGS && cetta_python_lower15_from[low] == code) {
        uint8_t width = cetta_python_lower15_widths[low];
        for (uint8_t k = 0u; k < width; k++) out[k] = cetta_python_lower15_to[low][k];
        return width;
    }
    out[0] = code;
    return 1u;
}

/* U+03A3 lowers to final sigma U+03C2 when a cased code point precedes it and
 * none follows it, case-ignorable code points between them skipped; otherwise
 * to U+03C3. This is CPython's handle_capital_sigma. */
static uint32_t lower15_sigma(const uint32_t *codes, size_t count, size_t at) {
    size_t j = at;
    while (j > 0u && lower15_has(cetta_python_lower15_ignorable_starts,
                                 CETTA_PYTHON_LOWER15_IGNORABLE_STARTS, codes[j - 1u]))
        j--;
    bool final = j > 0u && lower15_has(cetta_python_lower15_cased_starts,
                                       CETTA_PYTHON_LOWER15_CASED_STARTS, codes[j - 1u]);
    if (final) {
        j = at + 1u;
        while (j < count && lower15_has(cetta_python_lower15_ignorable_starts,
                                        CETTA_PYTHON_LOWER15_IGNORABLE_STARTS, codes[j]))
            j++;
        final = j == count || !lower15_has(cetta_python_lower15_cased_starts,
                                           CETTA_PYTHON_LOWER15_CASED_STARTS, codes[j]);
    }
    return final ? 0x03C2u : 0x03C3u;
}

/* CPython 3.12's str.lower of a Python text (UTF-8 extended by surrogatepass,
 * as str:python-codepoints reads it): full lowercase mappings over Unicode
 * 15.0.0, one code point to several included, and the Final_Sigma context of
 * U+03A3. Lone surrogates are kept. Not casefold. The answer's length is
 * known before it is written, and one that cannot fit a string is refused. */
static Atom *str_python_lower(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len, count = 0u;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    uint32_t *codes = malloc((len ? len : 1u) * sizeof(*codes));
    if (!codes)
        return str_expects(r, "memory for the code points");
    for (size_t at = 0u; at < len; count++) {
        uint32_t width;
        if (!cetta_utf8_decode_surrogatepass((const uint8_t *)text, len, at, &codes[count], &width)) {
            free(codes);
            return str_refuses(r, "StrMalformedUtf8V1", at);
        }
        at += width;
    }
    uint64_t total = 0u;
    uint32_t mapped[CETTA_PYTHON_LOWER15_WIDTH];
    uint8_t encoded[4];
    for (size_t k = 0u; k < count; k++) {
        uint8_t width = codes[k] == 0x03A3u ? (mapped[0] = lower15_sigma(codes, count, k), 1u)
                                             : lower15_map(codes[k], mapped);
        for (uint8_t m = 0u; m < width; m++) total += cetta_utf8_encode(mapped[m], encoded);
    }
    if (total > CETTA_STRING_BYTES_MAX) {
        free(codes);
        return str_expects(r, "expected a text whose lowercase fits a string");
    }
    char *out = malloc((size_t)total + 1u);
    if (!out) {
        free(codes);
        return str_expects(r, "memory for the lowercase text");
    }
    size_t written = 0u;
    for (size_t k = 0u; k < count; k++) {
        uint8_t width = codes[k] == 0x03A3u ? (mapped[0] = lower15_sigma(codes, count, k), 1u)
                                             : lower15_map(codes[k], mapped);
        for (uint8_t m = 0u; m < width; m++) {
            size_t bytes = cetta_utf8_encode(mapped[m], encoded);
            memcpy(out + written, encoded, bytes);
            written += bytes;
        }
    }
    free(codes);
    Atom *result = atom_string_n(a, out, written);
    free(out);
    return result;
}

static Atom *str_unicode_version(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    (void)args;
    (void)r;
    return atom_string(a, cetta_unicode_category15_version);
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
    return atom_symbol_data(a, text);
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

/* ASCII letters in lower case; every other byte unchanged. */
static Atom *str_lower(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    char *out = arena_alloc(a, len + 1u);
    for (size_t i = 0u; i < len; i++)
        out[i] = text[i] >= 'A' && text[i] <= 'Z' ? (char)(text[i] + 32) : text[i];
    out[len] = '\0';
    return atom_string_n(a, out, len);
}

/* The width of the character at text[i]: a well-formed UTF-8 sequence, or one
 * byte for a byte that does not begin one.  Unlike the codepoint operations,
 * the char operations take any bytes, so a cut never fails on stray input. */
static size_t str_char_width(const char *text, size_t len, size_t i) {
    unsigned char c = (unsigned char)text[i];
    size_t width = c < 0x80u ? 1u : (c >> 5) == 6u ? 2u
        : (c >> 4) == 14u ? 3u : (c >> 3) == 30u ? 4u : 1u;
    for (size_t k = 1u; k < width; k++)
        if (i + k >= len || (((unsigned char)text[i + k]) & 0xc0u) != 0x80u)
            return 1u;
    return width;
}

static Atom *str_char_length(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len, count = 0u;
    if (!str_text(args[0], &text, &len))
        return str_expects(r, "expected text argument");
    for (size_t i = 0u; i < len; i += str_char_width(text, len, i))
        count++;
    return atom_int(a, (int64_t)count);
}

/* Characters start up to end; offsets past the text are clamped to it. */
static Atom *str_char_slice(Arena *a, Atom *const *args, CettaStrRefusal *r) {
    const char *text;
    size_t len, start, stop, count = 0u, from = 0u, to = 0u, i = 0u;
    if (!str_text(args[0], &text, &len) || !str_offset(args[1], &start) ||
        !str_offset(args[2], &stop))
        return str_expects(r, "expected text, non-negative start, non-negative end");
    if (stop < start)
        stop = start;
    from = to = len;
    for (;;) {
        if (count == start && from == len)
            from = i;
        if (count == stop) {
            to = i;
            break;
        }
        if (i >= len)
            break;
        i += str_char_width(text, len, i);
        count++;
    }
    if (from > to)
        from = to;
    return atom_string_n(a, text + from, to - from);
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
    {"concat", 2u, 0u, str_concat, CETTA_STR_RESULT_ATOM},
    {"join", 2u, 1u << 1, str_join, CETTA_STR_RESULT_ATOM},
    {"split", 2u, 0u, str_split, CETTA_STR_RESULT_STRUCTURE},
    {"split-whitespace", 1u, 0u, str_split_whitespace, CETTA_STR_RESULT_STRUCTURE},
    {"find", 2u, 0u, str_find, CETTA_STR_RESULT_STRUCTURE},
    {"starts-with", 2u, 0u, str_starts_with, CETTA_STR_RESULT_ATOM},
    {"ends-with", 2u, 0u, str_ends_with, CETTA_STR_RESULT_ATOM},
    {"trim", 1u, 0u, str_trim, CETTA_STR_RESULT_ATOM},
    {"format-fixed", 2u, 0u, str_format_fixed, CETTA_STR_RESULT_ATOM},
    {"compare", 2u, 0u, str_compare, CETTA_STR_RESULT_ATOM},
    {"codepoint-count", 1u, 0u, str_codepoint_count, CETTA_STR_RESULT_ATOM},
    {"utf8-error", 1u, 0u, str_utf8_error, CETTA_STR_RESULT_STRUCTURE},
    {"unicode-category", 1u, 0u, str_unicode_category, CETTA_STR_RESULT_ATOM},
    {"unicode-version", 0u, 0u, str_unicode_version, CETTA_STR_RESULT_ATOM},
    {"python-codepoints", 1u, 0u, str_python_codepoints, CETTA_STR_RESULT_STRUCTURE},
    {"from-python-codepoints", 1u, 1u << 0, str_from_python_codepoints, CETTA_STR_RESULT_ATOM},
    {"python-lower", 1u, 0u, str_python_lower, CETTA_STR_RESULT_ATOM},
    {"codepoints", 1u, 0u, str_codepoints, CETTA_STR_RESULT_STRUCTURE},
    {"from-codepoints", 1u, 1u << 0, str_from_codepoints, CETTA_STR_RESULT_ATOM},
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
    {"lower", 1u, 0u, str_lower, CETTA_STR_RESULT_ATOM},
    {"char-length", 1u, 0u, str_char_length, CETTA_STR_RESULT_ATOM},
    {"char-slice", 3u, 0u, str_char_slice, CETTA_STR_RESULT_ATOM},
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
