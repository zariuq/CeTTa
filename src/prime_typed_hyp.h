#ifndef CETTA_PRIME_TYPED_HYP_H
#define CETTA_PRIME_TYPED_HYP_H

#include "prime_typed_flow.h"

/* Prime's proof-relevant indexed hypothesis family is an ordinary client of
 * dependent application.  `primitive_rule` is an authored Prime declaration
 * already specialized to its sort-code type and primitive-symbol vocabulary
 * at the raw ingress.  This adapter adds no typing rule and mints no judgment
 * of its own; it applies the carried Pi judgment and exposes the resulting
 * family indices.
 *
 *     symbol : primitives sourceSort targetSort
 *   ------------------------------------------------
 *   hyp:primitive : hyp sorts primitives sourceSort targetSort
 */
CettaPrimeTypedValueV1 *cetta_prime_typed_hyp_primitive_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *primitive_rule,
    const CettaPrimeTypedValueV1 *source_sort,
    const CettaPrimeTypedValueV1 *target_sort,
    const CettaPrimeTypedValueV1 *primitive_symbol);

/* Exact indexed composition.  Construction is available only when the two
 * hypotheses have the same family parameters and an identical shared middle
 * index.  The result retains both premise occurrences and the middle witness.
 *
 *     earlier : hyp sorts primitives source middle
 *     later   : hyp sorts primitives middle target
 *   ------------------------------------------------
 *     hyp:chain : hyp sorts primitives source target
 */
CettaPrimeTypedValueV1 *cetta_prime_typed_hyp_chain_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *chain_rule,
    const CettaPrimeTypedValueV1 *source_sort,
    const CettaPrimeTypedValueV1 *middle_sort,
    const CettaPrimeTypedValueV1 *target_sort,
    const CettaPrimeTypedValueV1 *earlier,
    const CettaPrimeTypedValueV1 *later);

#endif /* CETTA_PRIME_TYPED_HYP_H */
