#ifndef CETTA_PRIME_TYPED_FLOW_BOUNDARY_H
#define CETTA_PRIME_TYPED_FLOW_BOUNDARY_H

#include "prime_regular_kernel_admission.h"
#include "prime_semantics.h"
#include "prime_typed_flow.h"

/* Raw-to-typed ingress.  The supplied synthesis must be current and establish
 * a positive Prime judgment.  Refutations and coverage failures do not become
 * typed values. */
CettaPrimeTypedValueV1 *cetta_prime_typed_value_import_synthesis_v1(
    Arena *owner, Space *space,
    const CettaPrimeRegularKernelAdmittedSynthesisV1 *synthesis);

/* Declaration-aware check-once ingress.  A successful observation is always
 * returned, including OutsideFragment, Incomplete, and engine-fault results;
 * only an Established native Prime route constructs `value_out`. */
bool cetta_prime_typed_value_import_term_v1(
    Arena *owner, Space *space, Atom *term,
    bool steps_limited, uint64_t steps,
    CettaPrimeTypingSynthesisObservationV1 *observation_out,
    CettaPrimeTypedValueV1 **value_out);

/* Check-once ingress for a closed term whose expected type is already a
 * current typed value.  This is the boundary needed by closed lambdas and
 * other checkable-but-not-synthesizable Prime programs.  Every outcome is
 * observed; only an Established native route constructs `value_out`. */
bool cetta_prime_typed_value_import_checked_term_v1(
    Arena *owner, Space *space, Atom *term,
    const CettaPrimeTypedValueV1 *expected_type,
    bool steps_limited, uint64_t steps,
    CettaPrimeTypingCheckingObservationV1 *observation_out,
    CettaPrimeTypedValueV1 **value_out);

/* Materialize explicit staging redexes in syntax data.  Bare quotations stay
 * data; only `unquote (quote term)` is reduced, recursively, and quoted
 * subterms are otherwise opaque.  This grants no typing or execution
 * authority and is shared by the public judgment boundary and external proof
 * producers. */
Atom *cetta_prime_typed_boundary_splice_explicit_v1(
    Arena *owner, Atom *term);

#endif /* CETTA_PRIME_TYPED_FLOW_BOUNDARY_H */
