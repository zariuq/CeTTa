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

/* An explicitly computational argument remains a demand even when its
 * source contains open logic variables. Inferred arguments retain the
 * declaration-free template behavior. This role does not make demand eager. */
typedef enum {
    PRIME_NEED_SOURCE_INFERRED = 0,
    PRIME_NEED_SOURCE_COMPUTATION,
} PrimeNeedSourceRole;

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
    /* Identity of the actual cell update. A qualified view import keeps it
     * while adding a fresh record to the recipient's history. */
    uint64_t update_id;
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
    PrimeNeedSourceRole source_role;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    bool capture_known;
    const VarId *capture_var_ids;
    size_t capture_var_count;
#endif
} PrimeNeedCellView;

/* Operational Need-read support for one resident production. The admitted
 * common context, force entrance and query payloads are borrowed and must
 * remain owned by the host. The force entrance descends from that context;
 * it fixes both the expected reads and the beginning of produced updates.
 * This captures Need cells and stored-cell lookup/absence, not the surrounding
 * registry, Space, logical bindings, resource authority or scheduling policy.
 * A host must qualify those separately before reusing a production. */
typedef struct PrimeNeedReadCapture PrimeNeedReadCapture;
typedef struct PrimeNeedReadScope {
    PrimeNeedReadCapture *capture;
    struct PrimeNeedReadScope *previous;
    bool resident_boundary;
} PrimeNeedReadScope;

PrimeNeedReadCapture *prime_need_read_capture_new(const PrimeNeedSnapshot *context,
                                                const PrimeNeedSnapshot *entrance);
void prime_need_read_capture_free(PrimeNeedReadCapture *capture);
/* Freeze the currently earned support for a row. Subsequent reads cannot
 * mutate this copy. Payload ownership remains with the enclosing host. */
PrimeNeedReadCapture *prime_need_read_capture_copy(const PrimeNeedReadCapture *capture);
/* Independent metadata transport through the enclosing owner map. Returns
 * a fresh capture owner; snapshots and Atom images share the supplied copy
 * session with the producer's other roots. Both cuts and every dependency
 * origin are transported, including negative lookups and incomplete evidence.
 * The existing plain relocation profile excludes source forwarding, resolving
 * rewrites and destination interning. Borrowed payloads, if selected by the
 * session, keep their separate lifetime contract.
 * The source must stay live and quiescent during preparation. On refusal,
 * discard the unpublished destination graph and session. This supplies no
 * lifetime authority for external StateCell/Space identities. */
PrimeNeedReadCapture *prime_need_read_capture_copy_with_session(
    Arena *destination, AtomDeepCopySession *session,
    const PrimeNeedReadCapture *capture);
void prime_need_read_capture_storage(const PrimeNeedReadCapture *capture,CettaStorageCensus *census);
bool prime_need_read_capture_complete(const PrimeNeedReadCapture *capture);
/* The enclosing packet uses these exact captured cuts, including storage
 * watermarks. A separately copied capture must travel with its mapped cuts. */
bool prime_need_read_capture_has_cuts(const PrimeNeedReadCapture *capture,
    const PrimeNeedSnapshot *context, const PrimeNeedSnapshot *entrance);
void prime_need_read_scope_enter(PrimeNeedReadScope *scope, PrimeNeedReadCapture *capture);
/* Physical execution in another resident records only that resident's reads.
 * Successful qualified import propagates support in the recipient's world. */
void prime_need_read_scope_enter_resident(PrimeNeedReadScope *scope,PrimeNeedReadCapture *capture);
void prime_need_read_scope_leave(PrimeNeedReadScope *scope);

/* Install an already-earned completion and its introduced cells in a
 * descendant of the admitted common context. Only updates after the force
 * entrance are transported; unrelated caller history preceding that force
 * is not a production. Read/absence and write conflicts are checked before
 * publication. This neither runs the source nor identifies
 * sibling histories. The host retains source payload owners, merges the
 * qualified bindings/resources/factors, and commits its reader cursor only
 * after the complete return has been accepted. Refusal leaves out unchanged.
 * An incomplete capture is never permission to reuse. */
