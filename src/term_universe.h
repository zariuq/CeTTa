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
    Atom **atoms;
    uint32_t len, cap;
    uint32_t *intern_slots;
    uint32_t intern_mask;
    uint32_t intern_used;
    uint32_t *ptr_slots;
    uint32_t ptr_mask;
    uint32_t ptr_used;
} TermUniverse;

typedef uint32_t AtomId;

#define CETTA_ATOM_ID_NONE UINT32_MAX

void term_universe_init(TermUniverse *universe);
void term_universe_free(TermUniverse *universe);
void term_universe_set_persistent_arena(TermUniverse *universe,
                                        Arena *persistent_arena);
Atom *term_universe_canonicalize_atom(Arena *dst, Atom *src);
AtomId term_universe_store_atom_id(TermUniverse *universe, Arena *fallback,
                                   Atom *src);
Atom *term_universe_get_atom(const TermUniverse *universe, AtomId id);
Atom *term_universe_store_atom(TermUniverse *universe, Arena *fallback,
                               Atom *src);

#endif /* CETTA_TERM_UNIVERSE_H */
