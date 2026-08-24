#ifndef CETTA_PRIME_TYPED_LIST_RELATOR_H
#define CETTA_PRIME_TYPED_LIST_RELATOR_H

#include "prime_typed_finite_relation.h"

/* Empty proof for the ordinary Prime family
 * `map-rel source target relation (list:nil source) (list:nil target)`. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_map_rel_nil_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation);

/* Proof-relevant cons lifting.  The two endpoint heads and tails, head
 * evidence, and recursive tail evidence remain explicit typed premises. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_map_rel_cons_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *cons_rule,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source_head,
    const CettaPrimeTypedValueV1 *target_head,
    const CettaPrimeTypedValueV1 *source_tail,
    const CettaPrimeTypedValueV1 *target_tail,
    const CettaPrimeTypedValueV1 *head_evidence,
    const CettaPrimeTypedValueV1 *tail_evidence);

/* Native fused realization of the authored `map-rel:eliminate` iota rules.
 * The exact relation proof and both endpoint Lists are retained, including
 * every head and recursive-tail evidence occurrence.  Noncanonical evidence
 * remains available to ordinary relational execution instead of being
 * treated as a refutation. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_map_rel_eliminate_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *eliminate_rule,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *motive,
    const CettaPrimeTypedValueV1 *nil_case,
    const CettaPrimeTypedValueV1 *cons_case,
    const CettaPrimeTypedValueV1 *source_list,
    const CettaPrimeTypedValueV1 *target_list,
    const CettaPrimeTypedValueV1 *evidence);

/* One exact query-local lifting of a complete finite relation through List.
 * The source List is an ordinary already-typed value.  Each output retains
 * one target List, its full `map-rel` evidence, and the exact base-provider
 * occurrence selected at every source position.  The mixed-radix order is
 * the ordinary left-to-right relational bind order: earlier List positions
 * vary more slowly than later positions. */
typedef struct {
    const CettaPrimeTypedValueV1 *source_list;
    const CettaPrimeTypedValueV1 *lifted_relation;
    const CettaPrimeTypedValueV1 *const *target_lists;
    const CettaPrimeTypedValueV1 *const *evidences;
    const size_t *base_occurrence_indices;
    size_t source_length;
    CettaPrimeTypedFiniteRelationSearchV1 search;
} CettaPrimeTypedListMapRelFiniteV1;

CettaPrimeTypedFiniteRelationBuildV1
cetta_prime_typed_list_map_rel_finite_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedFiniteRelationV1 *provider,
    const CettaPrimeTypedValueV1 *source_list,
    const CettaPrimeTypedValueV1 *list_family,
    const CettaPrimeTypedValueV1 *list_nil_rule,
    const CettaPrimeTypedValueV1 *list_cons_rule,
    const CettaPrimeTypedValueV1 *map_rel_family,
    const CettaPrimeTypedValueV1 *map_rel_nil_rule,
    const CettaPrimeTypedValueV1 *map_rel_cons_rule,
    CettaPrimeTypedListMapRelFiniteV1 *result_out);

#endif /* CETTA_PRIME_TYPED_LIST_RELATOR_H */
