/* rc_set.h - ordered sets as lists of an interned list sort (two constructors: NIL, CONS head
 * tail), over a comparator that is a judgment of the module (compiled from its rules, or a block).
 *
 * Contract (the merge that a module's rules write as union/remove on sorted lists):
 *   rc_set_union(d, cmp, l1, l2): walk both lists; when the heads compare EQ keep l1's head and
 *     advance both; LT keep l1's head; GT keep l2's head; then the rest of the list that remains,
 *     shared, not copied.
 *   rc_set_remove(d, cmp, t, l): skip the leading elements s with cmp(t, s) = GT; if the next
 *     element compares EQ, drop it; the rest is shared.
 * cmp returns the module's order constructors (d->lt, d->eq, d->gt), or fails (NULL): then the
 * operation fails (NULL), as its rules do; any other value stops the run.
 * The lists are rebuilt through the interning store.  Bound to judgments whose rules are these
 * recursions (for HOL Light: fusion.ml's term_union and term_remove over alphaorder): proved for
 * every comparator (RuleModuleComponents.lean: setUnion_eq_termUnion, setRemove_eq_termRemove);
 * trusted: this C is the model.
 */
#ifndef RC_SET_H
#define RC_SET_H

#include "rc_intern.h"

typedef struct { uint16_t nil, cons, lt, eq, gt; } RcSetDesc;
typedef T *(*RcCmpFn)(T *a, T *b);
T *rc_set_union(const RcSetDesc *d, RcCmpFn cmp, T *l1, T *l2);
T *rc_set_remove(const RcSetDesc *d, RcCmpFn cmp, T *t, T *l);

#endif
