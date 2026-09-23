#ifndef CETTA_PRIME_SEMANTICS_H
#define CETTA_PRIME_SEMANTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"
#include "nik_direct_authority.h"
#include "prime_typing_authority.h"
#include "prime_regular_kernel.h"
#include "space.h"

/* Certificate-free NIK face for Prime's native `type:` judgments, including
 * `type:may` and `type:must` over explicit answer producers.  Proof replay
 * through external authorities is the distinct `nik:check` judgment. */
typedef struct {
    const CettaNikDirectAuthorityV1 *authority;
    Atom *(*judge)(Arena *arena, Space *space, Atom *judgment,
                   bool steps_limited, uint64_t steps);
} CettaPrimeTypingDirectServiceV1;

extern const CettaNikDirectAuthorityV1
    cetta_prime_typing_direct_authority_v1;
extern const CettaPrimeTypingDirectServiceV1
    cetta_prime_typing_direct_service_v1;

bool cetta_prime_typing_direct_service_v1_is_valid(
    const CettaPrimeTypingDirectServiceV1 *service);

/* C-side observations for benchmark producers.  The public MeTTa spellings
 * remain `type:check` and `type:of`; this interface adds no executable syntax.
 * One authority result is shared by formation, synthesis, and checking. */

typedef enum {
    CETTA_PRIME_TYPING_ROUTE_NONE = 0,
    CETTA_PRIME_TYPING_ROUTE_SCOPED_REGULAR,
    CETTA_PRIME_TYPING_ROUTE_AUTHORED_REGULAR,
    CETTA_PRIME_TYPING_ROUTE_DECLARED_REGULAR,
    CETTA_PRIME_TYPING_ROUTE_CLOSED_REGULAR,
    CETTA_PRIME_TYPING_ROUTE_AMBIENT_FORMATION,
    CETTA_PRIME_TYPING_ROUTE_LEGACY_HE
} CettaPrimeTypingRouteV1;

typedef struct {
    bool limited;
    uint64_t initial;
    uint64_t spent;
    uint64_t remaining;
    uint64_t formation;
    uint64_t synthesis;
    uint64_t normalization;
    uint64_t checking;
    uint64_t refinement;
    uint64_t evaluation;
} CettaPrimeTypingResourceObservationV1;

/* The result tag determines how to read `payload`: a positive derivation, a
 * checked obstruction, an outside-fragment reason, an incomplete frontier,
 * or an engine-fault diagnostic.  The Atom payload keeps Prime evidence
 * typed without placing a language-specific pointer in the generic NIK ABI. */
typedef struct {
    CettaNikResultV1 result;
    CettaPrimeTypingRouteV1 route;
    Atom *payload;
    /* Checked native object retained by an Established Prime route. Closed
     * routes return intrinsic syntax, scoped routes keep their context, and
     * declared routes quote named declarations from the checked context.
     * This is NULL for non-established results and for legacy routes. A raw
     * boundary can reuse it without repeating elaboration or checking. */
    Atom *canonical_term;
    CettaPrimeTypingResourceObservationV1 resources;
} CettaPrimeTypingAuthorityObservationV1;

typedef struct {
    Atom *term;
    Atom *expected_type;
    bool steps_limited;
    uint64_t steps;
} CettaPrimeTypingCheckingCandidateV1;

typedef struct {
    Atom *type;
    bool steps_limited;
    uint64_t steps;
} CettaPrimeTypingFormationCandidateV1;

typedef struct {
    Atom *term;
    bool steps_limited;
    uint64_t steps;
} CettaPrimeTypingSynthesisCandidateV1;

typedef struct {
    CettaPrimeTypingCheckingCandidateV1 candidate;
    CettaPrimeTypingAuthorityObservationV1 authority;
} CettaPrimeTypingCheckingObservationV1;

typedef struct {
    CettaPrimeTypingFormationCandidateV1 candidate;
    CettaPrimeTypingAuthorityObservationV1 authority;
} CettaPrimeTypingFormationObservationV1;

typedef struct {
    CettaPrimeTypingSynthesisCandidateV1 candidate;
    CettaPrimeTypingAuthorityObservationV1 authority;
} CettaPrimeTypingSynthesisObservationV1;

typedef struct {
    size_t count;
    CettaPrimeTypingCheckingObservationV1 *occurrences;
    size_t established_count;
    size_t refuted_count;
    size_t undetermined_count;
    size_t incomplete_count;
    size_t engine_fault_count;
} CettaPrimeTypingCheckingBagV1;

/* Observe a candidate exactly once.  Evidence and all returned Atom pointers
 * are owned by `arena`.  A zero explicit budget is invalid rather than being
 * reinterpreted as an unbounded request. */
