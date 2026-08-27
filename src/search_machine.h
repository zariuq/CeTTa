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
    CETTA_BRANCH_STORAGE_EXCLUSIVE_DEPTH_FIRST,
    CETTA_BRANCH_STORAGE_OWNED_FRONTIER,
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
CettaBranchStorageMode cetta_branch_select_storage(
    CettaBranchCaptureCapacity available,
    CettaBranchStorageMode requested);

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

/* A controller-owned sequence of independent successor continuations.
 * Expansion preserves occurrence identity: two equal successors remain two
 * entries.  The sequence order is the backend's authored occurrence order;
 * DFS, FIFO, valuation, and learned controllers may store or select the
 * entries differently without changing their denotation. */
typedef struct {
    CettaOwnedContinuation *items;
    size_t length;
} CettaContinuationFrontier;

/* Physical FIFO storage for one controller lane.  The queue owns every
 * continuation accepted by push operations.  It knows nothing about the
 * evaluator payload or answer algebra, so the same lane can schedule any
 * backend implementing CettaContinuationBackend. */
typedef struct {
    CettaOwnedContinuation *items;
    size_t begin;
    size_t end;
    size_t capacity;
} CettaContinuationQueue;

struct CettaContinuationBackend {
    CettaContinuationStatus (*capture)(
        void *machine, void **payload);
    CettaContinuationStatus (*restore)(
        void *machine, void **payload);
    void (*destroy)(void *payload);
    bool (*storage_bytes)(
        const void *payload,
        size_t *atom_bytes,
        size_t *exclusive_vector_bytes);
    CettaContinuationStatus (*expand)(
        void *machine, void ***payloads, size_t *length);
};

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
bool cetta_owned_continuation_storage_bytes(
    const CettaOwnedContinuation *continuation,
    size_t *atom_bytes,
    size_t *exclusive_vector_bytes);
void cetta_continuation_frontier_init(
    CettaContinuationFrontier *frontier);
void cetta_continuation_frontier_destroy(
    CettaContinuationFrontier *frontier);
/* Expand one admitted semantic choice.  READY returns one or more owned
 * successor occurrences and leaves the source machine's semantic state
 * unchanged.  Failure leaves FRONTIER empty. */
CettaContinuationStatus cetta_continuation_expand(
    CettaContinuationMachine machine,
    CettaContinuationFrontier *frontier);
void cetta_continuation_queue_init(
    CettaContinuationQueue *queue);
void cetta_continuation_queue_destroy(
    CettaContinuationQueue *queue);
size_t cetta_continuation_queue_length(
    const CettaContinuationQueue *queue);
/* Successful insertion consumes CONTINUATION. */
bool cetta_continuation_queue_push(
    CettaContinuationQueue *queue,
    CettaOwnedContinuation *continuation);
/* Successful insertion consumes every item in FRONTIER. */
bool cetta_continuation_queue_push_frontier(
    CettaContinuationQueue *queue,
    CettaContinuationFrontier *frontier);
/* Successful removal transfers ownership to CONTINUATION. */
bool cetta_continuation_queue_pop(
    CettaContinuationQueue *queue,
    CettaOwnedContinuation *continuation);
bool cetta_continuation_queue_storage_bytes(
    const CettaContinuationQueue *queue,
    size_t *atom_bytes,
    size_t *exclusive_vector_bytes);

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
