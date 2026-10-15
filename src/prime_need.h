#ifndef CETTA_PRIME_NEED_H
#define CETTA_PRIME_NEED_H

#include "atom.h"
#include "weight_ledger.h"

#ifndef CETTA_PRIME_NEED_HEAP_INDEX
#define CETTA_PRIME_NEED_HEAP_INDEX 0
#endif

#ifndef CETTA_PRIME_NEED_CLOSURE_CAPTURE
#define CETTA_PRIME_NEED_CLOSURE_CAPTURE 0
#endif

#ifndef CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
#define CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS 0
#endif

#ifndef CETTA_PRIME_NEED_IDENTITY_TEST_HOOKS
#define CETTA_PRIME_NEED_IDENTITY_TEST_HOOKS 0
#endif

/* Prime's call-by-need store is a persistent, branch-local extension order.
 *
 * A snapshot is a finite path of cell updates.  Lookup observes the newest
 * update for a thunk.  Two snapshots may be merged exactly when one extends
 * the other; the merge is their later snapshot.  Incomparable snapshots are
 * sibling branches and MUST NOT merge.  This partial-order law is the native
 * representation of call-time choice: a branch may refine its own thunk
 * cells, but no refinement can leak sideways into a sibling.
 *
 * Frames are arena-owned and immutable.  Cloning a snapshot is O(1), an
 * update appends one semantic history frame, and promotion copies the path
 * and its atom payloads into a longer-lived arena.  An optional immutable
 * radix index path-copies a bounded route to each newest cell frame; it is a
 * derived lookup accelerator and never replaces the semantic history.
 *
 * Persisted graph keys name cells only together with their immutable origin.
 * Rehydrating the same (key, origin) pair preserves sharing.  Substitution may
 * change an origin beneath a persisted key; that pair denotes a fresh runtime
 * cell, while repeated occurrences of the changed pair share that fresh cell.
 * This keeps graph identity distinct from structural origin equality. */

typedef enum {
    PRIME_NEED_CACHE_EMPTY = 0,
    PRIME_NEED_CACHE_EVALUATING,
    PRIME_NEED_CACHE_VALUE,
    PRIME_NEED_CACHE_STABLE_FAULT,
} PrimeNeedCacheState;

/* A completed demand may still require structural observation. A ready
 * value protects returned syntax from execution; embedded Need references
 * remain demands. EMPTY/EVALUATING cells have no completed value role. */
typedef enum {
    PRIME_NEED_VALUE_DEMAND_RESULT = 0,
    PRIME_NEED_VALUE_READY,
} PrimeNeedValueRole;

typedef struct PrimeNeedFrame PrimeNeedFrame;
typedef struct PrimeNeedArenaAudit PrimeNeedArenaAudit;
#if CETTA_PRIME_NEED_HEAP_INDEX
typedef struct PrimeNeedHeapIndexNode PrimeNeedHeapIndexNode;
#endif

typedef struct {
    const PrimeNeedFrame *top;
    uint64_t session_id;
    uint64_t max_storage_key;
    /* Arena containing `top`; `closure_owner` is non-NULL exactly when the
     * complete reachable frame path and its Atom payloads share one arena.
     * Native-handle leaves may retain independently managed control cells;
     * their arena owns the holders, not a shallow-copy licence for resources.
     * A NULL closure owner denotes an intentionally mixed-lifetime path: a
     * reclaimable suffix may reference a longer-lived immutable prefix. */
    Arena *owner;
    Arena *closure_owner;
#if CETTA_PRIME_NEED_HEAP_INDEX
    const PrimeNeedHeapIndexNode *heap_index;
    const PrimeNeedHeapIndexNode *lineage_index;
#endif
} PrimeNeedSnapshot;

typedef struct {
    PrimeNeedCacheState cache_state;
    PrimeNeedValueRole value_role;
    /* Optional direct producer of this VALUE update, in the same session.
     * Both identities are zero for ordinary results. The update identity
     * distinguishes sibling completions of the same producer cell. */
    uint64_t value_producer_thunk;
    uint64_t value_producer_update;
    Atom *origin;
    Atom *cached;
    uint64_t authority_id;
    uint64_t evaluator_id;
    uint64_t storage_key;
    uint64_t import_key;
    uint64_t source_occurrence_id;
    uint64_t source_argument_index;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    bool capture_known;
    const VarId *capture_var_ids;
    size_t capture_var_count;
#endif
} PrimeNeedCellView;

/* Borrowed immutable history, newest update first. The snapshot's arena
 * owners must remain alive while this cursor or its payload views are used.
 * No cell is forced, no occurrence is consumed, and lookup indexes are not
 * traversed. Repeated updates of one cell remain separate records. */
typedef struct {
    const PrimeNeedFrame *next;
    uint64_t session_id;
    uint64_t remaining;
    bool invalid;
} PrimeNeedHistoryIterator;

typedef struct {
    uint64_t session_id;
    uint64_t serial;
    uint64_t thunk_id;
    uint64_t depth;
    PrimeNeedCellView cell;
} PrimeNeedHistoryView;

typedef enum {
    PRIME_NEED_HISTORY_ENTRY,
    PRIME_NEED_HISTORY_DONE,
    PRIME_NEED_HISTORY_INVALID,
} PrimeNeedHistoryStep;

/* A failed begin invalidates the cursor rather than retaining its old view. */
bool prime_need_snapshot_history_begin(const PrimeNeedSnapshot *snapshot,
                                      PrimeNeedHistoryIterator *iterator);
