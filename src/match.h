#ifndef CETTA_MATCH_H
#define CETTA_MATCH_H

#include "atom.h"
#include "binding/frame_schema.h"
#include "gslt_term_view_v1.h"
#include "prime_need.h"
#include "term_universe.h"

/* ── Bindings ───────────────────────────────────────────────────────────── */

typedef enum {
    BINDING_VALUE_MATERIALIZED,
    BINDING_VALUE_CONTEXTUAL,
    BINDING_VALUE_CONTEXTUAL_PERSISTENT,
} BindingValueKind;

/* A frame reference is the same generational identity carried by variables.
 * It is independent of a binding image's storage addresses, so clone and
 * projection preserve it without a second namespace or rebasing pass. */
typedef struct {
    CettaFrameIdentity identity;
} BindingsFrameRef;

static inline bool bindings_frame_ref_is_valid(BindingsFrameRef ref) {
    return ref.identity != 0u;
}

/* A logical value retains syntax separately from its lexical interpretation.
 * Materialized syntax carries complete variable identities.  A contextual
 * value qualifies the skeleton's source variables.  A persistent contextual
 * value additionally certifies that the skeleton's owner outlives every
 * binding environment which may retain it; ordinary contextual values must
 * be transported at an ownership boundary.  The substitution itself is not
 * captured here: subsequent branch refinements remain observable.  Context
 * is stored by value, never as a pointer into a matcher's stack. Branch-local
 * row coordinates are read-view metadata, not lexical context. */
typedef struct {
    Atom *skeleton;
    /* The frame identity is the lexical environment; lookup uses its handle
     * directly and checks its generation against the authoritative frame. */
    uint32_t epoch;
    BindingValueKind kind;
} BindingValue;

_Static_assert(
    sizeof(BindingValue) == sizeof(Atom *) + 2u * sizeof(uint32_t),
    "BindingValue must not acquire alignment padding beyond its fields");

static inline BindingsFrameRef binding_value_frame_ref(BindingValue value) {
    return (BindingsFrameRef){
        .identity = value.epoch,
    };
}

static inline BindingValue binding_value_from_atom(Atom *atom) {
    return (BindingValue){.skeleton = atom, .kind = BINDING_VALUE_MATERIALIZED};
}

static inline BindingValue binding_value_from_context(
        Atom *skeleton, uint32_t epoch) {
    return (BindingValue){.skeleton = skeleton, .epoch = epoch,
                          .kind = BINDING_VALUE_CONTEXTUAL};
}

static inline BindingValue binding_value_from_persistent_context(
        Atom *skeleton, uint32_t epoch) {
    return (BindingValue){.skeleton = skeleton, .epoch = epoch,
                          .kind = BINDING_VALUE_CONTEXTUAL_PERSISTENT};
}

static inline bool binding_value_kind_is_contextual(BindingValueKind kind) {
    return kind == BINDING_VALUE_CONTEXTUAL ||
           kind == BINDING_VALUE_CONTEXTUAL_PERSISTENT;
}

static inline bool binding_value_is_contextual(BindingValue value) {
    return binding_value_kind_is_contextual(value.kind);
}

static inline bool binding_value_context_is_persistent(BindingValue value) {
    return value.kind == BINDING_VALUE_CONTEXTUAL_PERSISTENT;
}

static inline BindingValue binding_value_from_context_kind(
        Atom *skeleton, uint32_t epoch, BindingValueKind kind) {
    /* A scalar variable whose packed identifier already names a frame is
     * the same contextual coordinate before and after qualification.  Keep
     * that context explicit so a later binding does not mistake it for
     * context-free syntax and fall back to the generic environment domain. */
    if (kind == BINDING_VALUE_MATERIALIZED && skeleton &&
        skeleton->kind == ATOM_VAR &&
        var_epoch_suffix(skeleton->var_id) != 0u) {
        return binding_value_from_context(
            skeleton, var_epoch_suffix(skeleton->var_id));
    }
    return kind == BINDING_VALUE_CONTEXTUAL_PERSISTENT
        ? binding_value_from_persistent_context(skeleton, epoch)
        : kind == BINDING_VALUE_CONTEXTUAL
            ? binding_value_from_context(skeleton, epoch)
            : binding_value_from_atom(skeleton);
}

static inline VarId binding_value_variable_id(BindingValue value) {
    if (!value.skeleton || value.skeleton->kind != ATOM_VAR)
        return VAR_ID_NONE;
    return binding_value_is_contextual(value)
        ? var_epoch_id(value.skeleton->var_id, value.epoch)
        : value.skeleton->var_id;
}

typedef struct {
    VarId var_id;
    SymbolId spelling;
    Atom *name_key;
    BindingValue value;

} Binding;

typedef struct {
    BindingValue lhs;
    BindingValue rhs;
} BindingConstraint;

typedef struct BindingsOwners BindingsOwners;
typedef struct BindingsLookupIndex BindingsLookupIndex;
typedef struct BindingsFrameIndex BindingsFrameIndex;
typedef struct BindingsActivationView BindingsActivationView;
typedef struct BindingsFrameUndoEntry BindingsFrameUndoEntry;
typedef struct BindingsFrameRegistrationUndoEntry
    BindingsFrameRegistrationUndoEntry;
typedef struct BindingsExclusiveFrame BindingsExclusiveFrame;

typedef struct {
    BindingsOwners *owners; /* Immutable syntax owners retained by this version. */
    Binding *entries;
    uint32_t len;
    uint32_t cap;
    BindingConstraint *constraints;
    uint32_t eq_len;
    uint32_t eq_cap;
    /*
     * Cached summary of the substitution graph in `entries`.  Zero means
     * unknown, one means acyclic, and two means a cycle was witnessed.  The
     * entries remain authoritative; unknown state uses the full traversal.
    */
    uint8_t cycle_state;
    /*
     * Monotone conservative summary of variable ids occurring in binding
     * values.  Three bytes occupy the former alignment padding.  Rollback and
     * deletion may leave extra bits (a performance-only false positive), but
     * every live value's bits must remain present.
     */
    uint8_t rhs_variable_bloom[3];
    uint32_t private_entry_count;
    uint32_t private_constraint_count;
    /*
     * VarId -> newest-entry index for unframed variables and chronological
     * history.  Contextual frame slots do not consult it for current values.
     * Clones may share its immutable synchronized prefix; appends remain in a
     * lazy suffix until a generic lookup needs to extend the index.
     */
    BindingsLookupIndex *lookup_index;
    /*
     * Direct current-value storage for contextual variables whose activation
     * frame is known.  Frame slots are their sole value authority and the
     * undo log records their branch history.  The generic VarId index owns
     * only variables which have no registered frame.
     */
    BindingsFrameIndex *frame_index;
    /*
     * Frozen prefix of a captured image.  `entries`/`len`/`cap` remain the
     * exclusive suffix: `len` is the live total, `shared_len` the immutable
     * prefix length, and exclusive slots live at `entries[i - shared_len]`.
     * A unique image keeps `shared_len` at zero.
     */
    Binding *shared_entries;
    uint32_t shared_len;
    uint32_t shared_cap;
    /* Prime per-occurrence state -- the Need world (orthogonal to logical
     * substitutions) plus causal support/branch-local effects -- lives behind
     * this lazily-materialized pointer.  It is NULL for pure-HE evaluation, so
     * the shared matcher pays only 8 bytes here instead of the inlined
     * snapshot+receipt handles; Prime allocates the PrimeOccurrence on first
     * use.  Access via the bindings_need_* / bindings_receipt_* views below. */
    PrimeOccurrence *prime_ext;
} Bindings;