bool prime_need_snapshot_import_production(Arena *owner,
    const PrimeNeedReadCapture *capture, const PrimeNeedSnapshot *production,
    uint64_t thunk_id, const PrimeNeedSnapshot *recipient, PrimeNeedSnapshot *out);
CettaMatchCompletion prime_need_snapshot_import_production_checked(Arena *owner,
    const PrimeNeedReadCapture *capture, const PrimeNeedSnapshot *production,
    uint64_t thunk_id, const PrimeNeedSnapshot *recipient, PrimeNeedSnapshot *out);

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

/* Immutable branch-local reader values. Keys identify actual enclosing source
 * calls. Values are immutable, independently held reader versions supplied by
 * that call's owner. Retaining a value grants no invocation or world authority.
 * The host serializes payload callbacks with their actual services. */
typedef struct PrimeNeedReaderContext PrimeNeedReaderContext;
typedef struct {
    bool (*retain)(void *value);
    void (*release)(void *value);
    void (*storage)(const void *value, CettaStorageCensus *census);
    bool (*payloads)(const void *value, AtomTreePredicate atoms,
        bool (*cells)(const StateCell *, void *), void *context);
    /* The selected operational cache makes this lookup metadata redundant in
     * the exact supplied world. This grants no source exhaustion/cancellation
     * or independent callable transport. Missing support keeps it live. */
    CettaMatchCompletion (*settled)(const void *value,const PrimeNeedSnapshot *world);
    /* An existing owner explicitly includes this arena in its lifetime.
     * This permits internal relocation only, without retaining that owner,
     * renewing invocation borrows or licensing an independent graph copy. */
    bool (*lives_in)(const void *value,const Arena *arena);
    /* Independent copies require an explicit image of the actual owner in
     * the common copy session. These identify that provider's object record;
     * an absent record never grants permission to retain the source owner.
     * The provider separately supplies the owner's world and lifetime
     * transport; these record keys do not certify that transport. */
    const void *copy_kind;
    size_t copy_size;
} PrimeNeedReaderValueOps;
typedef struct {
    void *value;
    const PrimeNeedReaderValueOps *operations;
} PrimeNeedReaderValue;
void prime_need_reader_context_retain(PrimeNeedReaderContext *context);
void prime_need_reader_context_release(PrimeNeedReaderContext *context);
CettaMatchCompletion prime_need_reader_context_can_settle(const PrimeNeedReaderContext *,const PrimeNeedSnapshot *);
/* Return a tombstoned descendant only when every live value is settled.
 * The input and its outstanding source frontiers remain unchanged. */
CettaMatchCompletion prime_need_reader_context_settle(const PrimeNeedReaderContext *,const PrimeNeedSnapshot *,PrimeNeedReaderContext **out);
CettaMatchCompletion prime_need_reader_context_can_promote(const PrimeNeedReaderContext *,const PrimeNeedSnapshot *,const Arena *);
CettaMatchCompletion prime_need_reader_context_promote(const PrimeNeedReaderContext *,const PrimeNeedSnapshot *,const Arena *,PrimeNeedReaderContext **out);
bool prime_need_reader_context_values_absent(const PrimeNeedReaderContext *);
bool prime_need_reader_context_lookup(const PrimeNeedReaderContext *context,
    uint64_t key, PrimeNeedReaderValue *out);
/* Visit live immutable entries without transferring ownership. The caller
 * retains the context throughout; callbacks may build a distinct context,
 * but cannot mutate or release this borrowed input. */
CettaMatchCompletion prime_need_reader_context_visit(const PrimeNeedReaderContext *context,
    CettaMatchCompletion (*visit)(uint64_t key,const PrimeNeedReaderValue *value,void *state),
    void *state);
/* A provider can first prepare missing owner images. Both entry points use
 * the same retained context/lineage copier. The callback yields one owned
 * share with the same operations table and a distinct mapped value. The session
 * retains both endpoints until disposal, including on failure. Failure leaves
 * OUT unchanged and requires discarding the unpublished session. */
typedef CettaMatchCompletion (*PrimeNeedReaderValueCopy)(void *state,
    const PrimeNeedReaderValue *source, PrimeNeedReaderValue *image);