/* DONE is stable. INVALID is sticky and leaves the output view unchanged. */
PrimeNeedHistoryStep prime_need_snapshot_history_next(
    PrimeNeedHistoryIterator *iterator, PrimeNeedHistoryView *view);

/* A Prime answer carries the finite causal support that justified it.  The
 * native representation is an immutable event DAG: appending an event adds a
 * causal child, while merging compatible independent supports adds a join
 * node.  Event payloads are semantic observations/effects, not evaluator
 * traces.
 *
 * Algebra.  Reachability is the receipt order.  A compatible merge is the
 * occurrence-preserving union of its two parent DAGs and is a least upper
 * bound in that order.  Compatibility projects to a functional observation
 * law: one Need cell cannot have two distinct outcomes; equal reads of one
 * state cell may join; incomparable writes to one state cell do not join.
 * Valid inherited histories keep their common causal occurrences; hazards
 * are checked only between their branch-specific extensions.  Sequential
 * writes already in their shared history are not concurrent effects.
 * Repeated equal observations remain distinct events in the native receipt,
 * even though the corresponding functional world quotient contains one
 * cell/outcome pair.  StateCell pointer identity is episode-local and is not
 * a persistence or wire-format identity. */
typedef enum {
    PRIME_NEED_RECEIPT_OBSERVE_CELL = 0,
    PRIME_NEED_RECEIPT_INSPECT_ORIGIN,
    PRIME_NEED_RECEIPT_READ_STATE,
    PRIME_NEED_RECEIPT_WRITE_STATE,
    PRIME_NEED_RECEIPT_USE_EQUATION,
    PRIME_NEED_RECEIPT_RESAMPLE,
    PRIME_NEED_RECEIPT_ADMISSION_TRUE,
    PRIME_NEED_RECEIPT_ADMISSION_FALSE,
    PRIME_NEED_RECEIPT_ADMISSION_PENDING,
} PrimeNeedReceiptEventKind;

typedef struct PrimeNeedReceiptFrame PrimeNeedReceiptFrame;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
typedef struct PrimeNeedRecording PrimeNeedRecording;
typedef struct PrimeNeedRecordingSlice PrimeNeedRecordingSlice;
#endif

typedef struct {
    const PrimeNeedReceiptFrame *top;
    uint64_t session_id;
    Arena *owner;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    /* NULL for operational branch state and ordinary diagnostic receipts.
     * A recording slice is private mutable allocation state; its immutable
     * frames belong to one shared, bounded recording owner. */
    PrimeNeedRecordingSlice *recording;
#endif
} PrimeNeedBranchState;

/* A receipt has the same persistent-DAG handle representation as branch
 * state, but it is a distinct value with a distinct session.  Branch-state
 * events carry the operational StateCell overlay.  Receipt events are copied
 * into the separate observer carrier only when causal evidence is enabled and
 * requested. */
typedef PrimeNeedBranchState PrimeNeedReceipt;

/* Per-occurrence Prime contract carrier.
 *
 * A Bindings occurrence's operational state lives here, referenced from
 * Bindings by an 8-byte pointer that is NULL for pure-HE evaluation.  The
 * Need snapshot implements call-time choice, `branch_state` carries the
 * transactional StateCell overlay, and `occurrence_token` preserves exact
 * relational multiplicity.  A receipt-capable build adds a separate evidence
 * carrier; ordinary builds contain neither its handle nor its events. */
typedef struct {
    PrimeNeedSnapshot prime_need;
    PrimeNeedBranchState branch_state;
    CettaWeightLedger weights;
    /* Compact operational identity for one exact relational occurrence.
     * It preserves duplicate derivations when causal receipt observation is
     * absent; full rule-use evidence remains in `receipt` only when an
     * observer requests it. */
    uint64_t          occurrence_token;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    PrimeNeedReceipt  receipt;
#endif
} PrimeOccurrence;

typedef struct {
    PrimeNeedReceiptEventKind kind;
    uint64_t event_id;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    uint64_t need_session_id;
    uint64_t thunk_id;
    uint64_t source_occurrence_id;
    uint64_t source_argument_index;
    uint64_t rule_occurrence_id;
    /* The immutable cached update that this delivery observes.  Cell and
     * storage identities survive retries and branch choices; they cannot
     * identify a production.  Zero means that no completed update is named.
     * A true completion flag is supplied only by the successful force-return
     * boundary.  A later delivery may name an update produced before capture;
     * absence of its completion event is an external dependency, not a firing. */
    uint64_t producer_update_id;
    bool producer_completed;
#endif
    StateCell *state_cell;
    Atom *before;
    Atom *after;
} PrimeNeedReceiptEvent;

/* Identities.  Sessions, frame serials, thunks, authorities, storage keys,
 * receipt sessions and nodes, source occurrences, evaluators and resample
 * scopes are each a namespace that issues every identity at most once in a
 * process and never wraps.  Zero is never an identity.  An exhausted
 * namespace stays exhausted: a function below that would draw from it
 * returns false (or zero) and leaves what its caller holds unchanged.  Fresh
 * thunk identities stay below PRIME_NEED_NAMED_CELL_BIT.  Nothing here makes
 * an identity unique across processes. */
typedef void (*PrimeNeedIdentityRefusalObserver)(void);
/* Called once for each refusal, on the refusing thread.  The evaluator uses
 * it to report the evaluation that met an exhausted namespace as
 * incomplete. */
void prime_need_identity_set_refusal_observer(
    PrimeNeedIdentityRefusalObserver observer);

