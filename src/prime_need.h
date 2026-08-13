#ifndef CETTA_PRIME_NEED_H
#define CETTA_PRIME_NEED_H

#include "atom.h"

#ifndef CETTA_PRIME_NEED_HEAP_INDEX
#define CETTA_PRIME_NEED_HEAP_INDEX 0
#endif

#ifndef CETTA_PRIME_NEED_CLOSURE_CAPTURE
#define CETTA_PRIME_NEED_CLOSURE_CAPTURE 0
#endif

#ifndef CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
#define CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS 0
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
     * complete reachable frame path and its owned payloads share one arena.
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
} PrimeNeedReceiptEventKind;

typedef struct PrimeNeedReceiptFrame PrimeNeedReceiptFrame;

typedef struct {
    const PrimeNeedReceiptFrame *top;
    uint64_t session_id;
    Arena *owner;
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
#endif
    StateCell *state_cell;
    Atom *before;
    Atom *after;
} PrimeNeedReceiptEvent;

void prime_need_snapshot_init(PrimeNeedSnapshot *snapshot);
bool prime_need_snapshot_present(const PrimeNeedSnapshot *snapshot);
bool prime_need_snapshot_begin(PrimeNeedSnapshot *snapshot);
bool prime_need_snapshot_is_ancestor(const PrimeNeedSnapshot *ancestor,
                                     const PrimeNeedSnapshot *descendant);
bool prime_need_snapshot_merge(PrimeNeedSnapshot *dst,
                               const PrimeNeedSnapshot *src);
bool prime_need_snapshot_promote(Arena *dst,
                                 PrimeNeedSnapshot *snapshot);
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
bool prime_need_snapshot_allocate_source_argument(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id);
bool prime_need_snapshot_allocate_persisted(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t storage_key, PrimeNeedSnapshot *out,
    uint64_t *out_thunk_id);
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
bool prime_need_snapshot_resolve_stable_fault(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, Atom *fault, PrimeNeedSnapshot *out);
bool prime_need_snapshot_retry_evaluation(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, PrimeNeedSnapshot *out);
bool prime_need_snapshot_lookup(const PrimeNeedSnapshot *snapshot,
                                uint64_t thunk_id,
                                PrimeNeedCellView *out);

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
bool prime_need_branch_state_promote_suffix(
    Arena *dst, const Arena *forbidden, PrimeNeedBranchState *state);
bool prime_need_branch_state_promote_suffix_with_session(
    Arena *dst, const Arena *forbidden,
    AtomDeepCopySession *copy_session, PrimeNeedBranchState *state);
bool prime_need_branch_state_excludes_arena(
    const PrimeNeedBranchState *state, const Arena *forbidden);
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
    return prime_need_branch_state_compatible(left, right);
}
static inline bool prime_need_receipt_merge(
    PrimeNeedReceipt *dst, const PrimeNeedReceipt *src) {
    return prime_need_branch_state_merge(dst, src);
}
static inline bool prime_need_receipt_promote(
    Arena *dst, PrimeNeedReceipt *receipt) {
    return prime_need_branch_state_promote(dst, receipt);
}
static inline bool prime_need_receipt_promote_suffix(
    Arena *dst, const Arena *forbidden, PrimeNeedReceipt *receipt) {
    return prime_need_branch_state_promote_suffix(
        dst, forbidden, receipt);
}
static inline bool prime_need_receipt_promote_suffix_with_session(
    Arena *dst, const Arena *forbidden,
    AtomDeepCopySession *copy_session, PrimeNeedReceipt *receipt) {
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
bool prime_need_receipt_state_value(const PrimeNeedReceipt *receipt,
                                    StateCell *cell, Atom **out_value);
size_t prime_need_receipt_event_count(const PrimeNeedReceipt *receipt);
bool prime_need_receipt_event_at(const PrimeNeedReceipt *receipt,
                                 size_t index,
                                 PrimeNeedReceiptEvent *out_event);
#endif

#endif /* CETTA_PRIME_NEED_H */
