/* rc_order.h - the structural order on the values of interned sorts: generic over the signature.
 *
 * Contract.  rc_order_cmp(sort, a, b) returns -1, 0 or 1:
 *   - a sort of order kind RC_ORDER_BYTES is ordered by rc_bytes_cmp, RC_ORDER_NAT numerically,
 *     RC_ORDER_RANK (only nullary constructors, static terms) by constructor rank;
 *   - a sort of kind RC_ORDER_STRUCT (an interned sort) by constructor rank first (the order of the
 *     constructors in the sort's declaration), then the children from left to right, each by the
 *     order of its own sort; the first child that differs decides.
 * 0 exactly when a and b are equal (equal interned values are one node).  The tables are generated
 * by rulec from the signature: rc_sort_order[sort], rc_ctor_rank[ctor], and the argument sorts
 * rc_ctor_asort_off / rc_ctor_asort.
 * Bound to a module's order judgment (block (structural-order LT EQ GT)), the contract is that the
 * judgment's rules compute this order; for the HOL Light module's cmp-type and cmp-term that is
 * proved (RuleModuleComponents.lean: structuralOrder_cmpType, structuralOrder_cmpTerm, and
 * rcOrderCmp_eq for the identity test at every call); trusted: this C is the model.
 */
#ifndef RC_ORDER_H
#define RC_ORDER_H

#include "rc_intern.h"

enum { RC_ORDER_NONE = 0, RC_ORDER_STRUCT, RC_ORDER_BYTES, RC_ORDER_NAT, RC_ORDER_RANK };
extern const uint8_t rc_sort_order[];
extern const uint16_t rc_ctor_rank[];
extern const uint16_t rc_ctor_asort_off[];
extern const uint16_t rc_ctor_asort[];
int rc_order_cmp(int sort, T *a, T *b);

#endif
