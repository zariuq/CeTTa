#include "string_ops.h"

#include "string_literal.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cetta_byte_buffer_init(CettaByteBuffer *buffer) {
    buffer->bytes = NULL;
    buffer->len = 0u;
    buffer->cap = 0u;
}

static bool cetta_byte_buffer_reserve(CettaByteBuffer *buffer, size_t extra) {
    if (extra > SIZE_MAX - buffer->len - 1u)
        return false;
    size_t needed = buffer->len + extra + 1u;
    if (needed <= buffer->cap)
        return true;
    size_t cap = buffer->cap ? buffer->cap : 32u;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u) {
            cap = needed;
            break;
        }
        cap *= 2u;
    }
    char *bytes = realloc(buffer->bytes, cap);
    if (!bytes)
        return false;
    buffer->bytes = bytes;
    buffer->cap = cap;
    return true;
}

bool cetta_byte_buffer_append(CettaByteBuffer *buffer, const char *bytes,
                              size_t len) {
    if (!cetta_byte_buffer_reserve(buffer, len))
        return false;
    if (len)
        memcpy(buffer->bytes + buffer->len, bytes, len);
    buffer->len += len;
    buffer->bytes[buffer->len] = '\0';
    return true;
}

bool cetta_byte_buffer_append_byte(CettaByteBuffer *buffer, uint8_t byte) {
    char c = (char)byte;
    return cetta_byte_buffer_append(buffer, &c, 1u);
}

void cetta_byte_buffer_free(CettaByteBuffer *buffer) {
    free(buffer->bytes);
    cetta_byte_buffer_init(buffer);
}

bool cetta_utf8_count(const char *text, size_t len, size_t *count,
                      size_t *bad_offset) {
    const uint8_t *bytes = (const uint8_t *)text;
    size_t i = 0u;
    size_t n = 0u;
    while (i < len) {
        uint32_t scalar = 0u;
        uint32_t width = 0u;
        if (bytes[i] < 0x80u) {
            i++;
        } else if (cetta_utf8_decode_one(bytes, len, i, &scalar, &width)) {
            i += width;
        } else {
            if (bad_offset)
                *bad_offset = i;
            return false;
        }
        n++;
    }
    if (count)
        *count = n;
    return true;
}

bool cetta_utf8_units(const char *text, size_t len, CettaUtf8Unit **units,
                      size_t *count, size_t *bad_offset) {
    size_t n = 0u;
    if (!cetta_utf8_count(text, len, &n, bad_offset))
        return false;
    CettaUtf8Unit *out = n ? malloc(sizeof(CettaUtf8Unit) * n) : NULL;
    if (n && !out)
        return false;
    const uint8_t *bytes = (const uint8_t *)text;
    size_t i = 0u;
    for (size_t k = 0u; k < n; k++) {
        uint32_t scalar = bytes[i];
        uint32_t width = 1u;
        if (bytes[i] >= 0x80u)
            (void)cetta_utf8_decode_one(bytes, len, i, &scalar, &width);
        out[k].offset = i;
        out[k].width = width;
        out[k].scalar = scalar;
        i += width;
    }
    *units = out;
    *count = n;
    return true;
}

static const struct {
    const char *name;
    CettaCharClass cls;
} char_class_names[] = {
    {"alpha", CETTA_CHAR_CLASS_ALPHA},   {"digit", CETTA_CHAR_CLASS_DIGIT},
    {"alnum", CETTA_CHAR_CLASS_ALNUM},   {"lower", CETTA_CHAR_CLASS_LOWER},
    {"upper", CETTA_CHAR_CLASS_UPPER},   {"word", CETTA_CHAR_CLASS_WORD},
    {"space", CETTA_CHAR_CLASS_SPACE},   {"blank", CETTA_CHAR_CLASS_BLANK},
    {"punct", CETTA_CHAR_CLASS_PUNCT},   {"print", CETTA_CHAR_CLASS_PRINT},
    {"graph", CETTA_CHAR_CLASS_GRAPH},   {"cntrl", CETTA_CHAR_CLASS_CNTRL},
    {"xdigit", CETTA_CHAR_CLASS_XDIGIT}, {"ascii", CETTA_CHAR_CLASS_ASCII},
};

