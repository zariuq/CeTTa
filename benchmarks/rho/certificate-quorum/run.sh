#!/usr/bin/env bash
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$BENCH_DIR/../../.." && pwd)"
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"
RHOLANG_CLI="${RHOLANG_CLI:-$(command -v rholang-cli || true)}"
OUT_DIR="${OUT_DIR:-$ROOT/runtime/benchmarks/rho/certificate-quorum}"
REPEATS="${REPEATS:-3}"
RHOLANG_MAP_SIZE="${RHOLANG_MAP_SIZE:-268435456}"

if [ ! -x "$CETTA_BIN" ]; then
    echo "error: CeTTa binary not executable: $CETTA_BIN" >&2
    exit 2
fi

if [ ! -x "$RHOLANG_CLI" ]; then
    echo "error: rholang-cli not found or not executable: ${RHOLANG_CLI:-<unset>}" >&2
    exit 2
fi

GEN_DIR="$OUT_DIR/generated"
LOG_DIR="$OUT_DIR/output"
DATA_ROOT="$OUT_DIR/rholang-data/$(date -u +%Y%m%dT%H%M%SZ)-$$"
RESULTS="$OUT_DIR/results.tsv"
mkdir -p "$GEN_DIR" "$LOG_DIR" "$DATA_ROOT"
printf 'benchmark\tsize\tengine\trepeat\tstatus\tseconds\tmax_rss_kb\tstdout_bytes\n' > "$RESULTS"

time_case() {
    local size="$1"
    local engine="$2"
    local case_name="$3"
    local repeat="$4"
    shift 4
    local stem="$LOG_DIR/${case_name}.${engine}.${repeat}"
    local time_file="$stem.time"
    local status=0 seconds max_rss stdout_bytes
    /usr/bin/time -f '%e\t%M' -o "$time_file" -- "$@" \
        > "$stem.out" 2> "$stem.err" || status=$?
    if [ "$status" -eq 0 ] && grep -q '(Error ' "$stem.out"; then
        status=70
    fi
    if [ "$status" -eq 0 ] && grep -q 'NoReturn' "$stem.out"; then
        status=72
    fi
    if [ "$status" -eq 0 ] && [ ! -s "$stem.out" ]; then
        status=71
    fi
    read -r seconds max_rss < <(tail -n 1 "$time_file")
    stdout_bytes="$(wc -c < "$stem.out" | tr -d '[:space:]')"
    printf 'certificate-quorum\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$size" "$engine" "$repeat" "$status" "$seconds" "$max_rss" \
        "$stdout_bytes" >> "$RESULTS"
    return "$status"
}

sizes() {
    if [ -n "${SIZES:-}" ]; then
        printf '%s\n' $SIZES
    else
        awk 'NF && $1 !~ /^#/ { print $1 }' "$BENCH_DIR/sizes.tsv"
    fi
}

printf '== rho certificate-quorum benchmark ==\n'
printf 'CeTTa:      %s\n' "$CETTA_BIN"
printf 'Rholang:    %s\n' "$RHOLANG_CLI"
printf 'Repeats:    %s\n' "$REPEATS"
printf 'Output dir: %s\n' "$OUT_DIR"
printf 'Status:     %s\n' "${CERT_STATUS:-valid}"

for size in $(sizes); do
    generated="$("$BENCH_DIR/generate.sh" "$size" "$GEN_DIR")"
    case_name="$(printf '%s' "$generated" | cut -f1)"
    metta="$(printf '%s' "$generated" | cut -f2)"
    mrho="$(printf '%s' "$generated" | cut -f3)"
    rho="$(printf '%s' "$generated" | cut -f4)"
    printf 'case=%s size=%s\n' "$case_name" "$size"
    for rep in $(seq 1 "$REPEATS"); do
        time_case "$size" "cetta-rhocalc-run" "$case_name" "$rep" \
            "$CETTA_BIN" --quiet --lang rhocalc --syntax mrho "$mrho"
        data_dir="$DATA_ROOT/$case_name/$rep"
        mkdir -p "$(dirname "$data_dir")"
        time_case "$size" "f1r3node-rholang-cli" "$case_name" "$rep" \
            "$RHOLANG_CLI" --quiet --map-size "$RHOLANG_MAP_SIZE" \
            --data-dir "$data_dir" "$rho"
    done
done

printf 'results=%s\n' "$RESULTS"