/* Read-only traversal of the logical substitution, including frame slots.
 * The returned record is a borrowed value; do not mutate the source image
 * while traversing it. Traversal order is unspecified. */
typedef struct {
    const Bindings *bindings;
    uint32_t row;
    uint32_t frame;
    uint32_t slot;
} BindingsIterator;
bool bindings_iterator_next(BindingsIterator *iterator, Binding *binding_out);

/* Storage index into the unframed prefix or suffix.  `i` must
 * be strictly less than `b->len`. */
static inline const Binding *bindings_entry_at(const Bindings *b, uint32_t i)
{
    if (i < b->shared_len)
        return &b->shared_entries[i];
    return &b->entries[i - b->shared_len];
}

typedef struct {
    Bindings *items;
    CettaCount len, cap;
} BindingSet;

#define CETTA_BINDING_SET_MAX_ROWS UINT64_MAX

typedef struct {
    uint32_t len;
    uint32_t eq_len;
    /*
     * Index into BindingsBuilder.prime_trail before this checkpoint's
     * optional effect snapshot.  The hot logical trail never embeds the
     * Prime payload: effect-free machines therefore retain only this compact
     * checkpoint, while effectful machines restore the exact cold snapshot.
     */
    uint32_t prime_state_mark;
    uint8_t cycle_state;
    /* Bit set iff the corresponding derived count was nonzero. */
    uint8_t derived_nonzero;
    uint8_t prime_state_present;
} BindingsBuilderTrailEntry;

typedef struct {
    BindingsOwners *previous;
    uint32_t trail_mark;
} BindingsOwnerUndo;

typedef struct {
    Bindings current;
    /* Ownership changes occur only at captured-version boundaries. Ordinary
     * logical checkpoints and writes allocate no ownership records. */
    BindingsOwnerUndo *owner_undo;
    uint32_t owner_undo_len, owner_undo_cap;
    /* Assigned for each successful initialization.  Revision-bound
     * accelerators retain this identity so a freed and reinitialized builder
     * at the same address cannot be mistaken for its predecessor.  Zero
     * disables such accelerators while ordinary bindings remain usable. */
    uint64_t instance_id;
    BindingsBuilderTrailEntry *trail;
    uint32_t trail_len;
    uint32_t trail_cap;
    /* Reverse chronological current-value writes for contextual slots.
     * Checkpoints store an index into this log, so rollback restores slots
     * directly rather than reconstructing them from chronological rows. */
    BindingsFrameUndoEntry *frame_undo;
    uint32_t frame_undo_len;
    uint32_t frame_undo_cap;
    /* Frame inventories have the same branch lifetime as their slot writes.
     * Registrations and schema extensions are restored from this log after
     * slot undo, without scanning the frame directory for dead candidates. */
    BindingsFrameRegistrationUndoEntry *frame_registration_undo;
    uint32_t frame_registration_undo_len;
    uint32_t frame_registration_undo_cap;
    PrimeOccurrence *prime_trail;
    uint32_t prime_trail_len;
    uint32_t prime_trail_cap;
    /*
     * Monotone count of successful logical writes performed by this builder.
     * Rollback restores logical state but deliberately does not erase work
     * already performed; collectors use this as a scheduling clock.
     */
    uint64_t growth_count;
    /*
     * Monotone count of destructive logical restores.  `growth_count`
     * distinguishes every successful append, while this counter prevents a
     * cache from confusing a state before and after rollback at the same
     * growth value.
     */
    uint64_t rollback_count;
    /*
     * An unobserved write region may share one entrance checkpoint across
     * consecutive append-only mutations.  `bindings_builder_save` ends the
     * current segment, so an observed intermediate rollback mark is never
     * coalesced away.
     */
    bool unobserved_write_region_active;
    bool unobserved_write_region_has_checkpoint;
    uint32_t unobserved_write_region_entry_mark;
    /* A registration made at the current trail length must make a later
     * save distinguish the state before and after that registration. */
    bool frame_registration_save_barrier;
} BindingsBuilder;

/*
 * Proof-erased classification of one immutable rule-pattern occurrence.
 * `source` remains semantic authority; the plan records exactly one node for
 * each authored occurrence and is valid only for the program snapshot that
 * owns that source.  Dynamic query structure and the live binding store are
 * deliberately absent.
 */
typedef struct CettaOpenPatternPlan CettaOpenPatternPlan;

/* Contiguous preorder realization of the same source-derived plan.  Each
 * instruction retains the exact tagged authored Atom occurrence plus only
 * support/cursor metadata; kind and arity remain available from that Atom,
 * and the instruction does not point back into the semantic plan tree.
 * `subtree_span` counts that occurrence and all descendants, so a dynamic
 * variable may advance over precisely one source subtree without scanning
 * it. */
typedef struct {
    Atom *source;
    uint64_t variable_mask;
    uint32_t subtree_span;
} CettaOpenPatternInstruction;

struct CettaOpenPatternPlan {
    Atom *source;
    AtomKind kind;
    CettaExprLen child_count;
    const CettaOpenPatternPlan *children;
    /* Exact subtree support over the equation template's sorted variable
     * inventory.  Common templates with at most 64 variables receive a
     * proof-erased bit mask; wider templates retain structural matching but
     * decline support-directed cycle admission. */
    const VarId *variable_ids;
    uint64_t variable_mask;
    /* Exact support whose source variable occurs at least twice in this
     * subtree.  This is derived while compiling the immutable source plan;
     * it admits shared observations only where at least one read can be
     * eliminated. */
    uint64_t repeated_variable_mask;
    /* Populated only on the root returned by the equation template.  Child
     * plan nodes remain ordinary exact occurrences and are referenced by the
     * root-owned preorder program. */
    const CettaOpenPatternInstruction *linear_program;
    uint32_t linear_program_len;
};

typedef Atom *(*BindingsRewriteVarFn)(Arena *a, Atom *var, void *ctx);
typedef Atom *(*BindingsAtomTransportFn)(void *context, Atom *atom);

