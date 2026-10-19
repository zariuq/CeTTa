#ifndef CETTA_WEIGHT_LEDGER_H
#define CETTA_WEIGHT_LEDGER_H

#include "atom.h"

/* Ordered semantic factors of one physical execution.  Equal coefficients
 * are not equal occurrences.  The dependency key is the captured activation
 * identity; it survives promotion together with the factor identity. */
typedef struct CettaWeightFactor {
    const struct CettaWeightFactor *previous;
    uint64_t occurrence;
    uint64_t dependency;
    Atom *value;
} CettaWeightFactor;

/* Handling a contribution does not undo its production or its shared lazy
 * choice. This persistent, branch-local map records the observation that
 * claimed it. An older cached world cannot make a claimed factor unhandled. */
typedef struct CettaWeightClaim {
    const struct CettaWeightClaim *previous;
    uint64_t occurrence;
    uint64_t handler;
} CettaWeightClaim;

typedef struct {
    const CettaWeightFactor *last;
    size_t length;
    Arena *owner;
    const CettaWeightClaim *claims;
    size_t claim_count;
} CettaWeightLedger;

/* A borrowed, oldest-first view.  The returned array is caller-owned;
 * factor nodes and their values retain the ledger's arena lifetime. */
const CettaWeightFactor **cetta_weight_ledger_ordered(
    const CettaWeightLedger *ledger);
bool cetta_weight_ledger_equal(const CettaWeightLedger *left,
                               const CettaWeightLedger *right);

/* Copy the complete path and coefficient values when ownership changes. */
bool cetta_weight_ledger_promote(Arena *owner, CettaWeightLedger *ledger);
/* Preserve factor and claim order/identities while sharing coefficient Atom
 * images with the enclosing preparation. Failure does not publish a ledger;
 * the caller owns rollback of the destination and copy session. */
bool cetta_weight_ledger_promote_with_session(
    Arena *owner, AtomDeepCopySession *copy_session, CettaWeightLedger *ledger);
/* Owned checkpoint transport also retains physical sharing of factor/claim
 * paths across carriers, through the same typed graph as coefficient Atoms.
 * Owners remain live/quiescent until preparation ends; mapped identity regions
 * cannot own a copied writable path. Refusal does not publish a ledger and
 * requires discarding the unpublished destination/session. */
bool cetta_weight_ledger_copy_with_session(
    Arena *owner, AtomDeepCopySession *copy_session, CettaWeightLedger *ledger);

/* Append a fresh physical factor.  A repeated occurrence is refused, even
 * if its value agrees: shared execution is handled by qualified merge. */
bool cetta_weight_ledger_append(Arena *owner, const CettaWeightLedger *base,
    uint64_t occurrence, uint64_t dependency, Atom *value,
    CettaWeightLedger *out);

/* Zero means this production has not been claimed by an observation. */
uint64_t cetta_weight_ledger_handler(const CettaWeightLedger *ledger,
                                     uint64_t occurrence);

/* The captured ordered prefix and every inherited claim remain present.
 * This is authority validation, independently of any new claim allocation. */
bool cetta_weight_ledger_extends(const CettaWeightLedger *base,
                                  const CettaWeightLedger *prefix);

/* A competing handler is an ownership refusal, not semantic refutation of
 * the two coefficient values or of the computation carrying them. */
bool cetta_weight_ledger_claims_conflict(const CettaWeightLedger *left,
                                         const CettaWeightLedger *right);

/* A read handles the unclaimed suffix produced after its captured prefix.
 * The returned world retains every production and claims that suffix before
 * any interpretation callback runs. `selected` contains exactly the ordered
 * factors to interpret. Later callback factors are outside this snapshot.
 * Its immutable factor path may share storage with the returned world;
 * both outputs use the supplied arena lifetime, with independent claim maps.
 * Refusal leaves both outputs unchanged. Handler IDs must be fresh within
 * the same occurrence namespace; sharing a value is not sharing an owner. */
bool cetta_weight_ledger_handle(Arena *owner, const CettaWeightLedger *base,
    const CettaWeightLedger *prefix, uint64_t handler,
    CettaWeightLedger *out, CettaWeightLedger *selected);

/* Merge only a shared ordered prefix and disjoint fresh suffixes.  The
 * caller supplies logical left-before-right order, not worker completion
 * order.  Reused prefix IDs must agree in value and dependency.  Any shared
 * ID after the common prefix is an incompatible ordering and is refused.
 * Claims merge only when a shared occurrence has the same handler. Two
 * incomparable observations cannot silently claim the same production.
 * Refusal leaves destination unchanged. */
bool cetta_weight_ledger_merge(CettaWeightLedger *destination,
                               const CettaWeightLedger *right);


#include "checkpoint_compare.h"
bool cetta_weight_ledger_compare(const CettaWeightLedger *, const CettaWeightLedger *, const CettaCheckpointCompare *);
#endif
