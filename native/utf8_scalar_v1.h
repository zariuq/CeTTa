#ifndef CETTA_UTF8_SCALAR_V1_H
#define CETTA_UTF8_SCALAR_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The single UTF-8 scalar encoding used by the language-definition codec.
 * Rejects NUL, surrogates, and values outside the Unicode range, exactly as
 * the codec does. Grows the buffer, keeping it NUL-terminated. */
bool cetta_utf8_append_scalar_v1(uint8_t **bytes, size_t *len, size_t *cap,
                                 int64_t scalar);

#endif
