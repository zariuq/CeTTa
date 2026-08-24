#ifndef CETTA_PRIME_TYPED_RELATION_H
#define CETTA_PRIME_TYPED_RELATION_H

#include "prime_typed_flow.h"

/* Native realization of Prime's ordinary proof-relevant relation former:
 *
 *   rel source target evidence :=
 *     Pi (_ : source), Pi (_ : target), evidence
 *
 * The returned value is the unfolded Pi type, not a privileged relation
 * opcode.  This first realization covers current closed typed values; a NULL
 * result leaves the ordinary relational route available. */
CettaPrimeTypedValueV1 *cetta_prime_typed_rel_type_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *evidence_universe);

/* The carried type of a relation value determines its source fibre, target
 * fibre, and evidence universe.  This view performs no checking search: it
 * only exposes the already-established Pi/Pi judgment of a current typed
 * value.  A dependent second domain is not an ordinary `rel` and has no such
 * view. */
typedef struct {
    AtomId source_type_id;
    AtomId target_type_id;
    AtomId evidence_universe_id;
} CettaPrimeTypedRelationViewV1;

bool cetta_prime_typed_relation_v1_view(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *relation,
    CettaPrimeTypedRelationViewV1 *view_out);

/* Form the dependent answer-occurrence type for one typed relation query:
 *
 *   Sigma (target : Target), relation source target
 *
 * This is the language-owned carrier of a proof-relevant engine answer.  It
 * retains the visible target and the exact evidence fibre instead of
 * flattening either to an endpoint pair or a Boolean support fact. */
CettaPrimeTypedValueV1 *cetta_prime_typed_relation_answer_type_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *target_type);

/* Introduce one dependent answer occurrence as `Pair target evidence`.
 * The expected Sigma type is derived from the carried relation and source;
 * the caller cannot supply a disconnected result type. */
CettaPrimeTypedValueV1 *cetta_prime_typed_relation_answer_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *target,
    const CettaPrimeTypedValueV1 *evidence);

/* Derive the result relation type for ordinary proof-relevant composition
 * from the three carrier types and the two premise relations:
 *
 *   rel Source Target
 *     (max (sort Middle) (max evidenceEarlier evidenceLater))
 *
 * Endpoint agreement is read from the carried relation judgments.  The
 * result is a typed universe-level term constructed by the ordinary Pi and
 * cumulative-level formation laws; no synthesis lookup or caller-supplied
 * result type is involved. */
CettaPrimeTypedValueV1 *
cetta_prime_typed_relation_chain_result_type_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *middle_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *earlier_relation,
    const CettaPrimeTypedValueV1 *later_relation);

/* Construct ordinary proof-relevant relational composition:
 *
 *   chain earlier later :=
 *     lam source, lam target,
 *       Sigma middle,
 *         Sigma (_ : earlier source middle), later middle target
 *
 * `result_relation_type` is the already-formed `rel Source Target Evidence`
 * judgment for the result.  This exact native fragment requires its evidence
 * universe to be the structural join of the middle carrier and both premise
 * evidence universes.  A larger cumulative target remains meaningful but is
 * left to the ordinary Prime route until intrinsic promotion is available.
 * The returned value is the unfolded lambda term with the carried relation
 * type; no checker, synthesis service, or private relation opcode is used. */
CettaPrimeTypedValueV1 *cetta_prime_typed_relation_chain_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *result_relation_type,
    const CettaPrimeTypedValueV1 *middle_type,
    const CettaPrimeTypedValueV1 *earlier_relation,
    const CettaPrimeTypedValueV1 *later_relation);

/* Form the ordinary witness-retaining composition type:
 *
 *   chain middle earlier later source target :=
 *     Sigma (mid : middle),
 *       Sigma (_ : earlier source mid), later mid target
 *
 * Each relation's already-established type supplies its endpoint fibres and
 * evidence universe, so a native realization never invents a typing judgment
 * from an endpoint match or accepts a redundant caller-supplied type. */
CettaPrimeTypedValueV1 *cetta_prime_typed_chain_type_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *middle_type,
    const CettaPrimeTypedValueV1 *earlier_relation,
    const CettaPrimeTypedValueV1 *later_relation,
    const CettaPrimeTypedValueV1 *source,
    const CettaPrimeTypedValueV1 *target);

/* Fused native introduction for the nested Sigma above.  Erasure is exactly
 * `Pair middle (Pair earlierEvidence laterEvidence)`; the derivation retains
 * the middle and both evidence occurrences. */
CettaPrimeTypedValueV1 *cetta_prime_typed_chain_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *chain_type,
    const CettaPrimeTypedValueV1 *middle,
    const CettaPrimeTypedValueV1 *earlier_evidence,
    const CettaPrimeTypedValueV1 *later_evidence);

#endif /* CETTA_PRIME_TYPED_RELATION_H */
