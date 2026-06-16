#!/usr/bin/env bash
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$BENCH_DIR/../../.." && pwd)"
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"
OUT_DIR="${OUT_DIR:-$ROOT/runtime/benchmarks/rho/rhometta-pln-deduction-farm}"
REPEATS="${REPEATS:-3}"
CHAIN_LEN="${CHAIN_LEN:-64}"
THREADS_LIST="${THREADS_LIST:-1 2 4 8}"

if [ ! -x "$CETTA_BIN" ]; then
    echo "error: CeTTa binary not executable: $CETTA_BIN" >&2
    exit 2
fi

GEN_DIR="$OUT_DIR/generated"
LOG_DIR="$OUT_DIR/output"
RESULTS="$OUT_DIR/results.tsv"
mkdir -p "$GEN_DIR" "$LOG_DIR"
printf 'benchmark\tsize\tchain_len\tthreads\trepeat\tstatus\tseconds\tmax_rss_kb\tstdout_bytes\n' > "$RESULTS"

time_case() {
    local size="$1"
    local threads="$2"
    local case_name="$3"
    local repeat="$4"
    shift 4
    local stem="$LOG_DIR/${case_name}.${repeat}"
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
    printf 'rhometta-pln-deduction-farm\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$size" "$CHAIN_LEN" "$threads" "$repeat" "$status" "$seconds" \
        "$max_rss" "$stdout_bytes" >> "$RESULTS"
    return "$status"
}

sizes() {
    if [ -n "${SIZES:-}" ]; then
        printf '%s\n' $SIZES
    else
        grep -v '^#' "$BENCH_DIR/sizes.tsv" | tr -d '[:space:]' | grep -v '^$'
    fi
}

overall=0
for size in $(sizes); do
    for threads in $THREADS_LIST; do
        line="$(CHAIN_LEN="$CHAIN_LEN" THREADS="$threads" \
                "$BENCH_DIR/generate.sh" "$size" "$GEN_DIR")"
        name="${line%%	*}"
        metta="${line#*	}"
        for repeat in $(seq 1 "$REPEATS"); do
            if ! time_case "$size" "$threads" "$name" "$repeat" \
                    "$CETTA_BIN" --quiet --profile he-extended --lang he "$metta"; then
                overall=1
            fi
        done
    done
done

echo "results: $RESULTS"
exit "$overall"