bool cetta_char_class_from_name(const char *name, CettaCharClass *out) {
    if (!name || !out)
        return false;
    for (size_t i = 0u; i < sizeof(char_class_names) / sizeof(char_class_names[0]); i++) {
        if (strcmp(name, char_class_names[i].name) == 0) {
            *out = char_class_names[i].cls;
            return true;
        }
    }
    return false;
}

bool cetta_char_class_holds(CettaCharClass cls, uint32_t c) {
    bool lower = c >= 'a' && c <= 'z';
    bool upper = c >= 'A' && c <= 'Z';
    bool digit = c >= '0' && c <= '9';
    bool graph = c >= 0x21u && c <= 0x7eu;
    switch (cls) {
    case CETTA_CHAR_CLASS_ALPHA: return lower || upper;
    case CETTA_CHAR_CLASS_DIGIT: return digit;
    case CETTA_CHAR_CLASS_ALNUM: return lower || upper || digit;
    case CETTA_CHAR_CLASS_LOWER: return lower;
    case CETTA_CHAR_CLASS_UPPER: return upper;
    case CETTA_CHAR_CLASS_WORD: return lower || upper || digit || c == '_';
    case CETTA_CHAR_CLASS_SPACE: return c == ' ' || (c >= '\t' && c <= '\r');
    case CETTA_CHAR_CLASS_BLANK: return c == ' ' || c == '\t';
    case CETTA_CHAR_CLASS_PUNCT: return graph && !(lower || upper || digit);
    case CETTA_CHAR_CLASS_PRINT: return c == ' ' || graph;
    case CETTA_CHAR_CLASS_GRAPH: return graph;
    case CETTA_CHAR_CLASS_CNTRL: return c < 0x20u || c == 0x7fu;
    case CETTA_CHAR_CLASS_XDIGIT:
        return digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    case CETTA_CHAR_CLASS_ASCII: return c <= 0x7fu;
    }
    return false;
}

bool cetta_utf8_spelled(const char *text, size_t len,
                        const CettaCharClass *initial,
                        const CettaCharClass *rest,
                        bool *holds, size_t *bad_offset) {
    const uint8_t *bytes = (const uint8_t *)text;
    bool spelled = !initial || len > 0u;
    size_t i = 0u;
    /* Every scalar is decoded, so malformed text is refused even when an
     * earlier scalar has already decided the answer. */
    while (i < len) {
        uint32_t scalar = bytes[i];
        uint32_t width = 1u;
        if (bytes[i] >= 0x80u &&
            !cetta_utf8_decode_one(bytes, len, i, &scalar, &width)) {
            if (bad_offset)
                *bad_offset = i;
            return false;
        }
        const CettaCharClass *cls = i == 0u && initial ? initial : rest;
        spelled = spelled && (!cls || cetta_char_class_holds(*cls, scalar));
        i += width;
    }
    *holds = spelled;
    return true;
}

bool cetta_quoting_scheme_from_name(const char *name, CettaQuotingScheme *out) {
    if (!name || !out)
        return false;
    if (strcmp(name, "metta") == 0)
        *out = CETTA_QUOTING_METTA;
    else if (strcmp(name, "json") == 0)
        *out = CETTA_QUOTING_JSON;
    else if (strcmp(name, "tptp-single") == 0)
        *out = CETTA_QUOTING_TPTP_SINGLE;
    else if (strcmp(name, "tptp-double") == 0)
        *out = CETTA_QUOTING_TPTP_DOUBLE;
    else
        return false;
    return true;
}

typedef struct {
    CettaByteBuffer *out;
    bool ok;
} EscapeSink;

static void escape_sink_emit(void *context, const char *bytes, size_t len) {
    EscapeSink *sink = context;
    if (sink->ok && !cetta_byte_buffer_append(sink->out, bytes, len))
        sink->ok = false;
}

