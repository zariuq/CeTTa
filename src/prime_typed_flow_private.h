#ifndef CETTA_PRIME_TYPED_FLOW_PRIVATE_H
#define CETTA_PRIME_TYPED_FLOW_PRIVATE_H

#include "nik_direct_authority.h"
#include "prime_typed_flow.h"

#define CETTA_PRIME_TYPED_FLOW_POLICY_V1 UINT32_C(0x50544631)

struct CettaPrimeTypedValueV1 {
    const TermUniverse *universe;
    uint64_t universe_instance_id;
    uint64_t universe_storage_epoch;
    AtomId context_id;
    AtomId term_id;
    AtomId type_id;
    CettaNikDirectAuthorityTokenV1 authority_token;
    CettaPrimeTypedValueConstructionV1 construction;
    uint64_t occurrence_identity;
    AtomId rule_id;
    CettaPrimeTypedDerivationNodeV1 *derivation_nodes;
    size_t derivation_node_count;
    uint64_t *premise_occurrences;
    size_t premise_occurrence_count;
    AtomId *witness_ids;
    size_t witness_count;
    AtomId family_head_id;
    AtomId *parameter_ids;
    size_t parameter_count;
    AtomId *index_ids;
    size_t index_count;
};

typedef struct {
    const char *rule_name;
    CettaPrimeTypedValueConstructionV1 construction;
    const CettaPrimeTypedValueV1 *const *premises;
    size_t premise_count;
    const AtomId *witness_ids;
    size_t witness_count;
    AtomId family_head_id;
    const AtomId *parameter_ids;
    size_t parameter_count;
    const AtomId *index_ids;
    size_t index_count;
} CettaPrimeTypedValueBuildPrivateV1;

/* Shared only by Prime's intrinsic constructor implementations and the
 * explicit raw-boundary adapter. */
CettaPrimeTypedValueV1 *cetta_prime_typed_value_allocate_private_v1(
    Arena *owner, TermUniverse *universe, AtomId context_id,
    AtomId term_id, AtomId type_id,
    const CettaNikDirectAuthorityTokenV1 *authority_token,
    const CettaPrimeTypedValueBuildPrivateV1 *build);

/* Retain one language-licensed computation step.  The language adapter owns
 * recognition of the redex and construction of the reduct; the shared typed
 * flow requires both sides to carry the same current judgment and preserves
 * both derivations in the resulting receipt. */
CettaPrimeTypedValueV1 *cetta_prime_typed_value_compute_private_v1(
    Arena *owner, Space *space, const char *rule_name,
    const CettaPrimeTypedValueV1 *redex,
    const CettaPrimeTypedValueV1 *reduct,
    const AtomId *witness_ids, size_t witness_count);

bool cetta_prime_typed_values_cohere_private_v1(
    const Space *space,
    const CettaPrimeTypedValueV1 *const *values,
    size_t value_count);

/* Retain an existing current judgment in another owner without constructing
 * a new proof occurrence.  This is an ownership transfer for an opaque typed
 * value, not a replay of checking or an application of a typing rule. */
CettaPrimeTypedValueV1 *cetta_prime_typed_value_retain_private_v1(
    Arena *owner, const Space *space,
    const CettaPrimeTypedValueV1 *value);

/* Shared intrinsic `App` spine operations.  Language-owned constructors and
 * eliminators use these to recognize or construct their exact ordinary Prime
 * terms without introducing private syntax encodings. */
bool cetta_prime_typed_application_spine_private_v1(
    Atom *term, const char *head_name,
    Atom **arguments, size_t argument_count);

Atom *cetta_prime_typed_application_term_private_v1(
    Arena *owner, Atom *function,
    Atom *const *arguments, size_t argument_count);

/* Recognize the exact intrinsic application spine carried by a typed value.
 * Constructor adapters use this before exposing a family view, so an
 * arbitrary same-result function cannot masquerade as the named constructor. */
bool cetta_prime_typed_value_has_application_head_private_v1(
    Arena *scratch, const Space *space,
    const CettaPrimeTypedValueV1 *value,
    const char *head_name, size_t argument_count);

/* Expose an already-constructed family application through the common
 * indexed view.  `value` must have type
 *
 *   family parameter... index...
 *
 * with exactly the supplied arities.  This records no new judgment: it only
 * retains the application spine already carried by the result type. */
CettaPrimeTypedValueV1 *
cetta_prime_typed_value_attach_indexed_application_private_v1(
    Arena *owner, Space *space, CettaPrimeTypedValueV1 *value,
    const char *family_name,
    size_t parameter_count, size_t index_count);

#endif /* CETTA_PRIME_TYPED_FLOW_PRIVATE_H */