void prime_need_snapshot_init(PrimeNeedSnapshot *snapshot);
bool prime_need_snapshot_present(const PrimeNeedSnapshot *snapshot);
bool prime_need_snapshot_begin(PrimeNeedSnapshot *snapshot);
bool prime_need_snapshot_is_ancestor(const PrimeNeedSnapshot *ancestor,
                                     const PrimeNeedSnapshot *descendant);
bool prime_need_snapshot_merge(PrimeNeedSnapshot *dst,
                               const PrimeNeedSnapshot *src);
/* The longest history two snapshots of one session share: the refinements
 * both branches inherited from before they parted.  `max_storage_key` is
 * kept as an upper bound. */
bool prime_need_snapshot_common_ancestor(const PrimeNeedSnapshot *left,
                                         const PrimeNeedSnapshot *right,
                                         PrimeNeedSnapshot *out);
/* The longest prefix of `snapshot`, extending `base`, that leaves no cell
 * begun above `base` in the middle of its evaluation: the part of a history
 * a continuation may keep. */
bool prime_need_snapshot_settled_ancestor(const PrimeNeedSnapshot *base,
                                          const PrimeNeedSnapshot *snapshot,
                                          PrimeNeedSnapshot *out);
bool prime_need_snapshot_promote(Arena *dst,
                                 PrimeNeedSnapshot *snapshot);
/* Full-path composition with a caller-owned Atom copy session. Preserve
 * frame serials, cell/world identities and the exact storage-key watermark;
 * share Atom images with other roots copied in this session. The caller
 * discards its private destination and session after any failed preparation.
 * StateCell/Space identities still require their external lifetime authority. */
bool prime_need_snapshot_promote_with_session(
    Arena *dst, AtomDeepCopySession *copy_session, PrimeNeedSnapshot *snapshot);
/* Owned checkpoint copy, distinct from ordinary path promotion. The common
 * session preserves actual frame, index and capture-array identity across
 * every carrier, including distinct equal paths and absent index roots.
 * Owners must be live throughout preparation. A region map must cover every
 * metadata allocation when it is not in the current frame's owner; borrowed
 * identity regions are refused for this metadata. Atom external identities
 * retain their existing lifetime contract. On refusal the snapshot is intact;
 * discard the unpublished destination graph and session before retrying. */
bool prime_need_snapshot_copy_with_session(
    Arena *dst, AtomDeepCopySession *copy_session, PrimeNeedSnapshot *snapshot);
/* Copy only the suffix owned by `forbidden` into `dst`, retaining an older
 * immutable prefix that already excludes `forbidden`.  The source-owned
 * suffix must be contiguous.  This is the promotion operation used before a
 * branch arena is reclaimed. */
bool prime_need_snapshot_promote_suffix(
    Arena *dst, const Arena *forbidden, PrimeNeedSnapshot *snapshot);
/* Transactional-composition form: the caller owns `copy_session` and the
 * destination rollback boundary, allowing one memoized atom-copy graph to be
 * shared by the visible answer, bindings, Need heap, and receipt. */
bool prime_need_snapshot_promote_suffix_with_session(
    Arena *dst, const Arena *forbidden,
    AtomDeepCopySession *copy_session, PrimeNeedSnapshot *snapshot);
bool prime_need_snapshot_excludes_arena(
    const PrimeNeedSnapshot *snapshot, const Arena *forbidden);
bool prime_need_snapshot_owner_excludes_arena(
    const PrimeNeedSnapshot *snapshot, const Arena *forbidden);
/* Inspect every retained Atom root, including superseded updates. This does
 * not force a cell, establish its owner's lifetime, or authenticate a cache
 * transition. The predicate inspects the complete Atom graph when required. */
bool prime_need_snapshot_atoms_satisfy(const PrimeNeedSnapshot *snapshot,
    AtomTreePredicate predicate, void *context);
PrimeNeedArenaAudit *prime_need_arena_audit_new(
    const Arena *forbidden);
bool prime_need_arena_audit_snapshot(
    PrimeNeedArenaAudit *audit, const PrimeNeedSnapshot *snapshot);

bool prime_need_snapshot_allocate(Arena *owner,
                                  const PrimeNeedSnapshot *base,
                                  Atom *term,
                                  PrimeNeedSnapshot *out,
                                  uint64_t *out_thunk_id);
uint64_t prime_need_fresh_source_occurrence(void);
/* The evaluation of a cell records the evaluator that runs it, so a demand
 * that meets its own evaluation is a cycle. */
uint64_t prime_need_fresh_evaluator_id(void);
/* Each force of a resampler evaluates in a scope of its own; the scope is
 * part of the name of each symbol cell it demands. */
uint64_t prime_need_fresh_resample_scope(void);
bool prime_need_snapshot_allocate_source_argument(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id);
bool prime_need_snapshot_allocate_persisted(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t storage_key, PrimeNeedSnapshot *out,
    uint64_t *out_thunk_id);
/* Allocate a closed cell under an identity the caller chose rather than a
 * fresh one.  The identity names the cell in every branch of the session:
 * siblings that allocate it allocate distinct frames of the same name, each
 * visible only in its own lineage, so a later demand in a branch finds the
 * cell that branch already holds.  Fresh identities are small counters;
 * a chosen identity has its top bit set, so the two never meet.  Fails when
 * `base` already holds a cell of that identity. */
