#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CETTA="${CETTA:-$ROOT/cetta}"
EXAMPLES="$ROOT/examples/prime"

passed=0
run_case() {
    local lane="$1"
    local source="$2"
    local fuel="$3"
    local expected="$EXAMPLES/${source%.metta}.${lane}.expected"
    local actual status=0 expected_status=0

    # Extended directives report an incomplete answer stream explicitly.
    # Prime keeps its local residual-judgment observation.
    if [[ "$lane" == extended && "$source" == evaluation_strategy_divergence.metta ]]; then
        expected_status=1
    fi

    if [[ "$lane" == prime ]]; then
        actual="$($CETTA --lang prime --fuel "$fuel" \
            "$EXAMPLES/$source" 2>&1)" || status=$?
    else
        actual="$($CETTA --lang he --profile "$lane" --fuel "$fuel" \
            "$EXAMPLES/$source" 2>&1)" || status=$?
    fi

    if [[ "$status" != "$expected_status" || "$actual" != "$(<"$expected")" ]]; then
        echo "FAIL: $source under $lane (exit $status, expected $expected_status)"
        diff -u "$expected" <(printf '%s\n' "$actual") | head -80
        return 1
    fi
    echo "PASS: $source under $lane"
    passed=$((passed + 1))
}

for lane in he he-compat extended he-prime prime; do
    run_case "$lane" evaluation_strategy_contrast.metta 10000
    run_case "$lane" evaluation_strategy_divergence.metta 64
done

echo "(PrimeEvaluationStrategyContrastSummary $passed 10 $((10 - passed)))"
