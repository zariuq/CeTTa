#define CETTA_RUNTIME_STATS_IMPL 1
#include "stats.h"
#include "space.h"
#include "symbol.h"

#include <limits.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static uint64_t g_runtime_counters[CETTA_RUNTIME_COUNTER_COUNT];
static bool g_runtime_stats_enabled = false;
static pthread_mutex_t g_runtime_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local CettaSurvivorAllocationRole
    g_survivor_allocation_role = CETTA_SURVIVOR_ALLOC_ROLE_OTHER;

_Static_assert(
    CETTA_RUNTIME_COUNTER_SURVIVOR_ALLOC_ROLE_WHOLE_EQUATION_INSTANTIATION_BYTES -
            CETTA_RUNTIME_COUNTER_SURVIVOR_ALLOC_ROLE_OTHER_BYTES + 1 ==
        CETTA_SURVIVOR_ALLOC_ROLE_COUNT,
    "survivor allocation roles and counters must remain isomorphic");

#include "generated/cetta_runtime_counter_names.generated.h"

static int64_t clamp_counter(uint64_t value) {
    if (value > (uint64_t)INT64_MAX)
        return INT64_MAX;
    return (int64_t)value;
}

const char *cetta_runtime_counter_name(CettaRuntimeCounter counter) {
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return "unknown-counter";
    return CETTA_RUNTIME_COUNTER_NAMES[counter];
}

void cetta_runtime_stats_reset(void) {
    pthread_mutex_lock(&g_runtime_stats_mutex);
    memset(g_runtime_counters, 0, sizeof(g_runtime_counters));
    pthread_mutex_unlock(&g_runtime_stats_mutex);
}

void cetta_runtime_stats_enable(void) {
    pthread_mutex_lock(&g_runtime_stats_mutex);
    g_runtime_stats_enabled = true;
#if CETTA_BUILD_WITH_RUNTIME_STATS
    g_runtime_counters[CETTA_RUNTIME_COUNTER_STATISTICS_RECORDED] = 1u;
#endif
    pthread_mutex_unlock(&g_runtime_stats_mutex);
}

void cetta_runtime_stats_disable(void) {
    pthread_mutex_lock(&g_runtime_stats_mutex);
    g_runtime_stats_enabled = false;
    pthread_mutex_unlock(&g_runtime_stats_mutex);
}

bool cetta_runtime_stats_is_enabled(void) {
    pthread_mutex_lock(&g_runtime_stats_mutex);
    bool enabled = g_runtime_stats_enabled;
    pthread_mutex_unlock(&g_runtime_stats_mutex);
    return enabled;
}

static CettaSurvivorAllocationRole survivor_allocation_role_normalize(
        CettaSurvivorAllocationRole role) {
    return (uint32_t)role < CETTA_SURVIVOR_ALLOC_ROLE_COUNT
        ? role : CETTA_SURVIVOR_ALLOC_ROLE_OTHER;
}

CettaSurvivorAllocationScope cetta_survivor_allocation_scope_enter(
        CettaSurvivorAllocationRole role) {
    CettaSurvivorAllocationScope scope = {
        .previous = g_survivor_allocation_role,
    };
    g_survivor_allocation_role =
        survivor_allocation_role_normalize(role);
    return scope;
}

void cetta_survivor_allocation_scope_leave(
        CettaSurvivorAllocationScope scope) {
    g_survivor_allocation_role =
        survivor_allocation_role_normalize(scope.previous);
}

void cetta_runtime_stats_note_survivor_allocation(uint64_t bytes) {
    CettaSurvivorAllocationRole role =
        survivor_allocation_role_normalize(g_survivor_allocation_role);
    CettaRuntimeCounter role_counter = (CettaRuntimeCounter)(
        CETTA_RUNTIME_COUNTER_SURVIVOR_ALLOC_ROLE_OTHER_BYTES +
        (uint32_t)role);
    pthread_mutex_lock(&g_runtime_stats_mutex);
    if (g_runtime_stats_enabled) {
        g_runtime_counters[
            CETTA_RUNTIME_COUNTER_QUERY_EPISODE_SURVIVOR_ARENA_ALLOC_BYTES] +=
            bytes;
        g_runtime_counters[role_counter] += bytes;
    }
    pthread_mutex_unlock(&g_runtime_stats_mutex);
}

void cetta_runtime_stats_add(CettaRuntimeCounter counter, uint64_t delta) {
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    pthread_mutex_lock(&g_runtime_stats_mutex);
    if (__builtin_expect(!g_runtime_stats_enabled, 1)) {
        pthread_mutex_unlock(&g_runtime_stats_mutex);
        return;
    }
    g_runtime_counters[counter] += delta;
    pthread_mutex_unlock(&g_runtime_stats_mutex);
}

