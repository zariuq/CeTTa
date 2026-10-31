#ifndef CETTA_SPACE_SKELETON_H
#define CETTA_SPACE_SKELETON_H

#include "space_tuples.h"
#include "term_universe.h"

/*
 * A skeleton: the spine a head's deep facts share, in preorder.  A node is
 * an expression whose first element is a symbol, followed by the nodes of
 * its other elements, or a hole.  A fact the skeleton fits is its holes'
 * parts, in preorder: the row a relation keeps, with no record of the fact
 * or of its spine.  Cutting a fact and building it back are inverse
 * (Mettapedia/Machines/ShapeStore.lean, dec_injective and reassemble_dec).
 */
enum {
    CETTA_SKELETON_NODES = 32u,
    CETTA_SKELETON_HOLES = CETTA_TUPLE_MAX_ARITY,
};

typedef struct {
    /* An expression: its number of elements; a hole: 0. */
    uint32_t len;
    /* An expression: its first element, a symbol, and that symbol's id. */
    SymbolId head;
    AtomId head_id;
} CettaSkeletonNode;

typedef struct {
    uint32_t count;
    uint32_t holes;
    CettaSkeletonNode nodes[CETTA_SKELETON_NODES];
} CettaSkeleton;

/* The shape of the record ID: its expressions headed by a symbol, the
 * others and every atom holes.  False when ID is no such expression or the
 * shape outgrows the nodes. */
bool cetta_skeleton_of(const TermUniverse *universe, AtomId id,
                       CettaSkeleton *skeleton);
/* Generalize SKELETON so the record ID fits it too: where the two differ,
 * the skeleton takes a hole.  False when the roots differ. */
bool cetta_skeleton_generalize(CettaSkeleton *skeleton,
                               const TermUniverse *universe, AtomId id);
/* Merge the spine's deepest expressions into holes until at most MAX holes
 * remain; false when even the root's elements are more. */
bool cetta_skeleton_fit_holes(CettaSkeleton *skeleton, uint32_t max);
/* The holes of the record ID, in preorder; false when ID does not fit. */
bool cetta_skeleton_cut(const TermUniverse *universe,
                        const CettaSkeleton *skeleton, AtomId id,
                        AtomId *holes);
/* The holes of ATOM, as parts of its alpha key (variables numbered by first
 * occurrence across the fact), stored when STORE, looked up otherwise.
 * False when ATOM does not fit or its key would not form; when looked up,
 * a part the universe lacks gives CETTA_ATOM_ID_NONE with *MISSING. */
bool cetta_skeleton_cut_atom(TermUniverse *universe,
                             const CettaSkeleton *skeleton, Atom *atom,
                             bool store, AtomId *holes, bool *missing);
/* The record of the subtree at node *I, its holes from *H on, advancing
 * both past it; CETTA_ATOM_ID_NONE without room. */
AtomId cetta_skeleton_build_at(TermUniverse *universe,
                               const CettaSkeleton *skeleton, uint32_t *i,
                               const AtomId *holes, uint32_t *h);
/* The record of the fact whose holes are HOLES when the universe holds
 * it, CETTA_ATOM_ID_NONE when it does not: nothing is stored. */
AtomId cetta_skeleton_lookup(const TermUniverse *universe,
                             const CettaSkeleton *skeleton,
                             const AtomId *holes);
/* The record of the fact whose holes are HOLES; CETTA_ATOM_ID_NONE
 * without room. */
AtomId cetta_skeleton_build(TermUniverse *universe,
                            const CettaSkeleton *skeleton,
                            const AtomId *holes);
/* How PATTERN meets the skeleton's facts.  False when none can match it:
 * another spine, or a part no fact can hold.  Otherwise FIXED holds per
 * hole the id a fact's hole must be (CETTA_DENSE_ANY where the pattern
 * leaves it to unification), taking a fixed part only on holes OPEN_HOLES
 * leaves clear (a hole holding a variable in some fact may unify with
 * parts it does not equal); *EXACT tells whether the ids decide every fact
 * the pattern matches, so that counting them answers the pattern. */
bool cetta_skeleton_align(const TermUniverse *universe,
                          const CettaSkeleton *skeleton, const Atom *pattern,
                          uint32_t open_holes, CettaDenseId *fixed,
                          bool *exact);
/* As cetta_skeleton_align, for a pattern given as its elements, the first
 * the head (a symbol, or a variable meeting any head). */
bool cetta_skeleton_align_elements(const TermUniverse *universe,
                                   const CettaSkeleton *skeleton,
                                   Atom *const *elements, uint32_t len,
                                   uint32_t open_holes, CettaDenseId *fixed,
                                   bool *exact);
/* The number of elements of the skeleton's facts. */
static inline uint32_t cetta_skeleton_len(const CettaSkeleton *skeleton) {
    return skeleton->nodes[0].len;
}

#endif
