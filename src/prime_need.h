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
 * longer-lived arena. */

typedef enum {
    PRIME_NEED_UNEVALUATED = 0,
    PRIME_NEED_BLACKHOLE,
    PRIME_NEED_VALUE,
    PRIME_NEED_FAULT,
} PrimeNeedCellState;

typedef struct PrimeNeedFrame PrimeNeedFrame;

typedef struct {
    const PrimeNeedFrame *top;
    uint64_t session_id;
} PrimeNeedSnapshot;

typedef struct {
    PrimeNeedCellState state;
    Atom *payload;
} PrimeNeedCellView;

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
bool prime_need_snapshot_update(Arena *owner,
                                const PrimeNeedSnapshot *base,
                                uint64_t thunk_id,
                                PrimeNeedCellState state,
                                Atom *payload,
                                PrimeNeedSnapshot *out);
bool prime_need_snapshot_lookup(const PrimeNeedSnapshot *snapshot,
                                uint64_t thunk_id,
                                PrimeNeedCellView *out);

Atom *prime_need_ref(Arena *arena, const PrimeNeedSnapshot *snapshot,
                     uint64_t thunk_id);
bool prime_need_ref_parse(const Atom *atom, uint64_t *session_id,
                          uint64_t *thunk_id);
bool prime_need_ref_belongs_to(const Atom *atom,
                               const PrimeNeedSnapshot *snapshot,
                               uint64_t *thunk_id);

#endif /* CETTA_PRIME_NEED_H */
