#ifndef CETTA_PRIME_TYPED_LIST_H
#define CETTA_PRIME_TYPED_LIST_H

#include "prime_typed_flow.h"

/* Construct the empty native List by ordinary dependent application of the
 * authored `list:nil` declaration.  The result remains an ordinary Prime
 * typed value and exposes `list` as a zero-index family with one parameter. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_nil_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *element_type);

/* Construct `list:cons element_type head tail`.  Exact carried types make an
 * ill-typed head or tail inexpressible through this operation; no synthesis
 * or checking service is called in its interior. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_cons_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *cons_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail);

/* Native realization of the ordinary authored `list:eliminate` rule.  The
 * operation recognizes the exact declaration and canonical List spine,
 * performs the repeated iota computation as one fused fold, and returns the
 * reduct together with a receipt retaining the typed redex, cases, spine,
 * and occurrence provenance.  It performs no interior checking or synthesis;
 * raw relational execution remains available independently. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_eliminate_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *eliminate_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *motive,
    const CettaPrimeTypedValueV1 *nil_case,
    const CettaPrimeTypedValueV1 *cons_case,
    const CettaPrimeTypedValueV1 *list);

/* Native realization of the actual checked Prime `map` program: four
 * lambdas whose body is the ordinary `list:eliminate` term.  The program is
 * not a privileged constant or opcode; its exact intrinsic term and carried
 * type license a fused beta/iota fold.  A different program of the same type
 * is simply ineligible and remains available to ordinary Prime execution. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_map_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *map_program,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *function,
    const CettaPrimeTypedValueV1 *list);

/* Realize the canonical constructor fragment as CeTTa's flat expression
 * carrier.  This is the direct native realization of the settled List
 * representation: empty is `()` and cons prepends one atom.  It accepts only
 * current List values constructed through the canonical authored names. */
bool cetta_prime_typed_list_runtime_representation_v1(
    Arena *destination, Space *space,
    const CettaPrimeTypedValueV1 *list,
    Atom **representation_out);

#endif /* CETTA_PRIME_TYPED_LIST_H */
