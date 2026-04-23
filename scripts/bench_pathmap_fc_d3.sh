#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ ! -x ./cetta ]]; then
  echo "cetta binary not found; build it first" >&2
  exit 1
fi

run_case() {
  local label="$1"
  local file="$2"
  echo "== $label =="
  /usr/bin/time -f "wall=%E rss_kb=%M" ./cetta --quiet "$file"
  echo
}

run_case "native-fc-d3" "tests/bench_outcome_variant_fc_d3.metta"
run_case "pathmap-fc-d3" "tests/bench_pathmap_fc_d3.metta"
