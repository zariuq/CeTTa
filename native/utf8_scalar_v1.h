#ifndef CETTA_UTF8_SCALAR_V1_H
#define CETTA_UTF8_SCALAR_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The single UTF-8 scalar encoding used by the language-definition codec.
 * Rejects NUL, surrogates, and values outside the Unicode range, exactly as
 * the codec does. Grows the buffer, keeping it NUL-terminated. */
bool cetta_utf8_append_scalar_v1(uint8_t **bytes, size_t *len, size_t *cap,
                                 int64_t scalar);

/* Header-local implementation also serves standalone codec consumers. */
static inline bool cetta_utf8_append_scalar_impl_v1(
    uint8_t **bytes, size_t *len, size_t *cap, int64_t scalar) {
    uint8_t encoded[4];
    size_t width;
    if (!bytes || !len || !cap || scalar <= 0 || scalar > 0x10ffff ||
        (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
    uint32_t cp = (uint32_t)scalar;
    if (cp <= 0x7f) { encoded[0] = (uint8_t)cp; width = 1u; }
    else if (cp <= 0x7ff) {
        encoded[0] = (uint8_t)(0xc0 | (cp >> 6));
        encoded[1] = (uint8_t)(0x80 | (cp & 63)); width = 2u;
    } else if (cp <= 0xffff) {
        encoded[0] = (uint8_t)(0xe0 | (cp >> 12));
        encoded[1] = (uint8_t)(0x80 | ((cp >> 6) & 63));
        encoded[2] = (uint8_t)(0x80 | (cp & 63)); width = 3u;
    } else {
        encoded[0] = (uint8_t)(0xf0 | (cp >> 18));
        encoded[1] = (uint8_t)(0x80 | ((cp >> 12) & 63));
        encoded[2] = (uint8_t)(0x80 | ((cp >> 6) & 63));
        encoded[3] = (uint8_t)(0x80 | (cp & 63)); width = 4u;
    }
    if (*len > SIZE_MAX - width - 1u) return false;
    if (*len + width + 1u > *cap) {
        size_t next = *cap ? *cap : 32u;
        while (next < *len + width + 1u) {
            if (next > SIZE_MAX / 2u) return false;
            next *= 2u;
        }
        uint8_t *grown = realloc(*bytes, next);
        if (!grown) return false;
        *bytes = grown; *cap = next;
    }
    memcpy(*bytes + *len, encoded, width);
    *len += width; (*bytes)[*len] = 0u;
    return true;
}

#endif
