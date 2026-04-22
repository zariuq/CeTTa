#!/bin/bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CETTA_BIN:-$ROOT/cetta}"
RUNTIME_DIR="$ROOT/runtime"
BENCH_REL="benchmarks/bench_bio_1m_mork_open_act.metta"

source "$ROOT/scripts/cetta_bench_build.sh"

ACT_FILES=(
    "bench_bio_1m_eqtl.act"
    "bench_bio_1m_refseq.act"
    "bench_bio_1m_tfbs.act"
    "bench_bio_1m_pathway.act"
    "bench_bio_1m_hetionet.act"
    "bench_bio_1m_patterns.act"
)

PREP_INPUTS=(
    "tests/support/prepare_bio_1m_eqtl_act.metta"
    "tests/support/prepare_bio_1m_refseq_act.metta"
    "tests/support/prepare_bio_1m_tfbs_act.metta"
    "tests/support/prepare_bio_1m_pathway_act.metta"
    "tests/support/prepare_bio_1m_hetionet_act.metta"
    "tests/support/prepare_bio_1m_patterns_act.metta"
)

REBUILD=0
if [ "${1:-}" = "--rebuild" ]; then
    REBUILD=1
fi

if [ ! -x "$BIN" ]; then
    cetta_ensure_build_mode "$ROOT" "$BIN" pathmap "MORK bio-1m ACT attach benchmark"
fi

cetta_ensure_build_mode "$ROOT" "$BIN" pathmap "MORK bio-1m ACT attach benchmark"

mkdir -p "$RUNTIME_DIR"

counter() {
    local file="$1"
    local name="$2"
    awk -v counter_name="$name" '$1 == "runtime-counter" && $2 == counter_name { print $3; found=1; exit } END { if (!found) print "missing" }' "$file"
}

ensure_acts() {
    local i
    for i in "${!ACT_FILES[@]}"; do
        local act_path="$RUNTIME_DIR/${ACT_FILES[$i]}"
        local prep_input="${PREP_INPUTS[$i]}"
        local prep_base
        local prep_result
        prep_base="$(basename "$prep_input" .metta)"
        prep_result="$RUNTIME_DIR/${prep_base}.out"
        if [ "$REBUILD" -eq 0 ] && [ -f "$act_path" ]; then
            continue
        fi
        echo "preparing compiled ACT artifact: $act_path" >&2
        bash -lc "ulimit -v 6291456 && cd '$ROOT' && ./cetta --quiet --lang he '$prep_input' >'$prep_result' 2>&1"
        if [ -s "$prep_result" ]; then
            echo "error: ACT prepare produced unsuppressed output for $prep_input" >&2
            cat "$prep_result" >&2
            exit 1
        fi
    done
}

ensure_acts

stats_file="$RUNTIME_DIR/bench_bio_1m_mork_open_act.stats"
time_file="$RUNTIME_DIR/bench_bio_1m_mork_open_act.time"
result_file="$RUNTIME_DIR/bench_bio_1m_mork_open_act.out"
result_hash=""
result_bytes=""

set +e
/usr/bin/time -v -o "$time_file" \
    bash -lc "ulimit -v 6291456 && cd '$ROOT' && ./cetta --emit-runtime-stats --quiet --lang he '$BENCH_REL' >'$result_file'" \
    2> "$stats_file"
status=$?
set -e

if [ "$status" -ne 0 ]; then
    echo "error: benchmark mode failed: attach" >&2
    cat "$stats_file" >&2
    exit "$status"
fi

result_hash="$(sha256sum "$result_file" | awk '{print $1}')"
result_bytes="$(wc -c < "$result_file" | tr -d ' ')"

elapsed="$(grep -F 'Elapsed (wall clock) time' "$time_file" | awk -F': ' '{print $2}')"
rss="$(grep -F 'Maximum resident set size' "$time_file" | awk -F': ' '{print $2}')"

printf 'mode=attach\n'
printf 'benchmark=%s\n' "$BENCH_REL"
printf 'query_equations=%s\n' "$(counter "$stats_file" query-equations)"
printf 'query_candidates=%s\n' "$(counter "$stats_file" query-equation-candidates)"
printf 'query_subst_candidates=%s\n' "$(counter "$stats_file" query-equation-subst-candidates)"
printf 'rename_vars=%s\n' "$(counter "$stats_file" rename-vars)"
printf 'bindings_clone=%s\n' "$(counter "$stats_file" bindings-clone)"
printf 'bindings_apply=%s\n' "$(counter "$stats_file" bindings-apply)"
printf 'bindings_free=%s\n' "$(counter "$stats_file" bindings-free)"
printf 'imported_bridge_v2_hit=%s\n' "$(counter "$stats_file" imported-bridge-v2-hit)"
printf 'imported_bridge_v2_fallback=%s\n' "$(counter "$stats_file" imported-bridge-v2-fallback)"
printf 'imported_bridge_v3_hit=%s\n' "$(counter "$stats_file" imported-bridge-v3-hit)"
printf 'imported_bridge_v3_fallback=%s\n' "$(counter "$stats_file" imported-bridge-v3-fallback)"
printf 'attached_act_open=%s\n' "$(counter "$stats_file" attached-act-open)"
printf 'attached_act_query=%s\n' "$(counter "$stats_file" attached-act-query)"
printf 'attached_act_materialize=%s\n' "$(counter "$stats_file" attached-act-materialize)"
printf 'attached_act_materialize_atoms=%s\n' "$(counter "$stats_file" attached-act-materialize-atoms)"
printf 'result_sha256=%s\n' "$result_hash"
printf 'result_bytes=%s\n' "$result_bytes"
printf 'wall=%s\n' "$elapsed"
printf 'rss_kb=%s\n' "$rss"