CettaMatchCompletion prime_need_reader_context_prepare_copy_with_session(
    const PrimeNeedReaderContext *source, AtomDeepCopySession *session,
    PrimeNeedReaderValueCopy prepare, void *state, PrimeNeedReaderContext **out);
/* Copy immutable reader versions and their complete ancestry, using only
 * owner images already registered by the enclosing provider. The source is
 * held quiescent through session disposal. Sharing within the graph is
 * preserved; unknown, stale and identity owner mappings refuse. A refusal
 * leaves out unchanged; discard its unpublished copy session. */
CettaMatchCompletion prime_need_reader_context_copy_with_session(
    const PrimeNeedReaderContext *source, Arena *owner,
    AtomDeepCopySession *session, PrimeNeedReaderContext **out);
/* Borrow an explicitly prepared metadata image. This lookup constructs
 * nothing and grants no independent ownership of a producer or its world. */
PrimeNeedReaderContext *prime_need_reader_context_copy_lookup(
    const PrimeNeedReaderContext *source, const AtomDeepCopySession *session);
/* NULL value removes a key. Only the newest values are held; version ancestry
 * retains no payloads or producers. out owns a nullable context and changes
 * only after complete preparation. Removing the last key preserves the empty
 * version, so an older merge cannot resurrect its retired reader. */
CettaMatchCompletion prime_need_reader_context_update(const PrimeNeedReaderContext *base,
    uint64_t key, const PrimeNeedReaderValue *value, PrimeNeedReaderContext **out);
/* Clear selected live values in one private descendant, retaining every key
 * and prior tombstone. Visit live entries once in ascending key order; the
 * predicate borrows immutable input and cannot release it. No-op keeps the
 * original context identity; refusal leaves out and all owners unchanged. */
CettaMatchCompletion prime_need_reader_context_clear_if(const PrimeNeedReaderContext *base,
    CettaMatchCompletion (*should_clear)(uint64_t key,const PrimeNeedReaderValue *value,
        void *state,bool *clear),void *state,PrimeNeedReaderContext **out);
/* An actual descendant wins over its ancestor. Incomparable versions may
 * combine equal/disjoint entries; different values of one key refuse. */
CettaMatchCompletion prime_need_reader_context_merge(const PrimeNeedReaderContext *left,
    const PrimeNeedReaderContext *right, PrimeNeedReaderContext **out);
/* Transport only reader entries changed/introduced after the actual source
 * entrance. Preserve unrelated recipient entries; conflicting changes refuse.
 * Already transported progress remains idempotent after later local updates.
 * The host independently qualifies source inputs, services and authority. */
CettaMatchCompletion prime_need_reader_context_import(const PrimeNeedReaderContext *entrance,
    const PrimeNeedReaderContext *production, const PrimeNeedReaderContext *recipient,
    PrimeNeedReaderContext **out);
void prime_need_reader_context_storage(const PrimeNeedReaderContext *context,
    CettaStorageCensus *census);
bool prime_need_reader_context_payloads(const PrimeNeedReaderContext *context,
    AtomTreePredicate atoms, bool (*cells)(const StateCell *, void *), void *state);

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
    PrimeNeedReaderContext *reader_context;
    /* Compact operational identity for one exact relational occurrence.
     * It preserves duplicate derivations when causal receipt observation is
     * absent; full rule-use evidence remains in `receipt` only when an
     * observer requests it. */
    uint64_t          occurrence_token;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    PrimeNeedReceipt  receipt;
#endif
} PrimeOccurrence;

/* copy initializes a new carrier; assign/clear operate on initialized ones.
 * Persistent machine carriers remain borrowed under their existing arena
 * contracts; the immutable reader context is a separately held ownership edge. */
void prime_occurrence_init(PrimeOccurrence *occurrence);
void prime_occurrence_copy(PrimeOccurrence *out, const PrimeOccurrence *source);
void prime_occurrence_assign(PrimeOccurrence *out, const PrimeOccurrence *source);
void prime_occurrence_clear(PrimeOccurrence *occurrence);

/* Scope entry refreshes the linked evaluator and binding operations. */
void prime_need_observation_policy_changed(void);

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
    /* Canonical operational State occurrence described by this observation.
     * Zero denotes an independent diagnostic event with no such link. */
    uint64_t state_event_id;
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
/* A sufficient cell-identity condition for discarding an extension: every
 * suffix frame belongs to a fresh anonymous, unimported cell above the base's
 * storage-key watermark. Updates to caller cells and stable named/imported
 * identities must survive. The caller separately checks returned references,
 * logical bindings, effects and weights before actually discarding anything. */
