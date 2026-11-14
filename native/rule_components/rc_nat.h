/* rc_nat.h - natural numbers of a sort declared (rep SORT nat ZERO B0 B1) or (rep SORT unary ZERO
 * SUCC): immediates, read as the module's binary numerals (ZERO; B0 m = 2m with m > 0; B1 m = 2m + 1)
 * or unary ones (ZERO; SUCC m = m + 1); the views are generated.
 *
 * Contract.
 *   - rc_nat_cmp(a, b): the numeric order, -1, 0 or 1.
 *   - rc_nat_succ(a): a + 1; rc_nat_add(a, b): a + b; a value outside the immediate range stops the
 *     run (refinement violation), never wraps.
 * Label: proved equal to a module's numeral rules for modules with those rules (binary:
 * RuleModulePrimitives.lean cmpNat_refines, natInc_refines; unary: the order, successor and addition
 * of RuleModuleUnary.lean); trusted: the comparison and addition of uint64_t; checked at run time:
 * the immediate range.
 */
#ifndef RC_NAT_H
#define RC_NAT_H

#include "../rule_runtime/rc_rt.h"

static inline int rc_nat_cmp(T *a, T *b) {
    uint64_t x = RC_IMM_VAL(a), y = RC_IMM_VAL(b);
    return x < y ? -1 : x > y ? 1 : 0;
}
static inline T *rc_nat_succ(T *a) {
    uint64_t x = RC_IMM_VAL(a);
    if (x >= (UINT64_MAX >> 3)) rc_refinement_fail("a natural beyond the immediate range");
    return RC_IMM(x + 1);
}
/* the same range as rc_nat_succ: a result up to UINT64_MAX >> 3 */
static inline T *rc_nat_add(T *a, T *b) {
    uint64_t x = RC_IMM_VAL(a), y = RC_IMM_VAL(b), lim = UINT64_MAX >> 3;
    if (x > lim || y > lim - x) rc_refinement_fail("a natural beyond the immediate range");
    return RC_IMM(x + y);
}

#endif
