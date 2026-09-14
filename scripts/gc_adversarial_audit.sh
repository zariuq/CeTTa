#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CETTA_BIN:-$ROOT/cetta}"
TESTS=("$ROOT"/tests/gc/test_*.metta)

if [ ! -x "$BIN" ]; then
    echo "error: missing executable $BIN" >&2
    exit 2
fi

normalize() {
    sed -E 's/0x[0-9a-fA-F]+/0xADDR/g'
}

run_case() {
    local label="$1"
    shift
    timeout "${CETTA_GC_AUDIT_TIMEOUT:-120}" "$@" 2>&1 | normalize
}

for test_path in "${TESTS[@]}"; do
    expected_path="${test_path%.metta}.expected"
    test_name="${test_path#$ROOT/}"
    if [ ! -f "$expected_path" ]; then
        echo "FAIL: $test_name missing ${expected_path#$ROOT/}" >&2
        exit 1
    fi

    expected="$(normalize < "$expected_path")"
    baseline="$(run_case gc-on-budget-64 env CETTA_GC=1 CETTA_GC_BUDGET_MB=64 "$BIN" --profile extended --lang he "$test_path")"
    if [ "$baseline" != "$expected" ]; then
        echo "FAIL: $test_name differs from expected output" >&2
        diff <(printf '%s\n' "$expected") <(printf '%s\n' "$baseline") | head -80 >&2
        exit 1
    fi

    for budget in 1 2 8 64; do
        current="$(run_case "gc-on-budget-$budget" env CETTA_GC=1 CETTA_GC_BUDGET_MB="$budget" "$BIN" --profile extended --lang he "$test_path")"
        if [ "$current" != "$baseline" ]; then
            echo "FAIL: $test_name output depends on CETTA_GC_BUDGET_MB=$budget" >&2
            diff <(printf '%s\n' "$baseline") <(printf '%s\n' "$current") | head -80 >&2
            exit 1
        fi
    done
    echo "PASS: $test_name budget-invariance"

    hashcons="$(run_case gc-on-eval-hashcons env CETTA_GC=1 CETTA_GC_BUDGET_MB=1 "$BIN" --eval-hashcons --profile extended --lang he "$test_path")"
    if [ "$hashcons" != "$baseline" ]; then
        echo "FAIL: --eval-hashcons changed $test_name output" >&2
        diff <(printf '%s\n' "$baseline") <(printf '%s\n' "$hashcons") | head -80 >&2
        exit 1
    fi
    echo "PASS: $test_name eval-hashcons guard"
done

echo "PASS: GC adversarial audit"
