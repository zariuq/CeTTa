/* rc_atoms.c - see rc_atoms.h */
#include "../rule_runtime/rc_rt.h"
#include "rc_atoms.h"

T *rc_atoms_int(uint16_t intc, int64_t v) {
    if (rc_atoms_fits(v)) return RC_ATOMS_IMM(v);
    uint8_t b[8];
    uint64_t u = (uint64_t)v;
    for (int i = 0; i < 8; i++) { b[i] = (uint8_t)u; u >>= 8; }
    T *x = rc_bytes_new(b, 8);
    x->tag = intc;
    return x;
}

T *rc_atoms_bytes(uint16_t tag, const uint8_t *b, size_t n) {
    T *x;
    if (n) x = rc_bytes_new(b, n);
    else {   /* the empty string tagged: an object of its own (rc_empty_bytes is shared and untagged) */
        x = rc_alloc_bytes(sizeof(TH) + 8);
        x->space = RC_SPACE_YOUNG; x->flags = RC_FLAG_BYTES; x->n = 0;
    }
    x->tag = tag;
    return x;
}

RC_HEAP_LOCAL RcAtomsSource rc_atoms_source;
static uint16_t src_cons, src_cp, src_nil;
/* (CP n) of the ASCII scalars, made once */
static struct { TH h; T *k[1]; } ascii_cp[128];
void rc_atoms_source_ctors(uint16_t cons, uint16_t cp, uint16_t nil) {
    src_cons = cons; src_cp = cp; src_nil = nil;
    for (uint32_t c = 0; c < 128u; c++) {
        ascii_cp[c].h = (TH){cp, RC_SPACE_STATIC, 0, 1};
        ascii_cp[c].k[0] = RC_ATOMS_IMM(c);
    }
}

size_t rc_atoms_scalar_at(size_t pos, uint32_t *scalar) {
    const uint8_t *b = rc_atoms_source.bytes + pos;
    size_t n = rc_atoms_source.len - pos;
    uint8_t c = b[0];
    if (c < 0x80u) { *scalar = c; return 1u; }
    size_t w = (c >> 5) == 6u ? 2u : (c >> 4) == 14u ? 3u : (c >> 3) == 30u ? 4u : 0u;
    uint32_t v = w ? c & (0x3Fu >> (w - 1u)) : c;
    bool ok = w > 1u && w <= n;
    for (size_t j = 1; ok && j < w; j++) {
        ok = (b[j] & 0xC0u) == 0x80u;
        v = (v << 6) | (b[j] & 0x3Fu);
    }
    if (ok && ((w == 2u && v < 0x80u) || (w == 3u && v < 0x800u) || (w == 4u && v < 0x10000u) || v > 0x10FFFFu ||
               (v >= 0xD800u && v <= 0xDFFFu)))
        ok = false;
    if (!ok) { *scalar = 0xDC00u + c; return 1u; }
    *scalar = v;
    return w;
}

T *rc_atoms_cursor(size_t pos) {
    if (pos >= rc_atoms_source.len) return RC_NUL_symbolic(src_nil);
    return (T *)(uintptr_t)(((uint64_t)pos << 3) | 2u);
}

T *rc_atoms_cursor_head(const T *x) {
    uint32_t c;
    rc_atoms_scalar_at(rc_atoms_cursor_pos(x), &c);
    return c < 128u ? (T *)(void *)&ascii_cp[c] : rc_mk1(src_cp, RC_ATOMS_IMM(c));
}

T *rc_atoms_cursor_tail(const T *x) {
    uint32_t c;
    size_t pos = rc_atoms_cursor_pos(x);
    return rc_atoms_cursor(pos + rc_atoms_scalar_at(pos, &c));
}

/* a cursor against any list value: cell by cell, the scalars compared without building them */
static bool cursor_equal(const T *cur, const T *b) {
    for (;;) {
        if (cur == b) return true;
        if (!rc_atoms_is_cursor(cur)) return rc_atoms_equal(cur, b);
        size_t pos = rc_atoms_cursor_pos(cur);
        if (rc_atoms_is_cursor(b)) {   /* two suffixes: equal lists exactly when equal bytes */
            size_t q = rc_atoms_cursor_pos(b);
            size_t m = rc_atoms_source.len - pos;
            return rc_atoms_source.len - q == m && memcmp(rc_atoms_source.bytes + pos, rc_atoms_source.bytes + q, m) == 0;
        }
        if (!RC_IS_HEAP(b) || b->tag != src_cons || b->n != 2 || (b->flags & RC_FLAG_BYTES)) return false;
        const T *h = b->k[0];
        if (!RC_IS_HEAP(h) || h->tag != src_cp || h->n != 1 || (h->flags & RC_FLAG_BYTES) || !RC_IS_IMM(h->k[0])) return false;
        uint32_t c;
        size_t w = rc_atoms_scalar_at(pos, &c);
        if (RC_ATOMS_INT(h->k[0]) != (int64_t)c) return false;   /* a scalar is an immediate */
        cur = rc_atoms_cursor(pos + w);
        b = b->k[1];
    }
}

bool rc_atoms_equal(const T *a, const T *b) {
    for (;;) {
        if (a == b) return true;
        if (rc_atoms_is_cursor(a)) return cursor_equal(a, b);
        if (rc_atoms_is_cursor(b)) return cursor_equal(b, a);
        if (!RC_IS_HEAP(a) || !RC_IS_HEAP(b)) return false;
        if (a->tag != b->tag || a->n != b->n || ((a->flags ^ b->flags) & RC_FLAG_BYTES)) return false;
        if (a->flags & RC_FLAG_BYTES) return memcmp(a->k, b->k, a->n) == 0;
        uint32_t n = a->n;
        if (!n) return true;
        for (uint32_t i = 0; i + 1 < n; i++) if (!rc_atoms_equal(a->k[i], b->k[i])) return false;
        a = a->k[n - 1];
        b = b->k[n - 1];
    }
}
