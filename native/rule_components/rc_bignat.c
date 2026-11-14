/* rc_bignat.c - natural numbers of any size (see rc_bignat.h for the contract). */
#include "rc_bignat.h"

#include <stdlib.h>
#include <string.h>

/* magnitudes as little-endian 64-bit limbs, at most RC_BN_LIMBS of them */
#define RC_BN_LIMBS 16384
typedef struct { uint64_t *l; size_t n; } Mag;

/* a magnitude's limbs: in the caller's buffer of MAG_SMALL limbs when they fit, else on the heap */
#define MAG_SMALL 4
static void mag_of(T *x, Mag *m, uint64_t *small) {
    if (RC_IS_IMM(x)) { small[0] = RC_IMM_VAL(x); m->l = small; m->n = small[0] ? 1 : 0; return; }
    size_t nb = rc_ibytes_len(x), n = (nb + 7) / 8;
    m->l = n <= MAG_SMALL ? small : malloc(n * sizeof(uint64_t));
    if (!m->l) rc_refinement_fail("out of memory (a large natural)");
    memset(m->l, 0, n * sizeof(uint64_t));
    memcpy(m->l, rc_ibytes(x), nb);   /* little-endian host */
    m->n = n;
}
static void mag_free(Mag *m, uint64_t *small) { if (m->l != small) free(m->l); }
/* the value of n limbs (trailing zero limbs dropped): an immediate, or an interned magnitude */
static T *mag_value(const uint64_t *l, size_t n) {
    while (n && !l[n - 1]) n--;
    if (n == 0) return RC_IMM(0);
    if (n == 1 && l[0] < RC_BN_SMALL) return RC_IMM(l[0]);
    if (n > RC_BN_LIMBS) rc_refinement_fail("a natural beyond 2^20 bits");
    size_t nb = n * 8;
    const uint8_t *b = (const uint8_t *)l;
    while (nb && !b[nb - 1]) nb--;
    return rc_intern_bytes(b, nb);
}

T *rc_bn_from_u64_big(uint64_t v) { return mag_value(&v, 1); }
bool rc_bn_to_u64_big(T *x, uint64_t *out) {
    if (RC_IS_IMM(x)) { *out = RC_IMM_VAL(x); return true; }
    size_t nb = rc_ibytes_len(x);
    if (nb > 8) return false;
    uint64_t v = 0;
    memcpy(&v, rc_ibytes(x), nb);
    *out = v;
    return true;
}

T *rc_bn_shr1_big(T *x) {
    if (RC_IS_IMM(x)) return RC_IMM(RC_IMM_VAL(x) >> 1);
    uint64_t w;
    if (rc_bn_to_u64(x, &w)) return rc_bn_from_u64(w >> 1);
    uint64_t one[MAG_SMALL]; Mag m; mag_of(x, &m, one);
    for (size_t i = 0; i < m.n; i++) m.l[i] = (m.l[i] >> 1) | (i + 1 < m.n ? m.l[i + 1] << 63 : 0);
    T *r = mag_value(m.l, m.n);
    mag_free(&m, one);
    return r;
}
/* two limbs (a number below 2^128) as a value */
static T *two_limbs(unsigned __int128 r) { uint64_t l[2] = {(uint64_t)r, (uint64_t)(r >> 64)}; return mag_value(l, 2); }
T *rc_bn_shl1_or_big(T *x, unsigned bit) {
    if (RC_IS_IMM(x)) {
        uint64_t v = RC_IMM_VAL(x);
        if (v < (RC_BN_SMALL >> 1)) return RC_IMM((v << 1) | (bit & 1u));
    }
    uint64_t w;
    if (rc_bn_to_u64(x, &w)) return two_limbs(((unsigned __int128)w << 1) | (bit & 1u));
    uint64_t one[MAG_SMALL]; Mag m; mag_of(x, &m, one);
    uint64_t *r = malloc((m.n + 1) * sizeof(uint64_t));
    if (!r) rc_refinement_fail("out of memory (a large natural)");
    uint64_t carry = bit & 1u;
    for (size_t i = 0; i < m.n; i++) { r[i] = (m.l[i] << 1) | carry; carry = m.l[i] >> 63; }
    r[m.n] = carry;
    T *v = mag_value(r, m.n + 1);
    free(r);
    mag_free(&m, one);
    return v;
}

static int mag_cmp(const Mag *a, const Mag *b) {
    size_t na = a->n, nb = b->n;
    while (na && !a->l[na - 1]) na--;
    while (nb && !b->l[nb - 1]) nb--;
    if (na != nb) return na < nb ? -1 : 1;
    for (size_t i = na; i-- > 0;) if (a->l[i] != b->l[i]) return a->l[i] < b->l[i] ? -1 : 1;
    return 0;
}
int rc_bn_cmp_big(T *a, T *b) {
    if (RC_IS_IMM(a) && RC_IS_IMM(b)) { uint64_t x = RC_IMM_VAL(a), y = RC_IMM_VAL(b); return x < y ? -1 : x > y ? 1 : 0; }
    if (a == b) return 0;
    uint64_t x, y;
    if (rc_bn_to_u64(a, &x) && rc_bn_to_u64(b, &y)) return x < y ? -1 : x > y ? 1 : 0;
    uint64_t oa[MAG_SMALL], ob[MAG_SMALL]; Mag ma, mb;
    mag_of(a, &ma, oa); mag_of(b, &mb, ob);
    int c = mag_cmp(&ma, &mb);
    mag_free(&ma, oa); mag_free(&mb, ob);
    return c;
}