void      bindings_init(Bindings *b);
void      bindings_free(Bindings *b);
bool      bindings_clone(Bindings *dst, const Bindings *src);
bool      bindings_copy(Bindings *dst, const Bindings *src);
/* Import the context-free part of an external substitution into one machine
 * frame. Modern VarId rows become authoritative frame slots, and open
 * materialized values become syntax interpreted in that same frame. Existing
 * contextual frames, Prime state, and entry order for
 * the remaining external rows are preserved. The operation is transactional. */
typedef struct CettaVarMap CettaVarMap;
bool      bindings_contextualize_unframed(Bindings *bindings, Arena *arena,
                                          CettaVarMap *inventory, uint32_t epoch);
/* Capture retains a frozen image.  Ensure both flat logical arrays are
 * exclusively writable before changing an entry or constraint. */
bool      bindings_prepare_logical_write(Bindings *bindings);
/* Transport the logical binding product through an identity-preserving Atom
 * representation map.  Entry order, VarIds, constraints,
 * and exact multiplicity are retained; derived indexes are rebuilt lazily.
 * Prime occurrence state has its own ownership algebra and is deliberately
 * refused rather than shallow-copied through this logical-only operation. */
bool      bindings_transport_logical(Bindings *dst, const Bindings *src,
                                     BindingsAtomTransportFn transport,
                                     void *context);
/*
 * Retain exactly the logical environment reachable from `roots`.
 *
 * Reachability follows binding values transitively and retains every
 * constraint connected to that closure.
 * Prime's orthogonal occurrence state is copied unchanged.
 */
bool      bindings_project_reachable(const Bindings *src,
                                     Atom *const *roots,
                                     size_t root_count,
                                     Bindings *dst);
/* As above, while translating each entry-prefix boundary through the
 * projection.  A mark names the number of logical binding entries preceding
 * a live activation frame; it is updated to the corresponding prefix length
 * in `dst`.  The operation is transactional with respect to the marks. */
bool      bindings_project_reachable_with_entry_marks(
              const Bindings *src, Atom *const *roots,
              size_t root_count, uint32_t *entry_marks,
              size_t entry_mark_count, Bindings *dst);
struct CettaTermVariableSupport;

/* A persistent source term can name a fresh activation namespace without
 * first being copied into that namespace.  Each epoch root contributes the
 * variables of `atom`, rewritten through `epoch`, to the same reachability
 * closure as ordinary materialized roots.  When present, `variable_support`
 * is the immutable structural summary owned by the term's universe; a NULL
 * summary retains the exact traversal fallback. */
typedef struct {
    Atom *atom;
    uint32_t epoch;
    const struct CettaTermVariableSupport *variable_support;
} BindingsEpochRoot;

/*
 * Direct view of one finite, immutable source-variable inventory under a
 * fresh activation epoch.  Inventory arrays come from compiled source
 * metadata. Reads resolve the stable frame identity in the current branch
 * image; no slot-storage pointer is retained. `first_write_version` selects
 * the activation's writes. Missing slots remain unbound variables.
 */
struct BindingsActivationView {
    const VarId *source_ids;
    Atom *const *source_variables;
    BindingsFrameRef authority_ref;
    uint32_t len;
    VarId source_first_id;
    bool source_ids_contiguous;
    uint32_t epoch;
    BindingValueKind source_kind;
    uint32_t first_entry;
    uint64_t first_write_version;
};

void      bindings_activation_view_init(BindingsActivationView *frame);
void      bindings_activation_view_free(BindingsActivationView *frame);
bool      bindings_activation_view_prepare(
              BindingsActivationView *frame,
              BindingsBuilder *builder,
              const VarId *source_ids, Atom *const *source_variables,
              uint32_t variable_count, uint32_t epoch,
              uint32_t first_entry);
/* Borrow the complete frame already published for one compiled equation.
 * The schema owns and validates the ordered source-id inventory; the aligned
 * source-variable array remains owned by the equation template.  A missing
 * publication is installed once, while the common path reads the existing
 * authoritative slots without repeating schema registration. */
bool      bindings_activation_view_prepare_schema(
              BindingsActivationView *frame,
              BindingsBuilder *builder,
              BindingsFrameSchema *schema,
              Atom *const *source_variables,
              uint32_t epoch, uint32_t first_entry);
/* A view names an activation independently of the builder that prepared it.
 * Its inventory and source syntax must outlive the view. Each read explicitly
 * chooses a live or captured binding image that owns the referenced frame. */
bool      bindings_activation_view_available(
              const BindingsActivationView *frame,
              const Bindings *bindings);
/* A read borrow is valid only while its image and inventory remain unchanged
 * and alive. It owns no storage and must not cross a bind, rollback, projection,
 * promotion or image release. Mutating consumers use read_slot instead. */
struct BindingsFrameIndexEntry;
typedef struct {
    const BindingsActivationView *view;
    const struct BindingsFrameIndexEntry *authority;
} BindingsActivationRead;
bool      bindings_activation_view_borrow(
              const Bindings *bindings, const BindingsActivationView *frame,
              BindingsActivationRead *read_out);
bool      bindings_activation_read_slot(
              const BindingsActivationRead *read, uint32_t source_slot,
              BindingValue *value_out, bool *present_out);
bool      bindings_activation_view_read_slot(
              const Bindings *bindings,
              const BindingsActivationView *frame, uint32_t source_slot,
              BindingValue *value_out, bool *present_out);
bool      bindings_project_reachable_with_epoch_roots(
              const Bindings *src, Atom *const *roots,
              size_t root_count, const BindingsEpochRoot *epoch_roots,
              size_t epoch_root_count, Bindings *dst);
bool      bindings_project_reachable_with_epoch_roots_and_entry_marks(
              const Bindings *src, Atom *const *roots,
              size_t root_count, const BindingsEpochRoot *epoch_roots,
              size_t epoch_root_count, uint32_t *entry_marks,
              size_t entry_mark_count, Bindings *dst);
/*
 * Replace `full` by the branch-relative suffix beyond `base` when `base` is
 * an exact ordered prefix of its logical bindings and constraints.
 *
 * Exact ordered comparison decides the prefix relation directly: any mismatch
 * leaves `full` unchanged and reports `factored == false`.  Prime occurrence
 * state is orthogonal to the logical prefix and is retained whenever it
 * differs from the base.  This lets evaluator boundaries carry only newly
 * learned logical substitutions while preserving the canonical
 * full-environment fallback.
 */
bool      bindings_factor_prefix(Bindings *full, const Bindings *base,
                                 bool *factored,
                                 uint64_t *logical_items_elided);
bool      bindings_promote_atoms_to_arena(Bindings *bindings, Arena *dst);
bool      bindings_promote_logical_atoms_to_arena(Bindings *bindings,
                                                   Arena *dst);
bool      bindings_promote_logical_atoms_with_session(
              Bindings *bindings, AtomDeepCopySession *session);
