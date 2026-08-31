#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CETTA_BIN:-$ROOT/cetta}"
BUDGET_MB="${CETTA_GC_ASAN_BUDGET_MB:-1}"
TIMEOUT_SEC="${CETTA_GC_ASAN_TIMEOUT:-180}"

TESTS=(
    tests/gc/test_eval_gc_adversarial.metta
    tests/gc/test_eval_gc_indirect_state_stream.metta
    tests/gc/test_eval_gc_precise_let_suspension.metta
    tests/gc/diagnostics/test_eval_gc_precise_suspension.metta
    tests/gc/diagnostics/test_eval_gc_survivor_reset.metta
    tests/test_lib_parse_regression.metta
    tests/test_current_env_live_merge_regression.metta
    tests/test_table_delayed_single_tail_reenter_regression.metta
    tests/test_table_delayed_query_replay_regression.metta
    tests/test_state_ops.metta
    tests/test_he_nested_binding_substitution_regression.metta
    tests/test_bio_bc_let_hidden_env_regression.metta
)

if [ ! -x "$BIN" ]; then
    echo "error: missing executable $BIN" >&2
    exit 2
fi

normalize() {
    sed -E 's/0x[0-9a-fA-F]+/0xADDR/g'
}

run_one() {
    local test_path="$1"
    timeout "$TIMEOUT_SEC" \
        env CETTA_GC=1 CETTA_GC_BUDGET_MB="$BUDGET_MB" \
        "$BIN" --profile extended --lang he "$ROOT/$test_path" 2>&1 | normalize
}

pass=0
fail=0

for test_path in "${TESTS[@]}"; do
    expected_path="${test_path%.metta}.expected"
    if [ ! -f "$ROOT/$expected_path" ]; then
        echo "FAIL: $test_path (missing $expected_path)" >&2
        fail=$((fail + 1))
        continue
    fi

    set +e
    result="$(run_one "$test_path")"
    status=$?
    set -e

    if [ "$status" -ne 0 ]; then
        echo "FAIL: $test_path (exit $status)" >&2
        printf '%s\n' "$result" | head -80 >&2
        fail=$((fail + 1))
        continue
    fi

    expected="$(normalize < "$ROOT/$expected_path")"
    if [ "$result" = "$expected" ]; then
        echo "PASS: $test_path"
        pass=$((pass + 1))
    else
        echo "FAIL: $test_path" >&2
        diff <(printf '%s\n' "$expected") <(printf '%s\n' "$result") | head -80 >&2
        fail=$((fail + 1))
    fi
done

PRIME_TESTS=(
    tests/gc/diagnostics/test_eval_gc_precise_suspension.metta
    tests/gc/diagnostics/test_eval_gc_control_suspensions.metta
)

for test_path in "${PRIME_TESTS[@]}"; do
    expected_path="${test_path%.metta}.expected"
    set +e
    result="$(timeout "$TIMEOUT_SEC" \
        env CETTA_GC=1 CETTA_GC_BUDGET_MB="$BUDGET_MB" \
        CETTA_PRIME_NEED_HEAP_INDEX=1 \
        "$BIN" --lang prime "$ROOT/$test_path" 2>&1 | normalize)"
    status=$?
    set -e

    if [ "$status" -ne 0 ]; then
        echo "FAIL: $test_path [prime] (exit $status)" >&2
        printf '%s\n' "$result" | head -80 >&2
        fail=$((fail + 1))
        continue
    fi

    expected="$(normalize < "$ROOT/$expected_path")"
    if [ "$result" = "$expected" ]; then
        echo "PASS: $test_path [prime]"
        pass=$((pass + 1))
    else
        echo "FAIL: $test_path [prime]" >&2
        diff <(printf '%s\n' "$expected") \
             <(printf '%s\n' "$result") | head -80 >&2 || true
        fail=$((fail + 1))
    fi
done

set +e
prime_observation="$(timeout "$TIMEOUT_SEC" \
    env CETTA_GC=1 CETTA_GC_BUDGET_MB="$BUDGET_MB" \
    "$BIN" --lang prime --count-only \
    "$ROOT/tests/prime/prepared_pure_observation_stack.metta" 2>&1 | normalize)"
prime_status=$?
set -e
if [ "$prime_status" -eq 0 ] && [ "$prime_observation" = 1 ]; then
    echo "PASS: tests/prime/prepared_pure_observation_stack.metta"
    pass=$((pass + 1))
else
    echo "FAIL: tests/prime/prepared_pure_observation_stack.metta" >&2
    printf '%s\n' "$prime_observation" | head -80 >&2
    fail=$((fail + 1))
fi

prime_update_test="tests/prime/prepared_pure_need_sharing.metta"
prime_update_expected="tests/prime/prepared_pure_need_sharing.expected"
set +e
prime_update_result="$(timeout "$TIMEOUT_SEC" \
    env CETTA_GC=1 CETTA_GC_BUDGET_MB="$BUDGET_MB" \
    "$BIN" --lang prime "$ROOT/$prime_update_test" 2>&1 | normalize)"
prime_update_status=$?
set -e
if [ "$prime_update_status" -eq 0 ] &&
   [ "$prime_update_result" = "$(normalize < "$ROOT/$prime_update_expected")" ]; then
    echo "PASS: $prime_update_test"
    pass=$((pass + 1))
else
    echo "FAIL: $prime_update_test (exit $prime_update_status)" >&2
    diff <(normalize < "$ROOT/$prime_update_expected") \
         <(printf '%s\n' "$prime_update_result") | head -80 >&2 || true
    fail=$((fail + 1))
fi

echo "---"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
