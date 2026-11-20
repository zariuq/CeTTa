#ifndef CETTA_PETTA_CLPFD_H
#define CETTA_PETTA_CLPFD_H

#include "atom.h"
#include "symbol.h"

#include <stdbool.h>

/*
 * PeTTa's integer constraints, natively.  metta.pl defines
 *
 *   (#+ A B) ... (#max A B)   as  R #= A op B   (op: + - * div // mod min max)
 *   (#< A B) (#> A B) (#= A B) (#\= A B)
 *                             as  A rel B, answering true, or false when
 *                                 the constraint cannot hold
 *
 * over SWI's CLP(FD); a program may also post A rel B itself through
 * translatePredicate.  CeTTa decides these constraints as equations and
 * inequations over the integers:
 *
 *   Each side is an integer, an unbound variable, or an operation over
 *   such sides.  With every variable's value substituted, the constraint
 *   is linear when every product has a known factor and every div, //,
 *   mod, min and max has known operands (those are computed; a zero
 *   divisor makes the constraint false).  A linear constraint is
 *   L - R = c1*x1 + ... + cn*xn + k compared with 0, like terms gathered:
 *
 *     no unknown      the comparison of k with 0 decides it;
 *     one unknown     an equation c*x + k = 0 is solved: x = -k/c when c
 *                     divides k (unification modulo the abelian group of
 *                     the integers, which is unitary), and has no solution
 *                     otherwise; an inequation x != -k/c holds outright
 *                     when c does not divide k;
 *     otherwise       the constraint is residual.
 *
 *   A constraint that is not linear and has an unknown is residual.  A
 *   residual constraint is delayed on its variables and decided again
 *   whenever one of them is bound; an answer that still carries it shows
 *   the variables unbound.
 *
 * SWI additionally narrows the domains of residual variables, so where
 * several constraints together, or one non-linear constraint alone, leave
 * a variable a single value, SWI binds it and CeTTa keeps the constraints
 * residual until the variable is bound.  A side that is neither an integer
 * nor a variable raises SWI's domain_error(clpfd_expression, Side).
 */

typedef enum {
    PETTA_CLPFD_EQ = 0,  /* #=  */
    PETTA_CLPFD_NE,      /* #\= */
    PETTA_CLPFD_LT,      /* #<  */
    PETTA_CLPFD_GT,      /* #>  */
    PETTA_CLPFD_LE,      /* #=< */
    PETTA_CLPFD_GE,      /* #>= */
} PettaClpfdRelation;

typedef enum {
    PETTA_CLPFD_FAILS = 0,
    PETTA_CLPFD_RAISES,
    PETTA_CLPFD_HOLDS,
    PETTA_CLPFD_BINDS,
    PETTA_CLPFD_RESIDUAL,
} PettaClpfdKind;

typedef struct {
    PettaClpfdKind kind;
    /* RAISES: the error. */
    Atom *error;
    /* BINDS: the one unknown and its value. */
    Atom *variable;
    Atom *value;
    /* RESIDUAL: the constraint as delayed, a Prolog goal term. */
    Atom *goal;
} PettaClpfdDecision;

/* The operation of PeTTa's integer function `head` (#+ ... #max): its
 * CLP(FD) operator ("+", "div", ...). */
bool petta_clpfd_function(SymbolId head, const char **op);
/* The relation of PeTTa's reified comparison `head` (#< #> #= #\=). */
bool petta_clpfd_reified(SymbolId head, PettaClpfdRelation *relation);

/* Result #= A op B, as the Prolog goal metta.pl posts. */
Atom *petta_clpfd_function_goal(Arena *arena, const char *op, Atom *a,
                                Atom *b, Atom *result);
/* A rel B, as a Prolog goal. */
Atom *petta_clpfd_relation_goal(Arena *arena, PettaClpfdRelation relation,
                                Atom *a, Atom *b);

/* Whether `goal` is a CLP(FD) relation this layer decides: a two-place
 * #=, #\=, #<, #>, #=< or #>= goal, as a Prolog compound or as written. */
bool petta_clpfd_is_relation_goal(Atom *goal);

/* Decide `goal`, a relation goal whose variables' current values are
 * substituted.  False only when memory runs out. */
bool petta_clpfd_decide(Arena *arena, Atom *goal, PettaClpfdDecision *out);

#endif