bool prime_need_snapshot_suffix_is_private(const PrimeNeedSnapshot *base,
                                            const PrimeNeedSnapshot *snapshot);
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
    PrimeNeedSourceRole source_role,
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
    PrimeNeedSourceRole source_role,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id);
bool prime_need_snapshot_allocate_persisted_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t storage_key,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id);
#endif
/* A cell born resolved to a ready value: the Need cell of sequencing
 * (`return value`), allocated and demanded at once, so its origin and its
 * cached value are the value itself. */
bool prime_need_snapshot_allocate_ready_value(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *value,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id);
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
bool prime_need_snapshot_allocate_ready_value_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *value,
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
/* Find the existing branch-local cell for a stored key and its instantiated
 * origin, using the same identity policy as persisted allocation. This read
 * neither allocates nor forces a cell. Refusal leaves the output unchanged. */
bool prime_need_snapshot_find_imported_cell(
    const PrimeNeedSnapshot *snapshot, uint64_t import_key,
    Atom *origin, uint64_t *out_thunk_id);
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
/* Successful lookup writes NULL only for an absent logical update. Refusal
 * leaves the output untouched; it must not license a physical-value fallback.
 * The caller retains the immutable state and all its payload owners. */
bool prime_need_branch_state_lookup(
    const PrimeNeedBranchState *state, StateCell *cell, Atom **out_value);
CettaMatchCompletion prime_need_branch_state_lookup_checked(
    const PrimeNeedBranchState *state, StateCell *cell, Atom **out_value);
/* A coherent value and its canonical supplying write in this State session.
 * A zero write ID denotes the selected initial value, including a read-only
 * frontier; a NULL value denotes no logical observation. Neither case reads
 * the physical cell. Resolve graft aliases by causal ancestry, never by node
 * depth or allocation order. The caller holds the State and payload owners.
 * Failure leaves both outputs unchanged. This inspection does not supply a
 * recorded receipt, resource authority, or permission to replay an operation. */
CettaMatchCompletion prime_need_branch_state_lookup_version_checked(
    const PrimeNeedBranchState *state, StateCell *cell,
    Atom **out_value, uint64_t *out_write_event_id);
/* Whether this validated State DAG holds the exact canonical write occurrence
 * for the selected cell. Graft aliases preserve the answer. A missing event
 * writes false; a reused ID with another role/cell is refusal. Failure leaves
 * the output unchanged. */
CettaMatchCompletion prime_need_branch_state_holds_write_checked(
    const PrimeNeedBranchState *state, StateCell *cell,
    uint64_t write_event_id, bool *out_held);
/* Agreement of canonical write occurrences/payloads and their causal order
 * for one selected cell. Unrelated cells and equal commuting read additions
 * do not distinguish the inputs. ABA writes do. Both held DAGs are validated;
 * malformed/cyclic or unavailable inspection is INCOMPLETE, not disagreement.
 * This does not compare the selected/frozen initial values or grant lifetime. */
CettaMatchCompletion prime_need_branch_state_same_writes_checked(
    const PrimeNeedBranchState *left,const PrimeNeedBranchState *right,StateCell *cell);
bool prime_need_branch_state_value(
    const PrimeNeedBranchState *state, StateCell *cell, Atom **out_value);

/* Resolve a held resource's selected initial value, only after lookup has
 * proved that no operational overlay supplies it. This is a passive read
 * in the caller's captured world; it must not consult an unrelated ambient
 * world or acquire authority. Failure leaves the answer unpublished. */
typedef CettaMatchCompletion (*PrimeNeedStateInitialValue)(
    void *context, StateCell *cell, Atom **out_value);