bool      bindings_logical_atoms_closed_for_arena(
              const Bindings *bindings, const Arena *arena);
/* Conservative support query over the logical substitution carrier only.
 * Prime occurrence state is intentionally excluded: callers use this to
 * decide whether a visible continuation can retain a registry capability. */
bool      bindings_logical_has_registry_refs(const Bindings *bindings);
size_t    bindings_entry_active_bytes(void);
size_t    bindings_constraint_active_bytes(void);
void      bindings_thread_cache_free(void);
void      bindings_move(Bindings *dst, Bindings *src);
void      bindings_replace(Bindings *dst, Bindings *src);
bool      bindings_remove_entry_at(Bindings *bindings, uint32_t index);
/* Replace an existing logical value through its authoritative coordinate. */
bool      bindings_rewrite_value_id(Bindings *bindings, VarId id,
                                    BindingValue value);
/* Call after rewriting binding keys outside the Bindings API. */
bool      bindings_invalidate_after_key_rewrite(Bindings *bindings);

/* Prime per-occurrence (prime_ext) views.  Reads are valid even when the
 * occurrence is absent -- they return a shared zero-initialized singleton,
 * matching the former zero-inited inline fields.  Mutable views materialize the
 * PrimeOccurrence on first use (Prime-only; HE evaluation never calls them). */
const PrimeNeedSnapshot *bindings_need_view(const Bindings *b);
const PrimeNeedBranchState *bindings_branch_state_view(const Bindings *b);
PrimeNeedSnapshot        *bindings_need_mut(Bindings *b);
PrimeNeedBranchState      *bindings_branch_state_mut(Bindings *b);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
const PrimeNeedReceipt    *bindings_receipt_view(const Bindings *b);
PrimeNeedReceipt          *bindings_receipt_mut(Bindings *b);
#endif
uint64_t                  bindings_occurrence_token(const Bindings *b);
bool                      bindings_refresh_occurrence_token(Bindings *b);
bool                      bindings_prime_present(const Bindings *b);
/* Copy both Prime components from src into dst (materializing dst's occurrence
 * only when src has present state; otherwise dst's occurrence is released). */
void      bindings_prime_assign(Bindings *dst, const Bindings *src);
/* Set dst's Prime components from explicit values (builder-trail restore, which
 * snapshots by value); clears dst's occurrence when both values are absent. */
void      bindings_prime_set(Bindings *dst, const PrimeNeedSnapshot *need,
                             const PrimeNeedBranchState *branch_state,
                             uint64_t occurrence_token,
                             const PrimeNeedReceipt *receipt);
/* Borrow the value without discarding its lexical interpretation. */
BindingValue bindings_lookup_value_id(Bindings *b, VarId var_id);
/* Register the complete slot domain of a newly created contextual frame
 * before its syntax can execute.  Source ids are stable, unqualified ids in
 * ascending order; `epoch` supplies the frame coordinate. */
bool      bindings_register_contextual_frame(
              Bindings *bindings, const VarId *source_ids,
              uint32_t source_len, uint32_t epoch);
bool      bindings_builder_register_contextual_frame(
              BindingsBuilder *builder, const VarId *source_ids,
              uint32_t source_len, uint32_t epoch);
/* Admit the complete variable inventory of one activation.  A completed
 * frame is closed: matchers consume its indexed coordinates without
 * rediscovering subterm schemas, and later attempts cannot add identifiers
 * to that epoch. */
bool      bindings_register_complete_contextual_frame(
             Bindings *bindings, const VarId *source_ids,
             uint32_t source_len, uint32_t epoch);
bool bindings_register_complete_frame_schema(
    Bindings *bindings, BindingsFrameSchema *schema, uint32_t epoch);
/* Admit one immutable presented schema into an activation whose complete
 * variable inventory is not known in advance.  Later schemas for the same
 * epoch are merged by source id until that frame is explicitly completed. */
bool bindings_builder_register_frame_schema(
    BindingsBuilder *builder, BindingsFrameSchema *schema, uint32_t epoch);
bool bindings_builder_register_complete_frame_schema(
    BindingsBuilder *builder, BindingsFrameSchema *schema, uint32_t epoch);
/* Runtime-created slots extend the activation's value array, leaving its
 * prepared syntax inventory immutable. No source-id inventory is collected. */
Atom *bindings_builder_new_variable(BindingsBuilder *builder, Arena *arena,
                                    CettaFrameIdentity frame);
/* One continuation-owned activation frame.  Writes remain private while a
 * equation candidate is being tested.  A failed candidate discards the region;
 * a surviving candidate publishes its ordered slot writes into the shared
 * substitution exactly once.  Only writes to an outer frame retain an
 * explicit payload until that boundary. */
BindingsExclusiveFrame *bindings_exclusive_frame_new(void);
void bindings_exclusive_frame_free(BindingsExclusiveFrame *frame);
bool bindings_exclusive_frame_begin(
    BindingsExclusiveFrame *frame, BindingsFrameSchema *schema,
    uint32_t epoch);
/* As begin, for an identity minted for this activation: no binding outside
 * the frame may mention it.  The frame then elides occurs checks that the
 * freshness proves vacuous, until a value it stores mentions the frame. */
bool bindings_exclusive_frame_begin_fresh(
    BindingsExclusiveFrame *frame, BindingsFrameSchema *schema,
    uint32_t epoch);
bool bindings_exclusive_frame_freeze(
    BindingsExclusiveFrame *frame, BindingsBuilder *builder);
/* Publish a rule-local candidate directly into authoritative frame slots.
 * This path accepts only values owned by the candidate's complete schema;
 * writes to an outer frame still require the general composition path. */
bool bindings_exclusive_frame_publish_slots(
    BindingsExclusiveFrame *frame, BindingsBuilder *builder);
uint64_t bindings_builder_frame_write_boundary(
    const BindingsBuilder *builder);
bool bindings_exclusive_frame_has_external_writes(
    const BindingsExclusiveFrame *frame);
bool bindings_exclusive_frame_aliases_normalized(
    const BindingsExclusiveFrame *frame, bool *cross_frame_alias);
bool bindings_builder_aliases_normalized_since(
    const BindingsBuilder *builder, uint32_t trail_mark,
    uint32_t first_entry, uint32_t identity, bool *cross_frame_alias);
Atom *bindings_apply_exclusive_frame_then_all(
    BindingsBuilder *builder, Arena *arena, Atom *atom,
    const BindingsExclusiveFrame *frame);
/* Register the exact variable inventory carried by authored activation
 * syntax when no compiled template inventory is available. */
bool      bindings_register_activation_source_frame(
              Bindings *bindings, Atom *source, uint32_t epoch);
bool      bindings_builder_register_activation_source_frame(
              BindingsBuilder *builder, Atom *source, uint32_t epoch);
