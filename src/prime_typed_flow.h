#ifndef CETTA_PRIME_TYPED_FLOW_H
#define CETTA_PRIME_TYPED_FLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"
#include "space.h"
#include "term_universe.h"

/* Prime-owned typed values hosted by NIK's logic-polymorphic authority waist.
 * The structure is private: typed constructors consume and produce the term
 * and its judgment together, so callers cannot assemble a term/type pair. */
typedef struct CettaPrimeTypedValueV1 CettaPrimeTypedValueV1;

typedef enum {
    CETTA_PRIME_TYPED_VALUE_BOUNDARY_IMPORT_V1 = 1,
    CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1 = 2,
} CettaPrimeTypedValueConstructionV1;

typedef struct {
    uint64_t universe_instance_id;
    uint64_t universe_storage_epoch;
    AtomId context_id;
    AtomId term_id;
    AtomId type_id;
    uint64_t occurrence_identity;
    AtomId rule_id;
    CettaPrimeTypedValueConstructionV1 construction;
} CettaPrimeTypedValueMetadataV1;

/* One node in the occurrence-retaining derivation program carried by a typed
 * value.  Premises name occurrences, not term ids: two applications with the
 * same erased syntax therefore remain distinct proof programs. */
typedef struct {
    uint64_t occurrence_identity;
    AtomId rule_id;
    CettaPrimeTypedValueConstructionV1 construction;
    size_t premise_offset;
    size_t premise_count;
    size_t witness_offset;
    size_t witness_count;
} CettaPrimeTypedDerivationNodeV1;

typedef struct {
    uint64_t root_occurrence_identity;
    const CettaPrimeTypedDerivationNodeV1 *nodes;
    size_t node_count;
    const uint64_t *premise_occurrences;
    size_t premise_occurrence_count;
    const AtomId *witness_ids;
    size_t witness_count;
} CettaPrimeTypedDerivationViewV1;

/* Generic indexed-family view.  The family is language-owned; NIK sees the
 * resulting opaque typed value and its authority/currentness, never a global
 * `hyp` opcode. */
typedef struct {
    AtomId family_head_id;
    const AtomId *parameter_ids;
    size_t parameter_count;
    const AtomId *index_ids;
    size_t index_count;
} CettaPrimeTypedIndexedViewV1;

/* Prime's identity introduction rule as an admitted-rule flow:
 *
 *     value : A
 *   ----------------
 *   refl value : Id A value value
 *
 * This constructor derives the result judgment from the typed premise.  It
 * performs no synthesis, checking, conversion, certificate replay, or source
 * lookup. */
CettaPrimeTypedValueV1 *cetta_prime_typed_value_refl_v1(
    Arena *owner, Space *space, const CettaPrimeTypedValueV1 *value);

/* Prime's ordinary dependent function elimination rule:
 *
 *     function : Pi domain codomain    argument : domain
 *   ----------------------------------------------------
 *     App function argument : codomain[argument / idx 0]
 *
 * The domain must already be carried exactly by `argument`; conversion and
 * cumulative promotion are separate typed operations.  No checker, synthesis
 * service, declaration lookup, or admission replay occurs here. */
CettaPrimeTypedValueV1 *cetta_prime_typed_value_apply_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *function,
    const CettaPrimeTypedValueV1 *argument);

/* Dependent function elimination after explicit judgmental conversion of the
 * argument's carried type to the function domain.  Exact application remains
 * the primitive operation above; this composition retains a
 * `conv:judgmental` node whenever conversion is actually needed.  A coverage
 * failure or unequal types simply produces no typed value. */
CettaPrimeTypedValueV1 *cetta_prime_typed_value_apply_converting_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *function,
    const CettaPrimeTypedValueV1 *argument);

/* Repeated dependent elimination through one typed application spine.  Every
 * intermediate value is constructed from the preceding judgment and the
 * next typed argument; no raw term crosses the boundary between steps. */
CettaPrimeTypedValueV1 *cetta_prime_typed_value_apply_many_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *function,
    const CettaPrimeTypedValueV1 *const *arguments,
    size_t argument_count);

/* Transport a value along the intrinsic beta-conversion rule.  `target_type`
 * is itself a current typed type.  Weak-head beta normalization of its term
 * and the value's carried type must agree, with at least one real beta step;
 * the returned value retains both judgments and the two normal forms.  No
 * checker, synthesis service, or declaration lookup is invoked. */
CettaPrimeTypedValueV1 *cetta_prime_typed_value_convert_beta_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *value,
    const CettaPrimeTypedValueV1 *target_type);

bool cetta_prime_typed_value_v1_is_current(
    const CettaPrimeTypedValueV1 *value, const Space *space);

bool cetta_prime_typed_value_v1_metadata(
    const CettaPrimeTypedValueV1 *value,
    CettaPrimeTypedValueMetadataV1 *metadata_out);

bool cetta_prime_typed_value_v1_derivation(
    const CettaPrimeTypedValueV1 *value,
    CettaPrimeTypedDerivationViewV1 *derivation_out);

bool cetta_prime_typed_value_v1_indexed_view(
    const CettaPrimeTypedValueV1 *value,
    CettaPrimeTypedIndexedViewV1 *indexed_out);

/* Erasure is observation, not authorization.  It remains available for a
 * stale value while the same TermUniverse storage generation survives, so a
 * caller can deoptimize to the original Prime term. */
bool cetta_prime_typed_value_v1_erase(
    const CettaPrimeTypedValueV1 *value,
    const TermUniverse *live_universe, Arena *destination,
    Atom **term_out, Atom **type_out);

#endif /* CETTA_PRIME_TYPED_FLOW_H */
