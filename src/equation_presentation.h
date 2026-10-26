#ifndef CETTA_EQUATION_PRESENTATION_H
#define CETTA_EQUATION_PRESENTATION_H

#include "atom.h"
#include "space.h"

#include <stdbool.h>
#include <stddef.h>

/* A native realization of an authored operation replaces one presentation of
 * it: the exact equations it was written for.  Users may redefine, extend or
 * remove those equations at any time, so a realization asks, before each use,
 * whether the live presentation is still that one; otherwise the authored
 * definition runs as written. */

/* One term parsed from source, or NULL when `source` is not exactly one term. */
Atom *cetta_presentation_parse_one(Arena *owner, const char *source);

/* Whether two patterns unify, the verdict alone. */
bool cetta_patterns_overlap(Atom *left, Atom *right);

/* Whether the equations `space` holds for the head `head_name` are exactly
 * `sources` (alpha-equivalent, in order), with no other equation whose left
 * side could match one of theirs. */
bool cetta_equation_presentation_exact(
    Arena *scratch, Space *space, const char *head_name,
    const char *const *sources, size_t count);

/* The same, for equations already parsed: `expected` are `(= lhs rhs)`. */
bool cetta_equation_presentation_exact_atoms(
    Arena *scratch, Space *space, SymbolId head,
    Atom *const *expected, size_t expected_count);

#endif /* CETTA_EQUATION_PRESENTATION_H */
