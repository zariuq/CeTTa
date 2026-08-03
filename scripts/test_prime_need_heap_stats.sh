#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=${CETTA_BIN:-"$ROOT/runtime/cetta-main-runtime-stats"}

if [[ ! -x "$BIN" ]]; then
    echo "FAIL: runtime-stats CeTTa binary is unavailable" >&2
    echo "hint: build runtime/cetta-main-runtime-stats first" >&2
    exit 1
fi

scratch=$(mktemp -d "$ROOT/runtime/prime-need-heap-stats.XXXXXX")
trap 'rm -rf "$scratch"' EXIT

definition='(= (need-index-depth $n)
                (if (<= $n 0)
                    done
                    (need-index-depth (- $n 1))))'
query='!(need-index-depth 4000)'

run_probe() {
    local mode=$1
    local stdout_file=$2
    local stderr_file=$3
    if [[ "$mode" == "indexed" ]]; then
        CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
            "$BIN" --emit-runtime-stats --lang prime \
                -e "$definition" -e "$query" \
                >"$stdout_file" 2>"$stderr_file"
    else
        CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
            CETTA_PRIME_NEED_HEAP_INDEX=0 \
            "$BIN" --emit-runtime-stats --lang prime \
                -e "$definition" -e "$query" \
                >"$stdout_file" 2>"$stderr_file"
    fi
}

counter() {
    local file=$1
    local name=$2
    local value
    value=$(sed -n "s/^runtime-counter ${name} //p" "$file")
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: missing runtime counter ${name}" >&2
        exit 1
    fi
    printf '%s\n' "$value"
}

run_probe indexed "$scratch/indexed.stdout" "$scratch/indexed.stderr"
run_probe oracle "$scratch/oracle.stdout" "$scratch/oracle.stderr"

if [[ "$(cat "$scratch/indexed.stdout")" != '[done]' ]] ||
   [[ "$(cat "$scratch/oracle.stdout")" != '[done]' ]] ||
   ! cmp -s "$scratch/indexed.stdout" "$scratch/oracle.stdout"; then
    echo "FAIL: indexed and oracle Need heaps disagree on the exact answer" >&2
    diff -u "$scratch/oracle.stdout" "$scratch/indexed.stdout" >&2 || true
    exit 1
fi

indexed_queries=$(counter "$scratch/indexed.stderr" \
    prime-need-heap-lookup-query)
indexed_hits=$(counter "$scratch/indexed.stderr" \
    prime-need-heap-lookup-index-hit)
indexed_misses=$(counter "$scratch/indexed.stderr" \
    prime-need-heap-lookup-index-miss)
indexed_steps=$(counter "$scratch/indexed.stderr" \
    prime-need-heap-lookup-index-step)
indexed_fallbacks=$(counter "$scratch/indexed.stderr" \
    prime-need-heap-lookup-log-fallback)
indexed_frames=$(counter "$scratch/indexed.stderr" \
    prime-need-heap-lookup-log-frame)

oracle_queries=$(counter "$scratch/oracle.stderr" \
    prime-need-heap-lookup-query)
oracle_hits=$(counter "$scratch/oracle.stderr" \
    prime-need-heap-lookup-index-hit)
oracle_misses=$(counter "$scratch/oracle.stderr" \
    prime-need-heap-lookup-index-miss)
oracle_steps=$(counter "$scratch/oracle.stderr" \
    prime-need-heap-lookup-index-step)
oracle_fallbacks=$(counter "$scratch/oracle.stderr" \
    prime-need-heap-lookup-log-fallback)
oracle_frames=$(counter "$scratch/oracle.stderr" \
    prime-need-heap-lookup-log-frame)

if ((indexed_queries == 0 ||
     indexed_queries != oracle_queries ||
     indexed_hits != indexed_queries ||
     indexed_misses != 0 ||
     indexed_fallbacks != 0 ||
     indexed_frames != 0)); then
    echo "FAIL: indexed Need-heap lookups did not exactly replace log scans" >&2
    exit 1
fi

# The immutable radix index consumes sixteen key nibbles plus its leaf.
if ((indexed_steps < indexed_queries ||
     indexed_steps > 17 * indexed_queries)); then
    echo "FAIL: indexed Need-heap work exceeded the fixed radix-depth bound" >&2
    exit 1
fi

if ((oracle_hits != 0 ||
     oracle_misses != 0 ||
     oracle_steps != 0 ||
     oracle_fallbacks != oracle_queries ||
     oracle_frames <= 100 * oracle_queries)); then
    echo "FAIL: oracle leg did not expose the history-length scan" >&2
    exit 1
fi

printf '%s\n' \
    "PrimeNeedHeapIndexStatsSummary PASS queries=${indexed_queries} indexed_steps=${indexed_steps} oracle_frames=${oracle_frames}"