/* Resolve one original activation-view variable through the rule-local
 * binding suffix, then through ordinary outer variable links, without
 * allocating a substituted term.  A successful call with
 * `*ground_out == NULL` means the exact closed value is not directly
 * available and the caller must defer to the general view semantics. */
bool      bindings_resolve_epoch_view_ground(
              const Bindings *bindings, const Atom *source_variable,
              uint32_t epoch, uint32_t first_entry, Atom **ground_out);
Atom     *binding_variable_atom(Arena *a, const Binding *binding);
/* Follow root aliases without substitution or demand. The returned syntax
 * retains its lexical context; false means the chain cannot certify a root. */
bool      bindings_resolve_value_preview(
              Bindings *b, BindingValue value, BindingValue *out);
/* Resolve a type substitution by variable identity. */
bool      bindings_resolve_value_exact(
              Bindings *b, BindingValue value, BindingValue *out);
/* Adapter from the shared immutable term-view interface to a Bindings
 * environment.  It follows only a variable's root chain and never constructs
 * a substituted term, so evaluator dialects can share structural observers
 * without sharing their control semantics. Open contextual results return
 * DEFER because this Atom-only interface cannot preserve their context. */
CettaGsltTermViewStatusV1 bindings_resolve_term_view_root_v1(
              void *context, Atom *source_variable, Atom **target_out);
static inline CettaGsltTermViewV1 bindings_term_view_v1(
        Atom *source, const Bindings *bindings) {
    return (CettaGsltTermViewV1){
        .source = source,
        .resolve = bindings_resolve_term_view_root_v1,
        .resolve_context = (void *)bindings,
    };
}

/* A stable read of the current binding image. A NULL cursor scope denotes
 * an ordinary outer term; `frame` denotes an authored activation subtree.
 * The image and frame must remain unchanged throughout the consuming call. */
typedef struct {
    const Bindings *bindings;
    const BindingsActivationView *frame;
} BindingsTermCursorContextV1;
CettaGsltTermViewStatusV1 bindings_resolve_term_cursor_v1(
    void *context, CettaGsltTermCursorV1 source,
    CettaGsltTermCursorV1 *target_out);
bool      bindings_add_id(Bindings *b, VarId var_id, SymbolId spelling, Atom *val);
bool      bindings_add_id_acyclic(Bindings *b, VarId var_id,
                                  SymbolId spelling, Atom *val);
bool      bindings_add_var(Bindings *b, Atom *var, Atom *val);
bool      bindings_add_var_acyclic(Bindings *b, Atom *var, Atom *val);
bool      bindings_add_constraint(Bindings *b, Atom *lhs, Atom *rhs);
bool      bindings_try_merge(Bindings *dst, const Bindings *src);
bool      bindings_try_merge_live(Bindings *dst, const Bindings *src);
bool      bindings_clone_merge(Bindings *dst, const Bindings *base,
                               const Bindings *extra);
bool      bindings_contains_private_variant_slots(const Bindings *b);
void      bindings_assert_no_private_variant_slots(const Bindings *b);
/* True when the substitution has at least one authoritative current value,
 * whether that value is represented by a chronological row or only by its
 * owning frame slot. */
bool      bindings_has_bound_values(const Bindings *b);
size_t    bindings_frame_binding_count(const Bindings *bindings,
                                       BindingsFrameRef frame);
/* Count authoritative logical assignments independently of their storage.
 * Framed variables contribute their bound slots; unframed rows are disjoint.
 * This operation is constant time. */
bool      bindings_current_binding_count(const Bindings *bindings,
                                         size_t *count_out);
/* Representation-independent emptiness of the logical substitution.  Prime
 * state is orthogonal and is intentionally not part of this predicate. */
static inline bool bindings_logically_empty(const Bindings *b) {
    return !b || (!bindings_has_bound_values(b) && b->eq_len == 0u);
}
Atom     *bindings_apply(Bindings *b, Arena *a, Atom *atom);
static inline Atom *bindings_apply_if_vars(const Bindings *b, Arena *a, Atom *atom) {
    if (!b || !bindings_has_bound_values(b) ||
        !atom || !atom_has_vars(atom))
        return atom;
    return bindings_apply((Bindings *)b, a, atom);
}
Atom     *bindings_apply_value(const Bindings *b, Arena *a, BindingValue value);
/* Observe a value with one binding key hidden, preserving its lexical context.
 * The original branch is unchanged; this is an observation operation. */
Atom     *bindings_apply_value_without_id(
              const Bindings *b, Arena *a, VarId skip_id, BindingValue value);
Atom     *bindings_apply_rewrite_vars(Bindings *b, Arena *a, Atom *atom,
                                      BindingsRewriteVarFn rewrite_var,
                                      void *rewrite_ctx);
Atom     *bindings_apply_epoch(Bindings *b, Arena *a, Atom *atom, uint32_t epoch);
/* Apply an epoch-standardized term through only the binding suffix beginning
 * at `first_entry`.  This is the activation-frame view used when a caller has
 * already instantiated the query through the older prefix: rule-local slots
 * are substituted now, while unresolved outer variables remain live trail
 * references for later demand. */
Atom     *bindings_apply_epoch_since(Bindings *b, Arena *a, Atom *atom,
                                     uint32_t epoch, uint32_t first_entry);
/* Compose activation-local substitution with the full outer environment in
 * one traversal.  Source variables consult only the activation suffix;
 * variables reached through their values consult the complete environment. */
Atom     *bindings_apply_epoch_then_all(Bindings *b, Arena *a, Atom *atom,
                                        uint32_t epoch,
                                        uint32_t first_entry);
/* Activation-view realization of bindings_apply_epoch_then_all.  It has the
 * same result and ownership contract; the finite activation inventory avoids
 * repeated hash/index lookup for source-local variables. */
Atom     *bindings_apply_activation_view_then_all(
              Bindings *bindings, Arena *a, Atom *atom,
              const BindingsActivationView *frame);
/* Resolve one compiler-known source occurrence by its dense slot.  This is
 * extensionally the same as applying the corresponding source variable
 * through bindings_apply_activation_view_then_all, without searching the
 * inventory for an identifier already decided at admission. */
Atom     *bindings_apply_activation_view_slot_then_all(
              Bindings *bindings, Arena *a,
              const BindingsActivationView *frame,
              Atom *source_variable, uint32_t slot);
/* Resolve only the root of a compiler-known slot.  Nested structure remains
 * paired with the live environment for an exact downstream consumer. */
Atom     *bindings_resolve_activation_view_slot_root(
              Bindings *bindings, Arena *a,
              const BindingsActivationView *frame,
              Atom *source_variable, uint32_t slot);
Atom     *atom_freshen_epoch(Arena *a, Atom *atom, uint32_t epoch);
/* Reify lexical identities at a consumer that requires an Atom.  This does
 * not apply branch substitutions or force Prime Need state.  Unchanged
 * subterms may be shared with the source; promotion is a separate operation. */
