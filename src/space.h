#ifndef CETTA_SPACE_H
#define CETTA_SPACE_H

#include "atom.h"
#include "match.h"

typedef struct Space {
    Atom **atoms;
    uint32_t len, cap;
} Space;

void space_init(Space *s);
void space_add(Space *s, Atom *atom);

/* ── Equation Query ─────────────────────────────────────────────────────── */

/* A single query result: the RHS with bindings applied, plus the bindings
   themselves (needed for propagating variable bindings to the caller). */
typedef struct {
    Atom *result;
    Bindings bindings;
} QueryResult;

typedef struct {
    QueryResult *items;
    uint32_t len, cap;
} QueryResults;

void query_results_init(QueryResults *qr);
void query_results_push(QueryResults *qr, Atom *result, Bindings *b);

/* Find all (= lhs rhs) in space where lhs matches query (bidirectional).
   Returns substituted RHS for each match, plus bindings. */
void query_equations(Space *s, Atom *query, Arena *a, QueryResults *out);

/* ── Type Lookup (from HE spec Space.lean) ─────────────────────────────── */

/* Get intrinsic type for grounded atom: int/float→Number, bool→Bool, string→String */
Atom *get_grounded_type(Arena *a, Atom *atom);

/* Get all types for an atom from the space.
   Returns count; fills out_types (arena-allocated array). */
uint32_t get_atom_types(Space *s, Arena *a, Atom *atom,
                        Atom ***out_types);

#endif /* CETTA_SPACE_H */
