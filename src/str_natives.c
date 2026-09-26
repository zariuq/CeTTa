#include "str_natives.h"

#include "native_sha256.h"

#include "string_ops.h"
#include "utf8.h"

#include <ctype.h>
#include <errno.h>
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
    if (i < len && (text[i] == '-' || text[i] == '+'))
        i++;
    size_t digits_start = i;
    while (i < len && text[i] >= '0' && text[i] <= '9')
        i++;
    char *copy = arena_alloc(a, len + 1u);
    memcpy(copy, text, len);
    copy[len] = '\0';
    if (i > digits_start && i == len) {
        errno = 0;
        char *end = NULL;
        long long value = strtoll(copy, &end, 10);
        if (errno == 0 && end == copy + len)
            return atom_int(a, (int64_t)value);
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

static const CettaStrNative str_natives[] = {
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
    {"compare", 2u, 0u, str_compare, CETTA_STR_RESULT_ATOM},
    {"codepoint-count", 1u, 0u, str_codepoint_count, CETTA_STR_RESULT_ATOM},
    {"codepoints", 1u, 0u, str_codepoints, CETTA_STR_RESULT_STRUCTURE},
    {"from-codepoints", 1u, 1u << 0, str_from_codepoints, CETTA_STR_RESULT_ATOM},
    {"all-in?", 2u, 0u, str_all_in, CETTA_STR_RESULT_ATOM},
    {"initial-in?", 2u, 0u, str_initial_in, CETTA_STR_RESULT_ATOM},
    {"alphanumeric?", 1u, 0u, str_alphanumeric, CETTA_STR_RESULT_ATOM},
    {"lower-initial?", 1u, 0u, str_lower_initial, CETTA_STR_RESULT_ATOM},
    {"upper-initial?", 1u, 0u, str_upper_initial, CETTA_STR_RESULT_ATOM},
    {"lower-word?", 1u, 0u, str_lower_word, CETTA_STR_RESULT_ATOM},
    {"upper-word?", 1u, 0u, str_upper_word, CETTA_STR_RESULT_ATOM},
    {"string->symbol", 1u, 0u, str_to_symbol, CETTA_STR_RESULT_ATOM},
    {"symbol->string", 1u, 0u, str_from_symbol, CETTA_STR_RESULT_ATOM},
    {"string->number", 1u, 0u, str_to_number, CETTA_STR_RESULT_ATOM},
    {"number->string", 1u, 0u, str_from_number, CETTA_STR_RESULT_ATOM},
    {"set", 1u, 1u << 0, str_set, CETTA_STR_RESULT_STRUCTURE},
    {"set-member?", 2u, 0u, str_set_member, CETTA_STR_RESULT_ATOM},
    {"escape", 2u, 0u, str_escape, CETTA_STR_RESULT_ATOM},
    {"unescape", 2u, 0u, str_unescape, CETTA_STR_RESULT_ATOM},
    {"sha256", 1u, 0u, str_sha256, CETTA_STR_RESULT_ATOM},
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
