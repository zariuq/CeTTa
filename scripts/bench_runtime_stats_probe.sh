#!/bin/bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/cetta"
RUNTIME_DIR="$ROOT/runtime"
LOG_FILE="$RUNTIME_DIR/optimization_log.tsv"
TIMESTAMP="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
GIT_REV="$(git -C "$ROOT" describe --always --dirty --broken 2>/dev/null || echo unknown)"

if [ ! -x "$BIN" ]; then
    echo "error: missing executable $BIN" >&2
    echo "hint: run 'make cetta' first" >&2
    exit 1
fi

mkdir -p "$RUNTIME_DIR"

if [ ! -f "$LOG_FILE" ]; then
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "timestamp" "git_rev" "workload" "results" "bindings_clone" \
        "rename_vars" "query_equations" "query_candidates" \
        "hashcons_attempt" "hashcons_hit" "hashcons_insert" \
        "hash_exact_lookup" "hash_exact_hit" "exact_shortcut" \
        "wall" "rss_kb" \
        > "$LOG_FILE"
fi

run_probe() {
    local name="$1"
    local source_file="$2"
    local query_expr="$3"
    local tmp_file="$RUNTIME_DIR/.perf_runtime_stats_${name}.metta"
    local out_file="$RUNTIME_DIR/perf_runtime_stats_${name}.out"
    local time_file="$RUNTIME_DIR/perf_runtime_stats_${name}.time"
    local -a lines
    local elapsed rss

    {
        sed '$d' "$source_file"
        printf '\n'
        printf '!(reset-runtime-stats!)\n'
        printf '!(%s)\n' "$query_expr"
        printf '!(bind! &stats (runtime-stats!))\n'
        printf '!(match &stats (runtime-counter bindings-clone $n) $n)\n'
        printf '!(match &stats (runtime-counter rename-vars $n) $n)\n'
        printf '!(match &stats (runtime-counter query-equations $n) $n)\n'
        printf '!(match &stats (runtime-counter query-equation-candidates $n) $n)\n'
        printf '!(match &stats (runtime-counter hashcons-attempt $n) $n)\n'
        printf '!(match &stats (runtime-counter hashcons-hit $n) $n)\n'
        printf '!(match &stats (runtime-counter hashcons-insert $n) $n)\n'
        printf '!(match &stats (runtime-counter hash-space-exact-lookup $n) $n)\n'
        printf '!(match &stats (runtime-counter hash-space-exact-hit $n) $n)\n'
        printf '!(match &stats (runtime-counter subst-query-exact-shortcut $n) $n)\n'
    } > "$tmp_file"

    /usr/bin/time -v -o "$time_file" \
        bash -lc "ulimit -v 6291456 && cd '$ROOT' && ./cetta --count-only --profile he_extended --lang he '$tmp_file'" \
        > "$out_file"

    mapfile -t lines < "$out_file"
    if [ "${#lines[@]}" -ne 13 ]; then
        echo "error: unexpected output shape for $name" >&2
        cat "$out_file" >&2
        rm -f "$tmp_file"
        exit 1
    fi

    elapsed="$(grep -F 'Elapsed (wall clock) time' "$time_file" | awk -F': ' '{print $2}')"
    rss="$(grep -F 'Maximum resident set size' "$time_file" | awk -F': ' '{print $2}')"

    printf '%-12s %8s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s\n' \
        "$name" "${lines[1]}" "${lines[3]}" "${lines[4]}" "${lines[5]}" "${lines[6]}" \
        "${lines[7]}" "${lines[8]}" "${lines[9]}" "${lines[10]}" "${lines[11]}" "${lines[12]}"
    printf '  wall=%s rss_kb=%s\n' "$elapsed" "$rss"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$TIMESTAMP" "$GIT_REV" "$name" "${lines[1]}" "${lines[3]}" "${lines[4]}" \
        "${lines[5]}" "${lines[6]}" "${lines[7]}" "${lines[8]}" "${lines[9]}" \
        "${lines[10]}" "${lines[11]}" "${lines[12]}" "$elapsed" "$rss" >> "$LOG_FILE"

    rm -f "$tmp_file"
}

printf '%-12s %8s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s\n' \
    "workload" "results" "bind-clone" "rename" "query-eq" "query-cand" \
    "hc-attempt" "hc-hit" "hc-insert" "hash-look" "hash-hit" "shortcut"
printf '%-12s %8s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s\n' \
    "--------" "-------" "----------" "------" "--------" "----------" \
    "----------" "------" "---------" "---------" "--------" "--------"

run_probe "backchain" "$ROOT/tests/bench_backchain_he.metta" \
    "ift (deduce (Evaluation (mortal \$x))) \$x"
run_probe "matchjoin3" "$ROOT/tests/bench_matchjoin3_he.metta" \
    "threehop"