#define PRIME_NEED_NAMED_CELL_BIT (UINT64_C(1) << 63)
bool prime_need_snapshot_allocate_named(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t thunk_id, PrimeNeedSnapshot *out);
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
bool prime_need_snapshot_allocate_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id);
bool prime_need_snapshot_allocate_source_argument_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id);
bool prime_need_snapshot_allocate_persisted_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t storage_key,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id);
#endif
bool prime_need_snapshot_start_evaluation(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, PrimeNeedSnapshot *out);
bool prime_need_snapshot_resolve_value(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, Atom *value, PrimeNeedSnapshot *out);
/* Complete the consumer with a value returned directly by an already
 * completed producer. The caller supplies the returned value (possibly an
 * owned transport of it); this operation records, and later validates, the
 * producer's exact immutable update and preserves its return role. It does
 * not infer provenance by Atom equality or change embedded child roles. */
bool prime_need_snapshot_resolve_forwarded_value(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, Atom *value, uint64_t producer_thunk_id,
    PrimeNeedSnapshot *out);
bool prime_need_snapshot_forwarded_value_source(
    const PrimeNeedSnapshot *snapshot, uint64_t thunk_id,
    PrimeNeedCellView *source);
/* The same owner-checked completion, for an already produced value whose
 * callable-looking syntax must stay data during structural observation. */
bool prime_need_snapshot_resolve_ready_value(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, Atom *value, PrimeNeedSnapshot *out);
bool prime_need_snapshot_resolve_stable_fault(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, Atom *fault, PrimeNeedSnapshot *out);
bool prime_need_snapshot_retry_evaluation(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, PrimeNeedSnapshot *out);
bool prime_need_snapshot_lookup(const PrimeNeedSnapshot *snapshot,
                                uint64_t thunk_id,
                                PrimeNeedCellView *out);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
/* Observation identity of the latest VALUE/STABLE_FAULT update.  It is fresh
 * for each successful cache transition, preserved by whole/suffix promotion,
 * and qualified by snapshot->session_id.  Refusal leaves *out unchanged.
 * This read does not retain the snapshot or certify that recording witnessed
 * the production.  Ordinary cell lookup need not construct this evidence. */
bool prime_need_snapshot_cached_update_id(
    const PrimeNeedSnapshot *snapshot, uint64_t thunk_id, uint64_t *out);
#endif

Atom *prime_need_ref(Arena *arena, const PrimeNeedSnapshot *snapshot,
                     uint64_t thunk_id);
bool prime_need_ref_parse(const Atom *atom, uint64_t *session_id,
                          uint64_t *thunk_id, uint64_t *authority_id);
bool prime_need_ref_parse_rights(const Atom *atom, uint64_t *session_id,
                                 uint64_t *thunk_id,
                                 uint64_t *authority_id,
                                 uint32_t *rights);
bool prime_need_ref_belongs_to(const Atom *atom,
                               const PrimeNeedSnapshot *snapshot,
                               uint64_t *thunk_id);
bool prime_need_ref_belongs_to_with_rights(
    const Atom *atom, const PrimeNeedSnapshot *snapshot,
    uint32_t required_rights, uint64_t *thunk_id);
Atom *prime_need_ref_restrict(Arena *arena, const Atom *atom,
                              const PrimeNeedSnapshot *snapshot,
                              uint32_t rights);

void prime_need_branch_state_init(PrimeNeedBranchState *state);
bool prime_need_branch_state_begin(
    Arena *owner, PrimeNeedBranchState *state);
bool prime_need_branch_state_present(
    const PrimeNeedBranchState *state);
static inline bool prime_need_branch_state_has_events(
    const PrimeNeedBranchState *state) {
    return state && state->top != NULL;
}
bool prime_need_branch_state_is_ancestor(
    const PrimeNeedBranchState *ancestor,
    const PrimeNeedBranchState *descendant);
bool prime_need_branch_state_compatible(
    const PrimeNeedBranchState *left,
    const PrimeNeedBranchState *right);
bool prime_need_branch_state_merge_nonempty(
    PrimeNeedBranchState *dst, const PrimeNeedBranchState *src);
static inline bool prime_need_branch_state_merge(
    PrimeNeedBranchState *dst, const PrimeNeedBranchState *src) {
    if (!dst)
        return false;
    if (!src || !prime_need_branch_state_present(src))
        return true;
    if (!prime_need_branch_state_has_events(src)) {
        if (!prime_need_branch_state_present(dst)) {
            *dst = *src;
            return true;
        }
        return dst->session_id == src->session_id;
    }
    return prime_need_branch_state_merge_nonempty(dst, src);
}
bool prime_need_branch_state_promote(
    Arena *dst, PrimeNeedBranchState *state);
bool prime_need_branch_state_promote_with_session(
    Arena *dst, AtomDeepCopySession *copy_session, PrimeNeedBranchState *state);
/* Copy an unpublished owned carrier through the enclosing physical graph.
 * Source frames/owners remain quiescent and live through preparation. Shared
 * nodes keep one image, distinct nodes are not identified by event/value IDs.
 * Owner/extent and session checks also apply to memo hits. External StateCell
 * identities keep their separately captured lifetime/currentness authority.
 * On refusal retain the original and discard the unpublished copy/session. */
bool prime_need_branch_state_copy_with_session(
    Arena *dst, AtomDeepCopySession *copy_session, PrimeNeedBranchState *state);
bool prime_need_branch_state_promote_suffix(
    Arena *dst, const Arena *forbidden, PrimeNeedBranchState *state);
bool prime_need_branch_state_promote_suffix_with_session(
    Arena *dst, const Arena *forbidden,
    AtomDeepCopySession *copy_session, PrimeNeedBranchState *state);