/* Graft actual post-entrance events onto a recipient of the common call.
 * All four carriers belong to that call's already-begun State session.
 * Preserve canonical event occurrences and source causal joins while giving
 * imported physical records fresh identities. Earlier unrelated producer
 * state stays outside the cut; the recipient retains its own state.
 *
 * The admitted four-cut profile requires preceding source writes on observed
 * cells to be held, and refuses competing recipient writes since common on
 * those cells, including ABA. Reusing an entirely
 * held suffix is idempotent even after later recipient writes. A partial
 * held suffix must be causally closed. The host separately qualifies Need,
 * weights, logical support and current resource authority before committing
 * the whole packet. Neither refusal status grants another source execution.
 * Source payload/resource owners remain held throughout preparation; out is
 * written only on MATCHED. */
CettaMatchCompletion prime_need_branch_state_import_suffix(Arena *owner,
    const PrimeNeedBranchState *common, const PrimeNeedBranchState *entrance,
    const PrimeNeedBranchState *production, const PrimeNeedBranchState *recipient,
    PrimeNeedStateInitialValue initial_value, void *context, PrimeNeedBranchState *out);

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
/* After its writers have joined, end this observer's write authority.
 * Existing roots remain readable; borrowed carriers cannot extend the diary.
 * New recording requires a new group. This does not reclaim shared storage. */
void prime_need_recording_seal(PrimeNeedRecording *recording);
/* End the recorder's observation. Borrowed carriers become absent; explicit
 * readers retain their own readable payload until their arena retires. The
 * caller owns the quiescent recorder and releases its reference separately. */
void prime_need_recording_forget(PrimeNeedRecording *recording);
void prime_need_recording_retain(void *recording);
void prime_need_recording_release(void *recording);
void prime_need_recording_carrier_retain(void *recording);
void prime_need_recording_carrier_release(void *recording);
/* Consume one payload owner and return one carrier owner, preserving the
 * control block for existing borrowed handles. Adoption transfers that
 * carrier owner into a newly prepared recorder's predecessor chain. */
void prime_need_recording_demote(PrimeNeedRecording *recording);
void prime_need_recording_adopt_previous(PrimeNeedRecording *recording,
    PrimeNeedRecording *previous);
void prime_need_recording_storage(const void *recording, CettaStorageCensus *census);
void prime_need_recording_carrier_storage(const void *recording, CettaStorageCensus *census);
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
/* A reader sets receipt.owner to its retaining arena. A borrowed carrier's
 * owner remains its slice arena. Carrying never weakens an acquired reader;
 * neither operation can restore a revoked reader or erased borrowed view. */
bool prime_need_recording_promote(Arena *dst, PrimeNeedReceipt *receipt);
bool prime_need_recording_promote_carrier(Arena *dst, PrimeNeedReceipt *receipt);
bool prime_need_recording_readable(const PrimeNeedReceipt *receipt);
bool prime_need_recording_same_group(
    const PrimeNeedReceipt *left, const PrimeNeedReceipt *right);
bool prime_need_recording_erased(const PrimeNeedReceipt *receipt);
bool prime_need_recording_audit_receipt(PrimeNeedArenaAudit *audit,
    const PrimeNeedReceipt *receipt);
bool prime_need_recording_excludes_arena(const PrimeNeedReceipt *receipt, const Arena *forbidden);
bool prime_need_recording_merge(PrimeNeedReceipt *dst, const PrimeNeedReceipt *src);
/* Dynamic allocation authority is separate from immutable captured roots.
 * A present scope with an empty slice silences recording after refusal;
 * it never falls back to unbounded diagnostic allocation. */
extern _Thread_local const PrimeNeedReceipt *g_prime_need_recording_scope;
typedef struct {
    const PrimeNeedReceipt *previous;
    bool entered;
} PrimeNeedRecordingScope;
/* An optional host hook may cache its execution policy after a scope change.
 * The host must not reenter or mutate the scope service from this hook.
 * Ordinary execution sends no notifications; standalone Need has no host. */
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
/* Project bounded recording evidence out of explicit receipt copies in one
 * unpublished graph-copy session. Select before the first bounded receipt
 * copy attempt. The default retains evidence; copy_enable selects an owned
 * duplicate. A selection is idempotent and cannot change once chosen, so
 * its receipt copies cannot change from retention to erasure partway through
 * the session.
 *
 * Only the copied bounded receipt is cleared. Operational Need/branch state,
 * coefficients and permissions follow their existing transport operations;
 * promotions and nonrecording diagnostic receipts keep their own contracts.
 * This adds no destination hold on a recording group and revokes no source
 * observer. Releasing the source remains its holders' responsibility. */
