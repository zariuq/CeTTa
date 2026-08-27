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
} CettaSearchControllerPolicy;

const char *cetta_search_controller_policy_name(
    CettaSearchControllerPolicy policy);
bool cetta_search_controller_policy_parse(
    const char *name, CettaSearchControllerPolicy *policy);

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

typedef struct CettaContinuationBackend CettaContinuationBackend;

/* A backend-erased machine endpoint.  PeTTa relational control, Prime's
 * evaluation stack, and HE outcome continuations may provide different
 * adapters without sharing their physical state layout. */
typedef struct {
    void *machine;
    const CettaContinuationBackend *backend;
} CettaContinuationMachine;

/* An owned delimited continuation plus the evaluator state required for
 * resumption.  The backend remains visible only as an identity/operation
 * table; a controller treats the payload as opaque.  The handle is move-only
 * by convention and one successful restore consumes it.  Multi-shot capacity
 * means that independently captured images may coexist; it does not make one
 * C handle implicitly copyable. */
typedef struct {
    void *payload;
    const CettaContinuationBackend *backend;
} CettaOwnedContinuation;

/* Physical storage receipt for one continuation occurrence.  Shared bytes
 * are counted once per non-NULL identity across a live frontier; exclusive
 * bytes belong only to this occurrence.  A full-image backend therefore uses
 * a distinct identity per payload, while a persistent/cactus backend may
 * report one identity for many lightweight branch handles. */
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

/* A backend-produced sequence of independent successor continuations.
 * Expansion preserves occurrence identity: two equal successors remain two
 * entries.  The sequence order is the backend's authored occurrence order;
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
} CettaContinuationStore;

struct CettaContinuationBackend {
    /* Physical representation receipt.  This is not a controller name and
     * may change independently of FIFO, valuation, or learned selection. */
    const char *storage_name;
    CettaContinuationStatus (*capture)(
        void *machine, void **payload);
    CettaContinuationStatus (*restore)(
        void *machine, void **payload);
    void (*destroy)(void *payload);
    bool (*storage)(
        const void *payload,
        CettaContinuationStorage *storage);
    CettaContinuationStatus (*expand)(
        void *machine, void ***payloads, size_t *length);
};

const char *cetta_continuation_machine_storage_name(
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

bool search_context_init(SearchContext *ctx, const Bindings *base,
                         Arena *scratch_arena);
void search_context_init_owned(SearchContext *ctx, Bindings *owned,
                               Arena *scratch_arena);
void search_context_free(SearchContext *ctx);
ChoicePoint search_context_save(const SearchContext *ctx);
void search_context_rollback(SearchContext *ctx, ChoicePoint point);
Arena *search_context_scratch(SearchContext *ctx);
BindingsBuilder *search_context_builder(SearchContext *ctx);
const Bindings *search_context_bindings(const SearchContext *ctx);
void search_context_take(SearchContext *ctx, Bindings *out);

#endif /* CETTA_SEARCH_MACHINE_H */
