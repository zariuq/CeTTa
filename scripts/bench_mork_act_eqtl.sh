#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CETTA_BIN:-$ROOT/cetta}"
RUNTIME_DIR="$ROOT/runtime"
ACT_PATH="$RUNTIME_DIR/bench_eqtl_for_mining.act"
PREP_INPUT="$RUNTIME_DIR/.prepare_eqtl_for_mining_act.generated.metta"
PREP_RESULT="$RUNTIME_DIR/prepare_eqtl_for_mining_act.out"
PREP_VM_LIMIT_KB="${PREP_VM_LIMIT_KB:-10485760}"

source "$ROOT/scripts/cetta_bench_build.sh"

MODE="${1:-attach}"
REBUILD=0
RESULT_HASH_BASELINE=""

if [ "$MODE" = "--rebuild" ]; then
    REBUILD=1
    MODE="${2:-all}"
elif [ "${2:-}" = "--rebuild" ]; then
    REBUILD=1
fi

if [ ! -x "$BIN" ]; then
    cetta_ensure_build_mode "$ROOT" "$BIN" pathmap "MORK eQTL ACT benchmark"
fi

cetta_ensure_build_mode "$ROOT" "$BIN" pathmap "MORK eQTL ACT benchmark"

mkdir -p "$RUNTIME_DIR"

bench_for_mode() {
    case "$1" in
        source)
            printf '%s\n' "benchmarks/bench_bio_eqtl_mork_source.metta"
            ;;
        materialize|import)
            printf '%s\n' "benchmarks/bench_bio_eqtl_mork_import_act.metta"
            ;;
        attach)
            printf '%s\n' "benchmarks/bench_bio_eqtl_mork_open_act.metta"
            ;;
        *)
            return 1
            ;;
    esac
}

ensure_act() {
    if [ "$REBUILD" -eq 0 ] && [ -f "$ACT_PATH" ]; then
        return 0
    fi

    cat > "$PREP_INPUT" <<EOF
!(import! &self mork)

!(import! &kb ../tests/eqtl_for_mining)
!(space-set-match-backend! &kb pathmap-imported)
!(bind! &compiled (mork:new-space))
!(match &kb \$atom (mork:add-atom &compiled \$atom))
!(mork:dump! &compiled "$ACT_PATH")
EOF

    echo "preparing compiled ACT artifact: $ACT_PATH" >&2
    bash -lc "ulimit -v $PREP_VM_LIMIT_KB && cd '$ROOT' && ./cetta --quiet --lang he '$PREP_INPUT' >'$PREP_RESULT' 2>&1"
    if [ -s "$PREP_RESULT" ]; then
        echo "error: ACT prepare produced unsuppressed output" >&2
        cat "$PREP_RESULT" >&2
        exit 1
    fi
}

counter() {
    local file="$1"
    local name="$2"
    awk -v counter_name="$name" '$1 == "runtime-counter" && $2 == counter_name { print $3; found=1; exit } END { if (!found) print "missing" }' "$file"
}

run_mode() {
    local mode="$1"
    local bench_rel
    local base_name
    local stats_file
    local time_file
    local result_file
    local status
    local elapsed
    local rss
    local result_hash
    local result_bytes

    bench_rel="$(bench_for_mode "$mode")"
    base_name="$(basename "$bench_rel" .metta)"
    stats_file="$RUNTIME_DIR/${base_name}.stats"
    time_file="$RUNTIME_DIR/${base_name}.time"
    result_file="$RUNTIME_DIR/${base_name}.out"

    set +e
    /usr/bin/time -f 'elapsed_sec=%e rss_kb=%M' -o "$time_file" \
        bash -lc "ulimit -v 6291456 && cd '$ROOT' && ./cetta --emit-runtime-stats --quiet --lang he '$bench_rel' >'$result_file'" \
        2> "$stats_file"
    status=$?
    set -e

    if [ "$status" -ne 0 ]; then
        echo "error: benchmark mode failed: $mode" >&2
        cat "$stats_file" >&2
        exit "$status"
    fi
    elapsed="$(sed -n 's/.*elapsed_sec=\([0-9.]*\).*/\1/p' "$time_file")"
    rss="$(sed -n 's/.*rss_kb=\([0-9][0-9]*\).*/\1/p' "$time_file")"
    result_hash="$(sha256sum "$result_file" | awk '{print $1}')"
    result_bytes="$(wc -c < "$result_file" | tr -d ' ')"

    if [ -z "$RESULT_HASH_BASELINE" ]; then
        RESULT_HASH_BASELINE="$result_hash"
    elif [ "$RESULT_HASH_BASELINE" != "$result_hash" ]; then
        echo "error: benchmark modes disagree semantically: $mode" >&2
        echo "expected hash: $RESULT_HASH_BASELINE" >&2
        echo "actual hash:   $result_hash" >&2
        exit 1
    fi

    printf 'mode=%s\n' "$mode"
    printf 'benchmark=%s\n' "$bench_rel"
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
    printf '\n'
}

ensure_act

case "$MODE" in
    prepare)
        ensure_act
        ;;
    all)
        run_mode source
        run_mode materialize
        run_mode attach
        ;;
    source|materialize|import|attach)
        run_mode "$MODE"
        ;;
    *)
        echo "usage: $0 [prepare|source|materialize|import|attach|all] [--rebuild]" >&2
        exit 1
        ;;
esac
