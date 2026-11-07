#ifndef CETTA_STRING_LITERAL_H
#define CETTA_STRING_LITERAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "utf8.h"

/* The MeTTa string literal escapes, written as the body between the quotes.
 * Quote and backslash are escaped in every mode, and newline too unless
 * raw_newlines: PeTTa's swrite keeps a string's newlines, and its reader
 * takes them back.
 *
 * ESCAPED (the HE printers) writes every other control byte, DEL and each
 * byte outside a valid UTF-8 sequence as \xhh, which the HE readers read
 * back, as upstream HE does for bytes up to 0x7f.  RAW (the PeTTa and Prime
 * printers) writes those bytes as they are, which those lanes' readers read
 * back, as upstream PeTTa does.  RAW_TEXT is RAW for text held as a C string,
 * which cannot hold NUL: NUL is written \x00, which no other byte is, since a
 * backslash is always escaped.  Tab and carriage return use named escapes in ESCAPED mode and are
 * literal in the raw display modes. */
typedef enum {
    CETTA_STRING_LITERAL_ESCAPED,
    CETTA_STRING_LITERAL_RAW,
    CETTA_STRING_LITERAL_RAW_TEXT
} CettaStringLiteralBytes;

typedef void (*CettaStringLiteralEmit)(void *context, const char *bytes,
                                       size_t len);

static inline int cetta_string_literal_hex(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode one unit of a MeTTa literal body. Unknown/incomplete escapes
 * preserve the backslash and are consumed only one byte at a time. Numeric
 * escapes consume nothing beyond a complete byte or valid Unicode scalar.
 * JSON remains a separate format; its nonconflicting named escapes and
 * UTF-16 scalar spellings are also useful MeTTa input spellings.
 * The output is at most four bytes, never longer than the consumed input. */
static inline size_t cetta_string_literal_decode_unit(
    const uint8_t *text, size_t len, uint8_t output[4], size_t *written) {
    *written = 0u;
    if (!len) return 0u;
    output[0] = text[0];
    *written = 1u;
    if (text[0] != '\\' || len < 2u) return 1u;
    switch (text[1]) {
    case 'n': output[0] = '\n'; return 2u;
    case 'r': output[0] = '\r'; return 2u;
    case 't': output[0] = '\t'; return 2u;
    case 'b': output[0] = '\b'; return 2u;
    case 'f': output[0] = '\f'; return 2u;
    case '"': case '\\': case '/': case '\'':
        output[0] = text[1]; return 2u;
    default: break;
    }
    if (text[1] == 'x' && len >= 4u) {
        int high = cetta_string_literal_hex(text[2]);
        int low = cetta_string_literal_hex(text[3]);
        if (high >= 0 && low >= 0) {
            output[0] = (uint8_t)(16 * high + low);
            return 4u;
        }
    }
    if (text[1] == 'u') {
        uint32_t scalar = 0u;
        size_t end = 2u;
        if (len > 2u && text[2] == '{') {
            /* One or more digits; leading zeros are unlimited. A value
             * beyond the Unicode range is no scalar escape. */
            end = 3u;
            while (end < len) {
                int digit = cetta_string_literal_hex(text[end]);
                if (digit < 0) break;
                scalar = 16u * scalar + (uint32_t)digit;
                if (scalar > 0x10ffffu) return 1u;
                ++end;
            }
            if (end == 3u || end >= len || text[end] != '}') return 1u;
            ++end;
        } else {
            if (len < 6u) return 1u;
            for (; end < 6u; ++end) {
                int digit = cetta_string_literal_hex(text[end]);
                if (digit < 0) return 1u;
                scalar = 16u * scalar + (uint32_t)digit;
            }
            if (scalar >= 0xd800u && scalar < 0xdc00u && len >= 12u &&
                text[6] == '\\' && text[7] == 'u') {
                uint32_t low = 0u;
                for (size_t j = 8u; j < 12u; ++j) {
                    int digit = cetta_string_literal_hex(text[j]);
                    if (digit < 0) return 1u;
                    low = 16u * low + (uint32_t)digit;
                }
                if (low >= 0xdc00u && low < 0xe000u) {
                    scalar = 0x10000u + (scalar - 0xd800u) * 1024u + low - 0xdc00u;
                    end = 12u;
                }
            }
        }
        if (cetta_utf8_scalar_valid(scalar)) {
            *written = cetta_utf8_encode(scalar, output);
            return end;
        }
    }
    return 1u;
}

static inline void cetta_string_literal_escape(const char *text, size_t len,
                                               CettaStringLiteralBytes mode,
                                               bool raw_newlines,
                                               CettaStringLiteralEmit emit,
                                               void *context) {
    bool raw_bytes = mode != CETTA_STRING_LITERAL_ESCAPED;
    const uint8_t *bytes = (const uint8_t *)text;
    size_t i = 0;
    size_t run = 0;
    char hex[5];
    while (i < len) {
        uint8_t c = bytes[i];
        const char *escape = NULL;
        size_t width = 1u;
        if (c == '\n') {
            if (!raw_newlines)
                escape = "\\n";
        } else if (!raw_bytes && c == '\t')
            escape = "\\t";
        else if (!raw_bytes && c == '\r')
            escape = "\\r";
        else if (c == '"')
            escape = "\\\"";
        else if (c == '\\')
            escape = "\\\\";
        else if (c < 0x80u) {
            if ((!raw_bytes && ((c < 0x20u && c != '\t' && c != '\r') ||
                                c == 0x7fu)) ||
                (c == 0u && mode == CETTA_STRING_LITERAL_RAW_TEXT)) {
                snprintf(hex, sizeof hex, "\\x%02x", (unsigned)c);
                escape = hex;
            }
        } else {
            uint32_t scalar = 0;
            uint32_t sequence = 0;
            if (cetta_utf8_decode_one(bytes, len, i, &scalar, &sequence)) {
                width = sequence;
            } else if (!raw_bytes) {
                snprintf(hex, sizeof hex, "\\x%02x", (unsigned)c);
                escape = hex;
            }
        }
        if (escape) {
            if (i > run)
                emit(context, text + run, i - run);
            emit(context, escape, 2u + (escape == hex ? 2u : 0u));
            i += 1u;
            run = i;
        } else {
            i += width;
        }
    }
    if (len > run)
        emit(context, text + run, len - run);
}

#endif
