#ifndef CETTA_SEARCH_MACHINE_H
#define CETTA_SEARCH_MACHINE_H

#include "atom.h"
#include "match.h"

/*
 * Context-role seam for the post-SymbolId runtime split.
 *
 * Positive example:
 *   - SearchContext owns branch-local speculative bindings, rollback marks,
 *     and scratch allocation.
 *
 * Negative example:
 *   - Repeating ad hoc `bindings_builder_save/rollback` and `arena_mark/reset`
 *     pairs across evaluator hot paths.
 */

typedef struct {
    uint32_t bindings_mark;
    ArenaMark scratch_mark;
    bool has_scratch_mark;
} ChoicePoint;

typedef struct {
    Arena *scratch_arena;
    BindingsBuilder bindings;
    Arena owned_scratch_arena;
    bool owns_scratch_arena;
} SearchContext;

/*
 * Algebraic branch-storage seam shared by evaluator backends.
 *
 * Capacity describes ownership, not traversal order.  A depth-first,
 * breadth-first, valuation-driven, or learned controller may request any
 * storage mode; the weakest component of the concrete evaluator state decides
 * which mode is physically admissible.
 *
 * Positive example: immutable syntax plus independently owned ABT state may
 * admit an owned frontier.  Negative example: a live transaction with no
 * snapshot operation remains inline-only even when the controller asks for
 * breadth-first order.
 */
typedef enum {
    CETTA_BRANCH_CAPTURE_INLINE_ONLY = 0,
    CETTA_BRANCH_CAPTURE_ONE_SHOT,
    CETTA_BRANCH_CAPTURE_MULTI_SHOT,
} CettaBranchCaptureCapacity;

typedef enum {
    CETTA_BRANCH_STORAGE_INLINE = 0,
    CETTA_BRANCH_STORAGE_EXCLUSIVE_ONE_SHOT,
    CETTA_BRANCH_STORAGE_OWNED_MULTI_SHOT,
} CettaBranchStorageMode;

/* Controller policy names semantic occurrence order, not an evaluator
 * dialect.  Ordered streams, prefixes, and bounded observations may therefore
 * distinguish policies.  Completed runs agree across controllers only after
 * an order-quotient readout such as an occurrence bag; equal occurrences keep
 * their multiplicity.  This enum contains only representation-level built-ins.
 * Valuation, portfolio, and learned scorers are controller data; they do not
 * create profile names or one enum value per scoring discipline. */
typedef enum {
    CETTA_SEARCH_CONTROLLER_INLINE_DEPTH_FIRST = 0,
    CETTA_SEARCH_CONTROLLER_FIFO,
    CETTA_SEARCH_CONTROLLER_RATIO,
} CettaSearchControllerPolicy;

const char *cetta_search_controller_policy_name(
    CettaSearchControllerPolicy policy);
bool cetta_search_controller_policy_parse(
    const char *name, CettaSearchControllerPolicy *policy);

/* Observation-indexed default control.  This is a scope-entry plan, not a
 * traversal-mode alias: CONTROLLED leaves the concrete frontier discipline
 * open, and BULK is available only with a separate serializability license. */
typedef enum {
    CETTA_OBSERVATION_FIRST = 0,
    CETTA_OBSERVATION_FINITE_PREFIX,
    CETTA_OBSERVATION_COMPLETE_BAG,
    CETTA_OBSERVATION_ORDERED_STREAM,
    CETTA_OBSERVATION_UNDETERMINED,
} CettaObservationCompletion;

typedef struct {
    CettaObservationCompletion completion;
    uint64_t prefix_limit;
} CettaObservationDemand;

typedef enum {
    CETTA_CONTROL_BATCH_SINGLETON_ONLY = 0,
    CETTA_CONTROL_BATCH_SERIALIZABLE,
} CettaControlBatchAuthority;

typedef enum {
    CETTA_CONTROL_BRANCH_GENERAL = 0,
    CETTA_CONTROL_BRANCH_SINGLE_PATH,
} CettaControlBranchAuthority;

typedef enum {
    CETTA_CONTROL_ACTIVATE_NONE = 0,
    CETTA_CONTROL_ACTIVATE_SINGLE_PATH,
    CETTA_CONTROL_ACTIVATE_CONTROLLED,
    CETTA_CONTROL_ACTIVATE_BULK,
} CettaControlActivation;

typedef struct {
    CettaObservationCompletion readout;
    CettaControlActivation activation;
    uint64_t prefix_limit;
} CettaControlPlan;

