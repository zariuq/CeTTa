#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=${CETTA_BIN:-"$ROOT/runtime/cetta-main-runtime-stats"}
SOURCE="$ROOT/tests/prime/need_equation_choice_sharing.metta"
EXPECTED="$ROOT/tests/prime/need_equation_choice_sharing.expected"

if [[ ! -x "$BIN" ]]; then
    echo "FAIL: runtime-stats CeTTa binary is unavailable" >&2
    exit 1
fi

actual=$(
    "$BIN" --lang prime "$SOURCE" 2>&1
)
if [[ "$actual" != "$(<"$EXPECTED")" ]]; then
    echo "FAIL: one-pass planner changed Prime equation-choice semantics" >&2
    diff <(cat "$EXPECTED") <(printf '%s\n' "$actual") | head -40 >&2
    exit 1
fi

stats=$(
    "$BIN" --emit-runtime-stats --quiet --lang prime "$SOURCE" \
        2>&1 >/dev/null
)

counter() {
    local name=$1
    local value
    value=$(sed -n "s/^runtime-counter ${name} //p" <<<"$stats")
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: missing runtime counter ${name}" >&2
        exit 1
    fi
    printf '%s' "$value"
}

calls=$(counter prime-need-plan-call)
scanned=$(counter prime-need-plan-candidate-scan)
analyzed=$(counter prime-need-plan-demand-analysis)
admitted=$(counter prime-need-plan-admission)

if (( calls != 2 || scanned != 4 || analyzed != 4 || admitted != 4 )); then
    echo "FAIL: Prime planner did not analyze the four admitted occurrences exactly once" >&2
    printf 'calls=%s scanned=%s analyzed=%s admitted=%s\n' \
        "$calls" "$scanned" "$analyzed" "$admitted" >&2
    exit 1
fi

printf '(PrimeNeedPlannerStatsSummary PASS calls=%s scanned=%s analyzed=%s admitted=%s)\n' \
    "$calls" "$scanned" "$analyzed" "$admitted"