bool prime_need_branch_state_excludes_arena(
    const PrimeNeedBranchState *state, const Arena *forbidden);
/* StateCell identities are external roots even when no Atom wraps them.
 * Inspect the complete retained DAG, independently of recording completeness
 * or any destination copy memo. Collection refusal is false, never an empty
 * successful inspection. Callers retain all source owners during the walk. */
typedef bool (*PrimeNeedStateCellPredicate)(const StateCell *cell, void *context);
bool prime_need_branch_state_payloads_satisfy(const PrimeNeedBranchState *state,
    AtomTreePredicate atoms, PrimeNeedStateCellPredicate cells, void *context);
bool prime_need_arena_audit_branch_state(
    PrimeNeedArenaAudit *audit, const PrimeNeedBranchState *state);
void prime_need_arena_audit_free(PrimeNeedArenaAudit *audit);
bool prime_need_branch_state_equal(
    const PrimeNeedBranchState *left,
    const PrimeNeedBranchState *right);
bool prime_need_branch_state_read(
    Arena *owner, const PrimeNeedBranchState *base, StateCell *cell,
    Atom *value, PrimeNeedBranchState *out);
bool prime_need_branch_state_write(
    Arena *owner, const PrimeNeedBranchState *base, StateCell *cell,
    Atom *before, Atom *after, PrimeNeedBranchState *out);
bool prime_need_branch_state_value(
    const PrimeNeedBranchState *state, StateCell *cell, Atom **out_value);

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
typedef struct {
    size_t nodes;
    size_t payload_bytes;
    size_t slices;
    size_t owner_links;
} PrimeNeedRecordingLimits;

typedef struct {
    size_t nodes;
    size_t reserved_nodes;
    size_t payload_charge;
    size_t reserved_payload_bytes;
    size_t metadata_bound;
    size_t retained_bound;
    size_t owner_links;
    uint64_t omitted;
    bool incomplete;
    bool overflow;
} PrimeNeedRecordingStatus;

/* The coordinator reserves disjoint slices before concurrent execution.
 * Each slice has one writer. Reading status/census and releasing the final
 * owner require quiescence. No recording owner borrows a semantic arena.
 * Payload capacity covers conservative allocation/block charges; metadata
 * and destination-owner links have separate explicit bounds. */
PrimeNeedRecording *prime_need_recording_new(PrimeNeedRecordingLimits limits);
void prime_need_recording_retain(void *recording);
void prime_need_recording_release(void *recording);
void prime_need_recording_storage(const void *recording, CettaStorageCensus *census);
PrimeNeedRecordingStatus prime_need_recording_status(const PrimeNeedRecording *recording);
/* A failed reservation leaves both the captured receipt and output intact.
 * Only a seed from this owner is accepted; earlier external histories need
 * an explicit capture-boundary adapter rather than a borrowed pointer. */
bool prime_need_recording_reserve(PrimeNeedRecording *recording,
    const PrimeNeedReceipt *seed, size_t nodes, size_t payload_bytes,
    PrimeNeedReceipt *out);
/* A continuing worker may acquire further grants of this size from the
 * same bounded group. Ordinary reserve retains its fixed slice limits. */
bool prime_need_recording_reserve_growing(PrimeNeedRecording *recording,
    const PrimeNeedReceipt *seed, size_t node_quantum, size_t byte_quantum,
    PrimeNeedReceipt *out);
bool prime_need_recording_promote(Arena *dst, PrimeNeedReceipt *receipt);
bool prime_need_recording_merge(PrimeNeedReceipt *dst, const PrimeNeedReceipt *src);
/* Dynamic allocation authority is separate from immutable captured roots.
 * A present scope with an empty slice silences recording after refusal;
 * it never falls back to unbounded diagnostic allocation. */
extern _Thread_local const PrimeNeedReceipt *g_prime_need_recording_scope;
typedef struct {
    const PrimeNeedReceipt *previous;
    bool entered;
} PrimeNeedRecordingScope;
void prime_need_recording_enter(PrimeNeedRecordingScope *scope,
    const PrimeNeedReceipt *slice);
void prime_need_recording_leave(PrimeNeedRecordingScope *scope);
void prime_need_recording_omit_scope(void);
/* Reserve a sibling slice in the active scope's group. False leaves *out
 * untouched and does not mark the parent incomplete. */
bool prime_need_recording_inherit(PrimeNeedReceipt *out);
/* Quiescent publication adopts an initial root and joins independent roots
 * through bounded DAG
 * joins in the active scope. Source carriers and allocation owners remain
 * intact; refusal is optional evidence loss and records incompleteness. */
void prime_need_recording_publish(PrimeNeedReceipt *src);
bool prime_need_recording_merge_scoped(PrimeNeedReceipt *dst,
    const PrimeNeedReceipt *src);
typedef bool (*PrimeNeedRecordingRootVisitor)(const PrimeNeedReceipt *, void *);
/* Quiescent terminal roots retained by this slice, including an adopted root.
 * Roots are separate branch views and may have overlapping ancestors. */
bool prime_need_recording_roots(const PrimeNeedReceipt *slice,
    PrimeNeedRecordingRootVisitor visit, void *context);

