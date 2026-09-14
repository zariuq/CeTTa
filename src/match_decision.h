#ifndef CETTA_MATCH_DECISION_H
#define CETTA_MATCH_DECISION_H

#include "atom.h"
#include "space.h"
#include "gslt_term_view_v1.h"

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

/* Physical realization choices used only for differential qualification.
 * The zero value is the optimized realization.  These fields cannot change
 * candidate meaning: the ordinary matcher remains semantic authority over
 * every survivor.  A compiled decision retains its realization, so selection
 * never consults ambient process state. */
typedef struct {
    bool use_direct_prefix_observation;
    bool use_eager_prefix_observation;
    bool use_direct_equality_observation;
} CettaMatchDecisionRealization;

/* Read the process-level qualification controls once, at an explicit artifact
 * construction boundary.  Tests and embedded clients should normally pass a
 * literal realization instead. */
CettaMatchDecisionRealization
cetta_match_decision_realization_from_process(void);

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
    uint64_t equality_checks;
    uint64_t equality_refutations;
    uint64_t equality_observation_reads;
    uint64_t equality_observation_fallbacks;
    uint64_t equality_observation_direct_edges;
    uint64_t equality_observation_graph_edges;
    uint64_t prefix_observation_build_attempts;
    uint64_t prefix_observation_build_commits;
    uint64_t prefix_observation_build_declines;
    uint64_t prefix_observation_runs;
    uint64_t prefix_observation_node_visits;
    uint64_t prefix_observation_absorbed_suffixes;
    uint64_t prefix_observation_skipped_edges;
    uint64_t prefix_observation_direct_edges;
    uint64_t prefix_observation_trie_edges;
} CettaMatchDecisionStats;

/* Compile an ordered clause family against a complete Space read.  `max_depth`
 * counts expression edges; zero requests the implementation default.  Pattern
 * pointers remain owned by the pinned Space revision. */
CettaMatchDecision *cetta_match_decision_compile(
    SpaceReadToken read,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    const CettaMatchDecisionClause *clauses,
    size_t clause_count,
    CettaMatchDecisionMode mode,
    uint32_t max_depth,
    CettaMatchDecisionRealization realization,
    CettaMatchDecisionClassifyPatternFn classify,
    void *classify_context);

/* Compile an ordered clause family whose complete construction depends only
 * on the Space's ordered equation projection and the supplied semantic
 * identity.  This is narrower than `cetta_match_decision_compile`: callers
 * must not use it for a selector that reads ordinary data atoms.  It remains
 * current across data-only mutations and is rejected by every equation or
 * opaque mutation. */
CettaMatchDecision *cetta_match_decision_compile_equation_projection(
    SpaceEquationToken equations,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    const CettaMatchDecisionClause *clauses,
    size_t clause_count,
    CettaMatchDecisionMode mode,
    uint32_t max_depth,
    CettaMatchDecisionRealization realization,
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

/* A synchronous read of split constructor coordinates. Each coordinate may
 * carry a different substitution scope; the observer resolves only demanded
 * roots. All cursor storage and environments must outlive this selection
 * and remain stable until it returns. No cursor is retained by a result. */
typedef struct {
    CettaGsltTermCursorV1 head;
    const CettaGsltTermCursorV1 *arguments;
    size_t arity;
    CettaGsltTermCursorObserverV1 observer;
} CettaMatchDecisionQueryViewV1;

/* Like the materialized verifier, this callback may only reject candidates
 * proved impossible. It must preserve the observer's stable-read contract. */
typedef bool (*CettaMatchDecisionVerifyViewCandidateFnV1)(
    void *context, uint32_t source_ref, Atom *pattern,
    const CettaMatchDecisionQueryViewV1 *query);

CettaMatchDecisionSelectState cetta_match_decision_select_view_v1(
    CettaMatchDecision *decision, const Space *live_space,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    const CettaMatchDecisionQueryViewV1 *query, uint64_t ready_arguments,
    CettaMatchDecisionVerifyViewCandidateFnV1 verify, void *verify_context,
    const uint32_t **source_refs, size_t *source_ref_count);

void cetta_match_decision_stats(
    const CettaMatchDecision *decision,
    CettaMatchDecisionStats *stats);

#endif /* CETTA_MATCH_DECISION_H */
