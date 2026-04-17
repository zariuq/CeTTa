#ifndef CETTA_MATCH_H
#define CETTA_MATCH_H

#include "atom.h"
#include "term_universe.h"

/* ── Bindings ───────────────────────────────────────────────────────────── */

/* Keep recursive matcher behavior aligned across decoded and store-backed
   paths. */
#define CETTA_MATCH_DEPTH_LIMIT 64

typedef struct {
    VarId var_id;
    SymbolId spelling;
    Atom *val;
    bool legacy_name_fallback;
} Binding;

typedef struct {
    Atom *lhs;
    Atom *rhs;
} BindingConstraint;

typedef struct {
    Binding *entries;
    uint32_t len;
    uint32_t cap;
    BindingConstraint *constraints;
    uint32_t eq_len;
    uint32_t eq_cap;
    VarId lookup_cache_ids[4];
    uint32_t lookup_cache_indices[4];
    uint8_t lookup_cache_count;
    uint8_t lookup_cache_next;
} Bindings;

typedef struct {
    Bindings *items;
    uint32_t len, cap;
} BindingSet;

typedef struct {
    uint32_t len;
    uint32_t eq_len;
    uint8_t lookup_cache_count;
    uint8_t lookup_cache_next;
} BindingsBuilderTrailEntry;

typedef struct {
    Bindings current;
    BindingsBuilderTrailEntry *trail;
    uint32_t trail_len;
    uint32_t trail_cap;
} BindingsBuilder;

typedef Atom *(*BindingsRewriteVarFn)(Arena *a, Atom *var, void *ctx);

void      bindings_init(Bindings *b);
void      bindings_free(Bindings *b);
bool      bindings_clone(Bindings *dst, const Bindings *src);
bool      bindings_copy(Bindings *dst, const Bindings *src);
void      bindings_move(Bindings *dst, Bindings *src);
void      bindings_replace(Bindings *dst, Bindings *src);
Atom     *bindings_lookup_id(Bindings *b, VarId var_id);
Atom     *bindings_lookup_var(Bindings *b, Atom *var);
bool      bindings_add_id(Bindings *b, VarId var_id, SymbolId spelling, Atom *val);
bool      bindings_add_var(Bindings *b, Atom *var, Atom *val);
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
Atom     *atom_freshen_epoch(Arena *a, Atom *atom, uint32_t epoch);
Atom     *bindings_to_atom(Arena *a, const Bindings *b);
bool      bindings_from_atom(Atom *atom, Bindings *out);
void      binding_set_init(BindingSet *bs);
void      binding_set_free(BindingSet *bs);
bool      binding_set_push(BindingSet *bs, const Bindings *b);
void      binding_set_push_move(BindingSet *bs, Bindings *b);
bool      bindings_builder_init(BindingsBuilder *bb, const Bindings *base);
void      bindings_builder_init_owned(BindingsBuilder *bb, Bindings *owned);
void      bindings_builder_free(BindingsBuilder *bb);
uint32_t  bindings_builder_save(const BindingsBuilder *bb);
void      bindings_builder_rollback(BindingsBuilder *bb, uint32_t mark);
void      bindings_builder_commit(BindingsBuilder *bb);
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
   name. Returns new arena-allocated atom, or the original atom if unchanged. */
Atom *rename_vars_except(Arena *a, Atom *atom, Atom *ignore_spec);

/* ── Bidirectional matching (match_atoms from HE spec) ─────────────────── */

/* Match left against right. Variables on EITHER side can bind.
   On success, fills bindings and returns true.
   On failure, returns false. */
bool match_atoms(Atom *left, Atom *right, Bindings *b);
bool match_atoms_builder(Atom *left, Atom *right, BindingsBuilder *bb);
bool match_atoms_epoch(Atom *left, Atom *right, Bindings *b, Arena *a, uint32_t epoch);
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
bool bindings_has_loop(Bindings *b);

/* ── Type matching (from HE spec Matching.lean:188-195) ────────────────── */

/* Match two types. %Undefined% and Atom are wildcards (always match).
   Otherwise delegates to match_atoms. Returns true if types match. */
bool match_types(Atom *type1, Atom *type2, Bindings *b);
bool match_types_builder(Atom *type1, Atom *type2, BindingsBuilder *bb);

#endif /* CETTA_MATCH_H */
