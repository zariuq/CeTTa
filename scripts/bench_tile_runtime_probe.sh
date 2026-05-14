#!/bin/bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/cetta"
BACKEND="${1:-native}"
PROBE="$ROOT/benchmarks/bench_tilepuzzle_probe.metta"

if [ ! -x "$BIN" ]; then
    echo "error: missing executable $BIN" >&2
    echo "hint: run 'make cetta' first" >&2
    exit 1
fi

out_file="$ROOT/runtime/tile_runtime_probe_${BACKEND}.out"
stats_file="$ROOT/runtime/tile_runtime_probe_${BACKEND}.stats"
time_file="$ROOT/runtime/tile_runtime_probe_${BACKEND}.time"
mkdir -p "$ROOT/runtime"

/usr/bin/time -v -o "$time_file" \
    bash -lc "ulimit -v 6291456 && cd '$ROOT' && ./cetta --count-only --emit-runtime-stats --profile he-extended --space-engine '$BACKEND' --lang he '$PROBE'" \
    > "$out_file" 2> "$stats_file"

counter() {
    local name="$1"
    awk -v counter_name="$name" '$1 == "runtime-counter" && $2 == counter_name { print $3; found=1; exit } END { if (!found) print "missing" }' "$stats_file"
}

result_line() {
    local index="$1"
    mapfile -t lines < "$out_file"
    if [ "${#lines[@]}" -le "$index" ]; then
        echo "missing"
        return
    fi
    printf '%s\n' "${lines[$index]}"
}

elapsed="$(grep -F 'Elapsed (wall clock) time' "$time_file" | awk -F': ' '{print $2}')"
rss="$(grep -F 'Maximum resident set size' "$time_file" | awk -F': ' '{print $2}')"

printf 'backend=%s\n' "$BACKEND"
printf 'steps=%s\n' "$(result_line 2)"
printf 'seen=%s\n' "$(result_line 3)"
printf 'query_equations=%s\n' "$(counter query-equations)"
printf 'query_candidates=%s\n' "$(counter query-equation-candidates)"
printf 'query_legacy_candidates=%s\n' "$(counter query-equation-legacy-candidates)"
printf 'query_subst_candidates=%s\n' "$(counter query-equation-subst-candidates)"
printf 'rename_vars=%s\n' "$(counter rename-vars)"
printf 'hash_exact_lookup=%s\n' "$(counter hash-space-exact-lookup)"
printf 'hash_exact_hit=%s\n' "$(counter hash-space-exact-hit)"
printf 'space_push=%s\n' "$(counter space-push)"
printf 'space_pop=%s\n' "$(counter space-pop)"
printf 'queue_compact=%s\n' "$(counter queue-compact)"
printf 'resolve_registry_hit=%s\n' "$(counter resolve-registry-hit)"
printf 'resolve_registry_rewrite=%s\n' "$(counter resolve-registry-rewrite)"
printf 'resolve_registry_noop=%s\n' "$(counter resolve-registry-noop)"
printf 'bindings_clone=%s\n' "$(counter bindings-clone)"
printf 'bindings_apply=%s\n' "$(counter bindings-apply)"
printf 'bindings_free=%s\n' "$(counter bindings-free)"
printf 'bindings_free_nonempty=%s\n' "$(counter bindings-free-nonempty)"
printf 'bindings_released_entry_capacity=%s\n' "$(counter bindings-released-entry-capacity)"
printf 'bindings_released_constraint_capacity=%s\n' "$(counter bindings-released-constraint-capacity)"
printf 'query_subst_emitted=%s\n' "$(counter query-equation-subst-emitted)"
printf 'query_subst_candidate_fallback=%s\n' "$(counter query-equation-subst-candidate-fallback)"
printf 'query_subst_bucket_fallback=%s\n' "$(counter query-equation-subst-bucket-fallback)"
printf 'wall=%s\n' "$elapsed"
printf 'rss_kb=%s\n' "$rss"
