#ifndef CETTA_PROOF_BATCHES_H
#define CETTA_PROOF_BATCHES_H
#include "proof_row.h"

/* All TU/pool ownership operations are caller-serialized, including final
 * view/cursor release. Atomic handle refs alone do not permit concurrent TU
 * mutation, collection or root-list retirement. Immutable views/languages own
 * only backward dependencies and never retain a live head. This interface
 * supplies no source execution/publication authority. */
typedef struct CettaProofBatchStore CettaProofBatchStore;
typedef struct CettaProofBatchTransaction CettaProofBatchTransaction;
typedef struct CettaProofBatchView CettaProofBatchView;
typedef struct CettaProofBatchLanguage CettaProofBatchLanguage;
typedef struct CettaProofBatchCursor CettaProofBatchCursor;

typedef enum {
    CETTA_PROOF_BATCH_OK = 0,
    CETTA_PROOF_BATCH_DUPLICATE,
    CETTA_PROOF_BATCH_DECLINED,
    CETTA_PROOF_BATCH_CONFLICT,
    CETTA_PROOF_BATCH_SEALED,
    CETTA_PROOF_BATCH_CAPACITY,
    CETTA_PROOF_BATCH_INVALIDATED,
    CETTA_PROOF_BATCH_STALE,
} CettaProofBatchStatus;

/* Retains physical TU AND symbol-session storage, independently of the
 * mutable Space/head. Retaining a container that owns later published tail
 * is not a suitable implementation. One successful retain is balanced by
 * release after the final immutable dependency and root provider retire. */
typedef struct {
    void *owner;
    bool (*retain)(void *owner);
    void (*release)(void *owner);
} CettaProofBatchLifetime;

typedef struct {
    uint32_t states, batches, max_rank;
    uint64_t stored_rows, producer_offers, producer_occurrences, version;
} CettaProofBatchStats;
/* Rank is generation from the seed, not the source decreasing counter. */
typedef struct {
    uint32_t state, rank;
    AtomId key;
    uint64_t support;
    /* Identity borrowed through the view. It is an immutable language,
     * not a pointer into mutable State metadata or a live directory. */
    const CettaProofBatchLanguage *language;
} CettaProofBatchGroup;
typedef struct {
    uint32_t child_state;
    uint64_t appended_rows, producer_occurrences;
    bool duplicate;
} CettaProofBatchOffer;
typedef struct {
    uint64_t appended_rows, producer_offers, producer_occurrences;
    uint32_t new_states, new_batches;
} CettaProofBatchResult;

/* Exactly one closed scalar seed. Keys are complete alpha-canonical records;
 * the fixed layout erases one proof field while preserving whole-key aliases.
 * The admitted data fragment matches the ordinary proof-row decoder. */
CettaProofBatchStatus cetta_proof_batch_store_new(TermUniverse *,
    const CettaProofRowLayout *, AtomId root_key, AtomId seed,
    CettaProofLimits, CettaProofBatchLifetime, CettaProofBatchStore **out);
void cetta_proof_batch_store_release(CettaProofBatchStore *);
CettaProofBatchStats cetta_proof_batch_store_stats(const CettaProofBatchStore *);
/* Requested persistent bytes owned by the live head: immutable dependencies,
 * live directories and its cached sealed views, excluding caller cursors and
 * read-only view controls. Saturation cannot forgive memory pressure. */
size_t cetta_proof_batch_store_retained_bytes(const CettaProofBatchStore *);
bool cetta_proof_batch_store_matches_universe(const CettaProofBatchStore *, const TermUniverse *);
/* Borrowed metadata under the same caller-serialized store lifetime. */
const CettaProofRowLayout *cetta_proof_batch_store_layout(const CettaProofBatchStore *);

/* Optional fixed descending-counter certificate for source execution. A new
 * binding is permitted only while the store contains its sole seed. It
 * validates that seed's actual erased-key integer coordinate and changes the
 * transaction version. Rebinding to a different coordinate declines. Every
 * subsequently offered child is checked against root_counter - generation.
 * These data invariants do not grant source/effect execution permission. */
CettaProofBatchStatus cetta_proof_batch_store_bind_rank(CettaProofBatchStore *,
    const CettaProofFieldPath *, int64_t *root_counter);
bool cetta_proof_batch_store_rank_generation(const CettaProofBatchStore *,
    const CettaProofFieldPath *, int64_t source_counter, int64_t target_counter,
    uint32_t *generation);

/* Capture current groups without closing them, or freeze an input rank for
 * producers. Sealing is allocation-atomic and closes the whole rank. A new
 * contribution to a closed rank declines; an existing transition duplicate
 * still produces its frozen parent's count without appending rows. */
CettaProofBatchStatus cetta_proof_batch_capture_rank(CettaProofBatchStore *,
    uint32_t rank, CettaProofBatchView **out);
CettaProofBatchStatus cetta_proof_batch_seal_rank(CettaProofBatchStore *,
    uint32_t rank, CettaProofBatchView **out);
CettaProofBatchStatus cetta_proof_batch_capture(CettaProofBatchStore *,
    CettaProofBatchView **out);
