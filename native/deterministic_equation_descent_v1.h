#ifndef CETTA_DETERMINISTIC_EQUATION_DESCENT_V1_H
#define CETTA_DETERMINISTIC_EQUATION_DESCENT_V1_H

#include "deterministic_equation_plan_v1.h"
#include "src/atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One admitted equation: its left side (f p1 ... pn), a left-linear
 * symbol-headed call, and its right side, with the variables of both bound
 * alike. */
typedef struct {
    const char *name;
    const Atom *left;
    const Atom *right;
} CettaDescentRuleV1;

/* Whether every call the equations make descends, so that every evaluation
 * ends.  A function is a head at an arity.  A descent certificate gives
 * every function, at each of a fixed number of levels, a set of argument
 * positions and a potential; a call's measure at a level is the sum of the
 * norms of its arguments at the positions plus the potential, and the
 * measures of a call are compared lexicographically over the levels.  At a
 * position of the set every equation of the function has a symbol-headed
 * pattern.  Every call an equation makes must be non-increasing at each level
 * before one where it decreases by at least one, by the bound the call's
 * syntax gives: the arguments at the callee's positions are built from
 * disjoint uses of the variables of the caller's patterns at its positions
 * (through let binders that carry a variable or a norm-keeping view of one),
 * atoms, constructors, value views, norm-keeping primitives and atom-valued
 * primitives, and their norm is at most their constructors' overhead plus the
 * norms of the uses.  The certificate is searched for and then checked as
 * stated.  Positions past the 64th of a function are never measured.  On
 * failure error names an equation whose call does not descend, or
 * *resource_failure is set when the check ran out of memory. */
bool cetta_deterministic_equation_descends_v1(
    const CettaDescentRuleV1 *rules, uint32_t rule_count,
    const CettaDeterministicVocabularyV1 *vocabulary,
    bool *resource_failure, char *error, size_t error_size);

#endif /* CETTA_DETERMINISTIC_EQUATION_DESCENT_V1_H */
