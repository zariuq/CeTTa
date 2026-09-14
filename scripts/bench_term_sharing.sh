#!/usr/bin/env bash
# =============================================================================
# Term Sharing Benchmark Suite
# =============================================================================
#
# Runs the targeted term sharing tests and reports key metrics.
#
# Usage:
#   make bench-term-sharing
#   CETTA_BIN=runtime/cetta-python-runtime-stats \
#     ./scripts/bench_term_sharing.sh [quick|blowup|all]
#
# Modes:
#   quick  - Run stress test only (always completes)
#   blowup - Run blowup test at depth 4
#   all    - Run both
#
# =============================================================================

set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"
EXPECTED="$ROOT/benchmarks/bench_term_sharing_stress.expected"

MODE="${1:-quick}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-45}"

echo "=== Term Sharing Benchmark Suite ==="
echo "Mode: $MODE"
echo ""

run_stress_test() {
    local scratch output errors timing normalized status
    scratch=$(mktemp -d "$ROOT/runtime/term-sharing-stress.XXXXXX")
    output="$scratch/stdout"
    errors="$scratch/stderr"
    timing="$scratch/time"
    normalized="$scratch/normalized"
    status=0

    echo "--- Stress Test (targeted sharing invariants) ---"
    /usr/bin/time -q -f 'rss=%MKB time=%E' -o "$timing" \
        timeout "$TIMEOUT_SECONDS" "$CETTA_BIN" \
        --profile extended --lang he \
        "$ROOT/benchmarks/bench_term_sharing_stress.metta" \
        >"$output" 2>"$errors" || status=$?

    grep -E '^(===|PASS:|\(term-sharing-space-len|\(hashcons|\(term-universe)' \
        "$output" || true
    cat "$timing"

    grep -E '^(===|PASS:|\(term-sharing-space-len)' \
        "$output" >"$normalized" || true

    if [[ $status -ne 0 ]]; then
        echo "FAIL: evaluator exited with status $status" >&2
    elif grep -Fq '(Error' "$output" || grep -Fq '(Error' "$errors"; then
        echo "FAIL: evaluator emitted an Error payload" >&2
        status=1
    elif ! cmp -s "$EXPECTED" "$normalized"; then
        echo "FAIL: stable term-sharing oracle changed" >&2
        diff -u "$EXPECTED" "$normalized" >&2 || true
        status=1
    fi

    if [[ $status -ne 0 ]]; then
        if grep -Eq '^\(hashcons-(hit|attempt) 0\)$' "$output"; then
            echo "hint: use a binary built with ENABLE_RUNTIME_STATS=1" >&2
        fi
        echo "STATUS: fail"
        rm -rf "$scratch"
        return 1
    fi

    echo "STATUS: pass"
    rm -rf "$scratch"
    echo ""
}

run_blowup_test() {
    echo "--- Blowup Test (exact-sharing alone may still time out) ---"
    local status=0
    local output
    output=$(/usr/bin/time -f 'rss=%MKB time=%E' \
        bash -c "timeout $TIMEOUT_SECONDS '$CETTA_BIN' '$ROOT/benchmarks/bench_term_sharing_blowup.metta'" 2>&1) || status=$?

    if echo "$output" | grep -Fq '(Error'; then
        echo "STATUS: error payload"
        status=1
    elif [[ $status -eq 0 ]] && echo "$output" | grep -q 'SUCCESS: Blowup avoided'; then
        echo "STATUS: pass"
        echo "$output" | grep -E '(Proof count|rss=)'
    elif echo "$output" | grep -q 'out of memory'; then
        echo "STATUS: oom"
        echo "$output" | grep -E '(rss=|time=)'
        status=1
    elif [[ $status -eq 124 ]]; then
        echo "STATUS: timeout"
        status=1
    else
        echo "STATUS: error (exit=$status)"
        status=1
    fi
    echo ""
    return "$status"
}

case "$MODE" in
    quick)
        run_stress_test
        ;;
    blowup)
        run_blowup_test
        ;;
    all)
        run_stress_test
        run_blowup_test
        ;;
    *)
        echo "Unknown mode: $MODE"
        echo "Usage: $0 [quick|blowup|all]"
        exit 1
        ;;
esac

echo "=== Done ==="
