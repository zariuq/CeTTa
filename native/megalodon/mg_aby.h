#ifndef CETTA_MEGALODON_ABY_H
#define CETTA_MEGALODON_ABY_H

/*
 * Native aby (mg_aby.c): a proof of the goal an aby step leaves, found by
 * search from the goal's context and the facts the step names.
 */

#include "mg_elab_internal.h"

typedef struct {
    uint64_t steps;            /* search steps taken */
    bool out_of_steps;         /* the step budget ran out */
    uint32_t facts;            /* named facts usable as such */
    uint32_t unusable_facts;   /* named facts the search cannot state alone (polymorphic) */
    uint64_t rejected;         /* proofs found that failed the double check */
    int raised;                /* a raise the search caught, 0 if none */
} MgAbyStats;

/* A proof of g's claim in g's context, from its hypotheses and the facts
 * named, found by search and double checked; NULL when none is found
 * within MgElab.aby_max_steps search steps and aby_max_depth levels of
 * iterative deepening.  stats may be NULL. */
MgPf *mge_aby_prove(MgElab *e, const PfSt *g, const char *const *names, uint32_t count, MgAbyStats *stats);

#endif