typedef enum { OP_ADD, OP_SUB, OP_XOR } Op;
static T *binop(T *a, T *b, Op op) {
    uint64_t x, y;
    if (rc_bn_to_u64(a, &x) && rc_bn_to_u64(b, &y)) {   /* both below 2^64 */
        if (op == OP_ADD) return two_limbs((unsigned __int128)x + y);
        if (op == OP_XOR) return rc_bn_from_u64(x ^ y);
        return rc_bn_from_u64(x >= y ? x - y : 0);
    }
    uint64_t oa[MAG_SMALL], ob[MAG_SMALL]; Mag ma, mb;
    mag_of(a, &ma, oa); mag_of(b, &mb, ob);
    size_t n = (ma.n > mb.n ? ma.n : mb.n) + 1;
    uint64_t *r = calloc(n, sizeof(uint64_t));
    if (!r) rc_refinement_fail("out of memory (a large natural)");
    if (op == OP_SUB && mag_cmp(&ma, &mb) < 0) { free(r); mag_free(&ma, oa); mag_free(&mb, ob); return RC_IMM(0); }
    unsigned __int128 carry = 0;
    uint64_t borrow = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t x = i < ma.n ? ma.l[i] : 0, y = i < mb.n ? mb.l[i] : 0;
        if (op == OP_ADD) { unsigned __int128 s = (unsigned __int128)x + y + carry; r[i] = (uint64_t)s; carry = s >> 64; }
        else if (op == OP_XOR) r[i] = x ^ y;
        else { uint64_t d = x - y - borrow; borrow = (x < y) || (x - y < borrow); r[i] = d; }
    }
    T *v = mag_value(r, n);
    free(r);
    mag_free(&ma, oa); mag_free(&mb, ob);
    return v;
}
T *rc_bn_add_big(T *a, T *b) { return binop(a, b, OP_ADD); }
T *rc_bn_sub(T *a, T *b) {
    if (RC_IS_IMM(a) && RC_IS_IMM(b)) { uint64_t x = RC_IMM_VAL(a), y = RC_IMM_VAL(b); return RC_IMM(x > y ? x - y : 0); }
    return binop(a, b, OP_SUB);
}
T *rc_bn_xor(T *a, T *b) {
    if (RC_IS_IMM(a) && RC_IS_IMM(b)) return RC_IMM(RC_IMM_VAL(a) ^ RC_IMM_VAL(b));
    return binop(a, b, OP_XOR);
}
T *rc_bn_mul(T *a, T *b) {
    uint64_t x, y;
    if (rc_bn_to_u64(a, &x) && rc_bn_to_u64(b, &y)) {   /* both below 2^64 */
        unsigned __int128 p = (unsigned __int128)x * y;
        return p < RC_BN_SMALL ? RC_IMM((uint64_t)p) : two_limbs(p);
    }
    uint64_t oa[MAG_SMALL], ob[MAG_SMALL]; Mag ma, mb;
    mag_of(a, &ma, oa); mag_of(b, &mb, ob);
    size_t n = ma.n + mb.n + 1;
    if (n > RC_BN_LIMBS + 1) rc_refinement_fail("a natural beyond 2^20 bits");
    uint64_t *r = calloc(n, sizeof(uint64_t));
    if (!r) rc_refinement_fail("out of memory (a large natural)");
    for (size_t i = 0; i < ma.n; i++) {
        unsigned __int128 carry = 0;
        for (size_t j = 0; j < mb.n; j++) {
            unsigned __int128 t = (unsigned __int128)ma.l[i] * mb.l[j] + r[i + j] + carry;
            r[i + j] = (uint64_t)t;
            carry = t >> 64;
        }
        for (size_t k = i + mb.n; carry; k++) { unsigned __int128 t = (unsigned __int128)r[k] + carry; r[k] = (uint64_t)t; carry = t >> 64; }
    }
    T *v = mag_value(r, n);
    free(r);
    mag_free(&ma, oa); mag_free(&mb, ob);
    return v;
}
T *rc_bn_trim(T *a, T *width) {
    uint64_t w;
    if (!rc_bn_to_u64(width, &w) || w >= ((uint64_t)RC_BN_LIMBS * 64)) return a;   /* a < 2^(2^20) <= 2^w */
    if (RC_IS_IMM(a)) return w >= 62 ? a : RC_IMM(RC_IMM_VAL(a) & (((uint64_t)1 << w) - 1));
    uint64_t x;
    if (rc_bn_to_u64(a, &x)) return w >= 64 ? a : rc_bn_from_u64(x & (((uint64_t)1 << w) - 1));
    uint64_t one[MAG_SMALL]; Mag m; mag_of(a, &m, one);
    size_t full = (size_t)(w / 64), part = (size_t)(w % 64);
    if (full < m.n) {
        for (size_t i = full + 1; i < m.n; i++) m.l[i] = 0;
        m.l[full] &= part ? (((uint64_t)1 << part) - 1) : 0;
    }
    T *v = mag_value(m.l, m.n);
    mag_free(&m, one);
    return v;
}
T *rc_bn_pred(T *a) {
    if (rc_bn_is_zero(a)) return NULL;
    return rc_bn_sub(a, RC_IMM(1));
}
T *rc_bn_succ(T *a) { return rc_bn_add(a, RC_IMM(1)); }
T *rc_bn_pack(T *high, uint64_t v, unsigned bits) {
    if (RC_IS_IMM(high) && bits < 62 && RC_IMM_VAL(high) < (RC_BN_SMALL >> bits)) return RC_IMM((RC_IMM_VAL(high) << bits) | v);
    uint64_t h;
    if (bits < 64 && rc_bn_to_u64(high, &h)) return two_limbs(((unsigned __int128)h << bits) | v);
    T *s = high;
    for (unsigned i = 0; i < bits; i++) s = rc_bn_shl1_or(s, 0);
    return rc_bn_add(s, rc_bn_from_u64(v));
}