/* Derive the demand-indexed plan licensed by separate branching and batching
 * authorities.  First-answer demand stops after a witness; it does not grant
 * single-path execution before one is found.  Invalid inputs leave PLAN
 * unchanged.  A graded guard may later refine a plan, but it cannot
 * manufacture either authority. */
bool cetta_control_plan_derive(
    CettaObservationDemand demand,
    CettaControlBranchAuthority branch_authority,
    CettaControlBatchAuthority batch_authority,
    CettaControlPlan *plan);

/* True exactly when OBSERVED occurrences discharge the plan's finite
 * observation demand.  Complete bags, ordered streams, and undetermined
 * readouts require an exhaustion/closure judgment instead and therefore
 * never become satisfied from a finite count alone. */
bool cetta_control_plan_observation_satisfied(
    const CettaControlPlan *plan, uint64_t observed);

/* Selection discipline as checked data: a deterministic tick automaton whose
 * current state names which end of the controller-neutral occurrence store
 * is selected next.  FIFO and depth-biased ratio disciplines are instances
 * of one mechanism, not separate mechanisms.
 *
 * Admission checks the lane-recurrence property on the automaton itself:
 * the unique cycle reachable from the current state must visit an
 * OLDEST-lane state.  Composing that recurrence with the insertion-ordered
 * occurrence store is the separate persistent-selection duty.  A pure
 * NEWEST cycle (bare LIFO) is representable but fails the recurrence check:
 * it may be requested as inline descent, never admitted where recurrent
 * oldest selection is required.
 *
 * Positive example: ratio with a newest share of 3 cycles through
 * NEWEST,NEWEST,NEWEST,OLDEST and passes the check.  Negative example: the
 * one-state NEWEST automaton fails it. */
#define CETTA_SELECTION_AUTOMATON_STATE_CAPACITY 64u

typedef enum {
    CETTA_SELECTION_LANE_OLDEST = 0,
    CETTA_SELECTION_LANE_NEWEST,
} CettaSelectionLane;

/* A schedule duty is explicit scope data, not an implicit property of an
 * observation or a controller name.  NONE permits deliberately incomplete
 * disciplines.  RECURRENT_OLDEST requires a checked automaton whose reachable
 * cycle revisits the oldest lane. */
typedef enum {
    CETTA_SELECTION_DUTY_NONE = 0,
    CETTA_SELECTION_DUTY_RECURRENT_OLDEST,
} CettaSelectionDuty;

typedef struct {
    uint32_t state_count;
    uint32_t state;
    uint8_t lane[CETTA_SELECTION_AUTOMATON_STATE_CAPACITY];
    uint32_t next[CETTA_SELECTION_AUTOMATON_STATE_CAPACITY];
} CettaSelectionAutomaton;

bool cetta_selection_automaton_fifo(
    CettaSelectionAutomaton *automaton);
bool cetta_selection_automaton_lifo(
    CettaSelectionAutomaton *automaton);
/* NEWEST_SHARE deep selections followed by one OLDEST selection, cyclic.
 * A zero share degenerates to FIFO. */
bool cetta_selection_automaton_ratio(
    uint32_t newest_share,
    CettaSelectionAutomaton *automaton);
bool cetta_selection_automaton_valid(
    const CettaSelectionAutomaton *automaton);
/* The lane-recurrence check described above.  Structural: it inspects only
 * the automaton data, never an occurrence-store run. */
bool cetta_selection_automaton_has_recurrent_oldest(
    const CettaSelectionAutomaton *automaton);
/* Advance one tick and select an index into a live frontier of LENGTH
 * occurrences: 0 for the oldest, LENGTH-1 for the newest.  Fails only on an
 * invalid automaton or an empty frontier, leaving INDEX unchanged.  A
 * singleton frontier is controller-free: it selects index 0 without
 * advancing the automaton phase. */
bool cetta_selection_automaton_select(
    CettaSelectionAutomaton *automaton,
    size_t length,
    size_t *index);
/* Parse "fifo", "lifo", "ratio", or "ratio:<newest-share>". */
bool cetta_selection_automaton_parse(
    const char *name,
    CettaSelectionAutomaton *automaton);

CettaBranchCaptureCapacity cetta_branch_capture_weakest(
    CettaBranchCaptureCapacity first,
    CettaBranchCaptureCapacity second);
bool cetta_branch_capture_admits(
    CettaBranchCaptureCapacity available,
    CettaBranchStorageMode requested);
