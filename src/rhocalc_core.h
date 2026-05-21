#ifndef CETTA_RHOCALC_CORE_H
#define CETTA_RHOCALC_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"

typedef struct {
    Atom **items;
    uint32_t len;
} RhoStepSet;

bool rhocalc_process_well_formed(Atom *proc);
const char *rhocalc_last_validation_error(void);
bool rhocalc_one_step(Arena *arena, Atom *proc, RhoStepSet *out);
bool rhocalc_one_step_with_threads(Arena *arena, Atom *proc,
                                   uint32_t thread_count, RhoStepSet *out);
Atom *rhocalc_steps_atom(Arena *arena, const RhoStepSet *steps);

#endif /* CETTA_RHOCALC_CORE_H */
