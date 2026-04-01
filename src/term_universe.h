#ifndef CETTA_TERM_UNIVERSE_H
#define CETTA_TERM_UNIVERSE_H

#include "atom.h"

/*
 * TermUniverse isolates persistent term ownership from evaluator-local
 * scratch allocation.
 *
 * Positive example:
 *   - `bind!`, `add-atom`, and persistent space cloning all store atoms
 *     through one policy seam.
 *
 * Negative example:
 *   - Open-coding `persistent ? persistent : a` and copy policy at every
 *     storage boundary in eval.c and backend code.
 */

typedef struct TermUniverse {
    Arena *persistent_arena;
} TermUniverse;

Atom *term_universe_canonicalize_atom(Arena *dst, Atom *src);
Atom *term_universe_store_atom(const TermUniverse *universe, Arena *fallback,
                               Atom *src);

#endif /* CETTA_TERM_UNIVERSE_H */
