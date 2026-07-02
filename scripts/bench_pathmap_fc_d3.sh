#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ ! -x ./cetta ]]; then
  echo "cetta binary not found; build it first" >&2
  exit 1
fi

make -s BUILD=mork cetta

run_case() {
  local label="$1"
  local file="$2"
  local space_engine="$3"
  local tmp
  tmp="$(mktemp)"
  echo "== $label =="
  /usr/bin/time -f "wall=%E rss_kb=%M" \
    ./cetta --quiet --profile he-extended --space-engine "$space_engine" --lang he "$file" \
    >"$tmp" 2>&1
  cat "$tmp"
  if grep -Fq "(Error" "$tmp"; then
    echo "FAIL: $label emitted an Error payload" >&2
    rm -f "$tmp"
    exit 1
  fi
  if ! grep -Eq '^(\[3421\]|3421)$' "$tmp"; then
    echo "FAIL: $label did not report the 3421 depth-3 FC count" >&2
    rm -f "$tmp"
    exit 1
  fi
  rm -f "$tmp"
  echo
}

run_case "native-fc-d3" "tests/bench_outcome_variant_fc_d3.metta" "native"
run_case "pathmap-fc-d3" "tests/bench_pathmap_fc_d3.metta" "pathmap"
