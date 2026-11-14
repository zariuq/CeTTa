/* rc_bytes.c - byte strings (see rc_bytes.h for the contract). */
#include "rc_bytes.h"

#include <stdlib.h>
#include <string.h>

uint16_t rc_bytes_nil, rc_bytes_cons;
static uint16_t byte_ctor, nib_ctor[16];
static T *byte_terms[256];

void rc_bytes_init(uint16_t nil, uint16_t cons, uint16_t byte, const uint16_t nib[16]) {
    rc_bytes_nil = nil;
    rc_bytes_cons = cons;
    byte_ctor = byte;
    memcpy(nib_ctor, nib, sizeof nib_ctor);
    for (unsigned v = 0; v < 256; v++) {
        if (byte_terms[v]) continue;
        T *t = malloc(sizeof(TH) + 2 * sizeof(T *));
        if (!t) rc_refinement_fail("memory for the byte terms");
        t->tag = byte; t->space = RC_SPACE_STATIC; t->flags = 0; t->n = 2;
        t->k[0] = (T *)&rc_nul[nib[v >> 4]];
        t->k[1] = (T *)&rc_nul[nib[v & 15]];
        byte_terms[v] = t;
    }
}
T *rc_byte_term(unsigned v) { return byte_terms[v & 255]; }
static unsigned nib_value(T *n) {
    for (unsigned i = 0; i < 16; i++) if (n->tag == nib_ctor[i]) return i;
    rc_refinement_fail("not a nibble");
}
unsigned rc_byte_value(T *b) {
    if (b->tag != byte_ctor) rc_refinement_fail("not a byte");
    return 16u * nib_value(b->k[0]) + nib_value(b->k[1]);
}

int rc_bytes_cmp(T *a, T *b) {
    if (a == b) return 0;
    size_t la = rc_ibytes_len(a), lb = rc_ibytes_len(b);
    int c = memcmp(rc_ibytes(a), rc_ibytes(b), la < lb ? la : lb);
    if (c) return c < 0 ? -1 : 1;
    return la < lb ? -1 : la > lb ? 1 : 0;
}

T *rc_bytes_snoc(T *a, const uint8_t *s, size_t n) {
    size_t la = rc_ibytes_len(a);
    uint8_t small[256], *buf = la + n <= sizeof small ? small : malloc(la + n);
    if (!buf) rc_refinement_fail("memory for a byte string");
    memcpy(buf, rc_ibytes(a), la);
    memcpy(buf + la, s, n);
    T *r = rc_intern_bytes(buf, la + n);
    if (buf != small) free(buf);
    return r;
}

T *RC_MK2_bytes(uint16_t c, T *b, T *s) {
    (void)c;
    uint8_t v = (uint8_t)rc_byte_value(b);
    size_t ls = rc_ibytes_len(s);
    uint8_t small[256], *buf = ls + 1 <= sizeof small ? small : malloc(ls + 1);
    if (!buf) rc_refinement_fail("memory for a byte string");
    buf[0] = v;
    memcpy(buf + 1, rc_ibytes(s), ls);
    T *r = rc_intern_bytes(buf, ls + 1);
    if (buf != small) free(buf);
    return r;
}