bool cetta_proof_batch_view_retain(CettaProofBatchView *);
void cetta_proof_batch_view_release(CettaProofBatchView *);
uint64_t cetta_proof_batch_view_count(const CettaProofBatchView *);
uint32_t cetta_proof_batch_view_groups(const CettaProofBatchView *);
bool cetta_proof_batch_view_group(const CettaProofBatchView *, uint32_t,
    CettaProofBatchGroup *out);
const CettaProofRowLayout *cetta_proof_batch_view_layout(const CettaProofBatchView *);
bool cetta_proof_batch_view_current(const CettaProofBatchView *);

/* Offers are authored-order PREPARATION only. Parents must come from a
 * sealed rank view of this store. Whole child keys are numbered by the caller
 * while its unifier bindings remain valid. Every fallible allocation/root
 * lease precedes commit. Failure poisons this transaction, never the store.
 * Duplicate offers return W producer occurrences and append no stored rows.
 * The caller must not expose provisional result counts before commit. */
CettaProofBatchStatus cetta_proof_batch_begin(CettaProofBatchStore *,
    CettaProofBatchTransaction **out);
CettaProofBatchStatus cetta_proof_batch_offer(CettaProofBatchTransaction *,
    const CettaProofBatchView *parents, uint32_t group,
    AtomId exact_label, AtomId complete_child_key, CettaProofBatchOffer *out);
bool cetta_proof_batch_transaction_for_store(const CettaProofBatchTransaction *,
    const CettaProofBatchStore *);
bool cetta_proof_batch_transaction_result(const CettaProofBatchTransaction *,
    CettaProofBatchResult *out);
/* Allocation-free commit validates the captured store version before ANY
 * publication. External source tokens/authority remain the publisher's duty. */
CettaProofBatchStatus cetta_proof_batch_commit(CettaProofBatchTransaction *);
void cetta_proof_batch_transaction_release(CettaProofBatchTransaction *);

#define CETTA_PROOF_BATCH_ALL_STATES UINT32_MAX
typedef enum {
    CETTA_PROOF_BATCH_ROW,
    CETTA_PROOF_BATCH_PAUSED,
    CETTA_PROOF_BATCH_END,
    CETTA_PROOF_BATCH_CURSOR_INVALIDATED,
    CETTA_PROOF_BATCH_CURSOR_CAPACITY,
} CettaProofBatchNext;
typedef struct {
    CettaProofWord word;
    uint64_t ordinal;
    bool has_ordinal;
} CettaProofBatchWord;
/* Whole views enumerate authored append intervals. Rank views enumerate the
 * captured state/language bag and do not supply Space occurrence ordinals.
 * Only selected languages are expanded. Skipped chunks/groups cost quota.
 * Source IDs are retained through the view. Pending labels stay borrowed
 * through pause/repeated peek until commit/free/storage reset. Generated
 * full rows must be adopted by the caller before committing the word. */
CettaProofBatchStatus cetta_proof_batch_cursor_new(CettaProofBatchView *,
    uint32_t selected_state, CettaProofBatchCursor **out);
CettaProofBatchNext cetta_proof_batch_cursor_peek(CettaProofBatchCursor *,
    CettaProofBudget *, CettaProofBatchWord *out);
bool cetta_proof_batch_cursor_commit(CettaProofBatchCursor *, uint64_t token);
CettaProofBatchStatus cetta_proof_batch_cursor_clone(const CettaProofBatchCursor *,
    CettaProofBatchCursor **out);
/* Restores enumeration/pending position only. It never rewinds live storage
 * or applies/replays effects. Refusal leaves the destination unchanged. */
CettaProofBatchStatus cetta_proof_batch_cursor_restore(CettaProofBatchCursor *,
    const CettaProofBatchCursor *snapshot);
void cetta_proof_batch_cursor_release(CettaProofBatchCursor *);

/* Space storage adapters, declared here because they use the batch value
 * types. Promotion retains the existing physical seed row, adds no semantic
 * occurrence, and owns its new store. Append publishes only newly stored
 * support; duplicate producer results do not mutate the Space. The caller
 * still supplies source/effect authority before publication. */
SpaceProofImageStatus space_promote_proof_seed(Space *,
    const CettaProofRowLayout *, AtomId root_key, AtomId seed,
    CettaProofLimits, CettaProofBatchLifetime);
/* Borrowed live store: caller keeps the Space and its serialized TU domain
 * throughout use. Read-only views returned by capture own their independent
 * backward dependencies and physical domain lifetime. */
CettaProofBatchStore *space_proof_batch_store_borrow(Space *, SymbolId head);
CettaProofBatchStatus space_publish_proof_batch(Space *, SymbolId head,
    CettaProofBatchTransaction *, CettaProofBatchResult *out);

/* Exact requested control/array bytes and live object counts for this module,
 * not RSS or a per-worker allocation charge. Diagnostic reads do not reset
 * collector debt or acknowledge any collection. */
typedef struct {
    uint64_t stores, transactions, views, cursors, chunks, languages, roots;
    uint64_t bytes;
} CettaProofBatchDiagnostics;
CettaProofBatchDiagnostics cetta_proof_batch_diagnostics(void);
#endif
