#!/bin/bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/cetta"
RUNTIME_DIR="$ROOT/runtime"
BENCH_DIR="$RUNTIME_DIR/bench_mork_add_interface"
SIZES="${1:-1000 10000 100000}"
REPEAT="${2:-3}"

if [ ! -x "$BIN" ]; then
    echo "error: missing executable $BIN" >&2
    echo "hint: run 'make BUILD=main cetta' first" >&2
    exit 1
fi

mkdir -p "$RUNTIME_DIR" "$BENCH_DIR"

bench_program_path() {
    local mode="$1"
    local n="$2"
    printf '%s/%s_%s.metta\n' "$BENCH_DIR" "$mode" "$n"
}

prepare_case_program() {
    local mode="$1"
    local n="$2"
    local path
    local tmp
    path="$(bench_program_path "$mode" "$n")"
    if [ -f "$path" ]; then
        printf '%s\n' "$path"
        return
    fi

    tmp="$(mktemp "$BENCH_DIR/${mode}_${n}.XXXXXX")"
    case "$mode" in
        native-singular)
            {
                printf '!(bind! &s (new-space))\n'
                for ((i = 0; i < n; i++)); do
                    printf '!(add-atom &s (friend sam %d))\n' "$i"
                done
                printf '!(println! done)\n'
            } > "$tmp"
            ;;
        mork-singular)
            {
                printf '!(import! &self mork)\n'
                printf '!(bind! &s (mork:new-space))\n'
                for ((i = 0; i < n; i++)); do
                    printf '!(mork:add-atom &s (friend sam %d))\n' "$i"
                done
                printf '!(println! done)\n'
            } > "$tmp"
            ;;
        mork-bulk-stream)
            {
                printf '!(import! &self mork)\n'
                printf '!(bind! &s (mork:new-space))\n'
                printf '!(let $atoms (\n'
                for ((i = 0; i < n; i++)); do
                    printf '   (friend sam %d)\n' "$i"
                done
                printf ')\n'
                printf '   (mork:add-atoms &s (collapse (superpose $atoms))))\n'
                printf '!(println! done)\n'
            } > "$tmp"
            ;;
        mork-bulk-materialized)
            {
                printf '!(import! &self mork)\n'
                printf '!(bind! &s (mork:new-space))\n'
                printf '!(let $atoms (\n'
                for ((i = 0; i < n; i++)); do
                    printf '   (friend sam %d)\n' "$i"
                done
                printf ')\n'
                printf '   (mork:add-atoms &s $atoms))\n'
                printf '!(println! done)\n'
            } > "$tmp"
            ;;
        *)
            rm -f "$tmp"
            echo "error: unknown mode $mode" >&2
            exit 1
            ;;
    esac
    mv "$tmp" "$path"
    printf '%s\n' "$path"
}

run_case() {
    local mode="$1"
    local n="$2"
    local program
    program="$(prepare_case_program "$mode" "$n")"
    /usr/bin/time -f '%e' \
        bash -lc "ulimit -v 10485760 && cd '$ROOT' && ./cetta --quiet --lang he '$program' >/dev/null" \
        2>&1
}

printf '%-22s %8s %8s %8s %8s %8s\n' "mode" "n" "run1" "run2" "run3" "avg"
printf '%-22s %8s %8s %8s %8s %8s\n' "----------------------" "--------" "--------" "--------" "--------" "--------"

for n in $SIZES; do
    for mode in native-singular mork-singular mork-bulk-stream mork-bulk-materialized; do
        times=()
        for ((rep = 1; rep <= REPEAT; rep++)); do
            times+=("$(run_case "$mode" "$n")")
        done
        avg="$(printf '%s\n' "${times[@]}" | awk '{sum += $1} END { if (NR == 0) print "0.00"; else printf "%.4f", sum / NR }')"
        run1="${times[0]:-}"
        run2="${times[1]:-}"
        run3="${times[2]:-}"
        printf '%-22s %8s %8s %8s %8s %8s\n' "$mode" "$n" "$run1" "$run2" "$run3" "$avg"
    done
    printf '\n'
done
