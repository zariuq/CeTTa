#ifndef CETTA_MATCH_H
#define CETTA_MATCH_H

#include "atom.h"
#include "prime_need.h"
#include "term_universe.h"

/* ── Bindings ───────────────────────────────────────────────────────────── */

typedef struct {
    VarId var_id;
    SymbolId spelling;
    Atom *name_key;
    Atom *val;
    bool legacy_name_fallback;
} Binding;

typedef struct {
    Atom *lhs;
    Atom *rhs;
} BindingConstraint;

typedef struct BindingsLookupIndex BindingsLookupIndex;

typedef struct {
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
     * Derived count of entries that participate in the legacy spelling-keyed
     * lookup relation.  Modern VarId-only environments keep this at zero, so
     * a failed identity lookup never scans the whole environment merely to
     * prove that no spelling fallback exists.
     */
    uint32_t legacy_fallback_count;
    uint32_t private_entry_count;
    uint32_t private_constraint_count;
    /*
     * Derived VarId -> newest-entry accelerator.  `entries` remains the
     * semantic authority: clones may share its immutable synchronized prefix.
     * Appends remain in an authoritative lazy suffix until an uncached lookup
     * needs to extend the index; rollback truncates only the indexed prefix.
     */
    BindingsLookupIndex *lookup_index;
    /* Prime per-occurrence state -- the Need world (orthogonal to logical
     * substitutions) plus causal support/branch-local effects -- lives behind
     * this lazily-materialized pointer.  It is NULL for pure-HE evaluation, so
     * the shared matcher pays only 8 bytes here instead of the inlined
     * snapshot+receipt handles; Prime allocates the PrimeOccurrence on first
     * use.  Access via the bindings_need_* / bindings_receipt_* views below. */
    PrimeOccurrence *prime_ext;
} Bindings;

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
    Bindings current;
    /* Assigned for each successful initialization.  Revision-bound
     * accelerators retain this identity so a freed and reinitialized builder
     * at the same address cannot be mistaken for its predecessor.  Zero
     * disables such accelerators while ordinary bindings remain usable. */
    uint64_t instance_id;
    BindingsBuilderTrailEntry *trail;
    uint32_t trail_len;
    uint32_t trail_cap;
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
} BindingsBuilder;

typedef Atom *(*BindingsRewriteVarFn)(Arena *a, Atom *var, void *ctx);
typedef bool (*BindingsEpochCoordinateFn)(
    void *context, VarId source_variable, uint32_t *offset_out);

void      bindings_init(Bindings *b);
void      bindings_free(Bindings *b);
bool      bindings_clone(Bindings *dst, const Bindings *src);
bool      bindings_copy(Bindings *dst, const Bindings *src);
/*
 * Retain exactly the logical environment reachable from `roots`.
 *
 * Reachability follows binding values transitively and retains every
 * constraint connected to that closure.  Legacy spelling-fallback bindings
 * are retained conservatively because their lookup key is not a VarId.
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
 * Dense view of one finite, immutable source-variable inventory under a
 * fresh activation epoch.  The inventory arrays are borrowed from compiled
 * source metadata; values are rebuilt from the authoritative binding suffix
 * whenever the view is prepared.  Missing slots remain genuine unbound
 * epoch variables, never mismatches.
 */
typedef struct {
    BindingsBuilder *builder;
    uint64_t builder_instance;
    const VarId *source_ids;
    Atom *const *source_variables;
    Atom **values;
    uint32_t *slot_stamps;
    uint64_t binding_growth;
    uint64_t binding_rollbacks;
    uint32_t len;
    uint32_t cap;
    uint32_t slot_generation;
    VarId source_first_id;
    bool source_ids_contiguous;
    uint32_t epoch;
    uint32_t first_entry;
    uint32_t scanned_len;
} BindingsDenseEpochFrame;

void      bindings_dense_epoch_frame_init(BindingsDenseEpochFrame *frame);
void      bindings_dense_epoch_frame_free(BindingsDenseEpochFrame *frame);
bool      bindings_dense_epoch_frame_prepare(
              BindingsDenseEpochFrame *frame,
              BindingsBuilder *builder,
              const VarId *source_ids, Atom *const *source_variables,
              uint32_t variable_count, uint32_t epoch,
              uint32_t first_entry);
