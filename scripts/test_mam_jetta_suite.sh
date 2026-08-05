#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH="$ROOT/scripts/bench_mam_jetta_suite.sh"
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"

MAM_JETTA_SCALE=smoke \
MAM_JETTA_LANGUAGES='he prime petta' \
MAM_JETTA_HE_PROFILE=he-extended \
MAM_JETTA_REQUIRE_PASS=1 \
CETTA_BIN="$CETTA_BIN" \
  "$BENCH"

echo "MAMJeTTaSmokeContractSummary PASS he=5 prime=5 petta=5"
