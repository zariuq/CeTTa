#include "stats.h"
#include "space.h"
#include "symbol.h"

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
    "term-universe-byte-entry",
    "term-universe-fallback-entry",
    "term-universe-blob-bytes",
    "term-universe-lazy-decode",
    "outcome-variant-factor-attempt",
    "outcome-variant-factor-success",
    "outcome-variant-slot-materialize",
    "mork-add-call",
    "mork-add-lower-ns",
    "mork-add-ffi-ns",
    "mork-add-expr-bytes",
    "mork-add-batch-call",
    "mork-add-batch-items",
    "mork-add-batch-pack-ns",
    "mork-add-batch-ffi-ns",
    "mork-add-batch-packet-bytes",
    "mork-add-batch-native-ns",
    "mork-add-batch-dispatch-ns",
    "mork-add-batch-resolve-ns",
    "mork-add-stream-eval-ns",
    "mork-add-stream-insert-ns",
    "outcome-variant-slot-sink",
    "outcome-variant-prefix-compact",
    "outcome-variant-materialize-top-level",
    "outcome-variant-materialize-interpret-tuple",
    "outcome-variant-materialize-dispatch-head",
    "outcome-variant-materialize-dispatch-call-term",
    "outcome-variant-materialize-let-chain",
    "outcome-variant-materialize-error-filter",
    "bindings-lookup-cache-hit",
    "bindings-lookup-cache-miss",
    "bindings-apply-space-conj-default",
    "bindings-apply-space-conj-imported",
    "bindings-apply-eval-chain-step",
    "bindings-apply-eval-chain-last",
    "bindings-apply-eval-chain-body",
    "bindings-apply-match-template",
    "bindings-lookup-resolve",
    "bindings-lookup-add-guard",
    "bindings-lookup-apply",
    "bindings-lookup-loop-check",
    "bindings-lookup-match",
    "bindings-loop-call-parse",
    "bindings-loop-call-unify",
    "bindings-loop-call-native-query",
    "bindings-loop-call-mork-direct-row",
    "bindings-loop-call-mork-conj-merge",
    "bindings-loop-call-mork-conj-direct",
    "bindings-loop-call-imported-exact",
    "bindings-loop-call-imported-legacy",
    "bindings-loop-call-native-candidate",
    "bindings-loop-call-eq-store",
    "bindings-loop-call-eq-decoded",
    "bindings-apply-rewrite-node-visit",
    "bindings-apply-epoch-node-visit",
    "bindings-loop-node-visit",
    "query-visible-node-visit",
    "query-visible-dedup-scan",
    "bindings-seen-scan",
    "eval-c-stack-guard-budget-bytes",
    "eval-c-stack-guard-depth-peak",
    "eval-c-stack-guard-delta-bytes-peak",
    "eval-c-stack-guard-trip-eval",
    "eval-c-stack-guard-trip-bind",
    "eval-c-stack-guard-trip-bind-typed",
    "persistent-arena-alloc-bytes",
    "persistent-arena-live-bytes-peak",
    "persistent-arena-reserved-bytes-peak",
    "eval-arena-alloc-bytes",
    "eval-arena-live-bytes-peak",
    "eval-arena-reserved-bytes-peak",
    "scratch-arena-alloc-bytes",
    "scratch-arena-live-bytes-peak",
    "scratch-arena-reserved-bytes-peak",
    "bindings-entry-pool-bytes",
    "bindings-entry-pool-bytes-peak",
    "bindings-entry-retained-bytes",
    "bindings-entry-retained-bytes-peak",
    "bindings-entry-active-bytes-peak",
    "bindings-constraint-pool-bytes",
    "bindings-constraint-pool-bytes-peak",
    "bindings-constraint-retained-bytes",
    "bindings-constraint-retained-bytes-peak",
    "bindings-constraint-active-bytes-peak",
    "eval-tail-safe-point-count",
    "eval-tail-reclaimed-bytes",
    "eval-tail-promoted-binding-entries-peak",
    "eval-tail-promoted-binding-constraints-peak",
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
bool cetta_runtime_stats_is_enabled(void) { return g_runtime_stats_enabled; }

void cetta_runtime_stats_add(CettaRuntimeCounter counter, uint64_t delta) {
    if (__builtin_expect(!g_runtime_stats_enabled, 1))
        return;
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    g_runtime_counters[counter] += delta;
}

void cetta_runtime_stats_set(CettaRuntimeCounter counter, uint64_t value) {
    if (__builtin_expect(!g_runtime_stats_enabled, 1))
        return;
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    g_runtime_counters[counter] = value;
}

void cetta_runtime_stats_update_max(CettaRuntimeCounter counter, uint64_t value) {
    if (__builtin_expect(!g_runtime_stats_enabled, 1))
        return;
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    if (value > g_runtime_counters[counter])
        g_runtime_counters[counter] = value;
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
    if (space->universe) {
        AtomId fact_ids[CETTA_RUNTIME_COUNTER_COUNT];
        bool direct_ok = true;
        AtomId fact_head_id =
            tu_intern_symbol(space->universe,
                             symbol_intern_cstr(g_symbols, "runtime-counter"));
        if (fact_head_id != CETTA_ATOM_ID_NONE) {
            for (uint32_t i = 0; i < CETTA_RUNTIME_COUNTER_COUNT; i++) {
                AtomId counter_name_id =
                    tu_intern_symbol(space->universe,
                                     symbol_intern_cstr(
                                         g_symbols,
                                         cetta_runtime_counter_name(
                                             (CettaRuntimeCounter)i)));
                AtomId counter_value_id =
                    tu_intern_int(space->universe,
                                  clamp_counter(stats->counters[i]));
                AtomId fact_children[3] = {
                    fact_head_id,
                    counter_name_id,
                    counter_value_id,
                };
                fact_ids[i] = tu_expr_from_ids(space->universe, fact_children, 3);
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