bool prime_need_recording_copy_forget(AtomDeepCopySession *session);
bool prime_need_recording_copy_erases(const AtomDeepCopySession *session);
/* Borrow a completed capability in an explicitly enabled session. Missing,
 * stale and already-taken images refuse. The handle remains caller-owned. */
PrimeNeedRecordingCopy *prime_need_recording_copy_lookup(
    const AtomDeepCopySession *session, const PrimeNeedRecording *source);
bool prime_need_recording_copy_enclosing_owner(PrimeNeedRecordingCopy *copy,
    const Arena *source, Arena *image);
bool prime_need_recording_copy_with_session(Arena *dst,
    AtomDeepCopySession *session, PrimeNeedReceipt *receipt);
bool prime_need_recording_copy_carrier_with_session(Arena *dst,
    AtomDeepCopySession *session, PrimeNeedReceipt *receipt);

static inline void prime_need_receipt_init(PrimeNeedReceipt *receipt) {
    prime_need_branch_state_init(receipt);
}
bool prime_need_receipt_begin(Arena *owner, PrimeNeedReceipt *receipt);
static inline bool prime_need_receipt_present(
    const PrimeNeedReceipt *receipt) {
    return prime_need_branch_state_present(receipt) &&
        (!receipt->recording || prime_need_recording_readable(receipt));
}
static inline bool prime_need_receipt_has_events(
    const PrimeNeedReceipt *receipt) {
    return prime_need_branch_state_has_events(receipt) &&
        (!receipt->recording || prime_need_recording_readable(receipt));
}
static inline bool prime_need_receipt_is_ancestor(
    const PrimeNeedReceipt *ancestor,
    const PrimeNeedReceipt *descendant) {
    if (!prime_need_receipt_has_events(ancestor)) return true;
    return prime_need_receipt_has_events(descendant) &&
        prime_need_branch_state_is_ancestor(ancestor, descendant);
}
static inline bool prime_need_receipt_compatible(
    const PrimeNeedReceipt *left, const PrimeNeedReceipt *right) {
    if ((left && left->recording) || (right && right->recording))
        return true;
    return prime_need_branch_state_compatible(left, right);
}
/* Scope entry selects the actual merge operation. The ordinary operation
 * still preserves recording data carried by either value, without consulting
 * the current recorder. An empty scoped reservation keeps its refusal policy. */
typedef bool (*PrimeNeedReceiptMerge)(PrimeNeedReceipt *, const PrimeNeedReceipt *);
extern _Thread_local PrimeNeedReceiptMerge prime_need_receipt_merge_operation;
static inline bool prime_need_receipt_merge(
    PrimeNeedReceipt *dst, const PrimeNeedReceipt *src) {
    return prime_need_receipt_merge_operation(dst, src);
}
/* Join causal evidence only after the host has qualified the corresponding
 * Need, State, weight, authority and logical-support transport. This operation
 * supplies no compatibility or replay authority of its own. Plain receipts
 * are homed in owner and may join otherwise-conflicting observation branches
 * because the operational import already ordered them. Recording receipts use
 * a compatible active bounded scope; an unrelated scope cannot replace a held
 * reader. Unavailable/foreign evidence is exposed as omission.
 * Refusal leaves dst unchanged, though a bounded target may become incomplete. */
CettaMatchCompletion prime_need_receipt_join_qualified(
    Arena *owner, PrimeNeedReceipt *dst, const PrimeNeedReceipt *src);
/* Select one exact dependency from an immutable entrance catalogue and join
 * its causal prefix through the same already-qualified boundary. Missing
 * bounded evidence marks the target incomplete; missing plain evidence is an
 * unavailable causal dependency. `found` changes only on successful return. */
CettaMatchCompletion prime_need_receipt_join_cached_anchor_qualified(
    Arena *owner, PrimeNeedReceipt *dst, const PrimeNeedReceipt *catalogue,
    uint64_t need_session_id, uint64_t thunk_id, uint64_t update_id, bool *found);
