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
    value=$(
        sed -n "s/^runtime-counter ${name} //p" <<<"$output"
    )
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: missing runtime counter ${name}" >&2
        exit 1
    fi
    printf '%s\n' "$value"
}

if ! grep -qxF '[50]' <<<"$output"; then
    echo "FAIL: Prime carrier statistics probe returned the wrong answer" >&2
    exit 1
fi

queries=$(counter prime-need-carrier-reach-query)
empty=$(counter prime-need-carrier-reach-empty-target-accept)
boundary=$(counter prime-need-carrier-reach-boundary-reject)
self=$(counter prime-need-carrier-reach-self-accept)
depth=$(counter prime-need-carrier-reach-depth-reject)
parent=$(counter prime-need-carrier-reach-parent-accept)
index=$(counter prime-need-carrier-reach-index-accept)
index_steps=$(counter prime-need-carrier-reach-index-step)
fallback=$(counter prime-need-carrier-reach-fallback)
frames=$(counter prime-need-carrier-reach-fallback-frame)
accounted=$((empty + boundary + self + depth + parent + index + fallback))

if ((queries != accounted)); then
    echo "FAIL: carrier reachability outcomes do not partition queries" >&2
    exit 1
fi
if ((index > 0)); then
    if ((index_steps < index || fallback != 0 || frames != 0)); then
        echo "FAIL: indexed probe did not replace the recursive fallback" >&2
        exit 1
    fi
elif ((fallback == 0 || frames <= fallback)); then
    echo "FAIL: baseline probe did not exercise multi-frame exact fallback" >&2
    exit 1
fi

printf '%s\n' \
    "PrimeCarrierReachStatsSummary PASS queries=${queries} index=${index} index_steps=${index_steps} fallback=${fallback} frames=${frames}"
