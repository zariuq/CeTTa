/* he_typing.h — HE-style typing, formally regrounded, for the he-prime profile.
 *
 * Frame (implemented to, deviations named in the census):
 *  - HE typing is SUCCESS TYPING: a rejection is proven, an acceptance is not a
 *    promise.  Multiple declared types are INTERSECTION types.
 *  - %Undefined% is the GRADUAL DYNAMIC type ?: consistent with everything, and
 *    that consistency is deliberately NOT transitive — a value may not be
 *    laundered from one base type to another by passing through ?.
 *  - Atom is the value-lattice TOP with one-directional subsumption, kept
 *    distinct from ?; in parameter position it is a STAGING modality (the
 *    argument arrives unreduced).  The other meta-types (Symbol, Expression,
 *    Grounded, Variable) are staging modalities, not value-lattice members.
 *  - Consistency is licensed by exactly one named reason per edge (exact,
 *    dynamic-?, top, meta-staging, structural); naming them apart is what makes
 *    the dynamic edge non-composing.
 *
 * Constraint: every op is inert outside dependent-telescope profiles, so HE and
 * he-compat default typing behavior is untouched.
 * Constraint: verdicts are three-valued (accept / reject / unknown); unknown
 * propagates and is never conflated with reject or with a proven-empty type set.
 */
#ifndef CETTA_HE_TYPING_H
#define CETTA_HE_TYPING_H

#include <stdint.h>
#include "atom.h"

/* Returns NULL when head is not a typing op or the active profile does not
 * enable dependent telescopes; otherwise returns a verdict atom. */
Atom *he_typing_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs)
    __attribute__((weak));

/* True for the regrounded typing op names only; result constructors such as
 * consistent / inconsistent / type-set are ordinary inert atoms.
 * Weak: standalone unit-test binaries link grounded.c without he_typing.c, so
 * the symbol resolves to NULL there and the grounded.c hook is skipped. */
bool he_typing_is_op(const char *name) __attribute__((weak));

/* True when arg_index of the named typing op is a DATA argument that must
 * reach the op unevaluated.  Lives beside the op table so the surface and its
 * argument policy stay in one place.  Weak for the same standalone-binary
 * reason as above. */
bool he_typing_op_data_arg(const char *name, uint32_t arg_index)
    __attribute__((weak));

#endif
