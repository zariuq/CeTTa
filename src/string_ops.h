#ifndef CETTA_STRING_OPS_H
#define CETTA_STRING_OPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Byte and codepoint operations on strings, which are bytes plus a length.
 * Byte operations take any bytes.  Codepoint operations read strict UTF-8
 * and, on malformed input, report the byte offset of the first invalid
 * sequence instead of guessing.  Nothing here indexes characters in O(1). */

/* A growable byte buffer, NUL-terminated for C interoperation. */
typedef struct {
    char *bytes;
    size_t len;
    size_t cap;
} CettaByteBuffer;

void cetta_byte_buffer_init(CettaByteBuffer *buffer);
bool cetta_byte_buffer_append(CettaByteBuffer *buffer, const char *bytes,
                              size_t len);
bool cetta_byte_buffer_append_byte(CettaByteBuffer *buffer, uint8_t byte);
void cetta_byte_buffer_free(CettaByteBuffer *buffer);

/* The number of scalar values in `text`.  False, with *bad_offset the offset
 * of the first invalid sequence, when `text` is not UTF-8. */
bool cetta_utf8_count(const char *text, size_t len, size_t *count,
                      size_t *bad_offset);

/* The scalar values of `text`, each as the offset and width of its bytes. */
typedef struct {
    size_t offset;
    uint32_t width;
    uint32_t scalar;
} CettaUtf8Unit;

/* The units of valid UTF-8, into an allocated array the caller frees; false
 * with *bad_offset set when `text` is not UTF-8. */
bool cetta_utf8_units(const char *text, size_t len, CettaUtf8Unit **units,
                      size_t *count, size_t *bad_offset);

/* The character classes of the C locale, over scalar values: alpha, digit,
 * alnum, lower, upper, space, blank, punct, print, graph, cntrl and xdigit as
 * POSIX defines them there, word (alnum or '_') and ascii.  Every class is
 * ASCII: a scalar beyond U+007F is in none of them. */
typedef enum {
    CETTA_CHAR_CLASS_ALPHA,
    CETTA_CHAR_CLASS_DIGIT,
    CETTA_CHAR_CLASS_ALNUM,
    CETTA_CHAR_CLASS_LOWER,
    CETTA_CHAR_CLASS_UPPER,
    CETTA_CHAR_CLASS_WORD,
    CETTA_CHAR_CLASS_SPACE,
    CETTA_CHAR_CLASS_BLANK,
    CETTA_CHAR_CLASS_PUNCT,
    CETTA_CHAR_CLASS_PRINT,
    CETTA_CHAR_CLASS_GRAPH,
    CETTA_CHAR_CLASS_CNTRL,
    CETTA_CHAR_CLASS_XDIGIT,
    CETTA_CHAR_CLASS_ASCII
} CettaCharClass;

bool cetta_char_class_from_name(const char *name, CettaCharClass *out);
bool cetta_char_class_holds(CettaCharClass cls, uint32_t scalar);

/* Whether the scalars of `text` are spelled by two classes: the first scalar
 * is in `initial` (so the text is not empty) and every later one is in
 * `rest`.  A NULL class admits every scalar; with `initial` NULL, `rest`
 * constrains every scalar, the first included, and the empty text qualifies.
 * False with *bad_offset set when `text` is not UTF-8. */
bool cetta_utf8_spelled(const char *text, size_t len,
                        const CettaCharClass *initial,
                        const CettaCharClass *rest,
                        bool *holds, size_t *bad_offset);

/* The quoting schemes.  CETTA_QUOTING_METTA is the body of a MeTTa string
 * literal as the HE printer writes it.  CETTA_QUOTING_JSON is the body of a
 * JSON string (RFC 8259).  CETTA_QUOTING_TPTP_SINGLE and _DOUBLE are the
 * bodies of TPTP single-quoted atoms and double-quoted distinct objects, in
 * which a backslash escapes the next character. */
typedef enum {
    CETTA_QUOTING_METTA,
    CETTA_QUOTING_JSON,
    CETTA_QUOTING_TPTP_SINGLE,
    CETTA_QUOTING_TPTP_DOUBLE
} CettaQuotingScheme;

bool cetta_quoting_scheme_from_name(const char *name, CettaQuotingScheme *out);

/* The escaped body of `text` under a scheme, appended to `out`.  JSON and
 * TPTP escape UTF-8 text: false with *bad_offset set when `text` is not
 * UTF-8.  The MeTTa scheme escapes any bytes. */
bool cetta_string_escape(CettaQuotingScheme scheme, const char *text,
                         size_t len, CettaByteBuffer *out, size_t *bad_offset);

/* The text an escaped body denotes, appended to `out`.  False with
 * *bad_offset the offset of the first escape the scheme does not define. */
bool cetta_string_unescape(CettaQuotingScheme scheme, const char *text,
                           size_t len, CettaByteBuffer *out,
                           size_t *bad_offset);

#endif