/* Unpublished copy of one quiescent recording owner. The caller keeps all
 * source holders and the common Atom session live and excludes concurrent
 * recording, promotion and reset. Stage every returned arena pair before
 * sealing that session's injective region map. Preparation preserves the
 * complete slice inventory, identities, omissions and remaining quotas.
 * It does not capture the service that allocates future occurrence IDs.
 * A carrier's writable slice and root inventory are checked separately:
 * roots may belong to another slice of the same physical group, but equal
 * captured logical IDs do not authorize roots from a distinct owner.
 *
 * A restored holder must already retain the source group. Rebuilding its
 * physical owner link does not spend the logical quota a second time; the
 * new link and copied payload still occupy separately counted storage.
 * Receipt outputs borrow the image until TAKE or an explicit restored hold.
 * On refusal discard the unpublished copy/session; the source is unchanged.
 * This API neither authenticates replay nor admits a recorded parent copy. */
typedef struct PrimeNeedRecordingCopy PrimeNeedRecordingCopy;
/* Borrow the physical recording owner of a held receipt; this does not
 * retain it or authenticate the receipt. The caller holds its carrier. */
PrimeNeedRecording *prime_need_receipt_recording_owner(const PrimeNeedReceipt *receipt);
PrimeNeedRecordingCopy *prime_need_recording_copy_new(PrimeNeedRecording *source);
size_t prime_need_recording_copy_pair_count(const PrimeNeedRecordingCopy *copy);
bool prime_need_recording_copy_pair(const PrimeNeedRecordingCopy *copy,
    size_t index, AtomDeepCopyArenaPair *out);
bool prime_need_recording_copy_prepare(PrimeNeedRecordingCopy *copy,
    AtomDeepCopySession *session);
bool prime_need_recording_copy_receipt(const PrimeNeedRecordingCopy *copy,
    const PrimeNeedReceipt *source, PrimeNeedReceipt *out);
bool prime_need_recording_copy_restore_holder(PrimeNeedRecordingCopy *copy,
    const Arena *source, Arena *image);
PrimeNeedRecording *prime_need_recording_copy_take(PrimeNeedRecordingCopy *copy);
void prime_need_recording_copy_free(PrimeNeedRecordingCopy *copy);

/* Explicit recording-copy mode for one unpublished common Atom session.
 * Once enabled, receipt_copy_with_session refuses every recorded carrier
 * without a complete physical group/slice/inventory map. It never falls
 * back to retaining that carrier's source group. Ordinary receipt promotion
 * remains a retention operation, including in this session.
 *
 * Copy handles, source groups and all source holders outlive the session.
 * Complete all carrier copies before TAKE. The destination enclosing owner
 * must then acquire TAKE's group reference before releasing the copy handle.
 * Registering an enclosing pair asserts that the actual source owner holds
 * the source group directly and that the actual destination owner will hold
 * the image directly. Its carriers may borrow only within those lifetimes.
 * This does not manufacture an arena retain link or spend its logical quota.
 * Actual paid arena holders use restore_holder instead. Both relationships
 * use the session's captured injective private-arena map; borrowed identity
 * regions cannot become enclosing destinations. No concurrent session use. */
bool prime_need_recording_copy_enable(AtomDeepCopySession *session);
/* Borrow a completed capability in an explicitly enabled session. Missing,
 * stale and already-taken images refuse. The handle remains caller-owned. */
PrimeNeedRecordingCopy *prime_need_recording_copy_lookup(
    const AtomDeepCopySession *session, const PrimeNeedRecording *source);
bool prime_need_recording_copy_enclosing_owner(PrimeNeedRecordingCopy *copy,
    const Arena *source, Arena *image);
bool prime_need_recording_copy_with_session(Arena *dst,
    AtomDeepCopySession *session, PrimeNeedReceipt *receipt);