Atom     *binding_value_materialize(Arena *a, BindingValue value);
bool      binding_value_equal(BindingValue left, BindingValue right);
/* Capture an owned substitution value, opaque to syntax substitution.
 * bindings_to_atom is only the explicit structural observation/codec. */
Atom *bindings_capture_value(Arena *arena, const Bindings *bindings);
bool bindings_restore_captured_value(const Atom *atom, Bindings *out);
/* Exact logical support of an environment, including saved environments in
 * its range. The caller owns the returned array. No syntax is materialized. */
bool bindings_collect_support(const Bindings *bindings,
                              VarId **ids, size_t *count);
Atom     *bindings_to_atom(Arena *a, const Bindings *b);
/* Textual keys resolve only in the supplied receiving syntax and environment.
 * Missing or ambiguous names fail; explicit variable keys already carry identity.
 * Decode is transactional and does not merge into or mutate the receiver. */
bool bindings_from_atom_scoped(Atom *atom, const Atom *scope,
                               const Bindings *receiver, Bindings *out);
bool bindings_from_atom(Atom *atom, Bindings *out);
/* Version ownership is independent of value storage. These operations retain
 * immutable syntax; they never introduce another substitution authority. */
void bindings_owners_retain(BindingsOwners *owners);
void bindings_owners_release(BindingsOwners *owners);
void bindings_inherit_owners(Bindings *destination, const Bindings *source);
void      binding_set_init(BindingSet *bs);
void      binding_set_free(BindingSet *bs);
bool      binding_set_push(BindingSet *bs, const Bindings *b);
bool      binding_set_push_move(BindingSet *bs, Bindings *b);
bool      bindings_builder_init(BindingsBuilder *bb, const Bindings *base);
void      bindings_builder_init_owned(BindingsBuilder *bb, Bindings *owned);
/* Clone one complete rollback-capable branch state.  The destination owns
 * independent binding and trail arrays and receives a fresh builder identity;
 * immutable Atom graphs retain their existing owners until an enclosing
 * branch image promotes them into its own arena. */
bool      bindings_builder_clone(BindingsBuilder *dst,
                                 const BindingsBuilder *src);
/* Move every Atom-bearing current or rollback state into `owner`.  The
 * builder arrays remain independently owned.  This is the lifetime boundary
 * used by a materialized branch image after `bindings_builder_clone`. */
bool      bindings_builder_promote_atoms_to_arena(
              BindingsBuilder *bb, Arena *owner);
/* Promote only Prime need/effect/receipt state in the current binding image
 * and its rollback checkpoints.  Logical binding atoms are deliberately
 * excluded so an enclosing branch-image copy can preserve their sharing with
 * continuation roots through its own AtomDeepCopySession. */
bool      bindings_builder_promote_prime_atoms_to_arena(
              BindingsBuilder *bb, Arena *owner);
void      bindings_builder_free(BindingsBuilder *bb);
/* Begin/end one non-nested region whose intermediate physical states are not
 * observed.  Begin captures the exact entrance mark.  End either publishes
 * the final state or restores that mark before the region becomes observable;
 * ordinary saves and rollbacks remain exact internal barriers. */
bool      bindings_builder_begin_unobserved_write_region(
              BindingsBuilder *bb);
void      bindings_builder_end_unobserved_write_region(
              BindingsBuilder *bb, bool publish);
uint32_t  bindings_builder_save(BindingsBuilder *bb);
void      bindings_builder_rollback(BindingsBuilder *bb, uint32_t mark);
void      bindings_builder_commit(BindingsBuilder *bb);
/* True when either the current state or a rollback checkpoint carries an
 * optional Prime occurrence. */
bool      bindings_builder_prime_present(const BindingsBuilder *bb);
/* Reserve storage for a known upper bound of fresh logical entries without
 * changing the current bindings or trail. */
bool      bindings_builder_prepare_fresh_entries(
              BindingsBuilder *bb, uint32_t additional_entries);
/*
 * Retain only bindings reachable from `roots` while preserving every live
 * rollback state named by `checkpoint_marks`.
 *
 * Marks are rewritten in place to address the compacted trail.  The operation
 * is transactional: on failure neither the builder nor the mark array changes.
 * Logical entries and constraints remain in their original relative order.
 */
bool      bindings_builder_compact_reachable(
              BindingsBuilder *bb, Atom *const *roots, size_t root_count,
              uint32_t *checkpoint_marks, size_t checkpoint_count,
              uint64_t *discarded_logical_items,
              uint64_t *discarded_trail_entries);
bool      bindings_builder_compact_reachable_with_epoch_roots(
              BindingsBuilder *bb, Atom *const *roots, size_t root_count,
              const BindingsEpochRoot *epoch_roots,
              size_t epoch_root_count,
              uint32_t *checkpoint_marks, size_t checkpoint_count,
              uint64_t *discarded_logical_items,
              uint64_t *discarded_trail_entries);
/* Preserve both rollback checkpoints and logical entry-prefix boundaries.
 * The latter are used by activation views whose source variables may consult
 * only bindings created at or after a particular frame boundary. */
bool      bindings_builder_compact_reachable_with_entry_marks(
              BindingsBuilder *bb, Atom *const *roots, size_t root_count,
              uint32_t *checkpoint_marks, size_t checkpoint_count,
              uint32_t *entry_marks, size_t entry_mark_count,
              uint64_t *discarded_logical_items,
              uint64_t *discarded_trail_entries);
bool      bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
              BindingsBuilder *bb, Atom *const *roots, size_t root_count,
              const BindingsEpochRoot *epoch_roots,
              size_t epoch_root_count,
              uint32_t *checkpoint_marks, size_t checkpoint_count,
              uint32_t *entry_marks, size_t entry_mark_count,
              uint64_t *discarded_logical_items,
              uint64_t *discarded_trail_entries);
bool      bindings_builder_add_id_fresh(BindingsBuilder *bb, VarId var_id,
                                        SymbolId spelling, Atom *val);
bool      bindings_builder_add_var_fresh(BindingsBuilder *bb, Atom *var,
                                         Atom *val);
bool      bindings_builder_try_merge(BindingsBuilder *bb, const Bindings *src);
const Bindings *bindings_builder_bindings(const BindingsBuilder *bb);
void      bindings_builder_take(BindingsBuilder *bb, Bindings *out);

/* ── One-way pattern matching ───────────────────────────────────────────── */

/* Match pattern (may contain vars) against target (ground).
   On success, fills bindings and returns true.
   On failure, returns false (bindings undefined). */
bool simple_match(Atom *pattern, Atom *target, Bindings *b);
bool simple_match_builder(Atom *pattern, Atom *target, BindingsBuilder *bb);

/* ── Variable renaming (standardization apart, à la Vampire) ───────────── */

