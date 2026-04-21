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

void cetta_runtime_stats_snapshot(CettaRuntimeStats *out) {
    if (!out)
        return;
    memcpy(out->counters, g_test_runtime_stats_counters,
           sizeof(g_test_runtime_stats_counters));
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
