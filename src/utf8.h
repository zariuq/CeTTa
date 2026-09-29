#ifndef CETTA_UTF8_H
#define CETTA_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Strict UTF-8: a scalar value is at most U+10FFFF and never a surrogate, and
 * a sequence is the shortest encoding of its scalar.  NUL is a scalar. */

static inline bool cetta_utf8_scalar_valid(uint32_t scalar) {
    return scalar <= UINT32_C(0x10ffff) &&
        !(scalar >= UINT32_C(0xd800) && scalar <= UINT32_C(0xdfff));
}

static inline bool cetta_utf8_decode_one(const uint8_t *bytes,
                                         size_t len,
                                         size_t pos,
                                         uint32_t *scalar,
                                         uint32_t *width) {
    uint8_t first;

    if (!bytes || !scalar || !width || pos >= len)
        return false;
    first = bytes[pos];
    if (first <= UINT8_C(0x7f)) {
        *scalar = first;
        *width = 1u;
        return true;
    }
    if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
        uint8_t second;
        if (pos + 1u >= len)
            return false;
        second = bytes[pos + 1u];
        if ((second & UINT8_C(0xc0)) != UINT8_C(0x80))
            return false;
        *scalar = ((uint32_t)(first & UINT8_C(0x1f)) << 6) |
            (uint32_t)(second & UINT8_C(0x3f));
        *width = 2u;
        return true;
    }
    if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
        uint8_t second;
        uint8_t third;
        if (pos + 2u >= len)
            return false;
        second = bytes[pos + 1u];
        third = bytes[pos + 2u];
        if ((third & UINT8_C(0xc0)) != UINT8_C(0x80) ||
            (first == UINT8_C(0xe0) &&
             (second < UINT8_C(0xa0) || second > UINT8_C(0xbf))) ||
            (first == UINT8_C(0xed) &&
             (second < UINT8_C(0x80) || second > UINT8_C(0x9f))) ||
            ((first != UINT8_C(0xe0) && first != UINT8_C(0xed)) &&
             (second & UINT8_C(0xc0)) != UINT8_C(0x80))) {
            return false;
        }
        *scalar = ((uint32_t)(first & UINT8_C(0x0f)) << 12) |
            ((uint32_t)(second & UINT8_C(0x3f)) << 6) |
            (uint32_t)(third & UINT8_C(0x3f));
        *width = 3u;
        return cetta_utf8_scalar_valid(*scalar);
    }
    if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
        uint8_t second;
        uint8_t third;
        uint8_t fourth;
        if (pos + 3u >= len)
            return false;
        second = bytes[pos + 1u];
        third = bytes[pos + 2u];
        fourth = bytes[pos + 3u];
        if ((third & UINT8_C(0xc0)) != UINT8_C(0x80) ||
            (fourth & UINT8_C(0xc0)) != UINT8_C(0x80) ||
            (first == UINT8_C(0xf0) &&
             (second < UINT8_C(0x90) || second > UINT8_C(0xbf))) ||
            (first == UINT8_C(0xf4) &&
             (second < UINT8_C(0x80) || second > UINT8_C(0x8f))) ||
            ((first != UINT8_C(0xf0) && first != UINT8_C(0xf4)) &&
             (second & UINT8_C(0xc0)) != UINT8_C(0x80))) {
            return false;
        }
        *scalar = ((uint32_t)(first & UINT8_C(0x07)) << 18) |
            ((uint32_t)(second & UINT8_C(0x3f)) << 12) |
            ((uint32_t)(third & UINT8_C(0x3f)) << 6) |
            (uint32_t)(fourth & UINT8_C(0x3f));
        *width = 4u;
        return cetta_utf8_scalar_valid(*scalar);
    }
    return false;
}

/* The UTF-8 encoding of a scalar value, NUL included, into four bytes. */
static inline size_t cetta_utf8_encode(uint32_t scalar, uint8_t out[4]) {
    if (scalar < 0x80u) {
        out[0] = (uint8_t)scalar;
        return 1u;
    }
    if (scalar < 0x800u) {
        out[0] = (uint8_t)(0xc0u | (scalar >> 6));
        out[1] = (uint8_t)(0x80u | (scalar & 0x3fu));
        return 2u;
    }
    if (scalar < 0x10000u) {
        out[0] = (uint8_t)(0xe0u | (scalar >> 12));
        out[1] = (uint8_t)(0x80u | ((scalar >> 6) & 0x3fu));
        out[2] = (uint8_t)(0x80u | (scalar & 0x3fu));
        return 3u;
    }
    out[0] = (uint8_t)(0xf0u | (scalar >> 18));
    out[1] = (uint8_t)(0x80u | ((scalar >> 12) & 0x3fu));
    out[2] = (uint8_t)(0x80u | ((scalar >> 6) & 0x3fu));
    out[3] = (uint8_t)(0x80u | (scalar & 0x3fu));
    return 4u;
}

#endif
