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

/*
 * Small environments use this inline identity cache.  Larger environments
 * acquire the complete lookup index at the threshold in match.c, so keeping
 * two recent identities here preserves the common producer/consumer pair
 * without widening every branch-local environment.
 */
#define CETTA_BINDINGS_LOOKUP_CACHE_SLOTS 2u

typedef struct {
    Binding *entries;
    uint32_t len;
    uint32_t cap;
    BindingConstraint *constraints;
    uint32_t eq_len;
    uint32_t eq_cap;
    VarId lookup_cache_ids[CETTA_BINDINGS_LOOKUP_CACHE_SLOTS];
    uint32_t lookup_cache_indices[CETTA_BINDINGS_LOOKUP_CACHE_SLOTS];
    uint8_t lookup_cache_count;
    uint8_t lookup_cache_next;
    /*
     * Derived certificate for the substitution graph in `entries`.
     * Zero means unknown, one means proved acyclic, and two means a cycle was
     * witnessed.  The entries remain authoritative; the full checker handles
     * unknown state.
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
     * semantic authority: clones may share this immutable index, while the
     * first append or rollback detaches it copy-on-write.
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
    uint8_t cycle_state;
    /* Bit set iff the corresponding derived count was nonzero. */
    uint8_t derived_nonzero;
    PrimeNeedSnapshot prime_need;
    PrimeNeedReceipt prime_receipt;
} BindingsBuilderTrailEntry;

typedef struct {
    Bindings current;
    BindingsBuilderTrailEntry *trail;
    uint32_t trail_len;
    uint32_t trail_cap;
    /*
     * Monotone count of successful logical writes performed by this builder.
     * Rollback restores logical state but deliberately does not erase work
     * already performed; collectors use this as a scheduling clock.
     */
    uint64_t growth_count;
} BindingsBuilder;

typedef Atom *(*BindingsRewriteVarFn)(Arena *a, Atom *var, void *ctx);

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
/*
 * Replace `full` by the branch-relative suffix beyond `base` when `base` is
 * an exact ordered prefix of its logical bindings and constraints.
 *
 * The prefix test is a certificate, not a semantic guess: any mismatch leaves
 * `full` unchanged and reports `factored == false`.  Prime occurrence state is
 * orthogonal to the logical prefix and is retained whenever it differs from
 * the base.  This lets evaluator boundaries carry only newly learned logical
 * substitutions while preserving the canonical full-environment fallback.
 */
bool      bindings_factor_prefix(Bindings *full, const Bindings *base,
                                 bool *factored,
                                 uint64_t *logical_items_elided);
bool      bindings_promote_atoms_to_arena(Bindings *bindings, Arena *dst);
bool      bindings_promote_logical_atoms_to_arena(Bindings *bindings,
                                                   Arena *dst);
bool      bindings_promote_logical_atoms_with_session(
              Bindings *bindings, AtomDeepCopySession *session);
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
const PrimeNeedReceipt  *bindings_receipt_view(const Bindings *b);
PrimeNeedSnapshot        *bindings_need_mut(Bindings *b);
PrimeNeedReceipt         *bindings_receipt_mut(Bindings *b);
bool                      bindings_prime_present(const Bindings *b);
/* Copy both Prime components from src into dst (materializing dst's occurrence
 * only when src has present state; otherwise dst's occurrence is released). */
void      bindings_prime_assign(Bindings *dst, const Bindings *src);
/* Set dst's Prime components from explicit values (builder-trail restore, which
 * snapshots by value); clears dst's occurrence when both values are absent. */
void      bindings_prime_set(Bindings *dst, const PrimeNeedSnapshot *need,
                             const PrimeNeedReceipt *receipt);
Atom     *bindings_lookup_id(Bindings *b, VarId var_id);
Atom     *bindings_lookup_var(Bindings *b, Atom *var);
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
Atom     *atom_freshen_epoch(Arena *a, Atom *atom, uint32_t epoch);
Atom     *bindings_to_atom(Arena *a, const Bindings *b);
bool      bindings_from_atom(Atom *atom, Bindings *out);
void      binding_set_init(BindingSet *bs);
void      binding_set_free(BindingSet *bs);
bool      binding_set_push(BindingSet *bs, const Bindings *b);
bool      binding_set_push_move(BindingSet *bs, Bindings *b);
bool      bindings_builder_init(BindingsBuilder *bb, const Bindings *base);
void      bindings_builder_init_owned(BindingsBuilder *bb, Bindings *owned);
void      bindings_builder_free(BindingsBuilder *bb);
uint32_t  bindings_builder_save(const BindingsBuilder *bb);
void      bindings_builder_rollback(BindingsBuilder *bb, uint32_t mark);
void      bindings_builder_commit(BindingsBuilder *bb);
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

/* Get a fresh suffix for variable renaming. Monotonically increasing. */
uint32_t fresh_var_suffix(void);

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
/* Leaf-patch view (env CETTA_LEAF_PATCH_VIEW=1, OFF by default). */
bool match_leaf_patch_view_enabled(void);
/* Positional bind for a flat linear pattern (lhs) vs a non-variable-arg query;
 * self-sound (refuses non-linear / non-flat / var-query / pre-bound by falling
 * back with no partial binding). Result == match_atoms_epoch on that shape. */
bool match_atoms_epoch_positional_linear(Atom *query, Atom *lhs, Bindings *b,
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

#endif /* CETTA_MATCH_H */
