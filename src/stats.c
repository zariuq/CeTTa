#include "stats.h"
#include "space.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint64_t g_runtime_counters[CETTA_RUNTIME_COUNTER_COUNT];
static bool g_runtime_stats_enabled = false;

static const char *const CETTA_RUNTIME_COUNTER_NAMES[CETTA_RUNTIME_COUNTER_COUNT] = {
    "bindings-lookup",
    "bindings-clone",
    "bindings-add",
    "bindings-constraint-add",
    "bindings-merge",
    "rename-vars",
    "query-equations",
    "query-equation-candidates",
    "query-equation-legacy-candidates",
    "query-equation-subst-candidates",
    "eq-index-rebuild",
    "ty-index-rebuild",
    "hashcons-attempt",
    "hashcons-hit",
    "hashcons-insert",
    "hash-space-exact-lookup",
    "hash-space-exact-hit",
    "subst-query-exact-shortcut",
    "imported-bridge-v2-hit",
    "imported-bridge-v2-fallback",
    "imported-bridge-v3-hit",
    "imported-bridge-v3-fallback",
    "space-push",
    "space-pop",
    "queue-compact",
    "resolve-registry-hit",
    "resolve-registry-rewrite",
    "resolve-registry-noop",
    "query-equation-subst-emitted",
    "query-equation-subst-candidate-fallback",
    "query-equation-subst-bucket-fallback",
    "bindings-apply",
    "bindings-free",
    "bindings-free-nonempty",
    "bindings-released-entry-capacity",
    "bindings-released-constraint-capacity",
    "bindings-normalize",
    "attached-act-open",
    "attached-act-query",
    "attached-act-materialize",
    "attached-act-materialize-atoms",
    "body-visible-cache-lookup",
    "body-visible-cache-hit",
    "body-visible-cache-store",
    "table-hit",
    "table-miss",
    "table-stale-miss",
    "table-reuse",
    "table-answer-staged",
    "space-revision-bump",
    "term-universe-lookup",
    "term-universe-hit",
    "term-universe-insert",
    "outcome-variant-factor-attempt",
    "outcome-variant-factor-success",
    "outcome-variant-slot-materialize",
    "outcome-variant-slot-sink",
    "outcome-variant-prefix-compact",
    "outcome-variant-materialize-top-level",
    "outcome-variant-materialize-interpret-tuple",
    "outcome-variant-materialize-dispatch-head",
    "outcome-variant-materialize-dispatch-call-term",
    "outcome-variant-materialize-let-chain",
    "outcome-variant-materialize-error-filter",
};

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
    memset(g_runtime_counters, 0, sizeof(g_runtime_counters));
}

void cetta_runtime_stats_enable(void) { g_runtime_stats_enabled = true; }
void cetta_runtime_stats_disable(void) { g_runtime_stats_enabled = false; }

void cetta_runtime_stats_add(CettaRuntimeCounter counter, uint64_t delta) {
    if (__builtin_expect(!g_runtime_stats_enabled, 1))
        return;
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    g_runtime_counters[counter] += delta;
}

void cetta_runtime_stats_snapshot(CettaRuntimeStats *out) {
    if (!out) return;
    memcpy(out->counters, g_runtime_counters, sizeof(g_runtime_counters));
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
    for (uint32_t i = 0; i < CETTA_RUNTIME_COUNTER_COUNT; i++) {
        Atom *fact[3] = {
            atom_symbol(a, "runtime-counter"),
            atom_symbol(a, cetta_runtime_counter_name((CettaRuntimeCounter)i)),
            atom_int(a, clamp_counter(stats->counters[i]))
        };
        space_add(space, atom_expr(a, fact, 3));
    }
}