/* Admit only the requested physical mode.  Refusal leaves ADMITTED unchanged;
 * storage capacity never selects a traversal policy or substitute mode. */
bool cetta_branch_storage_admit_exact(
    CettaBranchCaptureCapacity available,
    CettaBranchStorageMode requested,
    CettaBranchStorageMode *admitted);

#define CETTA_BRANCH_AUTHORITY_TOKEN_WORD_CAPACITY 8u
typedef struct {
    uint64_t words[CETTA_BRANCH_AUTHORITY_TOKEN_WORD_CAPACITY];
    uint32_t length;
} CettaBranchAuthorityToken;

bool cetta_branch_authority_token_equal(
    const CettaBranchAuthorityToken *left,
    const CettaBranchAuthorityToken *right);

/* Admission is sampled before and after capture.  AVAILABLE supplies the
 * host-owned component's capacity and exact mutable authority.  DEFERRED
 * makes no portability claim; INVALIDATED reports that no stable judgment
 * could be made for the current revision. */
typedef enum {
    CETTA_BRANCH_ADMISSION_DEFERRED = 0,
    CETTA_BRANCH_ADMISSION_AVAILABLE,
    CETTA_BRANCH_ADMISSION_INVALIDATED,
} CettaBranchAdmissionStatus;

typedef struct {
    CettaBranchAdmissionStatus status;
    CettaBranchCaptureCapacity capacity;
    CettaBranchAuthorityToken authority;
} CettaBranchAdmission;

typedef enum {
    CETTA_CONTINUATION_READY = 0,
    CETTA_CONTINUATION_DEFERRED,
    CETTA_CONTINUATION_UNSUPPORTED,
    CETTA_CONTINUATION_INVALIDATED,
    CETTA_CONTINUATION_CAPACITY,
} CettaContinuationStatus;

typedef struct CettaContinuationProvider CettaContinuationProvider;

/* A provider-erased machine endpoint.  PeTTa relational control, Prime's
 * evaluation stack, and HE outcome continuations may provide different
 * adapters without sharing their physical state layout. */
typedef struct {
    void *machine;
    const CettaContinuationProvider *provider;
} CettaContinuationMachine;

/* An owned delimited continuation plus the evaluator state required for
 * resumption.  The provider remains visible only as an identity/operation
 * table; a controller treats the payload as opaque.  The handle is move-only
 * by convention and one successful restore consumes it.  Multi-shot capacity
 * means that independently captured images may coexist; it does not make one
 * C handle implicitly copyable. */
typedef struct {
    void *payload;
    const CettaContinuationProvider *provider;
    /* Assigned exactly once when the handle enters an occurrence store.
     * This identity belongs to the logical occurrence, not to the backend
     * payload: equal payloads and shared physical images remain distinct. */
    uint64_t occurrence_id;
    /* Controller-neutral causal lineage.  Zero names a frontier root.  This
     * is advisory/provenance data only; it never licenses restoration. */
    uint64_t parent_occurrence_id;
} CettaOwnedContinuation;

/* A bounded advisor may inspect a provider-projected, owned description of a
 * continuation without learning its physical payload layout.  Projection
 * identity revisions the meaning of BYTES; consumers must reject a model
 * learned under any other identity. */
typedef struct {
    uint8_t *bytes;
    size_t length;
    uint64_t projection_identity;
} CettaContinuationTrace;

/* Projection is advisory and must remain bounded even when a backend owns a
 * very large continuation image. */
#define CETTA_CONTINUATION_TRACE_BYTE_LIMIT ((size_t)65536u)

/* Physical storage receipt for one continuation occurrence.  Shared bytes
 * are counted once per non-NULL identity across a live frontier; exclusive
 * bytes belong only to this occurrence.  An eagerly materialized owned
 * snapshot therefore uses a distinct identity per payload, while a shared
 * representation may report one identity for many lightweight branch
 * handles. */
typedef struct {
    const void *shared_identity;
    size_t shared_bytes;
    size_t exclusive_bytes;
} CettaContinuationStorage;

/* A scorer sees only authorized occurrence identities and opaque owned
 * continuations.  Equal continuation payloads remain distinct occurrences;
 * a scorer may reorder them but cannot remove or duplicate one. */
typedef struct {
    uint64_t occurrence_id;
    uint64_t age;
    const CettaOwnedContinuation *continuation;
} CettaControllerCandidateView;

