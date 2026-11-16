#ifndef CETTA_ATOM_WALK_H
#define CETTA_ATOM_WALK_H
#include "atom.h"

typedef struct AtomWalk AtomWalk;
typedef enum {
    ATOM_WALK_ENTER,
    ATOM_WALK_LEAVE,
    ATOM_WALK_SHARED,
    ATOM_WALK_DONE,
    ATOM_WALK_CYCLE,
    ATOM_WALK_RESOURCE,
    ATOM_WALK_INVALID,
} AtomWalkStep;
typedef struct {
    const Atom *node;
    const Atom *parent;
    CettaExprIndex child_index;
    size_t depth;
} AtomWalkEvent;

/* The immutable source graph outlives the cursor and all borrowed events.
 * unique=true emits SHARED instead of unfolding a completed subgraph again;
 * its observations and pruning decisions must be path-independent.
 * Occurrence-sensitive consumers use false. Neither mode evaluates values.
 * RESOURCE leaves the pending event available for a later retry. */
AtomWalk *atom_walk_new(const Atom *, bool unique);
AtomWalkStep atom_walk_next(AtomWalk *, AtomWalkEvent *);
/* Prune only immediately after ENTER; the matching LEAVE still follows. */
bool atom_walk_prune(AtomWalk *);
void atom_walk_free(AtomWalk *);
#endif
