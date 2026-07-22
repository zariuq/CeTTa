#ifndef CETTA_PRIME_NEED_H
#define CETTA_PRIME_NEED_H

#include "atom.h"

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
 * update is O(1), and promotion copies the path and its atom payloads into a
 * longer-lived arena.
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

typedef struct {
    const PrimeNeedFrame *top;
    uint64_t session_id;
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
    PRIME_NEED_RECEIPT_EVALUATE_CELL,
    PRIME_NEED_RECEIPT_RESAMPLE,
} PrimeNeedReceiptEventKind;

typedef struct PrimeNeedReceiptFrame PrimeNeedReceiptFrame;

typedef struct {
    const PrimeNeedReceiptFrame *top;
    uint64_t session_id;
    Arena *owner;
} PrimeNeedReceipt;

typedef struct {
    PrimeNeedReceiptEventKind kind;
    uint64_t event_id;
    uint64_t need_session_id;
    uint64_t thunk_id;
    uint64_t source_occurrence_id;
    uint64_t source_argument_index;
    uint64_t rule_occurrence_id;
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

void prime_need_receipt_init(PrimeNeedReceipt *receipt);
bool prime_need_receipt_begin(Arena *owner, PrimeNeedReceipt *receipt);
bool prime_need_receipt_present(const PrimeNeedReceipt *receipt);
bool prime_need_receipt_is_ancestor(const PrimeNeedReceipt *ancestor,
                                    const PrimeNeedReceipt *descendant);
bool prime_need_receipt_compatible(const PrimeNeedReceipt *left,
                                   const PrimeNeedReceipt *right);
bool prime_need_receipt_merge(PrimeNeedReceipt *dst,
                              const PrimeNeedReceipt *src);
bool prime_need_receipt_promote(Arena *dst, PrimeNeedReceipt *receipt);
bool prime_need_receipt_equal(const PrimeNeedReceipt *left,
                              const PrimeNeedReceipt *right);

bool prime_need_receipt_observe_cell(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id, Atom *outcome,
    PrimeNeedReceipt *out);
bool prime_need_receipt_observe_source_cell(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    Atom *outcome, PrimeNeedReceipt *out);
bool prime_need_receipt_evaluate_source_cell(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    Atom *origin, PrimeNeedReceipt *out);
bool prime_need_receipt_inspect_origin(
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
bool prime_need_receipt_resample(
    Arena *owner, const PrimeNeedReceipt *base, Atom *origin,
    PrimeNeedReceipt *out);
bool prime_need_receipt_state_value(const PrimeNeedReceipt *receipt,
                                    StateCell *cell, Atom **out_value);
size_t prime_need_receipt_event_count(const PrimeNeedReceipt *receipt);
bool prime_need_receipt_event_at(const PrimeNeedReceipt *receipt,
                                 size_t index,
                                 PrimeNeedReceiptEvent *out_event);

#endif /* CETTA_PRIME_NEED_H */
