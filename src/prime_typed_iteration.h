#ifndef CETTA_PRIME_TYPED_ITERATION_H
#define CETTA_PRIME_TYPED_ITERATION_H

#include "prime_typed_flow.h"

/* Construct the zero-count inhabitant of the ordinary proof-relevant
 * `rel:iterate` family. */
CettaPrimeTypedValueV1 *cetta_prime_typed_iteration_zero_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *zero_rule,
    const CettaPrimeTypedValueV1 *value_type,
    const CettaPrimeTypedValueV1 *counter_type,
    const CettaPrimeTypedValueV1 *step,
    const CettaPrimeTypedValueV1 *predecessor,
    const CettaPrimeTypedValueV1 *zero,
    const CettaPrimeTypedValueV1 *source);

/* Construct one iteration step while retaining the predecessor evidence,
 * value-step evidence, recursive evidence, and exact intermediate indices. */
CettaPrimeTypedValueV1 *cetta_prime_typed_iteration_step_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *step_rule,
    const CettaPrimeTypedValueV1 *value_type,
    const CettaPrimeTypedValueV1 *counter_type,
    const CettaPrimeTypedValueV1 *step,
    const CettaPrimeTypedValueV1 *predecessor,
    const CettaPrimeTypedValueV1 *zero,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *next,
    const CettaPrimeTypedValueV1 *later,
    const CettaPrimeTypedValueV1 *earlier,
    const CettaPrimeTypedValueV1 *target,
    const CettaPrimeTypedValueV1 *predecessor_evidence,
    const CettaPrimeTypedValueV1 *step_evidence,
    const CettaPrimeTypedValueV1 *recursive_evidence);

#endif /* CETTA_PRIME_TYPED_ITERATION_H */
