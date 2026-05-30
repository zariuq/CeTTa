#ifndef CETTA_RHOCALC_CORE_H
#define CETTA_RHOCALC_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"

typedef enum {
    RHO_SCHEDULER_CANONICAL = 0,
    RHO_SCHEDULER_ROTATING = 1
} RhoSchedulerPolicy;

typedef struct {
    RhoSchedulerPolicy scheduler_policy;
    uint32_t reduction_limit;
} RhoRuntimeProfile;

typedef enum {
    RHOCALC_REDUCTION_QUIESCENT = 0,
    RHOCALC_REDUCTION_LIMIT_EXHAUSTED
} RhoReductionStatus;

typedef struct {
    Atom *residual;
    uint32_t reductions_taken;
    RhoReductionStatus status;
} RhoReductionResult;

bool rhocalc_process_well_formed(Atom *proc);
const char *rhocalc_last_validation_error(void);
bool rhocalc_reduce_to_quiescence_with_profile(Arena *arena, Atom *proc,
                                               const RhoRuntimeProfile *profile,
                                               RhoReductionResult *out);
bool rhocalc_reduce_to_quiescence(Arena *arena, Atom *proc,
                                  uint32_t reduction_limit, RhoReductionResult *out);

#endif /* CETTA_RHOCALC_CORE_H */