typedef enum {
    CETTA_CONTROLLER_RANK_READY = 0,
    CETTA_CONTROLLER_RANK_DEFERRED,
    CETTA_CONTROLLER_RANK_INVALIDATED,
} CettaControllerRankStatus;

typedef CettaControllerRankStatus (*CettaControllerBatchRankFn)(
    void *context,
    const CettaControllerCandidateView *candidates,
    size_t length,
    size_t *permutation);

typedef struct {
    CettaControllerBatchRankFn rank;
    void *context;
    uint64_t scorer_identity;
    uint64_t model_revision;
} CettaControllerBatchRanker;

typedef enum {
    CETTA_CONTROLLER_RANKING_APPLIED = 0,
    CETTA_CONTROLLER_RANKING_IDENTITY_DEFAULT,
    CETTA_CONTROLLER_RANKING_IDENTITY_DEFERRED,
    CETTA_CONTROLLER_RANKING_IDENTITY_INVALID,
} CettaControllerRankingDecision;

typedef struct {
    CettaControllerRankingDecision decision;
    uint64_t scorer_identity;
    uint64_t model_revision;
    size_t candidates;
} CettaControllerRankingReceipt;

/* Rank one complete live occurrence set.  PERMUTATION is always initialized
 * to a total source-order permutation first.  A ready scorer replaces it only
 * when its result contains every input index exactly once; deferred, stale,
 * malformed, or absent scorers therefore have a deterministic zero-semantic-
 * effect fallback. */
CettaControllerRankingDecision cetta_controller_rank_complete(
    const CettaControllerBatchRanker *ranker,
    const CettaControllerCandidateView *candidates,
    size_t length,
    size_t *permutation,
    CettaControllerRankingReceipt *receipt);

/* A provider-produced sequence of independent successor continuations.
 * Expansion preserves occurrence identity: two equal successors remain two
 * entries.  The sequence order is the provider's authored occurrence order;
 * DFS, FIFO, valuation, and learned controllers may store or select the
 * entries differently without changing their denotation. */
typedef struct {
    CettaOwnedContinuation *items;
    size_t length;
} CettaContinuationBatch;

/* One controller-neutral live-occurrence store.  Each payload occurs exactly
 * once; FIFO, LIFO, valuation, and learned selectors choose an index without
 * converting or copying the stored continuation representation. */
typedef struct {
    CettaOwnedContinuation *items;
    size_t begin;
    size_t end;
    size_t capacity;
    uint64_t next_occurrence_id;
} CettaContinuationStore;

/* Evaluator-neutral control hub for one observation scope.  The hub owns one
 * occurrence store and one checked schedule state; changing a discipline
 * changes only the schedule/index view, never the continuation payloads.
 * Observation demand is carried beside selection so first/prefix stopping
 * cannot be mistaken for commitment to a particular branch. */
typedef struct {
    CettaContinuationStore store;
    CettaSelectionAutomaton schedule;
    CettaSelectionDuty selection_duty;
    CettaControlPlan plan;
    uint64_t observed_occurrences;
    bool initialized;
} CettaContinuationHub;

typedef struct {
    CettaContinuationStatus (*capture)(
        void *machine, void **payload);
    CettaContinuationStatus (*restore)(
        void *machine, void **payload);
    void (*destroy)(void *payload);
    bool (*storage)(
        const void *payload,
        CettaContinuationStorage *storage);
} CettaContinuationOwnershipOps;

typedef struct {
    CettaContinuationStatus (*expand)(
        void *machine, void ***payloads, size_t *length);
} CettaContinuationBranchOps;

typedef struct {
    /* Optional, read-only advisory projection.  READY returns one non-empty
     * malloc-owned trace.  A missing callback or any non-ready result makes
     * guidance decline; it never affects continuation authority. */
    CettaContinuationStatus (*trace)(
        const void *payload,
        CettaContinuationTrace *trace);
} CettaContinuationProjectionOps;

/* Stable semantic roles inside one physical continuation provider.  They are
 * accounting and replacement seams, not separately selectable backends: a
 * continuation is admitted, captured, validated, and restored as one atomic
 * product.  A provider may omit detailed component receipts when it has no
 * meaningful decomposition. */
