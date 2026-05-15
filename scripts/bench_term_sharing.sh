#!/usr/bin/env bash
# =============================================================================
# Term Sharing Benchmark Suite
# =============================================================================
#
# Runs the targeted term sharing tests and reports key metrics.
#
# Usage:
#   ./scripts/bench_term_sharing.sh [quick|blowup|all]
#
# Modes:
#   quick  - Run stress test only (always completes)
#   blowup - Run blowup test at depth 4
#   all    - Run both
#
# =============================================================================

set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CETTA_BIN="$ROOT/cetta"

MODE="${1:-quick}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-45}"

echo "=== Term Sharing Benchmark Suite ==="
echo "Mode: $MODE"
echo ""

run_stress_test() {
    echo "--- Stress Test (targeted sharing invariants) ---"
    if /usr/bin/time -f 'rss=%MKB time=%E' \
        bash -c "timeout $TIMEOUT_SECONDS '$CETTA_BIN' --emit-runtime-stats '$ROOT/benchmarks/bench_term_sharing_stress.metta'" 2>&1 | \
        grep -E '(hashcons|term-universe|space-len|Test|===|rss=)'; then
        echo "STATUS: pass"
    else
        echo "STATUS: fail"
    fi
    echo ""
}

run_blowup_test() {
    echo "--- Blowup Test (exact-sharing alone may still time out) ---"
    local status=0
    local output
    output=$(/usr/bin/time -f 'rss=%MKB time=%E' \
        bash -c "timeout $TIMEOUT_SECONDS '$CETTA_BIN' '$ROOT/benchmarks/bench_term_sharing_blowup.metta'" 2>&1) || status=$?

    if echo "$output" | grep -q 'SUCCESS: Blowup avoided'; then
        echo "STATUS: pass"
        echo "$output" | grep -E '(Proof count|rss=)'
    elif echo "$output" | grep -q 'out of memory'; then
        echo "STATUS: oom"
        echo "$output" | grep -E '(rss=|time=)'
    elif [[ $status -eq 124 ]]; then
        echo "STATUS: timeout"
    else
        echo "STATUS: error (exit=$status)"
    fi
    echo ""
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