#ifdef CETTA_TEST_HOOKS
/* Drop only the derived VarId index so projection-path tests can observe
 * whether an operation rebuilds it.  Logical bindings are unchanged. */
void bindings_lookup_index_test_clear(Bindings *bindings);
/* Observe the derived index prefix certificate without exposing its
 * production representation.  Returns false when no index is present. */
bool bindings_lookup_index_test_synced_len(const Bindings *bindings,
                                           uint32_t *synced_len_out);
bool bindings_lookup_index_test_single_cache_support(
    const Bindings *bindings, size_t *support_len_out,
    size_t *capacity_out);
/* Observe the partition between contextual frame coordinates and the
 * generic VarId index.  A known frame slot can be unbound, in which case
 * entry_index_out is UINT32_MAX. */
bool bindings_frame_index_test_lookup(
    const Bindings *bindings, VarId id, bool *known_out,
    uint32_t *entry_index_out);
/* Whether the frame still certifies that no binding mentions it. */
bool bindings_exclusive_frame_test_certified(
    const BindingsExclusiveFrame *frame);
bool bindings_frame_storage_test_identity(
    const Bindings *bindings, uint32_t epoch,
    const void **schema_out, const void **slots_out);
bool bindings_current_binding_count_test(
    const Bindings *bindings, size_t *count_out);
bool bindings_lookup_index_test_generic_contains(
    Bindings *bindings, VarId id, bool *present_out);
bool bindings_frame_ref_test_for_epoch(
    const Bindings *bindings, uint32_t epoch, BindingsFrameRef *ref_out);
bool bindings_frame_ref_test_resolves(
    const Bindings *bindings, BindingsFrameRef ref,
    uint32_t *epoch_out);
#endif

/* Rename all variables in atom: $name → $name#suffix.
   Returns new arena-allocated atom. Non-variable atoms returned as-is. */
Atom *rename_vars(Arena *a, Atom *atom, uint32_t suffix);

/* Rename all variables in atom except the variables mentioned anywhere inside
   `ignore_spec`. Non-ignored variables are freshened consistently per original
   identity. Canonical ABT `(idx k)` expressions contain no metavariables and
   remain inert. Returns a new arena-allocated atom, the original atom if
   unchanged, or NULL for a malformed cyclic variable-bearing graph. */
Atom *rename_vars_except(Arena *a, Atom *atom, Atom *ignore_spec);

/* Complement of rename_vars_except: rename ONLY the variables mentioned
   anywhere inside `listed_spec` (freshened consistently per original
   identity); every other variable keeps its identity — SWI copy_term/4's
   sharing contract.  An empty listed_spec is the identity. */
Atom *rename_vars_only(Arena *a, Atom *atom, Atom *listed_spec);

/* ── Bidirectional matching (match_atoms from HE spec) ─────────────────── */

/* Match left against right. Variables on EITHER side can bind.
   On success, fills bindings and returns true.
   On failure, returns false. */
/* The ordinary worklist accepts independent lexical contexts on both sides. */
bool match_binding_values(BindingValue left, BindingValue right, Bindings *b);
bool match_binding_values_builder(BindingValue left, BindingValue right, BindingsBuilder *bb);
/* Match one already-owned query closure against a freshly standardized rule
 * pattern.  The query value carries its lexical context by value, so callers
 * need neither a borrowed dense frame nor an eagerly substituted Atom. */
bool match_binding_value_epoch_builder_rule_local(
         BindingValue left, Atom *right, BindingsBuilder *bb,
         Arena *a, uint32_t right_epoch);
bool match_binding_value_epoch_builder_rule_local_planned(
         BindingValue left, Atom *right,
         const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t right_epoch);
/* The exclusive-frame forms take the rule pattern's ownership as
 * `right_kind`: BINDING_VALUE_CONTEXTUAL_PERSISTENT when its syntax owner
 * outlives the execution, as program-owned equation syntax does, so a
 * binding shares the pattern's subterms; BINDING_VALUE_CONTEXTUAL when a
 * binding must carry its own copy into `a`. */
bool match_binding_value_epoch_builder_rule_local_in_exclusive_frame(
         BindingValue left, Atom *right, BindingValueKind right_kind,
         const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t right_epoch,
         BindingsExclusiveFrame *exclusive, bool linear);
bool match_atoms(Atom *left, Atom *right, Bindings *b);
bool match_atoms_builder(Atom *left, Atom *right, BindingsBuilder *bb);
/* Same matcher when the caller already holds the attempt frame
 * (BindingDecision).  Lookups of that frame's coordinates are slot
 * indices; other identifiers keep the environment map. */
bool match_atoms_builder_with_attempt_frame(
        Atom *left, Atom *right, BindingsBuilder *bb,
        const BindingsActivationView *left_frame);
bool match_atoms_epoch(Atom *left, Atom *right, Bindings *b, Arena *a, uint32_t epoch);
/* Epoch-aware matcher over an existing trail-backed environment.  The caller
 * owns the save/rollback boundary when failure must be transactional. */
bool match_atoms_epoch_builder(Atom *left, Atom *right,
                               BindingsBuilder *bb, Arena *a,
                               uint32_t epoch);
/* Equation-activation-frame orientation of the same relation.  When two otherwise unbound
 * variables meet, bind the standardized-apart right rule slot to the live
 * left call variable.  This keeps a successful frame local when possible;
 * callers must still audit the appended keys and roll back on escape. */
bool match_atoms_epoch_builder_rule_local(
         Atom *left, Atom *right, BindingsBuilder *bb,
         Arena *a, uint32_t epoch);
/* The same equation-activation-frame matcher with a source-derived finite plan for the
 * right rule pattern.  The plan may remove repeated source classification
 * and source-side cycle bookkeeping, but never supplies query facts or
 * binding authority.  A mismatched plan fails without changing the caller's
 * save/rollback contract. */
bool match_atoms_epoch_builder_rule_local_planned(
         Atom *left, Atom *right, const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t epoch);
/* Experimental contiguous realization of the same exact plan.  It is kept
 * separate from the ordinary planned matcher until a measured physical-cost
 * predicate earns selection; callers retain the same save/rollback contract. */
bool match_atoms_epoch_builder_rule_local_linear(
         Atom *left, Atom *right, const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t epoch);
bool match_atoms_epoch_builder_rule_local_in_exclusive_frame(
         Atom *left, Atom *right, BindingValueKind right_kind,
         const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t epoch,
         BindingsExclusiveFrame *exclusive, bool linear);
