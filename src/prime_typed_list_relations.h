#ifndef CETTA_PRIME_TYPED_LIST_RELATIONS_H
#define CETTA_PRIME_TYPED_LIST_RELATIONS_H

#include "prime_typed_flow.h"

/* Ordinary proof-relevant construction for
 * `rel:all element predicate (list:nil element)`. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_all_nil_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *predicate);

/* Construct one `rel:all` cons proof from the typed head proof and recursive
 * tail proof.  Both evidence occurrences remain explicit premises. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_all_cons_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *cons_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *predicate,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail,
    const CettaPrimeTypedValueV1 *head_evidence,
    const CettaPrimeTypedValueV1 *tail_evidence);

/* Proof that the head occurrence is a member of this exact List spine. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_member_here_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *here_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail);

/* Lift a tail-membership occurrence through one retained head. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_member_there_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *there_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail,
    const CettaPrimeTypedValueV1 *member,
    const CettaPrimeTypedValueV1 *tail_evidence);

/* Construct an answer at the current suffix.  The predicate evidence remains
 * an explicit proof occurrence. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_any_here_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *here_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *predicate,
    const CettaPrimeTypedValueV1 *values,
    const CettaPrimeTypedValueV1 *answer,
    const CettaPrimeTypedValueV1 *evidence);

/* Retain one skipped head and lift an exact recursive answer occurrence. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_any_there_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *there_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *predicate,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail,
    const CettaPrimeTypedValueV1 *answer,
    const CettaPrimeTypedValueV1 *evidence);

/* Construct the nil branch of the ordinary proof-relevant List case family. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_case_nil_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *nil_case,
    const CettaPrimeTypedValueV1 *cons_case,
    const CettaPrimeTypedValueV1 *answer,
    const CettaPrimeTypedValueV1 *evidence);

/* Construct the cons branch while retaining its head, tail, answer, and
 * exact branch-evidence occurrence. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_case_cons_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *cons_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *nil_case,
    const CettaPrimeTypedValueV1 *cons_case,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail,
    const CettaPrimeTypedValueV1 *answer,
    const CettaPrimeTypedValueV1 *evidence);

/* Ordinary proof-relevant construction for the empty `rel:fold` fibre. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_fold_nil_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *accumulator_type,
    const CettaPrimeTypedValueV1 *step,
    const CettaPrimeTypedValueV1 *before);

/* Construct one `rel:fold` cons proof.  The intermediate accumulator, step
 * evidence, and recursive tail evidence are retained rather than reduced to
 * the endpoint pair. */
CettaPrimeTypedValueV1 *cetta_prime_typed_list_fold_cons_v1(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *cons_rule,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *accumulator_type,
    const CettaPrimeTypedValueV1 *step,
    const CettaPrimeTypedValueV1 *before,
    const CettaPrimeTypedValueV1 *head,
    const CettaPrimeTypedValueV1 *tail,
    const CettaPrimeTypedValueV1 *next,
    const CettaPrimeTypedValueV1 *after,
    const CettaPrimeTypedValueV1 *step_evidence,
    const CettaPrimeTypedValueV1 *tail_evidence);

#endif /* CETTA_PRIME_TYPED_LIST_RELATIONS_H */
