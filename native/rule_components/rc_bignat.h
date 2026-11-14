/* rc_bignat.h - natural numbers of any size, for a sort declared (rep SORT binary ZERO BIT BIT0 BIT1):
 * a module's binary numerals, least significant bit first (ZERO; BIT b n = 2n + b, where BIT BIT0 ZERO
 * is not canonical), held as an immediate below 2^62, or as an interned byte string with the
 * little-endian magnitude (no high zero byte) at or above 2^62.  Every number has exactly one value,
 * so two numbers are equal exactly when their values are the same word (an identity sort).
 *
 * Contract.
 *   - The view: 0 is ZERO; n > 0 is BIT (n mod 2) (n div 2).  Building BIT b m gives 2m + b; building
 *     BIT BIT0 ZERO stops the run (a refinement violation: no canonical numeral is that term).
 *   - rc_bn_cmp: the numeric order (-1, 0, 1); rc_bn_add, rc_bn_mul, rc_bn_xor (bitwise): exact;
 *     rc_bn_sub(a, b): a - b when b <= a, else 0; rc_bn_trim(a, w): a mod 2^w; rc_bn_pred(a): a - 1,
 *     NULL for 0; rc_bn_succ(a): a + 1; rc_bn_pack(high, v, bits): high * 2^bits + v.
 *   - A number above 2^20 bits stops the run (a refinement violation), never wraps; so does a product
 *     whose two factors have more than 16384 64-bit limbs together, even when the product would fit.
 * Label: the blocks that use these operations are proved equal to the module's arithmetic rules in
 * Mettapedia's Lean files for the modules that bind them; trusted: this C computes the stated
 * functions of the magnitudes.
 */
#ifndef RC_BIGNAT_H
#define RC_BIGNAT_H

#include "rc_intern.h"

#define RC_BN_SMALL ((uint64_t)1 << 62)

static inline bool rc_bn_small(T *x) { return RC_IS_IMM(x); }
static inline bool rc_bn_is_zero(T *x) { return RC_IS_IMM(x) && RC_IMM_VAL(x) == 0; }
static inline unsigned rc_bn_low_bit(T *x) {
    return RC_IS_IMM(x) ? (unsigned)(RC_IMM_VAL(x) & 1u) : (unsigned)(rc_ibytes(x)[0] & 1u);
}
T *rc_bn_from_u64_big(uint64_t v);
bool rc_bn_to_u64_big(T *x, uint64_t *out);
T *rc_bn_shr1_big(T *x);
T *rc_bn_shl1_or_big(T *x, unsigned bit);
int rc_bn_cmp_big(T *a, T *b);
T *rc_bn_add_big(T *a, T *b);
/* the immediate cases inline, the others in rc_bignat.c */
static inline T *rc_bn_from_u64(uint64_t v) { return v < RC_BN_SMALL ? RC_IMM(v) : rc_bn_from_u64_big(v); }
static inline bool rc_bn_to_u64(T *x, uint64_t *out) {
    if (RC_IS_IMM(x)) { *out = RC_IMM_VAL(x); return true; }
    return rc_bn_to_u64_big(x, out);
}
static inline T *rc_bn_shr1(T *x) { return RC_IS_IMM(x) ? RC_IMM(RC_IMM_VAL(x) >> 1) : rc_bn_shr1_big(x); }
static inline T *rc_bn_shl1_or(T *x, unsigned bit) {
    if (RC_IS_IMM(x) && RC_IMM_VAL(x) < (RC_BN_SMALL >> 1)) return RC_IMM((RC_IMM_VAL(x) << 1) | (bit & 1u));
    return rc_bn_shl1_or_big(x, bit);
}
static inline int rc_bn_cmp(T *a, T *b) {
    if (RC_IS_IMM(a) && RC_IS_IMM(b)) { uint64_t x = RC_IMM_VAL(a), y = RC_IMM_VAL(b); return x < y ? -1 : x > y ? 1 : 0; }
    return rc_bn_cmp_big(a, b);
}
static inline T *rc_bn_add(T *a, T *b) {
    if (RC_IS_IMM(a) && RC_IS_IMM(b)) { uint64_t s = RC_IMM_VAL(a) + RC_IMM_VAL(b); if (s < RC_BN_SMALL) return RC_IMM(s); }
    return rc_bn_add_big(a, b);
}
T *rc_bn_sub(T *a, T *b);
T *rc_bn_xor(T *a, T *b);
T *rc_bn_mul(T *a, T *b);
T *rc_bn_trim(T *a, T *width);
T *rc_bn_pred(T *a);
T *rc_bn_succ(T *a);
T *rc_bn_pack(T *high, uint64_t v, unsigned bits);

#endif
