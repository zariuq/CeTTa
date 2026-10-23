#ifndef CETTA_STATS_H
#define CETTA_STATS_H

#include "atom.h"
#include <stdio.h>
#include <stdint.h>

typedef struct Space Space;

#include "generated/cetta_runtime_counters.generated.h"

typedef struct {
    uint64_t counters[CETTA_RUNTIME_COUNTER_COUNT];
} CettaRuntimeStats;

#ifndef CETTA_BUILD_WITH_RUNTIME_STATS
#define CETTA_BUILD_WITH_RUNTIME_STATS 0
#endif

#ifndef CETTA_BUILD_WITH_RUNTIME_TIMING
#define CETTA_BUILD_WITH_RUNTIME_TIMING 0
#endif

/* A total, dynamically scoped classification of survivor-arena allocation.
 * The role account is diagnostic only: changing or refining a role cannot
 * alter allocation or evaluation.  OTHER is the explicit remainder that
 * makes the account exhaustive while semantic regions are refined. */
typedef enum {
    CETTA_SURVIVOR_ALLOC_ROLE_OTHER = 0,
    CETTA_SURVIVOR_ALLOC_ROLE_MATCH_STORED_EQUATION_VIEW = 1,
    CETTA_SURVIVOR_ALLOC_ROLE_MATCH_ACTIVATION_SOURCE_VIEW = 2,
    CETTA_SURVIVOR_ALLOC_ROLE_EQUATION_PATTERN_INSTANTIATION = 3,
    CETTA_SURVIVOR_ALLOC_ROLE_EQUATION_RESULT_INSTANTIATION = 4,
    CETTA_SURVIVOR_ALLOC_ROLE_EQUATION_RESULT_EXECUTION = 5,
    CETTA_SURVIVOR_ALLOC_ROLE_WHOLE_EQUATION_INSTANTIATION = 6,
    CETTA_SURVIVOR_ALLOC_ROLE_COUNT = 7
} CettaSurvivorAllocationRole;

typedef struct {
    CettaSurvivorAllocationRole previous;
} CettaSurvivorAllocationScope;

const char *cetta_runtime_counter_name(CettaRuntimeCounter counter);
void cetta_runtime_stats_reset(void);
void cetta_runtime_stats_enable(void);
void cetta_runtime_stats_disable(void);
bool cetta_runtime_stats_is_enabled(void);
void cetta_runtime_stats_add(CettaRuntimeCounter counter, uint64_t delta);
void cetta_runtime_stats_set(CettaRuntimeCounter counter, uint64_t value);
void cetta_runtime_stats_update_max(CettaRuntimeCounter counter, uint64_t value);
/* Change a gauge and raise its peak in one step under the statistics lock,
 * so the peak is the gauge's exact maximum. */
void cetta_runtime_stats_gauge_add(CettaRuntimeCounter gauge,
                                   CettaRuntimeCounter peak, int64_t delta);
void cetta_runtime_stats_snapshot(CettaRuntimeStats *out);
void cetta_runtime_stats_print(FILE *out, const CettaRuntimeStats *stats);
void cetta_runtime_stats_populate_space(Space *space, Arena *a,
                                        const CettaRuntimeStats *stats);
uint64_t cetta_runtime_stats_survivor_role_total(
    const CettaRuntimeStats *stats);
bool cetta_runtime_stats_survivor_role_account_is_exact(
    const CettaRuntimeStats *stats);

#if (CETTA_BUILD_WITH_RUNTIME_STATS && !defined(CETTA_RUNTIME_STATS_NOOP)) || \
    defined(CETTA_RUNTIME_STATS_IMPL)
CettaSurvivorAllocationScope cetta_survivor_allocation_scope_enter(
    CettaSurvivorAllocationRole role);
void cetta_survivor_allocation_scope_leave(
    CettaSurvivorAllocationScope scope);
void cetta_runtime_stats_note_survivor_allocation(uint64_t bytes);
#else
static inline CettaSurvivorAllocationScope
cetta_survivor_allocation_scope_enter(CettaSurvivorAllocationRole role) {
    (void)role;
    return (CettaSurvivorAllocationScope){
        .previous = CETTA_SURVIVOR_ALLOC_ROLE_OTHER,
    };
}

static inline void cetta_survivor_allocation_scope_leave(
        CettaSurvivorAllocationScope scope) {
    (void)scope;
}

static inline void cetta_runtime_stats_note_survivor_allocation(
        uint64_t bytes) {
    (void)bytes;
}
#endif

#if (!CETTA_BUILD_WITH_RUNTIME_STATS || defined(CETTA_RUNTIME_STATS_NOOP)) && \
    !defined(CETTA_RUNTIME_STATS_IMPL)
#define cetta_runtime_stats_is_enabled() false
/* Without runtime statistics a probe is an unevaluated call: the compiler
 * still checks its arguments against the declarations above, and it emits
 * no code. */
#define cetta_runtime_stats_add(counter, delta)                                  \
    ((void)sizeof((cetta_runtime_stats_add)((counter), (delta)), 0))
#define cetta_runtime_stats_set(counter, value)                                  \
    ((void)sizeof((cetta_runtime_stats_set)((counter), (value)), 0))
#define cetta_runtime_stats_update_max(counter, value)                           \
    ((void)sizeof((cetta_runtime_stats_update_max)((counter), (value)), 0))
#define cetta_runtime_stats_gauge_add(gauge, peak, delta)                        \
    ((void)sizeof((cetta_runtime_stats_gauge_add)((gauge), (peak), (delta)), 0))
#define cetta_runtime_stats_inc(counter)                                         \
    ((void)sizeof((cetta_runtime_stats_add)((counter), 1u), 0))
#else
static inline void cetta_runtime_stats_inc(CettaRuntimeCounter counter) {
    cetta_runtime_stats_add(counter, 1);
}
#endif

static inline bool cetta_runtime_timing_is_enabled(void) {
#if CETTA_BUILD_WITH_RUNTIME_STATS && CETTA_BUILD_WITH_RUNTIME_TIMING
    return cetta_runtime_stats_is_enabled();
#else
    return false;
#endif
}

#endif