static inline void prime_need_receipt_init(PrimeNeedReceipt *receipt) {
    prime_need_branch_state_init(receipt);
}
bool prime_need_receipt_begin(Arena *owner, PrimeNeedReceipt *receipt);
static inline bool prime_need_receipt_present(
    const PrimeNeedReceipt *receipt) {
    return prime_need_branch_state_present(receipt);
}
static inline bool prime_need_receipt_has_events(
    const PrimeNeedReceipt *receipt) {
    return prime_need_branch_state_has_events(receipt);
}
static inline bool prime_need_receipt_is_ancestor(
    const PrimeNeedReceipt *ancestor,
    const PrimeNeedReceipt *descendant) {
    return prime_need_branch_state_is_ancestor(ancestor, descendant);
}
static inline bool prime_need_receipt_compatible(
    const PrimeNeedReceipt *left, const PrimeNeedReceipt *right) {
    if ((left && left->recording) || (right && right->recording))
        return true;
    return prime_need_branch_state_compatible(left, right);
}
static inline bool prime_need_receipt_merge(
    PrimeNeedReceipt *dst, const PrimeNeedReceipt *src) {
    if (g_prime_need_recording_scope)
        return prime_need_recording_merge_scoped(dst, src);
    if ((dst && dst->recording) || (src && src->recording))
        return prime_need_recording_merge(dst, src);
    return prime_need_branch_state_merge(dst, src);
}
static inline bool prime_need_receipt_promote(
    Arena *dst, PrimeNeedReceipt *receipt) {
    if (receipt && receipt->recording)
        return prime_need_recording_promote(dst, receipt);
    return prime_need_branch_state_promote(dst, receipt);
}
static inline bool prime_need_receipt_promote_with_session(
    Arena *dst, AtomDeepCopySession *copy_session, PrimeNeedReceipt *receipt) {
    if (!atom_deep_copy_session_targets(copy_session, dst))
        return false;
    /* Bounded recordings retain their separate immutable payload owner;
     * this operation grants no new writable slice authority. */
    if (receipt && receipt->recording)
        return prime_need_recording_promote(dst, receipt);
    return prime_need_branch_state_promote_with_session(dst, copy_session, receipt);
}
static inline bool prime_need_receipt_copy_with_session(
    Arena *dst, AtomDeepCopySession *copy_session, PrimeNeedReceipt *receipt) {
    if (!atom_deep_copy_session_targets(copy_session, dst))
        return false;
    if (receipt && receipt->recording)
        return prime_need_recording_copy_with_session(dst, copy_session, receipt);
    return prime_need_branch_state_copy_with_session(dst, copy_session, receipt);
}
static inline bool prime_need_receipt_promote_suffix(
    Arena *dst, const Arena *forbidden, PrimeNeedReceipt *receipt) {
    if (receipt && receipt->recording)
        return prime_need_recording_promote(dst, receipt);
    return prime_need_branch_state_promote_suffix(
        dst, forbidden, receipt);
}
static inline bool prime_need_receipt_promote_suffix_with_session(
    Arena *dst, const Arena *forbidden,
    AtomDeepCopySession *copy_session, PrimeNeedReceipt *receipt) {
    if (!atom_deep_copy_session_targets(copy_session, dst))
        return false;
    if (receipt && receipt->recording)
        return prime_need_recording_promote(dst, receipt);
    return prime_need_branch_state_promote_suffix_with_session(
        dst, forbidden, copy_session, receipt);
}
static inline bool prime_need_receipt_excludes_arena(
    const PrimeNeedReceipt *receipt, const Arena *forbidden) {
    return prime_need_branch_state_excludes_arena(receipt, forbidden);
}
static inline bool prime_need_arena_audit_receipt(
    PrimeNeedArenaAudit *audit, const PrimeNeedReceipt *receipt) {
    return prime_need_arena_audit_branch_state(audit, receipt);
}
static inline bool prime_need_receipt_equal(
    const PrimeNeedReceipt *left, const PrimeNeedReceipt *right) {
    return prime_need_branch_state_equal(left, right);
}
bool prime_need_receipt_observe_cell(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id, Atom *outcome,
    PrimeNeedReceipt *out);
bool prime_need_receipt_observe_source_cell(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    Atom *outcome, PrimeNeedReceipt *out);
bool prime_need_receipt_observe_source_cell_branch_local(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    Atom *outcome, PrimeNeedReceipt *out);
/* Record delivery from an actual completed cache update.  `completed` names
 * the caller's successful commit boundary; false names a cached delivery.
 * Branch-local allocation has the same prefix-lifetime contract as the
 * other branch-local receipt operations. */
bool prime_need_receipt_observe_cached_cell(
    Arena *owner, const PrimeNeedReceipt *base,
    const PrimeNeedSnapshot *snapshot, uint64_t thunk_id,
    bool completed, bool branch_local, Atom *outcome, PrimeNeedReceipt *out);
bool prime_need_receipt_inspect_origin(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id, Atom *origin_view,
    PrimeNeedReceipt *out);
bool prime_need_receipt_inspect_origin_branch_local(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id, Atom *origin_view,
    PrimeNeedReceipt *out);
bool prime_need_receipt_read_state(
    Arena *owner, const PrimeNeedReceipt *base, StateCell *cell,
    Atom *value, PrimeNeedReceipt *out);
bool prime_need_receipt_write_state(
    Arena *owner, const PrimeNeedReceipt *base, StateCell *cell,
    Atom *before, Atom *after, PrimeNeedReceipt *out);
bool prime_need_receipt_use_equation(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t source_occurrence_id, uint64_t rule_occurrence_id,
    Atom *equation, Atom *result, PrimeNeedReceipt *out);
bool prime_need_receipt_use_equation_ids(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t source_occurrence_id, uint64_t rule_occurrence_id,
    PrimeNeedReceipt *out);
/* Extend a longer-lived receipt in a branch-local arena.  The base must
 * outlive `owner`, and the result must be promoted before it escapes that
 * arena.  This is the receipt analogue of allocating a search continuation
 * in a reclaimable branch heap. */
bool prime_need_receipt_use_equation_branch_local(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t source_occurrence_id, uint64_t rule_occurrence_id,
    Atom *equation, Atom *result, PrimeNeedReceipt *out);
bool prime_need_receipt_use_equation_ids_branch_local(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t source_occurrence_id, uint64_t rule_occurrence_id,
    PrimeNeedReceipt *out);
bool prime_need_receipt_resample(
    Arena *owner, const PrimeNeedReceipt *base, Atom *origin,
    PrimeNeedReceipt *out);
bool prime_need_receipt_resample_branch_local(
    Arena *owner, const PrimeNeedReceipt *base, Atom *origin,
    PrimeNeedReceipt *out);
/* An examined authored predicate return, not an inference from absent body
 * work. `before` retains the completed coefficient and `after` the actual
 * verdict. The dependency identifies its captured authored occurrence;
 * separate verdict returns keep separate receipt node identities. The caller
 * supplies the role from its actual admission transition. This recording
 * never supplies semantic compatibility or authority to replay the body. */
bool prime_need_receipt_admission(
    Arena *owner, const PrimeNeedReceipt *base,
    PrimeNeedReceiptEventKind kind, uint64_t dependency,
    Atom *coefficient, Atom *verdict, PrimeNeedReceipt *out);
bool prime_need_receipt_state_value(const PrimeNeedReceipt *receipt,
                                    StateCell *cell, Atom **out_value);
