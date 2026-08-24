#ifndef CETTA_PRIME_RULE_MACHINE_INGRESS_H
#define CETTA_PRIME_RULE_MACHINE_INGRESS_H

#include "prime_typed_flow_boundary.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A RuleMachine run is an external proof producer, not a typing authority.
 * Complete proof occurrences cross Prime's ordinary check-once boundary in
 * authored order.  An incomplete run remains incomplete and publishes no
 * typed values, even when it carries a useful partial occurrence bag. */
typedef enum {
    CETTA_PRIME_RULE_MACHINE_RUN_COMPLETE_V1 = 1,
    CETTA_PRIME_RULE_MACHINE_RUN_INCOMPLETE_V1 = 2,
} CettaPrimeRuleMachineRunCompletionV1;

typedef enum {
    CETTA_PRIME_RULE_MACHINE_INGRESS_NONE_V1 = 0,
    CETTA_PRIME_RULE_MACHINE_INGRESS_CHECKED_BOUNDARY_V1 = 1,
    CETTA_PRIME_RULE_MACHINE_INGRESS_NATIVE_CONSTRUCTION_V1 = 2,
} CettaPrimeRuleMachineIngressModeV1;

typedef struct {
    Atom *encoded_proof;
    Atom *elaborated_term;
    /* Native construction is the stronger path: the proof tree is rebuilt
     * from typed leaves and intrinsic rules.  `checking` is populated only
     * for the generic checked-boundary fallback. */
    CettaPrimeRuleMachineIngressModeV1 mode;
    CettaPrimeTypingCheckingObservationV1 checking;
    CettaPrimeTypedValueV1 *value;
} CettaPrimeRuleMachineTypedOccurrenceV1;

typedef struct {
    CettaPrimeRuleMachineRunCompletionV1 completion;
    Atom *incomplete_reason;
    Atom *producer_revision;
    Atom *metrics;
    CettaPrimeRuleMachineTypedOccurrenceV1 *occurrences;
    size_t occurrence_count;
} CettaPrimeRuleMachineIngressResultV1;

/* Import a `compile:run` result as typed Prime proof occurrences.
 *
 * Proof templates are explicit staged data:
 *
 *   (occurrence (quote proof-term))
 *
 * and recursive premise holes use only the redex
 *
 *   (unquote (quote premise-proof))
 *
 * before entering the existing raw-to-typed boundary.  Bare evaluation,
 * unresolved variables, malformed staging, and malformed producer results
 * are infrastructure failures.  Individual semantic checking outcomes stay
 * in `checking`; only Established native outcomes construct `value`.
 *
 * The returned arrays and atoms belong to `owner`.
 */
bool cetta_prime_rule_machine_import_run_v1(
    Arena *owner, Space *space, Atom *run_result,
    const CettaPrimeTypedValueV1 *expected_type,
    bool steps_limited, uint64_t steps,
    CettaPrimeRuleMachineIngressResultV1 *result_out);

#endif /* CETTA_PRIME_RULE_MACHINE_INGRESS_H */