/* Extend a prepared frame across an append-only suffix of the same live
 * builder.  The frame owns the exact growth/rollback revision established by
 * prepare; any rollback, replacement, or builder change is rejected. */
bool      bindings_dense_epoch_frame_refresh(
              BindingsDenseEpochFrame *frame,
              BindingsBuilder *builder);
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
size_t    bindings_entry_active_bytes(void);
size_t    bindings_constraint_active_bytes(void);
void      bindings_thread_cache_free(void);
void      bindings_move(Bindings *dst, Bindings *src);
void      bindings_replace(Bindings *dst, Bindings *src);
bool      bindings_remove_entry_at(Bindings *bindings, uint32_t index);
/* Call after rewriting binding keys outside the Bindings API. */
void      bindings_invalidate_after_key_rewrite(Bindings *bindings);

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
Atom     *bindings_lookup_id(Bindings *b, VarId var_id);
Atom     *bindings_lookup_var(Bindings *b, Atom *var);
/* Resolve one original activation-view variable through the rule-local
 * binding suffix, then through ordinary outer variable links, without
 * allocating a substituted term.  A successful call with
 * `*ground_out == NULL` means the exact closed value is not directly
 * available and the caller must defer to the general view semantics. */
bool      bindings_resolve_epoch_view_ground(
              const Bindings *bindings, const Atom *source_variable,
              uint32_t epoch, uint32_t first_entry, Atom **ground_out);
/*
 * Resolve through a certified rule-local suffix coordinate.  The coordinate
 * is accepted only when the authoritative entry at first_entry + offset has
 * the exact epoch-qualified variable identity.  A false result asks the
 * caller to use the ordinary lookup; no approximation is returned.
 */
bool      bindings_resolve_epoch_view_ground_at(
              const Bindings *bindings, const Atom *source_variable,
              uint32_t epoch, uint32_t first_entry, uint32_t offset,
              Atom **ground_out);
Atom     *binding_variable_atom(Arena *a, const Binding *binding);
Atom     *bindings_resolve_atom_preview(Bindings *b, Atom *atom);
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
Atom     *bindings_apply(Bindings *b, Arena *a, Atom *atom);
static inline Atom *bindings_apply_if_vars(const Bindings *b, Arena *a, Atom *atom) {
    if (!b || b->len == 0 || !atom || !atom_has_vars(atom))
        return atom;
    return bindings_apply((Bindings *)b, a, atom);
}
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
/* Dense-frame realization of bindings_apply_epoch_then_all.  It has the
 * same result and ownership contract; the finite activation inventory avoids
 * repeated hash/index lookup for source-local variables. */
Atom     *bindings_apply_dense_epoch_frame_then_all(
              BindingsBuilder *builder, Arena *a, Atom *atom,
              const BindingsDenseEpochFrame *frame);
/* Resolve one compiler-known source occurrence by its dense slot.  This is
 * extensionally the same as applying the corresponding source variable
 * through bindings_apply_dense_epoch_frame_then_all, without searching the
 * inventory for an identifier already decided at admission. */
Atom     *bindings_apply_dense_epoch_frame_slot_then_all(
              BindingsBuilder *builder, Arena *a,
              const BindingsDenseEpochFrame *frame,
              Atom *source_variable, uint32_t slot);
/* Resolve only the root of a compiler-known slot.  Nested structure remains
 * paired with the live environment for an exact downstream consumer. */
Atom     *bindings_resolve_dense_epoch_frame_slot_root(
              BindingsBuilder *builder, Arena *a,
              const BindingsDenseEpochFrame *frame,
              Atom *source_variable, uint32_t slot);
/* Apply the same activation substitution while consulting an optional
 * certified source-variable -> suffix-offset map before ordinary identity
 * lookup.  Every coordinate is checked against the authoritative binding
 * entry; a mismatch falls back and is counted rather than changing meaning. */
