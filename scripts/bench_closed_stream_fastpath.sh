#!/bin/bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/cetta"
SIZES="${1:-1000 10000 100000}"
REPEAT="${2:-3}"

if [ ! -x "$BIN" ]; then
    echo "error: missing executable $BIN" >&2
    echo "hint: run 'make BUILD=main cetta' first" >&2
    exit 1
fi

case_expr() {
    local mode="$1"
    case "$mode" in
        done)
            printf '%s' '!(import! &self list) !(let $xs (eval (list:range __N__)) (let $i (superpose $xs) done))'
            ;;
        id)
            printf '%s' '!(import! &self list) !(let $xs (eval (list:range __N__)) (let $i (superpose $xs) $i))'
            ;;
        plus)
            printf '%s' '!(import! &self list) !(let $xs (eval (list:range __N__)) (let $i (superpose $xs) (+ $i 1)))'
            ;;
        *)
            echo "error: unknown mode $mode" >&2
            exit 1
            ;;
    esac
}

run_case() {
    local mode="$1"
    local n="$2"
    local expr
    expr="$(case_expr "$mode")"
    expr="${expr//__N__/$n}"
    /usr/bin/time -f '%e' \
        bash -lc "ulimit -v 6291456 && cd '$ROOT' && ./cetta --quiet --lang he -e '$expr' >/dev/null" \
        2>&1
}

printf '%-10s %8s %8s %8s %8s %8s\n' "mode" "n" "run1" "run2" "run3" "avg"
printf '%-10s %8s %8s %8s %8s %8s\n' "----------" "--------" "--------" "--------" "--------" "--------"

for n in $SIZES; do
    for mode in done id plus; do
        times=()
        for ((rep = 1; rep <= REPEAT; rep++)); do
            times+=("$(run_case "$mode" "$n")")
        done
        avg="$(printf '%s\n' "${times[@]}" | awk '{sum += $1} END { if (NR == 0) print "0.00"; else printf "%.4f", sum / NR }')"
        run1="${times[0]:-}"
        run2="${times[1]:-}"
        run3="${times[2]:-}"
        printf '%-10s %8s %8s %8s %8s %8s\n' "$mode" "$n" "$run1" "$run2" "$run3" "$avg"
    done
    printf '\n'
done
