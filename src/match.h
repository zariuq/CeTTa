#ifndef CETTA_MATCH_H
#define CETTA_MATCH_H

#include "atom.h"

/* ── Bindings ───────────────────────────────────────────────────────────── */

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
bool      bindings_clone_merge(Bindings *dst, const Bindings *base,
                               const Bindings *extra);
Atom     *bindings_apply(Bindings *b, Arena *a, Atom *atom);
Atom     *bindings_apply_epoch(Bindings *b, Arena *a, Atom *atom, uint32_t epoch);
Atom     *bindings_to_atom(Arena *a, const Bindings *b);
bool      bindings_from_atom(Atom *atom, Bindings *out);
void      binding_set_init(BindingSet *bs);
void      binding_set_free(BindingSet *bs);
bool      binding_set_push(BindingSet *bs, const Bindings *b);
void      binding_set_push_move(BindingSet *bs, Bindings *b);

/* ── One-way pattern matching ───────────────────────────────────────────── */

/* Match pattern (may contain vars) against target (ground).
   On success, fills bindings and returns true.
   On failure, returns false (bindings undefined). */
bool simple_match(Atom *pattern, Atom *target, Bindings *b);

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
bool match_atoms_epoch(Atom *left, Atom *right, Bindings *b, Arena *a, uint32_t epoch);

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

#endif /* CETTA_MATCH_H */
