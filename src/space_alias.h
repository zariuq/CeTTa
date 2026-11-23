#ifndef CETTA_SPACE_H
#include "space.h"
#endif

#ifndef CETTA_SPACE_ALIAS_H
#define CETTA_SPACE_ALIAS_H

/*
 * Scoped Ground Equational Aliases: `(=e a1 ... an)` equates its members;
 * `(=e> a1 ... an)` and `(=e< a1 ... an)` also prefer each member's right or
 * left neighbour when a class is printed.
 *
 * Implements the Lean-proved contract in
 * Mettapedia.GSLT.Contracts.ScopedAliasDispatch and
 * Mettapedia.GSLT.Contracts.LiveTheoryProjections:
 *   - `(=e lhs rhs)` rows remain stored as ordinary raw atoms in the Space.
 *   - Active-equation emptiness is read from the compiled alias table
 *     (`active_edge_count > 0`), never from `equational_revision == 0`
 *     (`empty_after_edit`: install+remove leaves empty content at revision 2).
 *   - Terms outside the non-reflexive carrier bypass normalization at O(1)
 *     cost with zero allocations (`outside_carrier_iff`).
 *   - Both stored keys and query keys are normalized while returning raw
 *     occurrences in exact source order with duplicates preserved
 *     (`fixed_query_can_miss`, `prepare_exact`).
 */

struct SpaceAliasTable;

void space_alias_table_free(struct SpaceAliasTable *table);

/* Process-wide counter incremented only when an `=e` row (or opaque external
 * backend write) is published.  While zero, every Space in the process is
 * guaranteed free of `=e` aliases, collapsing all alias guards to one branch. */
extern _Atomic uint64_t cetta_space_global_equational_epoch;

static inline bool space_may_have_equational_aliases(const Space *s) {
    return __builtin_expect(
        atomic_load_explicit(&cetta_space_global_equational_epoch,
                             memory_order_relaxed) != 0u, 0) &&
        s != NULL &&
        (space_equational_revision(s) != 0u ||
         s->overlay_base != NULL || s->dep_count != 0u);
}

bool space_has_active_equations_slow(Space *s);

static inline bool space_has_active_equations(Space *s) {
    return space_may_have_equational_aliases(s) &&
           space_has_active_equations_slow(s);
}

/* Whether `head` appears in the non-reflexive alias carrier (e.g. `(=e Pet
 * Companion)` or `(=e AI:UnitRange (Range Float 0 1))`). */
bool space_alias_head_in_carrier_slow(Space *s, SymbolId head);
static inline bool space_alias_head_in_carrier(Space *s, SymbolId head) {
    return space_may_have_equational_aliases(s) && head != SYMBOL_ID_NONE &&
           space_alias_head_in_carrier_slow(s, head);
}

/* Whether rule dispatch for `head` is affected by active `=e` equations
 * (either `head` itself is in the alias carrier, or at least one equation for
 * `head` mentions a symbol/literal in the carrier).  Unaffected heads retain
 * raw literal fast paths at full native speed. */
bool space_alias_head_affected_slow(Space *s, SymbolId head);
static inline bool space_alias_head_affected(Space *s, SymbolId head) {
    return space_may_have_equational_aliases(s) && head != SYMBOL_ID_NONE &&
           space_alias_head_affected_slow(s, head);
}

/* Whether `head` (or any symbol equivalent to `head` under `=e`) has at least
 * one admitted rule in `s`. */
bool space_alias_head_has_rules(Space *s, SymbolId head,
                                unsigned admitted_forms);

/* Collect all SymbolIds equivalent to `head` under active `=e` aliases (empty
 * if `head` has no symbol aliases).  Caller frees `*out_syms`. */
bool space_alias_collect_symbol_class(Space *s, SymbolId head,
                                      SymbolId **out_syms, size_t *out_count);

/* Visit each non-trivial equivalence class of symbols in `s` (size >= 2)
 * outside any space maintenance lock.  Returns false only if `visitor` aborts
 * early by returning false. */
typedef bool (*SpaceAliasSymbolClassVisitor)(const SymbolId *members,
                                             size_t count, void *ctx);
bool space_alias_visit_symbol_classes(Space *s,
                                      SpaceAliasSymbolClassVisitor visitor,
                                      void *ctx);

/* Whether `term` touches any symbol or grounded literal in the active alias
 * carrier (`outside_carrier_iff`). */
bool space_alias_term_touches_carrier_slow(Space *s, const Atom *term);
static inline bool space_alias_term_touches_carrier(Space *s, const Atom *term) {
    return space_may_have_equational_aliases(s) && term != NULL &&
           space_alias_term_touches_carrier_slow(s, term);
}

/* Bottom-up normal form `N_E(term)` in `s`'s active `=e` theory.  Returns
 * `term` unchanged with zero allocations when no active alias applies. */
Atom *space_alias_normalize_atom(Space *s, Arena *arena, Atom *term);

/* Equivalence modulo `s`'s active `=e` theory: `atom_eq(N_E(left), N_E(right))`. */
bool space_alias_same(Space *s, Arena *arena, Atom *left, Atom *right);

/* Structural pattern match modulo `s`'s active `=e` theory.  Ground subterms
 * match by `space_alias_same`; pattern variables bind to the original raw
 * matched subterms (never rewritten to representatives). */
bool space_alias_match_pattern(Space *s, Arena *arena, Atom *pattern,
                               Atom *target, Bindings *env);

/* Apply one admitted equation/rule modulo `s`'s active `=e` theory, freshening
 * equation variables apart from `query` and invoking `visitor` with the raw
 * substituted RHS and bindings. */
CettaCount space_alias_query_equation_visit(Space *s, Atom *equation,
                                            Atom *query, unsigned admitted_forms,
                                            Arena *arena,
                                            QueryResultVisitor visitor,
                                            void *ctx);

/* Collect all candidate rules for `head` (including rules headed by any symbol
 * equivalent to `head` under `=e`, plus wildcard rules) in exact source order
 * preserving duplicates.  Caller frees `*out_equations`. */
bool space_alias_collect_head_rules(Space *s, SymbolId head,
                                    unsigned admitted_forms,
                                    Atom ***out_equations, size_t *out_count);

/* Select and instantiate eq:match templates at one read of the data and
 * its equation view. Raw bindings are kept, in occurrence order. The caller
 * evaluates the returned templates after this function has released the
 * space: a template may itself read, write, or enter a foreign runtime.
 * Templates live in arena; caller frees only the pointer array. */
bool space_alias_instantiate_matches(Space *s, Arena *arena, Atom *pattern,
                                    Atom *template, Atom ***out,
                                    size_t *out_count);

/* The members of `term`'s class for `(eq:class ...)`: the printed member
 * first, then the others by size and the standard order of terms; `term`
 * alone when no class holds it.  Caller frees `*out_members`. */
bool space_alias_class_members(Space *s, Arena *arena, Atom *term,
                               Atom ***out_members, size_t *out_count);

#endif /* CETTA_SPACE_ALIAS_H */