typedef enum {
    CETTA_CONTINUATION_COMPONENT_AUTHORITY = 0,
    CETTA_CONTINUATION_COMPONENT_TERMS,
    CETTA_CONTINUATION_COMPONENT_BINDINGS,
    CETTA_CONTINUATION_COMPONENT_CONTROL,
    CETTA_CONTINUATION_COMPONENT_OBLIGATIONS,
    CETTA_CONTINUATION_COMPONENT_READOUT,
    CETTA_CONTINUATION_COMPONENT_COUNT,
} CettaContinuationComponent;

typedef struct {
    const char *representation[CETTA_CONTINUATION_COMPONENT_COUNT];
    bool (*storage)(
        const void *payload,
        CettaContinuationComponent component,
        CettaContinuationStorage *storage);
} CettaContinuationComponentOps;

/* Exact reclamation is a provider operation over the complete live payload
 * set, not a selection discipline or a second continuation backend.  READY
 * returns one newly owned payload for every source payload.  The hub commits
 * that replacement only after validating the whole vector, so failure leaves
 * every live occurrence untouched.  DEFERRED is the constant-cost path when
 * the provider decides that collection is not yet profitable. */
typedef struct {
    size_t live_occurrences;
    size_t shared_bytes_before;
    size_t shared_bytes_after;
    size_t exclusive_bytes_before;
    size_t exclusive_bytes_after;
} CettaContinuationReclamationReceipt;

typedef struct {
    /* Optional constant-cost profitability check on one representative live
     * payload.  False returns DEFERRED before the hub allocates a root vector. */
    bool (*reclaim_due)(const void *payload);
    CettaContinuationStatus (*reclaim)(
        const void *const *source_payloads,
        size_t length,
        void ***replacement_payloads,
        CettaContinuationReclamationReceipt *receipt);
} CettaContinuationMaintenanceOps;

/* One physical continuation provider assembled from orthogonal operations.
 * Ownership is mandatory.  Branch production and advisory projection are
 * independently optional, so adding a scorer or a new selection discipline
 * never creates another continuation representation. */
struct CettaContinuationProvider {
    /* Physical representation receipt.  This is not a controller name and
     * may change independently of FIFO, valuation, or learned selection. */
    const char *representation_name;
    CettaContinuationOwnershipOps ownership;
    CettaContinuationBranchOps branching;
    CettaContinuationProjectionOps projection;
    CettaContinuationComponentOps components;
    CettaContinuationMaintenanceOps maintenance;
};

const char *cetta_continuation_machine_representation_name(
    CettaContinuationMachine machine);

void cetta_owned_continuation_init(
    CettaOwnedContinuation *continuation);
void cetta_owned_continuation_destroy(
    CettaOwnedContinuation *continuation);
CettaContinuationStatus cetta_continuation_capture(
    CettaContinuationMachine machine,
    CettaOwnedContinuation *continuation);
/* Restore consumes the owned continuation exactly when it succeeds. */
CettaContinuationStatus cetta_continuation_restore(
    CettaContinuationMachine machine,
    CettaOwnedContinuation *continuation);
bool cetta_owned_continuation_storage(
    const CettaOwnedContinuation *continuation,
    CettaContinuationStorage *storage);
const char *cetta_owned_continuation_component_representation(
    const CettaOwnedContinuation *continuation,
    CettaContinuationComponent component);
bool cetta_owned_continuation_component_storage(
    const CettaOwnedContinuation *continuation,
    CettaContinuationComponent component,
    CettaContinuationStorage *storage);
void cetta_continuation_trace_init(
    CettaContinuationTrace *trace);
void cetta_continuation_trace_destroy(
    CettaContinuationTrace *trace);
CettaContinuationStatus cetta_owned_continuation_trace(
    const CettaOwnedContinuation *continuation,
    CettaContinuationTrace *trace);
void cetta_continuation_batch_init(
    CettaContinuationBatch *batch);
void cetta_continuation_batch_destroy(
    CettaContinuationBatch *batch);
/* Expand one admitted semantic choice.  READY returns one or more owned
 * successor occurrences and leaves the source machine's semantic state
 * unchanged.  Failure leaves BATCH empty. */
CettaContinuationStatus cetta_continuation_expand(
    CettaContinuationMachine machine,
    CettaContinuationBatch *batch);
void cetta_continuation_store_init(
    CettaContinuationStore *store);
void cetta_continuation_store_destroy(
    CettaContinuationStore *store);
size_t cetta_continuation_store_length(
    const CettaContinuationStore *store);
/* Borrow one live occurrence without transferring ownership. */
const CettaOwnedContinuation *cetta_continuation_store_at(
    const CettaContinuationStore *store,
    size_t index);
