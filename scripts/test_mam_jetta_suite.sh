#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH="$ROOT/scripts/bench_mam_jetta_suite.sh"
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"

MAM_JETTA_SCALE=smoke \
MAM_JETTA_LANGUAGES=he \
MAM_JETTA_REQUIRE_PASS=1 \
CETTA_BIN="$CETTA_BIN" \
  "$BENCH"

MAM_JETTA_SCALE=smoke \
MAM_JETTA_LANGUAGES=prime \
MAM_JETTA_CASES='fibonacci interpreter differentiation backward_chaining' \
MAM_JETTA_REQUIRE_PASS=1 \
CETTA_BIN="$CETTA_BIN" \
  "$BENCH"

set +e
prime_synthesis="$(
  MAM_JETTA_SCALE=smoke \
  MAM_JETTA_LANGUAGES=prime \
  MAM_JETTA_CASES=synthesis \
  MAM_JETTA_REQUIRE_PASS=1 \
  CETTA_BIN="$CETTA_BIN" \
    "$BENCH" 2>&1
)"
prime_synthesis_rc=$?
set -e

printf '%s\n' "$prime_synthesis"
if [[ "$prime_synthesis_rc" -eq 0 ]]; then
  echo "FAIL: Prime synthesis negative witness unexpectedly passed" >&2
  exit 1
fi
if ! grep -Fq $'prime\tsynthesis\t1\t1\t0' <<<"$prime_synthesis"; then
  echo "FAIL: Prime synthesis did not produce the pinned expected=1 actual=0 witness" >&2
  exit 1
fi
if ! grep -Fq 'MAMJeTTaSuiteSummary FAIL smoke' <<<"$prime_synthesis"; then
  echo "FAIL: Prime synthesis failure summary absent" >&2
  exit 1
fi

echo "MAMJeTTaSmokeContractSummary PASS he=5 prime-positive=4 prime-synthesis-negative=1"
