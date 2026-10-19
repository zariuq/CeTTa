#ifndef CETTA_PREPARED_BYTES_V1_H
#define CETTA_PREPARED_BYTES_V1_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Small length-delimited, little-endian codec shared by prepared programs.
 * No native pointers, struct padding, symbol IDs or host byte order escape. */
typedef struct { uint8_t *data; size_t len, capacity; bool failed; } CettaPreparedWriterV1;
typedef struct { const uint8_t *data; size_t len, at; bool failed; } CettaPreparedReaderV1;

static inline void cpw_bytes(CettaPreparedWriterV1 *w, const void *data, size_t n) {
    if (w->failed || n > SIZE_MAX - w->len) { w->failed = true; return; }
    size_t needed = w->len + n;
    if (needed > w->capacity) {
        size_t cap = w->capacity ? w->capacity : 256u;
        while (cap < needed) { if (cap > SIZE_MAX / 2u) { cap = needed; break; } cap *= 2u; }
        void *p = realloc(w->data, cap);
        if (!p) { w->failed = true; return; }
        w->data = p; w->capacity = cap;
    }
    if (n) memcpy(w->data + w->len, data, n);
    w->len = needed;
}
static inline void cpw_u64(CettaPreparedWriterV1 *w, uint64_t value) {
    uint8_t bytes[8];
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (uint8_t)(value >> (8u * i));
    cpw_bytes(w, bytes, sizeof(bytes));
}
static inline void cpw_text(CettaPreparedWriterV1 *w, const char *text, size_t len) {
    cpw_u64(w, len); cpw_bytes(w, text, len);
}
static inline const uint8_t *cpr_bytes(CettaPreparedReaderV1 *r, size_t n) {
    if (r->failed || n > r->len - r->at) { r->failed = true; return NULL; }
    const uint8_t *p = r->data + r->at; r->at += n; return p;
}
static inline uint64_t cpr_u64(CettaPreparedReaderV1 *r) {
    const uint8_t *p = cpr_bytes(r, 8u); uint64_t n = 0;
    if (p) for (unsigned i = 0; i < 8; ++i) n |= (uint64_t)p[i] << (8u * i);
    return n;
}
static inline const char *cpr_text(CettaPreparedReaderV1 *r, size_t *len) {
    uint64_t n = cpr_u64(r);
    if (n > SIZE_MAX) { r->failed = true; return NULL; }
    *len = (size_t)n; return (const char *)cpr_bytes(r, *len);
}
static inline bool cpr_literal(CettaPreparedReaderV1 *r, const char *s, size_t n) {
    const uint8_t *p = cpr_bytes(r, n);
    return p && !memcmp(p, s, n);
}
static inline bool cpr_abi(CettaPreparedReaderV1 *r, const char *abi) {
    size_t len = 0; const char *s = cpr_text(r, &len);
    return s && len == strlen(abi) && !memcmp(s, abi, len);
}
#endif