bool cetta_prime_typing_observe_checking_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingCheckingCandidateV1 *candidate,
    CettaPrimeTypingCheckingObservationV1 *observation_out);

bool cetta_prime_typing_observe_formation_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingFormationCandidateV1 *candidate,
    CettaPrimeTypingFormationObservationV1 *observation_out);

bool cetta_prime_typing_observe_synthesis_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingSynthesisCandidateV1 *candidate,
    CettaPrimeTypingSynthesisObservationV1 *observation_out);

/* Observe every occurrence in input order with its own producer budget. */
bool cetta_prime_typing_observe_checking_bag_v1(
    Arena *arena, Space *space,
    const CettaPrimeTypingCheckingCandidateV1 *candidates, size_t count,
    CettaPrimeTypingCheckingBagV1 *bag_out);

bool cetta_prime_typing_checking_bag_v1_is_decision_complete(
    const CettaPrimeTypingCheckingBagV1 *bag);

/* Lossy public readout of semantic outcomes.  Engine faults have no status
 * readout and remain exclusively in the outer transport result. */
bool cetta_prime_typing_authority_observation_v1_status(
    const CettaPrimeTypingAuthorityObservationV1 *observation,
    CettaNikStatusV1 *status_out);

/* Exact multiset equality of term/type occurrences.  Resource limits are
 * experiment configuration, not candidate identity. */
bool cetta_prime_typing_checking_candidate_bag_equal_v1(
    Arena *scratch,
    const CettaPrimeTypingCheckingCandidateV1 *left, size_t left_count,
    const CettaPrimeTypingCheckingCandidateV1 *right, size_t right_count);

/* Returns NULL when the claim is not one of Prime's native type judgments. */
Atom *prime_semantics_judge_typing_direct(
    Arena *arena, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps);

/* The same judgment authority with its actual resource account and retained
 * native term returned to a composing caller. No judgment is replayed. The
 * optional term output follows AuthorityObservation's established-only rule. */
Atom *prime_semantics_judge_typing_accounted(
    Arena *arena, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps,
    CettaPrimeTypingResourceObservationV1 *resources_out,
    Atom **canonical_term_out);

/* Check already lowered, closed syntax against the current declaration
 * schemas. Uninstantiated DeclConst occurrences receive fresh universe
 * arguments, exactly as in authored checking. Lambda annotations are kept.
 * A caller may supply a private declaration overlay, without new equations. */
CettaPrimeRegularKernelResult prime_semantics_check_declared_intrinsic_v1(
    Arena *arena, Space *space, Atom *term, Atom *expected,
    CettaPrimeRegularKernelBudget *budget);

/* Collect the current logical view's type:rule atoms in kernel spelling.
 * Overlay snapshots and local removals are respected. This reads declarations;
 * it does not validate their typing or termination. NULL denotes no rules. */
Atom *prime_semantics_kernel_rules(Arena *arena, Space *space);

/* Reduce one saturated call by the space's admitted `type:rule` clauses,
 * or project `fst`/`snd` of a pair, using the kernel normalizer. The call
 * must already have passed telescope checking. NULL leaves the call as it
 * is: the head has no computation, the budget ran out, or the normal form
 * has no surface spelling. Exhaustion is not a value and not a refutation. */
Atom *prime_semantics_reduce_covered_call(
    Arena *arena, Space *space, Atom *call, int fuel);

/* C-internal entry point for proof replay through a named NIK authority.
 * The judgment is exactly `(nik:check authority claim proof)`. */
Atom *prime_semantics_check_nik_direct(
    Arena *arena, Space *space, Atom *judgment,
    bool steps_limited, uint64_t steps);

/* MeTTa-Prime is a language package, not an HE profile.  The weak hooks keep
 * standalone evaluator test binaries linkable when this module is omitted. */
Atom *prime_semantics_dispatch(Arena *a, Atom *head, Atom **args,
                               uint32_t nargs) __attribute__((weak));
bool prime_semantics_is_op(const char *name) __attribute__((weak));
bool prime_semantics_is_op_id(SymbolId id) __attribute__((weak));
bool prime_semantics_op_data_arg(const char *name, uint32_t arg_index)
    __attribute__((weak));

/* Internal package boundary used by the Prime gate.  Construction returns
 * NULL if the resulting schema does not validate. */
Atom *prime_semantics_package_atom(Arena *a);
bool prime_semantics_validate_package(Atom *package);
/* Neutral named-telescope elaboration.  A non-NULL result is a closed
 * canonical Pi/Var ABT, not evidence that the source type is well formed. */
Atom *prime_semantics_canonicalize_type(Arena *a, Atom *type);
bool prime_semantics_replay_conversion_certificate(
    Arena *a, Space *space, Atom *certificate, bool *equal_out);

#endif /* CETTA_PRIME_SEMANTICS_H */
