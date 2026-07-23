#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=${CETTA_BIN:-"$ROOT/runtime/cetta-main-runtime-stats"}

if [[ ! -x "$BIN" ]]; then
    echo "FAIL: runtime-stats CeTTa binary is unavailable" >&2
    echo "hint: build runtime/cetta-main-runtime-stats first" >&2
    exit 1
fi

# MeTTa variables must remain literal for CeTTa.
# shellcheck disable=SC2016
output=$(
    "$BIN" --emit-runtime-stats --lang prime \
        -e '(= (mam:sum $n) (if (< $n 1) 0 (+ 1 (mam:sum (- $n 1)))))' \
        -e '!(mam:sum 50)' 2>&1
)

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

if ! grep -qxF '[50]' <<<"$output"; then
    echo "FAIL: Prime Need-heap statistics probe returned the wrong answer" >&2
    exit 1
fi

queries=$(counter prime-need-heap-lookup-query)
hits=$(counter prime-need-heap-lookup-index-hit)
misses=$(counter prime-need-heap-lookup-index-miss)
index_steps=$(counter prime-need-heap-lookup-index-step)
fallbacks=$(counter prime-need-heap-lookup-log-fallback)
frames=$(counter prime-need-heap-lookup-log-frame)
accounted=$((hits + misses + fallbacks))

if ((queries == 0 || queries != accounted)); then
    echo "FAIL: Need-heap lookup outcomes do not partition queries" >&2
    exit 1
fi
if ((hits > 0 || misses > 0)); then
    if ((index_steps < hits + misses || fallbacks != 0 || frames != 0)); then
        echo "FAIL: indexed heap lookup did not replace the semantic-log scan" >&2
        exit 1
    fi
elif ((fallbacks != queries || frames <= fallbacks)); then
    echo "FAIL: baseline heap lookup did not exercise multi-frame log scans" >&2
    exit 1
fi

printf '%s\n' \
    "PrimeNeedHeapStatsSummary PASS queries=${queries} hits=${hits} misses=${misses} index_steps=${index_steps} fallbacks=${fallbacks} frames=${frames}"
