/* hol_native_ops.c - native operations replacing five judgments of the HOL Light kernel module.
 *
 * Representations (rulec --native-sort):
 *   Str: a byte string; R_Name(xs, bs) holds when decodeStr xs = bs (each byte hi lo is 16*hi + lo).
 *        decodeStr is a bijection (decodeStr_injective, decodeStr_surjective), and the view
 *        nnil / ncons (byte hi lo) rest that the compiled rules read is decodeStr unfolded once.
 *   Nat: an immediate natural n; R_Nat(a, n) holds when a = enc n, the canonical binary numeral of n
 *        (val_enc, canon_enc, enc_val).  The view is enc unfolded once: enc 0 = nz, enc n = nb0 (enc
 *        (n/2)) for even n > 0, enc n = nb1 (enc (n/2)) for odd n (enc_zero, enc_of_even, enc_of_odd);
 *        building nb0 from zero is refused, as no canonical numeral has that form (canon_nb0_iff).
 *
 * Each operation and its justification (theorems of the Lean file RuleModulePrimitives):
 *   hl:cmp-str   memcmp over the common length, then the shorter first.  Proved against the rules:
 *                cmpStr_refines (rules = nativeCmp on decoded bytes), nativeCmp_eq_memcmp (nativeCmp =
 *                the memcmp-then-length formulation).  Trusted: C's memcmp compares as unsigned char.
 *   hl:str-eq    equal lengths and memcmp = 0.  Proved: strEq_refines, eq_iff_memcmp.  Trusted: memcmp.
 *   hl:str-prime append the byte 0x27.  Proved: strPrime_refines.
 *   hl:cmp-nat   comparison of the naturals.  Proved: cmpNat_refines (for canonical numerals, which are
 *                exactly the values of the representation).  Trusted: the C comparison of uint64_t.
 *   hl:nat-inc   n + 1.  Proved: natInc_refines.  Checked here: no overflow of the immediate range.
 * Every operation is also checked against its compiled rules on every call of a corpus run
 * (RC_CHECK_NATIVE) and on bounded domains (holm selftest).
 */
#include "../rule_runtime/rc_rt.h"
#include "generated/hol_module_ids.h"

#define NUL(c) ((T *)&rc_nul[(c)])

static inline T *ord(int c) { return c < 0 ? NUL(C_hl_lt) : c > 0 ? NUL(C_hl_gt) : NUL(C_hl_eq); }

T *rc_native_cmp_str(T *a, T *b) {
    size_t la = a->n, lb = b->n, m = la < lb ? la : lb;
    int c = memcmp(rc_bytes(a), rc_bytes(b), m);
    if (c) return ord(c);
    return ord(la < lb ? -1 : la > lb ? 1 : 0);
}

T *rc_native_str_eq(T *a, T *b) {
    return a->n == b->n && memcmp(rc_bytes(a), rc_bytes(b), a->n) == 0 ? NUL(C_hl_yes) : NUL(C_hl_no);
}

T *rc_native_str_prime(T *a) {
    T *r = rc_bytes_new(NULL, (size_t)a->n + 1);
    uint8_t *d = (uint8_t *)r->k;
    memcpy(d, rc_bytes(a), a->n);
    d[a->n] = 0x27;
    return r;
}

T *rc_native_cmp_nat(T *a, T *b) {
    uint64_t x = RC_IMM_VAL(a), y = RC_IMM_VAL(b);
    return ord(x < y ? -1 : x > y ? 1 : 0);
}

T *rc_native_nat_inc(T *a) {
    uint64_t x = RC_IMM_VAL(a);
    if (x >= (UINT64_MAX >> 3)) rc_refinement_fail("nat-inc beyond the immediate range");
    return RC_IMM(x + 1);
}
