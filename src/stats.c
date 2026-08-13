#define CETTA_RUNTIME_STATS_IMPL 1
#include "stats.h"
#include "space.h"
#include "symbol.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static uint64_t g_runtime_counters[CETTA_RUNTIME_COUNTER_COUNT];
static bool g_runtime_stats_enabled = false;
static pthread_mutex_t g_runtime_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    "answer-ref-emit",
    "answer-ref-inflate-call",
    "answer-ref-materialize-call",
    "answer-ref-materialize-bytes",
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
    "eval-tail-survivor-arena-live-bytes-peak",
    "eval-tail-survivor-arena-reserved-bytes-peak",
    "eval-tail-survivor-reset-count",
    "eval-tail-survivor-reset-bytes",
    "query-episode-survivor-arena-alloc-bytes",
    "query-episode-survivor-arena-live-bytes-peak",
    "query-episode-survivor-arena-reserved-bytes-peak",
    "query-episode-promoted-answer-count",
    "query-episode-promoted-answer-bytes",
    "query-episode-delayed-outcome-survivor-count",
    "pathmap-projection-capture",
    "pathmap-projection-rows",
    "pathmap-shadow-refresh",
    "pathmap-shadow-refresh-atoms",
    "pathmap-materialize-native",
    "pathmap-materialize-native-atoms",
    "pathmap-direct-store",
    "pathmap-direct-remove",
    "space-atom-id-live-bytes-peak",
    "space-atom-id-capacity-bytes-peak",
    "match-chain-frontier-bindings-peak",
    "match-chain-subst-results-peak",
    "match-chain-eval-bytes-after-grounded-peak",
    "match-chain-eval-bytes-after-query-peak",
    "match-chain-eval-bytes-after-project-peak",
    "match-result-direct-stream-count",
    "match-result-snapshot-count",
    "match-result-direct-generated-outcome-count",
    "match-result-direct-generated-outcome-peak",
    "match-chain-grounded-delta-bytes-peak",
    "match-chain-query-delta-bytes-peak",
    "match-chain-project-delta-bytes-peak",
    "match-chain-substmatchset-bytes-peak",
    "match-chain-query-bindings-entry-delta-peak",
    "match-chain-query-bindings-constraint-delta-peak",
    "match-chain-seed-merge-bindings-entry-delta-peak",
    "match-chain-seed-merge-bindings-constraint-delta-peak",
    "hyperpose-threaded-run",
    "hyperpose-worker-started",
    "hyperpose-result-emitted",
    "hyperpose-cooperative-fallback",
    "hyperpose-fallback-thread-limit",
    "hyperpose-once-run",
    "hyperpose-cancel-request",
    "hyperpose-cancel-observed",
    "hyperpose-select-k-run",
    "parallel-queue-push",
    "parallel-queue-pop",
    "parallel-queue-wait",
    "parallel-queue-depth-peak",
    "parallel-queue-active-peak",
    "parallel-worker-task",
    "rho-async-endpoint-publish",
    "rho-async-endpoint-queued",
    "rho-async-endpoint-match",
    "arena-spare-hit",
    "arena-spare-miss",
    "arena-spare-recycle-block",
    "arena-spare-blocks-peak",
    "arena-spare-bytes-peak",
    "rho-quiet-macro-applied",
    "rho-quiet-macro-full-fire",
    "rho-quiet-macro-partial-fire",
    "rho-quiet-macro-bail-exact",
    "rho-quiet-macro-fallback-contention",
    "rho-quiet-macro-fallback-nonquiet",
    "rho-quiet-macro-fallback-unsafe-payload",
    "rho-quiet-macro-fallback-child-cap",
    "term-universe-copy-call",
    "term-universe-copy-node",
    "term-universe-copy-memo-hit",
    "term-universe-copy-estimated-arena-bytes",
    "term-universe-copy-memo-heap-bytes",
    "query-subst-flat-heap-bytes",
    "query-subst-matchset-heap-bytes",
    "cost-rho-parallel-wave",
    "cost-rho-parallel-wave-width-peak",
    "cost-rho-parallel-firing",
    "cost-rho-parallel-state-only-run",
    "cost-rho-parallel-receipt-run",
    "cost-rho-parallel-committed-claim",
    "cost-rho-funding-head-consumed",
    "cost-rho-receipt-producer-propagated",
    "cost-rho-receipt-cause-source-scanned",
    "cost-rho-receipt-event-allocation",
    "cost-rho-receipt-event-materialized",
    "cost-rho-receipt-event-retained",
    "cost-rho-receipt-validation",
    "cost-rho-parallel-acquired-claim",
    "cost-rho-parallel-released-claim",
    "prime-need-receipt-state-write",
    "prime-need-receipt-reach-query",
    "prime-need-receipt-reach-empty-target-accept",
    "prime-need-receipt-reach-boundary-reject",
    "prime-need-receipt-reach-self-accept",
    "prime-need-receipt-reach-depth-reject",
    "prime-need-receipt-reach-parent-accept",
    "prime-need-receipt-reach-fallback",
    "prime-need-receipt-reach-fallback-frame",
    "prime-need-receipt-reach-index-accept",
    "prime-need-receipt-reach-index-step",
    "prime-need-heap-lookup-query",
    "prime-need-heap-lookup-index-hit",
    "prime-need-heap-lookup-index-miss",
    "prime-need-heap-lookup-index-step",
    "prime-need-heap-lookup-log-fallback",
    "prime-need-heap-lookup-log-frame",
    "prime-need-capture-projection-query",
    "prime-need-capture-empty-binding-skip",
    "prime-need-capture-projection-exact",
    "prime-need-capture-projection-fallback",
    "prime-need-capture-scan-atom",
    "prime-need-capture-referenced-cell",
    "prime-need-capture-referenced-id",
    "prime-eval-stack-root-run",
    "prime-eval-stack-frame-push",
    "prime-eval-stack-frame-depth-peak",
    "prime-eval-stack-task-bind",
    "prime-eval-stack-task-call",
    "prime-eval-stack-task-normalize",
    "prime-eval-stack-frame-strict",
    "prime-eval-stack-frame-if",
    "prime-eval-stack-frame-force",
    "prime-eval-stack-frame-bind-finish",
    "prime-eval-stack-gc-frame-safe-point",
    "prime-eval-stack-poisoned-task",
    "prime-eval-stack-gc-evacuated-bytes",
    "prime-eval-stack-gc-fresh-budget-peak",
    "prime-need-ancestor-query",
    "prime-need-ancestor-index-step",
    "prime-need-ancestor-log-step",
    "prime-need-storage-key-scan-frame",
    "match-native-probe",
    "match-native-trie-lookup",
    "match-native-candidates",
    "match-smset-rows",
    "loop-view-eligible",
    "loop-view-calls",
    "subst-node-new",
    "bindings-project-sparse",
    "bindings-project-sparse-index-lookup",
    "bindings-project-sparse-entry",
    "bindings-project-dense",
    "petta-libpl-admission-negative",
    "lib-prolog-engine-claim",
    "lib-prolog-engine-release",
    "lib-prolog-prepare",
    "pathmap-indexed-query",
    "pathmap-indexed-catalog-build",
    "pathmap-indexed-catalog-row-scan",
    "pathmap-indexed-access-path-build",
    "pathmap-indexed-access-path-row",
    "pathmap-indexed-plan-build",
    "pathmap-indexed-plan-cache-hit",
    "pathmap-indexed-trie-seek",
    "pathmap-indexed-trie-descent",
    "pathmap-indexed-row-emit",
    "pathmap-indexed-frame-cell-peak",
    "pathmap-indexed-row-aggregate",
    "pathmap-indexed-count-pushdown",
    "pathmap-indexed-replay-hit",
    "pathmap-batch-mutation-call",
    "pathmap-batch-mutation-occurrence",
    "pathmap-batch-mutation-publication",
    "pathmap-batch-pack-ns",
    "pathmap-batch-clone-ns",
    "pathmap-batch-ffi-ns",
    "pathmap-pull-match-run",
    "pathmap-pull-match-row",
    "pathmap-pull-match-generated-outcome",
    "pathmap-pull-match-generated-outcome-peak",
    "pathmap-pull-atoms-run",
    "pathmap-pull-atoms-row",
    "pathmap-direct-transfer-call",
    "pathmap-direct-transfer-occurrence",
    "pathmap-direct-transfer-publication",
    "term-universe-source-memo-lookup",
    "term-universe-source-memo-hit",
    "term-universe-source-memo-store",
    "loop-view-prepared-tail",
    "prepared-fold-admission",
    "prepared-fold-step",
    "prepared-fold-commit",
    "prepared-pure-machine-admission",
    "prepared-pure-machine-step",
    "prepared-pure-machine-decline",
    "prepared-pure-call-admission",
    "prepared-pure-call-commit",
    "prepared-pure-call-decline",
    "prepared-pure-call-gc-collection",
    "prepared-pure-call-gc-evacuated-bytes",
    "prepared-pure-call-gc-reclaimed-bytes",
    "prepared-pure-call-gc-survivor-bytes-peak",
    "prepared-pure-call-gc-dead-slots",
    "prepared-pure-call-gc-live-slots-peak",
    "prepared-pure-call-program-cache-hit",
    "prepared-pure-call-program-cache-store",
    "prepared-pure-call-thunk-memo-store",
    "prepared-pure-call-thunk-memo-hit",
    "prepared-pure-call-thunk-blackhole",
    "prepared-pure-call-ephemeron-live-peak",
    "prepared-pure-call-ephemeron-reclaimed",
    "prepared-pure-call-thunk-path-compression",
    "prepared-pure-call-total-normalization",
    "prepared-pure-call-tail-reentry",
    "pathmap-indexed-residual-query",
    "prime-need-plan-call",
    "prime-need-plan-candidate-scan",
    "prime-need-plan-demand-analysis",
    "prime-need-plan-admission",
    "prepared-collection-pull-admission",
    "prepared-collection-pull-item",
    "prepared-collection-pull-commit",
    "prepared-collection-pull-decline",
    "prepared-pure-head-lookup",
    "prepared-pure-head-probe",
    "prepared-pure-decision-run",
    "prepared-pure-decision-fallback-unready",
    "prepared-pure-decision-clause-input",
    "prepared-pure-decision-clause-survivor",
    "prepared-pure-decision-full-match",
    "prepared-pure-intrinsic-type-hit",
    "prepared-pure-type-service-fallback",
    "prepared-pure-callable-lookup",
    "prepared-pure-callable-hit",
    "prepared-pure-callable-miss",
    "prepared-pure-decision-direct-demand",
    "match-decision-compile",
    "match-decision-run",
    "match-decision-clause-input",
    "match-decision-clause-survivor",
    "match-decision-linear-fallback",
    "match-decision-unavailable-path",
    "match-decision-invalidation",
    "match-decision-exact-attempt",
    "match-decision-whole-clause-freshen",
    "match-decision-whole-clause-freshen-bytes",
    "prime-need-publication-raw-result",
    "prime-need-publication-raw-with-ref",
    "prime-need-publication-forced-world",
    "prime-need-publication-pair-probe",
    "prime-need-publication-descendant",
    "prime-need-publication-fallback",
    "prime-need-snapshot-merge-identical",
    "prime-need-receipt-merge-identical",
    "petta-typecheck-boundary-entry",
    "petta-type-obligation-cache-hit",
    "petta-type-obligation-cache-miss",
    "petta-type-obligation-authority-retry",
    "petta-type-obligation-guard-scheduled",
    "petta-type-obligation-guard-established",
    "petta-typecheck-boundary-plan-cache-hit",
    "petta-typecheck-boundary-plan-cache-miss",
    "petta-typecheck-boundary-plan-all-none",
    "petta-typecheck-boundary-position-tested",
    "petta-libpl-plref-register",
    "petta-libpl-plref-fetch",
    "petta-libpl-plref-release",
    "petta-libpl-plref-live-per-runtime-peak",
    "petta-libpl-structural-list-cell-to-prolog",
    "petta-libpl-structural-list-cell-from-prolog",
    "prime-relational-plan-attempt",
    "prime-relational-plan-admission",
    "prime-relational-plan-static",
    "prime-relational-plan-guarded",
    "prime-relational-plan-value-decline",
    "prime-relational-plan-resolved-call-decline",
    "prime-relational-plan-suspend-decline",
    "prime-relational-plan-commit",
    "prime-relational-plan-replay-safe-argument",
    "prepared-pure-call-compile-attempt",
    "prepared-pure-call-compile-decline",
    "prepared-pure-call-execution-decline",
    "prime-need-region-admission",
    "prime-need-region-commit",
    "prime-need-region-fallback",
    "prime-need-region-promoted-outcome",
    "prime-need-region-depth-peak",
    "prime-need-region-reclaimed-bytes",
    "he-profiled-type-cache-hit",
    "he-profiled-type-cache-miss",
    "bindings-cycle-ground-value",
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
    pthread_mutex_lock(&g_runtime_stats_mutex);
    memset(g_runtime_counters, 0, sizeof(g_runtime_counters));
    pthread_mutex_unlock(&g_runtime_stats_mutex);
}

void cetta_runtime_stats_enable(void) {
    pthread_mutex_lock(&g_runtime_stats_mutex);
    g_runtime_stats_enabled = true;
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

void cetta_runtime_stats_snapshot(CettaRuntimeStats *out) {
    if (!out) return;
    pthread_mutex_lock(&g_runtime_stats_mutex);
    memcpy(out->counters, g_runtime_counters, sizeof(g_runtime_counters));
    pthread_mutex_unlock(&g_runtime_stats_mutex);
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
