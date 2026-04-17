#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-./cetta}"

run_case() {
    local title="$1"
    shift
    local output
    output=$("$BIN" "$@" 2>&1 >/dev/null)
    printf '=== %s ===\n' "$title"
    printf '%s\n' "$output" | grep '^runtime-counter ' || true
    printf '\n'
}

run_case "tilepuzzle-500" \
    --emit-runtime-stats --quiet --lang he \
    tests/support/runtime_witness_tilepuzzle_500.metta

run_case "conjunction12" \
    --emit-runtime-stats --quiet --lang he --count-only \
    tests/bench_conjunction12_he.metta

run_case "bio_1M" \
    --emit-runtime-stats --quiet --lang he --count-only \
    tests/bench_bio_1M.metta