Atom     *bindings_apply_epoch_then_all_coordinates(
              Bindings *b, Arena *a, Atom *atom, uint32_t epoch,
              uint32_t first_entry, BindingsEpochCoordinateFn coordinate,
              void *coordinate_context, uint64_t *coordinate_hits,
              uint64_t *coordinate_fallbacks);
Atom     *atom_freshen_epoch(Arena *a, Atom *atom, uint32_t epoch);
Atom     *bindings_to_atom(Arena *a, const Bindings *b);
bool      bindings_from_atom(Atom *atom, Bindings *out);
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
uint32_t  bindings_builder_save(const BindingsBuilder *bb);
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

/* Try to obtain a fresh nonzero suffix for variable renaming.  The finite
 * suffix space is never recycled: exhaustion fails closed. */
bool fresh_var_suffix_try(uint32_t *suffix_out);

/* Get a fresh suffix for variable renaming.  Legacy convenience wrapper;
 * aborts rather than reusing an identity if the finite suffix space is
 * exhausted. */
uint32_t fresh_var_suffix(void);

#ifdef CETTA_TEST_HOOKS
/* Single-threaded boundary-test hook.  Not present in production builds. */
void fresh_var_suffix_test_reset(uint64_t next_suffix);
/* Drop only the derived VarId index so projection-path tests can observe
 * whether an operation rebuilds it.  Logical bindings are unchanged. */
void bindings_lookup_index_test_clear(Bindings *bindings);
/* Observe the derived index prefix certificate without exposing its
 * production representation.  Returns false when no index is present. */
bool bindings_lookup_index_test_synced_len(const Bindings *bindings,
                                           uint32_t *synced_len_out);
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
bool match_atoms(Atom *left, Atom *right, Bindings *b);
bool match_atoms_builder(Atom *left, Atom *right, BindingsBuilder *bb);
bool match_atoms_epoch(Atom *left, Atom *right, Bindings *b, Arena *a, uint32_t epoch);
/* Epoch-aware matcher over an existing trail-backed environment.  The caller
 * owns the save/rollback boundary when failure must be transactional. */
bool match_atoms_epoch_builder(Atom *left, Atom *right,
                               BindingsBuilder *bb, Arena *a,
                               uint32_t epoch);
/* Clause-frame orientation of the same relation.  When two otherwise unbound
 * variables meet, bind the standardized-apart right rule slot to the live
 * left call variable.  This keeps a successful frame local when possible;
 * callers must still audit the appended keys and roll back on escape. */
bool match_atoms_epoch_builder_rule_local(
         Atom *left, Atom *right, BindingsBuilder *bb,
         Arena *a, uint32_t epoch);
/* Match an activation-local source term without first materializing the
 * complete substituted term.  Variables in `left_original` are interpreted
 * through `left_epoch` and the binding suffix beginning at
 * `left_first_entry`; values reached through those bindings retain their
 * ordinary outer identities.  The right term is standardized through
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
/* Exact matcher for the same activation view when its finite source-variable
 * inventory has already been resolved into dense slots. */
bool match_atoms_dense_epoch_view_builder(
         Atom *left_original, const BindingsDenseEpochFrame *left_frame,
         Atom *right_original, BindingsBuilder *bb, Arena *a,
         uint32_t right_epoch);
/* Match the same dense activation view against an ordinary live term.  The
 * right operand keeps its current variable identities instead of receiving
 * a fresh epoch.  This is the allocation-free form of applying the frame to
 * the left source and then invoking match_atoms_builder. */
bool match_atoms_dense_epoch_view_builder_current(
         Atom *left_original, const BindingsDenseEpochFrame *left_frame,
         Atom *right, BindingsBuilder *bb, Arena *a);
/* The same dense left view with right-side rule-slot orientation. */
bool match_atoms_dense_epoch_view_builder_rule_local(
         Atom *left_original, const BindingsDenseEpochFrame *left_frame,
         Atom *right_original, BindingsBuilder *bb, Arena *a,
         uint32_t right_epoch);
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

/* Check if bindings contain a variable loop (variable appears in its
   own binding value). Such bindings are unsound and must be rejected. */
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
