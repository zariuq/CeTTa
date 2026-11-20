#ifndef CETTA_PETTA_PRELUDE_H
#define CETTA_PETTA_PRELUDE_H

#include "atom.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Functions PeTTa's prelude (metta.pl) registers over SWI-Prolog
 * predicates, computed natively with SWI's answers and errors:
 *
 *   (=? A B)            whether A and B unify (\+ \+ A = B), as rational
 *                       trees: SWI's unification has no occurs check
 *   (atom_concat A B)   the atom of A's text followed by B's
 *   (atom_chars A)      the characters of A's text, as one-letter atoms
 *   (copy_term T)       T with fresh variables
 *   (term_hash T)       SWI's term_hash/2 of T as metta.pl passes it (an
 *                       expression is a Prolog list), unbound when T is
 *                       not ground
 *
 * A Prolog error raised by one of them is (Error Formal Context).
 */

/* Whether `head` applied to `nargs` arguments is one of these. */
bool petta_prelude_function(SymbolId head, uint32_t nargs);

/* The input arities at which `head` is one of these, as a bit mask (bit n
 * for n arguments).  The prelude registers copy_term and term_hash at
 * further arities, SWI's copy_term/3, copy_term/4 and term_hash/4, which
 * only the embedded engine serves. */
uint16_t petta_prelude_native_arities(SymbolId head);

/* Apply it to evaluated arguments: the value, or with *raised set the
 * error; NULL when `head` and `nargs` are not one of these or memory runs
 * out. */
Atom *petta_prelude_apply(Arena *arena, SymbolId head, Atom **args,
                          uint32_t nargs, bool *raised);

#endif
