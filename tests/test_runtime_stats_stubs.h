#ifndef CETTA_TEST_RUNTIME_STATS_STUBS_H
#define CETTA_TEST_RUNTIME_STATS_STUBS_H

#include <string.h>

#ifndef CETTA_RUNTIME_STATS_IMPL
#define CETTA_RUNTIME_STATS_IMPL 1
#endif
#include "stats.h"

#ifdef cetta_runtime_stats_is_enabled
#undef cetta_runtime_stats_is_enabled
#endif
#ifdef cetta_runtime_stats_add
#undef cetta_runtime_stats_add
#endif
#ifdef cetta_runtime_stats_set
#undef cetta_runtime_stats_set
#endif
#ifdef cetta_runtime_stats_update_max
#undef cetta_runtime_stats_update_max
#endif

static uint64_t g_test_runtime_stats_counters[CETTA_RUNTIME_COUNTER_COUNT];
static _Thread_local CettaSurvivorAllocationRole
    g_test_survivor_allocation_role = CETTA_SURVIVOR_ALLOC_ROLE_OTHER;

static inline void test_runtime_stats_reset_counters(void) {
    memset(g_test_runtime_stats_counters, 0,
           sizeof(g_test_runtime_stats_counters));
}

static inline uint64_t
test_runtime_stats_counter(CettaRuntimeCounter counter) {
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return 0;
    return g_test_runtime_stats_counters[counter];
}

void cetta_runtime_stats_reset(void) {
    test_runtime_stats_reset_counters();
}

void cetta_runtime_stats_enable(void) {
}

void cetta_runtime_stats_disable(void) {
}

bool cetta_runtime_stats_is_enabled(void) {
    return true;
}

void cetta_runtime_stats_add(CettaRuntimeCounter counter, uint64_t delta) {
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    g_test_runtime_stats_counters[counter] += delta;
}

void cetta_runtime_stats_set(CettaRuntimeCounter counter, uint64_t value) {
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    g_test_runtime_stats_counters[counter] = value;
}

void cetta_runtime_stats_update_max(CettaRuntimeCounter counter, uint64_t value) {
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    if (value > g_test_runtime_stats_counters[counter])
        g_test_runtime_stats_counters[counter] = value;
}

CettaSurvivorAllocationScope cetta_survivor_allocation_scope_enter(
        CettaSurvivorAllocationRole role) {
    CettaSurvivorAllocationScope scope = {
        .previous = g_test_survivor_allocation_role,
    };
    g_test_survivor_allocation_role =
        (uint32_t)role < CETTA_SURVIVOR_ALLOC_ROLE_COUNT
            ? role : CETTA_SURVIVOR_ALLOC_ROLE_OTHER;
    return scope;
}

void cetta_survivor_allocation_scope_leave(
        CettaSurvivorAllocationScope scope) {
    g_test_survivor_allocation_role =
        (uint32_t)scope.previous < CETTA_SURVIVOR_ALLOC_ROLE_COUNT
            ? scope.previous : CETTA_SURVIVOR_ALLOC_ROLE_OTHER;
}

void cetta_runtime_stats_note_survivor_allocation(uint64_t bytes) {
    CettaSurvivorAllocationRole role =
        (uint32_t)g_test_survivor_allocation_role <
                CETTA_SURVIVOR_ALLOC_ROLE_COUNT
            ? g_test_survivor_allocation_role
            : CETTA_SURVIVOR_ALLOC_ROLE_OTHER;
    g_test_runtime_stats_counters[
        CETTA_RUNTIME_COUNTER_QUERY_EPISODE_SURVIVOR_ARENA_ALLOC_BYTES] +=
        bytes;
    g_test_runtime_stats_counters[
        CETTA_RUNTIME_COUNTER_SURVIVOR_ALLOC_ROLE_OTHER_BYTES + role] +=
        bytes;
}

void cetta_runtime_stats_snapshot(CettaRuntimeStats *out) {
    if (!out)
        return;
    memcpy(out->counters, g_test_runtime_stats_counters,
           sizeof(g_test_runtime_stats_counters));
}

uint64_t cetta_runtime_stats_survivor_role_total(
        const CettaRuntimeStats *stats) {
    if (!stats)
        return 0u;
    uint64_t total = 0u;
    for (uint32_t role = 0u;
         role < CETTA_SURVIVOR_ALLOC_ROLE_COUNT; role++) {
        total += stats->counters[
            CETTA_RUNTIME_COUNTER_SURVIVOR_ALLOC_ROLE_OTHER_BYTES + role];
    }
    return total;
}

bool cetta_runtime_stats_survivor_role_account_is_exact(
        const CettaRuntimeStats *stats) {
    return stats &&
        cetta_runtime_stats_survivor_role_total(stats) ==
            stats->counters[
                CETTA_RUNTIME_COUNTER_QUERY_EPISODE_SURVIVOR_ARENA_ALLOC_BYTES];
}

void cetta_runtime_stats_print(FILE *out, const CettaRuntimeStats *stats) {
    (void)out;
    (void)stats;
}

void cetta_runtime_stats_populate_space(Space *space, Arena *a,
                                        const CettaRuntimeStats *stats) {
    (void)space;
    (void)a;
    (void)stats;
}

#endif