static bool escape_json(const char *text, size_t len, CettaByteBuffer *out,
                        size_t *bad_offset) {
    const uint8_t *bytes = (const uint8_t *)text;
    size_t i = 0u;
    while (i < len) {
        uint8_t c = bytes[i];
        uint32_t scalar = c;
        uint32_t width = 1u;
        if (c >= 0x80u &&
            !cetta_utf8_decode_one(bytes, len, i, &scalar, &width)) {
            if (bad_offset)
                *bad_offset = i;
            return false;
        }
        const char *escape = NULL;
        char hex[7];
        switch (scalar) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default:
            if (scalar < 0x20u) {
                snprintf(hex, sizeof hex, "\\u%04x", (unsigned)scalar);
                escape = hex;
            }
            break;
        }
        if (escape ? !cetta_byte_buffer_append(out, escape, strlen(escape))
                   : !cetta_byte_buffer_append(out, text + i, width))
            return false;
        i += width;
    }
    return true;
}

/* TPTP quoted atoms and distinct objects hold printable ASCII only; the quote
 * and the backslash are escaped by a backslash. */
static bool escape_tptp(const char *text, size_t len, uint8_t quote,
                        CettaByteBuffer *out, size_t *bad_offset) {
    for (size_t i = 0u; i < len; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c < 0x20u || c > 0x7eu) {
            if (bad_offset)
                *bad_offset = i;
            return false;
        }
        if ((c == quote || c == '\\') &&
            !cetta_byte_buffer_append_byte(out, '\\'))
            return false;
        if (!cetta_byte_buffer_append_byte(out, c))
            return false;
    }
    return true;
}

bool cetta_string_escape(CettaQuotingScheme scheme, const char *text,
                         size_t len, CettaByteBuffer *out, size_t *bad_offset) {
    switch (scheme) {
    case CETTA_QUOTING_METTA: {
        EscapeSink sink = {out, true};
        cetta_string_literal_escape(text, len, CETTA_STRING_LITERAL_ESCAPED, false,
                                    escape_sink_emit, &sink);
        return sink.ok;
    }
    case CETTA_QUOTING_JSON:
        return escape_json(text, len, out, bad_offset);
    case CETTA_QUOTING_TPTP_SINGLE:
        return escape_tptp(text, len, '\'', out, bad_offset);
    case CETTA_QUOTING_TPTP_DOUBLE:
        return escape_tptp(text, len, '"', out, bad_offset);
    }
    return false;
}

