#ifndef CETTA_MATCH_H
#define CETTA_MATCH_H

#include "atom.h"

/* ── Bindings ───────────────────────────────────────────────────────────── */

#define MAX_BINDINGS 64

typedef struct {
    const char *var;
    Atom *val;
} Binding;

typedef struct {
    Binding entries[MAX_BINDINGS];
    uint32_t len;
} Bindings;

void      bindings_init(Bindings *b);
Atom     *bindings_lookup(Bindings *b, const char *var);
bool      bindings_add(Bindings *b, const char *var, Atom *val);
Atom     *bindings_apply(Bindings *b, Arena *a, Atom *atom);

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

/* ── Bidirectional matching (match_atoms from HE spec) ─────────────────── */

/* Match left against right. Variables on EITHER side can bind.
   On success, fills bindings and returns true.
   On failure, returns false. */
bool match_atoms(Atom *left, Atom *right, Bindings *b);

/* ── Type matching (from HE spec Matching.lean:188-195) ────────────────── */

/* Match two types. %Undefined% and Atom are wildcards (always match).
   Otherwise delegates to match_atoms. Returns true if types match. */
bool match_types(Atom *type1, Atom *type2, Bindings *b);

#endif /* CETTA_MATCH_H */
