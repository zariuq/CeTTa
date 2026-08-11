#ifndef CETTA_MATCH_DECISION_H
#define CETTA_MATCH_DECISION_H

#include "atom.h"
#include "space.h"

/*
 * MatchDecision is a derived, revision-pinned candidate selector.  It never
 * binds variables and never establishes a match: its only soundness claim is
 * that every omitted clause is structurally impossible under the lane-owned
 * observation policy.  The ordinary matcher remains semantic authority.
 */
typedef struct CettaMatchDecision CettaMatchDecision;

/* Identity of the generated executable policy.  Backend tuning that
 * preserves that policy does not create a new semantic world. */
uint64_t cetta_match_decision_compiler_identity(void);

typedef enum {
    CETTA_MATCH_DECISION_LINEAR = 0,
    CETTA_MATCH_DECISION_DEEP = 1,
    /* Intersect every observable structural path.  This is useful for
     * finite clause families whose discrimination is distributed across
     * several positions; the exact matcher still verifies every survivor. */
    CETTA_MATCH_DECISION_CONJUNCTIVE = 2,
} CettaMatchDecisionMode;

typedef enum {
    CETTA_MATCH_DECISION_SELECT_ERROR = -1,
    CETTA_MATCH_DECISION_SELECT_INVALIDATED = 0,
    CETTA_MATCH_DECISION_SELECT_READY = 1,
} CettaMatchDecisionSelectState;

typedef enum {
    /* The node may have lane-specific operational meaning.  It contributes
     * no key and the compiler does not inspect its children. */
    CETTA_MATCH_DECISION_PATTERN_OPAQUE = 0,
    /* The node and its descendants have ordinary structural meaning. */
    CETTA_MATCH_DECISION_PATTERN_STRUCTURAL = 1,
} CettaMatchDecisionPatternClass;

typedef struct {
    Atom *pattern;
    uint32_t source_ref;
} CettaMatchDecisionClause;

/* Runtime artifacts are pinned to meaning as well as storage.  A caller may
 * choose its own stable identifiers, but every field participates in exact
 * equality: transport between distinct semantic worlds requires an explicit
 * refinement rather than cache-key coincidence. */
typedef struct {
    uint32_t language_id;
    uint32_t profile_id;
    uint32_t match_policy_id;
    uint32_t demand_policy_id;
    uint64_t presentation_identity;
    uint64_t compiler_identity;
} CettaMatchDecisionSemanticIdentity;

/* Paths are expression-child indices from the complete clause head.  The
 * empty path denotes that head.  Classification is consulted only while the
 * immutable artifact is compiled. */
typedef CettaMatchDecisionPatternClass
(*CettaMatchDecisionClassifyPatternFn)(
    void *context, uint32_t source_ref,
    const CettaExprIndex *path, uint32_t path_len,
    Atom *pattern);

/* A final lane-owned conservative check over a selected candidate.  False
 * must prove structural impossibility; true means possible or unknown. */
typedef bool (*CettaMatchDecisionVerifyCandidateFn)(
    void *context, uint32_t source_ref,
    Atom *pattern, Atom *query);

typedef struct {
    uint64_t compilations;
    uint64_t runs;
    uint64_t clause_inputs;
    uint64_t clause_survivors;
    uint64_t linear_fallbacks;
    uint64_t unavailable_path_fallbacks;
    uint64_t key_index_build_probes;
    uint64_t key_index_select_probes;
    uint64_t generic_key_policy_scans;
} CettaMatchDecisionStats;

/* Compile an ordered clause family.  `max_depth` counts expression edges;
 * zero requests the implementation default.  Pattern pointers remain owned
 * by the pinned Space revision. */
CettaMatchDecision *cetta_match_decision_compile(
    SpaceReadToken read,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    const CettaMatchDecisionClause *clauses,
    size_t clause_count,
    CettaMatchDecisionMode mode,
    uint32_t max_depth,
    CettaMatchDecisionClassifyPatternFn classify,
    void *classify_context);

/* Acquire an additional lifetime owner.  Selection scratch remains mutable,
 * so this is a lifetime lease rather than permission for concurrent selects. */
CettaMatchDecision *cetta_match_decision_retain(
    CettaMatchDecision *decision);

/* Release one lifetime owner. */
void cetta_match_decision_free(CettaMatchDecision *decision);

bool cetta_match_decision_is_current(
    const CettaMatchDecision *decision, const Space *live_space,
    CettaMatchDecisionSemanticIdentity semantic_identity);

/* Select an ordered candidate superset.  Bit i of `ready_arguments` governs
 * child i+1 of the complete call.  A path beneath an unavailable argument or
 * a query variable is unobservable and therefore cannot prune.  The returned
 * source-ref array belongs to `decision` and remains valid until its next
 * selection or destruction. */
CettaMatchDecisionSelectState cetta_match_decision_select(
    CettaMatchDecision *decision, const Space *live_space,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    Atom *query, uint64_t ready_arguments,
    CettaMatchDecisionVerifyCandidateFn verify,
    void *verify_context,
    const uint32_t **source_refs, size_t *source_ref_count);

/* Zero-allocation form for machines that already hold a call in split
 * registers.  `head` is child 0 of the logical call and arguments are its
 * remaining children.  This selector still establishes only a candidate
 * superset; the caller must run its authoritative matcher over every
 * returned source ref. */
CettaMatchDecisionSelectState cetta_match_decision_select_parts(
    CettaMatchDecision *decision, const Space *live_space,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    Atom *head, Atom *const *arguments, size_t arity,
    uint64_t ready_arguments,
    const uint32_t **source_refs, size_t *source_ref_count);

void cetta_match_decision_stats(
    const CettaMatchDecision *decision,
    CettaMatchDecisionStats *stats);

#endif /* CETTA_MATCH_DECISION_H */