void cetta_runtime_stats_set(CettaRuntimeCounter counter, uint64_t value) {
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    pthread_mutex_lock(&g_runtime_stats_mutex);
    if (__builtin_expect(!g_runtime_stats_enabled, 1)) {
        pthread_mutex_unlock(&g_runtime_stats_mutex);
        return;
    }
    g_runtime_counters[counter] = value;
    pthread_mutex_unlock(&g_runtime_stats_mutex);
}

void cetta_runtime_stats_update_max(CettaRuntimeCounter counter, uint64_t value) {
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    pthread_mutex_lock(&g_runtime_stats_mutex);
    if (__builtin_expect(!g_runtime_stats_enabled, 1)) {
        pthread_mutex_unlock(&g_runtime_stats_mutex);
        return;
    }
    if (value > g_runtime_counters[counter])
        g_runtime_counters[counter] = value;
    pthread_mutex_unlock(&g_runtime_stats_mutex);
}

/* A gauge counts only resources its owner charged while statistics were
 * enabled, and each owner returns exactly its own charges, so a gauge never
 * falls below zero; the guard keeps a broken charge from wrapping it. */
void cetta_runtime_stats_gauge_add(CettaRuntimeCounter gauge,
                                   CettaRuntimeCounter peak, int64_t delta) {
    if ((uint32_t)gauge >= CETTA_RUNTIME_COUNTER_COUNT ||
        (uint32_t)peak >= CETTA_RUNTIME_COUNTER_COUNT || delta == 0)
        return;
    pthread_mutex_lock(&g_runtime_stats_mutex);
    if (__builtin_expect(!g_runtime_stats_enabled, 1)) {
        pthread_mutex_unlock(&g_runtime_stats_mutex);
        return;
    }
    uint64_t value = g_runtime_counters[gauge];
    if (delta > 0)
        value += (uint64_t)delta;
    else
        value = (uint64_t)(-delta) > value ? 0u : value - (uint64_t)(-delta);
    g_runtime_counters[gauge] = value;
    if (value > g_runtime_counters[peak])
        g_runtime_counters[peak] = value;
    pthread_mutex_unlock(&g_runtime_stats_mutex);
}

void cetta_runtime_stats_snapshot(CettaRuntimeStats *out) {
    if (!out) return;
    pthread_mutex_lock(&g_runtime_stats_mutex);
    memcpy(out->counters, g_runtime_counters, sizeof(g_runtime_counters));
    pthread_mutex_unlock(&g_runtime_stats_mutex);
    out->counters[CETTA_RUNTIME_COUNTER_SYMBOL_TABLE_ENTRIES] = g_symbols
        ? atomic_load_explicit(&g_symbols->entry_len, memory_order_acquire)
        : 0u;
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
    if (!out || !stats) return;
    for (uint32_t i = 0; i < CETTA_RUNTIME_COUNTER_COUNT; i++) {
        fprintf(out, "runtime-counter %s %lld\n",
                cetta_runtime_counter_name((CettaRuntimeCounter)i),
                (long long)clamp_counter(stats->counters[i]));
    }
}

void cetta_runtime_stats_populate_space(Space *space, Arena *a,
                                        const CettaRuntimeStats *stats) {
    if (!space || !a || !stats) return;
    if (space->native.universe) {
        AtomId fact_ids[CETTA_RUNTIME_COUNTER_COUNT];
        bool direct_ok = true;
        AtomId fact_head_id =
            tu_intern_symbol(space->native.universe,
                             symbol_intern_cstr(g_symbols, "runtime-counter"));
        if (fact_head_id != CETTA_ATOM_ID_NONE) {
            for (uint32_t i = 0; i < CETTA_RUNTIME_COUNTER_COUNT; i++) {
                AtomId counter_name_id =
                    tu_intern_symbol(space->native.universe,
                                     symbol_intern_cstr(
                                         g_symbols,
                                         cetta_runtime_counter_name(
                                             (CettaRuntimeCounter)i)));
                AtomId counter_value_id =
                    tu_intern_int(space->native.universe,
                                  clamp_counter(stats->counters[i]));
                AtomId fact_children[3] = {
                    fact_head_id,
                    counter_name_id,
                    counter_value_id,
                };
                fact_ids[i] =
                    tu_expr_from_ids(space->native.universe, fact_children, 3);
                if (fact_ids[i] == CETTA_ATOM_ID_NONE) {
                    direct_ok = false;
                    break;
                }
            }
            if (direct_ok) {
                for (uint32_t i = 0; i < CETTA_RUNTIME_COUNTER_COUNT; i++) {
                    space_add_atom_id(space, fact_ids[i]);
                }
                return;
            }
        }
    }
    for (uint32_t i = 0; i < CETTA_RUNTIME_COUNTER_COUNT; i++) {
        Atom *fact[3] = {
            atom_symbol(a, "runtime-counter"),
            atom_symbol(a, cetta_runtime_counter_name((CettaRuntimeCounter)i)),
            atom_int(a, clamp_counter(stats->counters[i]))
        };
        Atom *fact_atom = atom_expr(a, fact, 3);
        if (!space_admit_atom(space, a, fact_atom))
            space_add(space, fact_atom);
    }
}