/* A checked view of the existing receipt DAG, including its non-event joins.
 * IDs and predecessors survive promotion.  The increasing-ID order is a
 * topological allocation order, not the parent agenda's publication order.
 * The node array is owned by the view; Atom and StateCell payloads remain
 * borrowed from the receipt's retained owners.  Free the view before releasing
 * those owners.  This view does not authenticate causes or certify replay. */
typedef struct {
    uint64_t identity_id;
    uint64_t left_id;
    uint64_t right_id;
    bool has_event;
    PrimeNeedReceiptEvent event;
} PrimeNeedReceiptGraphNode;

typedef struct {
    uint64_t session_id;
    uint64_t root_id;
    PrimeNeedReceiptGraphNode *nodes;
    size_t length;
    size_t event_count;
    bool incomplete;
} PrimeNeedReceiptGraph;

typedef enum {
    PRIME_NEED_RECEIPT_GRAPH_ABSENT = 0,
    PRIME_NEED_RECEIPT_GRAPH_COMPLETE,
    PRIME_NEED_RECEIPT_GRAPH_REFUSED,
    PRIME_NEED_RECEIPT_GRAPH_INVALID,
    PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE,
} PrimeNeedReceiptGraphStatus;

/* Initialize the destination to zero.  Successful collection replaces it;
 * refusal or invalid input preserves the entire previous view and receipt.
 * ABSENT is distinct from a COMPLETE event-free captured session.
 * INCOMPLETE replaces the view with a valid retained prefix, never a claim
 * that the recorded computation is complete. */
PrimeNeedReceiptGraphStatus prime_need_receipt_graph_collect(
    const PrimeNeedReceipt *receipt, PrimeNeedReceiptGraph *out);
void prime_need_receipt_graph_free(PrimeNeedReceiptGraph *graph);
/* Check finite structure only: fresh IDs, retained earlier predecessors,
 * one root, complete reachability and event count.  Erasing a genuine causal
 * edge can still pass this check; comparison with execution remains required.
 * Allocation refusal is never returned as an empty complete graph. */
PrimeNeedReceiptGraphStatus prime_need_receipt_graph_validate(
    const PrimeNeedReceiptGraph *graph);
/* Legacy event-only projections.  The count has no failure channel; consumers
 * requiring completeness must use the checked graph collection above. */
size_t prime_need_receipt_event_count(const PrimeNeedReceipt *receipt);
bool prime_need_receipt_event_at(const PrimeNeedReceipt *receipt,
                                 size_t index,
                                 PrimeNeedReceiptEvent *out_event);
#endif


void prime_need_snapshot_storage_census(const PrimeNeedSnapshot *snapshot, CettaStorageCensus *census);
void prime_need_branch_storage_census(const PrimeNeedBranchState *state, CettaStorageCensus *census);
void prime_occurrence_storage_census(const PrimeOccurrence *occurrence, CettaStorageCensus *census);

#if CETTA_PRIME_NEED_IDENTITY_TEST_HOOKS
/* Test builds only.  The runtime has no way to move a namespace; these let
 * a test place one near its end and draw through its runtime path.  The
 * environment variable CETTA_TEST_PRIME_NEED_IDENTITY_SEED does the same
 * before main (see prime_need.c). */
typedef enum {
    PRIME_NEED_IDENTITY_SESSION = 0,
    PRIME_NEED_IDENTITY_SERIAL,
    PRIME_NEED_IDENTITY_THUNK,
    PRIME_NEED_IDENTITY_AUTHORITY,
    PRIME_NEED_IDENTITY_STORAGE_KEY,
    PRIME_NEED_IDENTITY_RECEIPT_SESSION,
    PRIME_NEED_IDENTITY_RECEIPT_NODE,
    PRIME_NEED_IDENTITY_SOURCE_OCCURRENCE,
    PRIME_NEED_IDENTITY_EVALUATOR,
    PRIME_NEED_IDENTITY_RESAMPLE_SCOPE,
    PRIME_NEED_IDENTITY_NAMESPACE_COUNT
} PrimeNeedIdentityNamespace;

const char *prime_need_identity_test_name(PrimeNeedIdentityNamespace ns);
/* The last identity the namespace issues. */
uint64_t prime_need_identity_test_last(PrimeNeedIdentityNamespace ns);
/* Places the next identity; zero is refused. */
bool prime_need_identity_test_seed(PrimeNeedIdentityNamespace ns,
                                   uint64_t next);
uint64_t prime_need_identity_test_next(PrimeNeedIdentityNamespace ns);
uint64_t prime_need_identity_test_take(PrimeNeedIdentityNamespace ns);
void prime_need_identity_test_reserve_storage_keys_through(uint64_t key);
#endif


#include "checkpoint_compare.h"
void prime_need_checkpoint_issuance(CettaCheckpointIssuance *);
bool prime_need_snapshot_compare(const PrimeNeedSnapshot *, const PrimeNeedSnapshot *, const CettaCheckpointCompare *);
bool prime_need_branch_state_compare(const PrimeNeedBranchState *, const PrimeNeedBranchState *, const CettaCheckpointCompare *);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
bool prime_need_recording_compare(const PrimeNeedRecording *, const PrimeNeedRecording *, const CettaCheckpointCompare *);
bool prime_need_receipt_compare(const PrimeNeedReceipt *, const PrimeNeedReceipt *, const CettaCheckpointCompare *);
#endif
#endif /* CETTA_PRIME_NEED_H */
