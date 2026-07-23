#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=${CETTA_BIN:-"$ROOT/runtime/cetta-main-runtime-stats"}

if [[ ! -x "$BIN" ]]; then
    echo "FAIL: runtime-stats CeTTa binary is unavailable" >&2
    exit 1
fi

output=$(
    "$BIN" --emit-runtime-stats --lang prime \
        "$ROOT/tests/prime/need_closure_capture.metta" 2>&1
)

program_output=$(grep -v '^runtime-counter ' <<<"$output")
expected=$(cat "$ROOT/tests/prime/need_closure_capture.expected")
if [[ "$program_output" != "$expected" ]]; then
    echo "FAIL: exact closure-capture result drifted under instrumentation" >&2
    diff <(printf '%s\n' "$expected") \
         <(printf '%s\n' "$program_output") | head -40
    exit 1
fi

counter() {
    local name=$1
    local value
    value=$(sed -n "s/^runtime-counter ${name} //p" <<<"$output")
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: missing runtime counter ${name}" >&2
        exit 1
    fi
    printf '%s\n' "$value"
}

queries=$(counter prime-need-capture-projection-query)
exact=$(counter prime-need-capture-projection-exact)
fallback=$(counter prime-need-capture-projection-fallback)
scanned=$(counter prime-need-capture-scan-atom)
cells=$(counter prime-need-capture-referenced-cell)
ids=$(counter prime-need-capture-referenced-id)

if ((queries == 0 || exact != queries || fallback != 0)); then
    echo "FAIL: evaluator-created closure summaries did not remain exact" >&2
    exit 1
fi
if ((scanned == 0 || cells == 0 || ids == 0)); then
    echo "FAIL: transitive closure-capture path was not exercised" >&2
    exit 1
fi

printf '%s\n' \
    "PrimeNeedCaptureStatsSummary PASS queries=${queries} exact=${exact} fallback=${fallback} scanned=${scanned} cells=${cells} ids=${ids}"
