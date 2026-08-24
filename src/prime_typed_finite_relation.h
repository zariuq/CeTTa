#ifndef CETTA_PRIME_TYPED_FINITE_RELATION_H
#define CETTA_PRIME_TYPED_FINITE_RELATION_H

#include "prime_typed_relation.h"

/* A finite-evidence capability for one ordinary proof-relevant Prime
 * relation.  The representation is opaque: callers supply already-typed
 * source, target, and evidence occurrences, and this module admits the
 * provider only when every occurrence inhabits the relation's exact
 * dependent fibre. */
typedef struct CettaPrimeTypedFiniteRelationV1
    CettaPrimeTypedFiniteRelationV1;

typedef struct {
    const CettaPrimeTypedValueV1 *source;
    const CettaPrimeTypedValueV1 *target;
    const CettaPrimeTypedValueV1 *evidence;
} CettaPrimeTypedFiniteRelationOccurrenceInputV1;

typedef struct {
    const CettaPrimeTypedValueV1 *source;
    const CettaPrimeTypedValueV1 *target;
    const CettaPrimeTypedValueV1 *evidence;
} CettaPrimeTypedFiniteRelationOccurrenceViewV1;

typedef enum {
    CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1 = 0,
    CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1,
    CETTA_PRIME_TYPED_FINITE_RELATION_FAULT_V1,
} CettaPrimeTypedFiniteRelationBuildV1;

/* Construct a complete finite provider from one ordered occurrence bag.
 * Duplicates and authored order are retained.  This is an intrinsic
 * construction over carried judgments; it performs no checking search or
 * declaration lookup. */
CettaPrimeTypedFiniteRelationBuildV1
cetta_prime_typed_finite_relation_create_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedFiniteRelationOccurrenceInputV1 *occurrences,
    size_t occurrence_count,
    CettaPrimeTypedFiniteRelationV1 **provider_out);

bool cetta_prime_typed_finite_relation_is_current_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider,
    const Space *space);

/* Copy the same admitted capability into another owner without creating new
 * proof occurrences. */
CettaPrimeTypedFiniteRelationV1 *
cetta_prime_typed_finite_relation_retain_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedFiniteRelationV1 *provider);

const CettaPrimeTypedValueV1 *
cetta_prime_typed_finite_relation_source_type_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider);

const CettaPrimeTypedValueV1 *
cetta_prime_typed_finite_relation_target_type_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider);

const CettaPrimeTypedValueV1 *
cetta_prime_typed_finite_relation_relation_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider);

size_t cetta_prime_typed_finite_relation_occurrence_count_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider);

bool cetta_prime_typed_finite_relation_occurrence_v1(
    const CettaPrimeTypedFiniteRelationV1 *provider,
    size_t index,
    CettaPrimeTypedFiniteRelationOccurrenceViewV1 *occurrence_out);

/* Origins name the exact pair of input occurrences retained by each output
 * occurrence of relational composition. */
typedef struct {
    size_t earlier_index;
    size_t later_index;
} CettaPrimeTypedFiniteRelationChainOriginV1;

/* Exact finite evidence providers compose by the ordinary proof-relevant
 * relation `chain`.  Every output occurrence carries the intermediate value
 * and both premise evidences; equal endpoints remain distinct occurrences. */
CettaPrimeTypedFiniteRelationBuildV1
cetta_prime_typed_finite_relation_chain_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedFiniteRelationV1 *earlier,
    const CettaPrimeTypedFiniteRelationV1 *later,
    CettaPrimeTypedFiniteRelationV1 **provider_out,
    CettaPrimeTypedFiniteRelationChainOriginV1 **origins_out);

typedef struct {
    const CettaPrimeTypedValueV1 *source;
    const CettaPrimeTypedValueV1 *answer_type;
    const CettaPrimeTypedValueV1 *answer_list;
    const CettaPrimeTypedValueV1 *receipt;
    const CettaPrimeTypedValueV1 *const *answers;
    const size_t *occurrence_indices;
    size_t answer_count;
} CettaPrimeTypedFiniteRelationSearchV1;

/* Materialize one exact dependent source fibre supplied as an ordered
 * occurrence bag.  Every occurrence must carry this exact source and inhabit
 * the relation's corresponding target fibre.  This is the query-local form
 * used by structure-preserving relation liftings; it makes no claim that the
 * supplied fibre enumerates any other source. */
CettaPrimeTypedFiniteRelationBuildV1
cetta_prime_typed_finite_relation_materialize_fibre_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedFiniteRelationOccurrenceInputV1 *occurrences,
    size_t occurrence_count,
    const CettaPrimeTypedValueV1 *list_nil_rule,
    const CettaPrimeTypedValueV1 *list_cons_rule,
    CettaPrimeTypedFiniteRelationSearchV1 *search_out);

/* Materialize the exact source fibre as
 *
 *   List (Sigma target, relation source target).
 *
 * The caller supplies the ordinary typed List constructors.  The result
 * retains a fresh occurrence for every answer while occurrence_indices maps
 * those occurrences back to the provider's ordered bag. */
CettaPrimeTypedFiniteRelationBuildV1
cetta_prime_typed_finite_relation_search_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedFiniteRelationV1 *provider,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *list_nil_rule,
    const CettaPrimeTypedValueV1 *list_cons_rule,
    CettaPrimeTypedFiniteRelationSearchV1 *search_out);

#endif /* CETTA_PRIME_TYPED_FINITE_RELATION_H */