CettaMatchCompletion prime_need_receipt_join_state_anchor_qualified(
    Arena *owner, PrimeNeedReceipt *dst, const PrimeNeedReceipt *catalogue,
    uint64_t state_event_id, bool *found);
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
/* Internal execution carries handles, rather than owning an observer's
 * notes. A previously acquired reader keeps that role through transport. */
static inline bool prime_need_receipt_carry_with_session(Arena *dst,
    AtomDeepCopySession *session, PrimeNeedReceipt *receipt) {
    if (!atom_deep_copy_session_targets(session, dst)) return false;
    if (receipt && receipt->recording)
        return prime_need_recording_promote_carrier(dst, receipt);
    return prime_need_branch_state_promote_with_session(dst, session, receipt);
}
static inline bool prime_need_receipt_copy_carrier_with_session(Arena *dst,
    AtomDeepCopySession *session, PrimeNeedReceipt *receipt) {
    if (!atom_deep_copy_session_targets(session, dst)) return false;
    if (receipt && receipt->recording)
        return prime_need_recording_copy_carrier_with_session(dst, session, receipt);
    return prime_need_branch_state_copy_with_session(dst, session, receipt);
}
static inline bool prime_need_receipt_carry_suffix_with_session(Arena *dst,
    const Arena *forbidden, AtomDeepCopySession *session, PrimeNeedReceipt *receipt) {
    if (!atom_deep_copy_session_targets(session, dst)) return false;
    if (receipt && receipt->recording)
        return prime_need_recording_promote_carrier(dst, receipt);
    return prime_need_branch_state_promote_suffix_with_session(dst, forbidden, session, receipt);
}
static inline bool prime_need_receipt_excludes_arena(
    const PrimeNeedReceipt *receipt, const Arena *forbidden) {
    if (!prime_need_branch_state_has_events(receipt)) return true;
    if (receipt->recording)
        return prime_need_recording_excludes_arena(receipt, forbidden);
    return prime_need_branch_state_excludes_arena(receipt, forbidden);
}
static inline bool prime_need_arena_audit_receipt(
    PrimeNeedArenaAudit *audit, const PrimeNeedReceipt *receipt) {
    if (!prime_need_branch_state_has_events(receipt)) return true;
    if (receipt->recording) return prime_need_recording_audit_receipt(audit, receipt);
    return prime_need_arena_audit_branch_state(audit, receipt);
}
static inline bool prime_need_receipt_equal(
    const PrimeNeedReceipt *left, const PrimeNeedReceipt *right) {
    bool has_left = prime_need_receipt_has_events(left);
    bool has_right = prime_need_receipt_has_events(right);
    if (!has_left || !has_right) return !has_left && !has_right;
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
/* Observe the latest supplied operational State read/write. Its canonical
 * identity and payload come from that carrier, including after promotion or
 * State grafting; no operation is executed or inferred from equal values.
 * The caller supplies/retains the actual operational carrier and external
 * cell owners. This observation grants no authority to execute or replay it.
 * Diagnostic read_state/write_state above leave state_event_id zero.
 * Refusal leaves out unchanged. */
bool prime_need_receipt_record_state(Arena *owner,const PrimeNeedReceipt *base,
    const PrimeNeedBranchState *operation,PrimeNeedReceipt *out);
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
 * The view owns its node array and retains bounded recording payloads until
 * graph_free. Unrecorded Atom payloads and external StateCell resources remain
 * borrowed from their held owners. A successful recorded observation survives
 * later forgetting; forgetting forbids new observations through a borrowed
 * carrier. Hold the receipt's metadata owner during collection and keep its
 * selected frames immutable. Move owned views; do not duplicate their ownership
 * by copying the struct. This view does not authenticate causes, certify replay
 * or provide a snapshot of concurrently changing recording metadata. */
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
    PrimeNeedRecording *recording_owner;
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
/* For each node, report whether its exact cache production is present in
 * this view. Session, cell and update identities all participate. The
 * temporary index is built only for this requested read; the node order,
 * graph and borrowed payloads stay unchanged. Output has graph->length
 * flags and does not overlap the node array. Allocation refusal leaves
 * every output flag untouched. This is presence, not replay authority. */
bool prime_need_receipt_graph_producers(
    const PrimeNeedReceiptGraph *graph, bool *retained);
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