/* Successful append consumes CONTINUATION. */
bool cetta_continuation_store_append(
    CettaContinuationStore *store,
    CettaOwnedContinuation *continuation);
/* Successful append consumes every item in BATCH. */
bool cetta_continuation_store_append_batch(
    CettaContinuationStore *store,
    CettaContinuationBatch *batch);
/* Successful removal transfers ownership to CONTINUATION while preserving
 * the relative order of every unselected live occurrence. */
bool cetta_continuation_store_take(
    CettaContinuationStore *store,
    size_t index,
    CettaOwnedContinuation *continuation);
bool cetta_continuation_store_storage(
    const CettaContinuationStore *store,
    size_t *shared_bytes,
    size_t *exclusive_bytes);
bool cetta_continuation_store_component_storage(
    const CettaContinuationStore *store,
    CettaContinuationComponent component,
    size_t *shared_bytes,
    size_t *exclusive_bytes);

bool cetta_continuation_hub_init(
    CettaContinuationHub *hub,
    const CettaSelectionAutomaton *schedule,
    CettaSelectionDuty selection_duty,
    const CettaControlPlan *plan);
void cetta_continuation_hub_destroy(CettaContinuationHub *hub);
size_t cetta_continuation_hub_length(const CettaContinuationHub *hub);
const CettaOwnedContinuation *cetta_continuation_hub_at(
    const CettaContinuationHub *hub, size_t index);
/* Return the lane and occurrence index selected for the current schedule
 * tick.  Singleton stores do not advance the schedule state. */
bool cetta_continuation_hub_select(
    CettaContinuationHub *hub,
    CettaSelectionLane *lane,
    size_t *index);
/* Replace only the schedule/index view.  The occurrence store and every
 * provider payload remain untouched.  The declared selection duty is
 * rechecked before the new schedule becomes visible. */
bool cetta_continuation_hub_switch_schedule(
    CettaContinuationHub *hub,
    const CettaSelectionAutomaton *schedule);
bool cetta_continuation_hub_append(
    CettaContinuationHub *hub,
    CettaOwnedContinuation *continuation);
bool cetta_continuation_hub_append_batch(
    CettaContinuationHub *hub,
    CettaContinuationBatch *batch);
bool cetta_continuation_hub_take(
    CettaContinuationHub *hub, size_t index,
    CettaOwnedContinuation *continuation);
bool cetta_continuation_hub_storage(
    const CettaContinuationHub *hub,
    size_t *shared_bytes,
    size_t *exclusive_bytes);
bool cetta_continuation_hub_component_storage(
    const CettaContinuationHub *hub,
    CettaContinuationComponent component,
    size_t *shared_bytes,
    size_t *exclusive_bytes);
size_t cetta_continuation_hub_control_capacity_bytes(
    const CettaContinuationHub *hub);
/* Ask the single physical provider to replace the hub's complete live root
 * set with an equivalent reclaimed representation.  Occurrence identity,
 * parentage, store order, observation state, and schedule state are preserved
 * exactly.  Empty or mixed-provider stores are not reclaimable. */
CettaContinuationStatus cetta_continuation_hub_reclaim(
    CettaContinuationHub *hub,
    CettaContinuationReclamationReceipt *receipt);
/* Add observed semantic occurrence mass with saturation and report whether
 * the finite observation demand is now discharged. */
bool cetta_continuation_hub_observe(
    CettaContinuationHub *hub, uint64_t occurrences);

bool search_context_init(SearchContext *ctx, const Bindings *base,
                         Arena *scratch_arena);
/* Initialize the binding-store component without admitting scratch
 * allocation.  A consumer using this capability may checkpoint only the
 * binding trail; search_context_scratch() returns NULL, so future code cannot
 * silently make an omitted scratch checkpoint observable. */
bool search_context_init_bindings_only(
    SearchContext *ctx, const Bindings *base);
void search_context_init_owned(SearchContext *ctx, Bindings *owned,
                               Arena *scratch_arena);
void search_context_free(SearchContext *ctx);
ChoicePoint search_context_save(SearchContext *ctx);
void search_context_rollback(SearchContext *ctx, ChoicePoint point);
Arena *search_context_scratch(SearchContext *ctx);
BindingsBuilder *search_context_builder(SearchContext *ctx);
const Bindings *search_context_bindings(const SearchContext *ctx);
void search_context_take(SearchContext *ctx, Bindings *out);

#endif /* CETTA_SEARCH_MACHINE_H */