static int hex_digit(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool append_scalar(CettaByteBuffer *out, uint32_t scalar) {
    uint8_t encoded[4];
    size_t width = cetta_utf8_encode(scalar, encoded);
    return cetta_byte_buffer_append(out, (const char *)encoded, width);
}

static bool unescape_metta(const char *text, size_t len, CettaByteBuffer *out,
                           size_t *bad_offset) {
    const uint8_t *bytes = (const uint8_t *)text;
    size_t i = 0u;
    while (i < len) {
        if (bytes[i] != '\\') {
            if (!cetta_byte_buffer_append_byte(out, bytes[i]))
                return false;
            i++;
            continue;
        }
        size_t at = i;
        if (i + 1u >= len)
            goto bad;
        uint8_t kind = bytes[i + 1u];
        uint8_t simple = 0u;
        switch (kind) {
        case 'n': simple = '\n'; break;
        case 'r': simple = '\r'; break;
        case 't': simple = '\t'; break;
        case '"': simple = '"'; break;
        case '\'': simple = '\''; break;
        case '\\': simple = '\\'; break;
        default: break;
        }
        if (simple) {
            if (!cetta_byte_buffer_append_byte(out, simple))
                return false;
            i += 2u;
            continue;
        }
        if (kind == 'x') {
            int high = i + 2u < len ? hex_digit(bytes[i + 2u]) : -1;
            int low = i + 3u < len ? hex_digit(bytes[i + 3u]) : -1;
            if (high < 0 || low < 0)
                goto bad;
            if (!cetta_byte_buffer_append_byte(out, (uint8_t)((high << 4) | low)))
                return false;
            i += 4u;
            continue;
        }
        if (kind == 'u' && i + 2u < len && bytes[i + 2u] == '{') {
            size_t j = i + 3u;
            uint32_t scalar = 0u;
            size_t digits = 0u;
            while (j < len && digits < 6u && hex_digit(bytes[j]) >= 0) {
                scalar = (scalar << 4) | (uint32_t)hex_digit(bytes[j]);
                digits++;
                j++;
            }
            if (digits == 0u || j >= len || bytes[j] != '}' ||
                !cetta_utf8_scalar_valid(scalar))
                goto bad;
            if (!append_scalar(out, scalar))
                return false;
            i = j + 1u;
            continue;
        }
    bad:
        if (bad_offset)
            *bad_offset = at;
        return false;
    }
    return true;
}

static bool unescape_json(const char *text, size_t len, CettaByteBuffer *out,
                          size_t *bad_offset) {
    const uint8_t *bytes = (const uint8_t *)text;
    size_t i = 0u;
    while (i < len) {
        size_t at = i;
        uint8_t c = bytes[i];
        if (c < 0x20u)
            goto bad;
        if (c != '\\') {
            uint32_t scalar = c;
            uint32_t width = 1u;
            if (c >= 0x80u &&
                !cetta_utf8_decode_one(bytes, len, i, &scalar, &width))
                goto bad;
            if (!cetta_byte_buffer_append(out, text + i, width))
                return false;
            i += width;
            continue;
        }
        if (i + 1u >= len)
            goto bad;
        uint8_t kind = bytes[i + 1u];
        uint8_t simple = 0u;
        switch (kind) {
        case '"': simple = '"'; break;
        case '\\': simple = '\\'; break;
        case '/': simple = '/'; break;
        case 'b': simple = '\b'; break;
        case 'f': simple = '\f'; break;
        case 'n': simple = '\n'; break;
        case 'r': simple = '\r'; break;
        case 't': simple = '\t'; break;
        default: break;
        }
        if (simple) {
            if (!cetta_byte_buffer_append_byte(out, simple))
                return false;
            i += 2u;
            continue;
        }
        if (kind != 'u' || i + 6u > len)
            goto bad;
        uint32_t unit = 0u;
        for (size_t k = 0u; k < 4u; k++) {
            int digit = hex_digit(bytes[i + 2u + k]);
            if (digit < 0)
                goto bad;
            unit = (unit << 4) | (uint32_t)digit;
        }
        i += 6u;
        if (unit >= 0xd800u && unit <= 0xdbffu) {
            /* A high surrogate must be followed by its low half. */
            uint32_t low = 0u;
            if (i + 6u > len || bytes[i] != '\\' || bytes[i + 1u] != 'u')
                goto bad;
            for (size_t k = 0u; k < 4u; k++) {
                int digit = hex_digit(bytes[i + 2u + k]);
                if (digit < 0)
                    goto bad;
                low = (low << 4) | (uint32_t)digit;
            }
            if (low < 0xdc00u || low > 0xdfffu)
                goto bad;
            unit = 0x10000u + ((unit - 0xd800u) << 10) + (low - 0xdc00u);
            i += 6u;
        } else if (unit >= 0xdc00u && unit <= 0xdfffu) {
            goto bad;
        }
        if (!append_scalar(out, unit))
            return false;
        continue;
    bad:
        if (bad_offset)
            *bad_offset = at;
        return false;
    }
    return true;
}

/* In TPTP quoted text a backslash stands before the character it escapes. */
static bool unescape_tptp(const char *text, size_t len, CettaByteBuffer *out,
                          size_t *bad_offset) {
    for (size_t i = 0u; i < len; i++) {
        if (text[i] == '\\') {
            if (i + 1u >= len) {
                if (bad_offset)
                    *bad_offset = i;
                return false;
            }
            i++;
        }
        if (!cetta_byte_buffer_append_byte(out, (uint8_t)text[i]))
            return false;
    }
    return true;
}

bool cetta_string_unescape(CettaQuotingScheme scheme, const char *text,
                           size_t len, CettaByteBuffer *out,
                           size_t *bad_offset) {
    switch (scheme) {
    case CETTA_QUOTING_METTA:
        return unescape_metta(text, len, out, bad_offset);
    case CETTA_QUOTING_JSON:
        return unescape_json(text, len, out, bad_offset);
    case CETTA_QUOTING_TPTP_SINGLE:
    case CETTA_QUOTING_TPTP_DOUBLE:
        return unescape_tptp(text, len, out, bad_offset);
    }
    return false;
}