/* Match an activation-local source term without first materializing the
 * complete substituted term.  Variables in `left_original` are interpreted
 * through `left_epoch` and the binding suffix beginning at
 * `left_first_entry`; values reached through those bindings retain their
 * own materialized or contextual lexical interpretation.  The right term is standardized through
 * `right_epoch` exactly as in match_atoms_epoch_builder.
 *
 * This is the demand-driven realization of
 *
 *   match_atoms_epoch_builder(
 *       bindings_apply_epoch_then_all(..., left_original, ...),
 *       right_original, ...)
 *
 * and has the same caller-owned save/rollback contract.  Invalid activation
 * boundaries fail closed. */
bool match_atoms_epoch_view_builder(
         Atom *left_original, uint32_t left_epoch,
         uint32_t left_first_entry, Atom *right_original,
         BindingsBuilder *bb, Arena *a, uint32_t right_epoch);
/* Match the same activation-local source view against an ordinary live term.
 * Unlike `match_atoms_epoch_view_builder`, the right operand keeps its current
 * variable identities.  This is the demand-driven equivalent of applying the
 * activation environment to the left source and then calling
 * match_atoms_builder. */
bool match_atoms_epoch_view_builder_current(
         Atom *left_original, uint32_t left_epoch,
         uint32_t left_first_entry, Atom *right,
         BindingsBuilder *bb, Arena *a);
/* Equation-activation-frame orientation of the activation view: unify a persistent open
 * head against the goal's skeleton and environment without first forcing a
 * substituted instance (SubstitutionAlgebra; Abadi, Cardelli, Curien, Lévy).
 * Materialise at observation.  Same save/rollback contract as
 * match_atoms_epoch_builder_rule_local on the forced term. */
bool match_atoms_epoch_view_builder_rule_local(
         Atom *left_original, uint32_t left_epoch,
         uint32_t left_first_entry, Atom *right_original,
         BindingsBuilder *bb, Arena *a, uint32_t right_epoch);
bool match_atoms_epoch_view_builder_rule_local_planned(
         Atom *left_original, uint32_t left_epoch,
         uint32_t left_first_entry, Atom *right_original,
         const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t right_epoch);
bool match_atoms_epoch_view_builder_rule_local_linear(
         Atom *left_original, uint32_t left_epoch,
         uint32_t left_first_entry, Atom *right_original,
         const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t right_epoch);
bool match_atoms_epoch_view_builder_rule_local_in_exclusive_frame(
         Atom *left_original, uint32_t left_epoch,
         uint32_t left_first_entry, Atom *right_original,
         BindingValueKind right_kind,
         const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t right_epoch,
         BindingsExclusiveFrame *exclusive, bool linear);
/* Exact matcher for the same activation view when its finite source-variable
 * inventory has already been resolved into dense slots. */
bool match_atoms_activation_view_builder(
         Atom *left_original, const BindingsActivationView *left_frame,
         Atom *right_original, BindingsBuilder *bb, Arena *a,
         uint32_t right_epoch);
/* Match the same dense activation view against an ordinary live term.  The
 * right operand keeps its current variable identities instead of receiving
 * a fresh epoch.  This is the allocation-free form of applying the frame to
 * the left source and then invoking match_atoms_builder. */
bool match_atoms_activation_view_builder_current(
         Atom *left_original, const BindingsActivationView *left_frame,
         Atom *right, BindingsBuilder *bb, Arena *a);
/* The same dense left view with right-side rule-slot orientation. */
bool match_atoms_activation_view_builder_rule_local(
         Atom *left_original, const BindingsActivationView *left_frame,
         Atom *right_original, BindingsBuilder *bb, Arena *a,
         uint32_t right_epoch);
bool match_atoms_activation_view_builder_rule_local_planned(
         Atom *left_original, const BindingsActivationView *left_frame,
         Atom *right_original, const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t right_epoch);
bool match_atoms_activation_view_builder_rule_local_linear(
         Atom *left_original, const BindingsActivationView *left_frame,
         Atom *right_original, const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t right_epoch);
bool match_atoms_activation_view_builder_rule_local_in_exclusive_frame(
         Atom *left_original, const BindingsActivationView *left_frame,
         Atom *right_original, BindingValueKind right_kind,
         const CettaOpenPatternPlan *right_plan,
         BindingsBuilder *bb, Arena *a, uint32_t right_epoch,
         BindingsExclusiveFrame *exclusive, bool linear);
/* Leaf-patch view (env CETTA_LEAF_PATCH_VIEW=1, OFF by default). */
bool match_leaf_patch_view_enabled(void);
/* Positional bind for a flat linear pattern (lhs) vs a non-variable-arg query;
 * self-sound (refuses non-linear / non-flat / var-query / pre-bound by falling
 * back with no partial binding). Result == match_atoms_epoch on that shape. */
bool match_atoms_epoch_positional_linear(Atom *query, Atom *lhs, Bindings *b,
                                         Arena *a, uint32_t epoch);
/* Builder form of the same admitted view.  It does not clone the environment:
 * callers that want a general-matcher fallback must save both the builder and
 * arena marks, then roll back on false before invoking that fallback. */
bool match_atoms_epoch_positional_linear_builder(
         Atom *query, Atom *lhs, BindingsBuilder *bb,
         Arena *a, uint32_t epoch);
bool match_binding_value_atom_id_epoch(
         BindingValue left, const TermUniverse *candidate_universe,
         AtomId right_id, Bindings *b, Arena *a, uint32_t epoch);
bool match_atoms_atom_id_epoch(Atom *left, const TermUniverse *candidate_universe,
                               AtomId right_id, Bindings *b, Arena *a,
                               uint32_t epoch);

/* Alpha-equivalence on atoms: two atoms are equivalent up to a bijective
   renaming of variable names. */
bool atom_alpha_eq(Atom *left, Atom *right);

/* Compare bindings as a set of (var,value) pairs, ignoring entry order. */
bool bindings_eq(Bindings *a, Bindings *b);
char *arena_tagged_var_name(Arena *a, const char *name, uint32_t suffix);

/* ── Loop-binding rejection (occurs check, HE spec metta.md line 435) ── */

/* Whole-environment audit: does any variable reach itself through the
   current bindings?  The bind paths refuse such an edge when it is written,
   so an environment built through them is acyclic after every successful
   bind.  This audit is for environments that arrive from outside those
   paths (parsed or decoded rows, wholesale key rewrites) and for the trial
   write inside the bind when cheaper evidence cannot decide. */
bool bindings_has_loop(const Bindings *b);

/* ── Type matching (from HE spec Matching.lean:188-195) ────────────────── */

/* Ordered HE type matching: actual first, expected second. %Undefined% is
   gradual at any depth on either side; Atom is top only on the expected side. */
bool match_types(Atom *actual, Atom *expected, Bindings *b);
bool match_types_builder(Atom *actual, Atom *expected, BindingsBuilder *bb);

/* SpaceType and concrete (Space discipline) value types form one runtime
 * space class; negative-decision paths must not refute across it. */
bool type_match_uses_space_class_bridge(Atom *actual, Atom *expected);

#endif /* CETTA_MATCH_H */
