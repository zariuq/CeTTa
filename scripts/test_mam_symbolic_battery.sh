#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cetta="${1:-$root/cetta}"
case_timeout="${CETTA_MAM_BATTERY_TIMEOUT:-30}"
mkdir -p "$root/runtime"
scratch="$(mktemp -d "$root/runtime/mam-symbolic-battery.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT INT TERM

lane_args() {
    case "$1" in
        he) printf '%s\n' --lang he --profile he-extended ;;
        prime|petta) printf '%s\n' --lang "$1" ;;
        *) return 1 ;;
    esac
}

render_case() {
    local program="$1" m="$2" n="$3" output="$4"
    case "$program" in
        ack|peano_ack)
            sed "s/@M@/$m/g; s/@N@/$n/g" \
                "$root/benchmarks/battery/$program.metta.in" >"$output"
            ;;
        nrev|deriv|queens)
            sed "s/@N@/$n/g" \
                "$root/benchmarks/battery/$program.metta.in" >"$output"
            ;;
        *) return 1 ;;
    esac
}

run_lane() {
    local lane="$1"
    local -a args=()
    mapfile -t args < <(lane_args "$lane")

    local program m n expected input actual
    while read -r program m n expected; do
        input="$scratch/$lane-$program-$n.metta"
        render_case "$program" "$m" "$n" "$input"
        actual="$(timeout "$case_timeout" "$cetta" "${args[@]}" "$input")"
        if [[ "$lane" != petta ]]; then
            expected="[$expected]"
        fi
        if [[ "$actual" != "$expected" ]]; then
            printf 'FAIL: %s %s(%s,%s) expected %s, got %s\n' \
                "$lane" "$program" "$m" "$n" "$expected" "$actual" >&2
            return 1
        fi
    done <<'EOF'
ack 3 8 2045
peano_ack 2 1000 2003
nrev x 1000 1000
deriv x 8 27267
EOF

    input="$scratch/$lane-queens-4.metta"
    render_case queens x 4 "$input"
    actual="$(timeout "$case_timeout" "$cetta" "${args[@]}" "$input")"
    local queens4_he queens4_petta
    queens4_he='[(Cons 2 (Cons 4 (Cons 1 (Cons 3 Nil)))), (Cons 3 (Cons 1 (Cons 4 (Cons 2 Nil))))]'
    queens4_petta=$'(Cons 2 (Cons 4 (Cons 1 (Cons 3 Nil))))\n(Cons 3 (Cons 1 (Cons 4 (Cons 2 Nil))))'
    expected="$queens4_he"
    [[ "$lane" == petta ]] && expected="$queens4_petta"
    if [[ "$actual" != "$expected" ]]; then
        printf 'FAIL: %s queens(4) exact frontier changed\n' "$lane" >&2
        return 1
    fi

    input="$scratch/$lane-queens-8.metta"
    render_case queens x 8 "$input"
    actual="$(timeout "$case_timeout" "$cetta" \
        "${args[@]}" --count-only "$input")"
    local count="$actual"
    if [[ "$lane" == petta ]]; then
        count="$(printf '%s\n' "$actual" | awk 'NF { n++ } END { print n + 0 }')"
    fi
    if [[ "$count" != 92 ]]; then
        printf 'FAIL: %s queens(8) expected 92 answers, got %s\n' \
            "$lane" "$count" >&2
        return 1
    fi
}

for lane in he prime petta; do
    run_lane "$lane"
done

printf '%s\n' \
    'PASS: shared MAM symbolic battery (nested calls, heap recursion, constructor dispatch, and choice frontiers)'
