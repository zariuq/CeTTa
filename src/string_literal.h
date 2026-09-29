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
 * backslash is always escaped.  Tab and carriage return are written as they
 * are in every mode. */
typedef enum {
    CETTA_STRING_LITERAL_ESCAPED,
    CETTA_STRING_LITERAL_RAW,
    CETTA_STRING_LITERAL_RAW_TEXT
} CettaStringLiteralBytes;

typedef void (*CettaStringLiteralEmit)(void *context, const char *bytes,
                                       size_t len);

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
        } else if (c == '"')
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
