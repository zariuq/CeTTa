#ifndef CETTA_RAW_SOURCE_REGION_H
#define CETTA_RAW_SOURCE_REGION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Recognition at an already identified token boundary. Offsets are bytes in
 * the original buffer; the body is never unescaped, copied or dedented.
 * A reader must opt in to this lexical extension. */
typedef enum {
    CETTA_RAW_NOT_PRESENT,
    CETTA_RAW_COMPLETE,
    CETTA_RAW_INCOMPLETE,
    CETTA_RAW_INVALID_UTF8
} CettaRawSourceStatus;

typedef struct {
    size_t start;
    size_t body_start;
    size_t body_end;
    size_t end;
    size_t fence_depth;
    size_t error_offset;
} CettaRawSourceRegion;

/* An opener is r, one or more # characters, then ". Once recognized, an
 * unfinished body cannot fall back to an ordinary word. A partial UTF-8
 * scalar at the buffer end is incomplete; a malformed scalar is invalid.
 * An unmatched r or r# remains ordinary host text. */
CettaRawSourceStatus cetta_raw_source_scan(
    const uint8_t *source, size_t length, size_t start,
    CettaRawSourceRegion *region);

/* Choose the smallest collision-free depth at least max(1, preferred).
 * The body must be UTF-8; embedded NUL and CRLF are preserved. */
bool cetta_raw_source_depth(const uint8_t *body, size_t length,
                            size_t preferred, size_t *depth);

/* With output == NULL, return the required byte count. Otherwise capacity
 * must cover that count. No terminating NUL is appended. Failure writes no
 * output. The body and output must not overlap. */
bool cetta_raw_source_render(const uint8_t *body, size_t length,
                             size_t preferred, uint8_t *output,
                             size_t capacity, size_t *written);

typedef enum {
    CETTA_SOURCE_DOCUMENT_COMPLETE,
    CETTA_SOURCE_DOCUMENT_INCOMPLETE,
    CETTA_SOURCE_DOCUMENT_INVALID,
    CETTA_SOURCE_DOCUMENT_ALLOCATION_FAILED
} CettaSourceDocumentStatus;
typedef bool (*CettaRawSourceVisitor)(void *, const CettaRawSourceRegion *,CettaRawSourceStatus);
/* Lexical submission framing and editor discovery. Complete here means all
 * lexical fences/brackets are closed; the active reader still validates
 * syntax. Strings and comments are never searched for nested raw openers. */
CettaSourceDocumentStatus cetta_source_document_scan(const uint8_t *,size_t,
    bool raw_enabled,CettaRawSourceVisitor,void *,size_t *error_offset);

#endif
