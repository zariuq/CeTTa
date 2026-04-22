#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ ! -x ./cetta ]]; then
  echo "cetta binary not found; build it first" >&2
  exit 1
fi

RUNTIME_DIR="$ROOT/runtime/bench_fc_backend_matrix"
mkdir -p "$RUNTIME_DIR"

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
run_dir="$RUNTIME_DIR/$stamp"
mkdir -p "$run_dir"

tsv="$run_dir/results.tsv"
printf "backend\tcase\tcount\twall_seconds\trss_kb\tstatus\tlog\n" > "$tsv"

run_case() {
  local backend="$1"
  local label="$2"
  local file="$3"
  local timeout_s="$4"
  local log="$run_dir/${backend}_${label}.log"
  local wall_file="$run_dir/${backend}_${label}.time"

  local status="ok"
  local count=""
  local rc=0
  set +e
  timeout "$timeout_s" /usr/bin/time -f "%e\t%M" -o "$wall_file" \
      ./cetta --quiet "$file" >"$log" 2>&1
  rc=$?
  set -e
  if [[ $rc -ne 0 ]]; then
    if [[ $rc -eq 124 ]]; then
      status="timeout"
    else
      status="error:$rc"
    fi
  fi

  local wall="NA"
  local rss="NA"
  if [[ -f "$wall_file" ]]; then
    IFS=$'\t' read -r wall rss < "$wall_file" || true
  fi

  if [[ "$status" == "ok" ]]; then
    count="$(tail -n 1 "$log" | tr -d '\r' | sed -n 's/^\[\([0-9][0-9]*\)\]$/\1/p')"
    if [[ ! "$count" =~ ^[0-9]+$ ]]; then
      status="nonnumeric"
    fi
  fi

  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$backend" "$label" "${count:-NA}" "$wall" "$rss" "$status" "$log" >> "$tsv"
}

run_case native  d3_dup   tests/bench_fc_d3_count.metta                 120
run_case pathmap d3_dup   tests/bench_fc_d3_pathmap_count.metta         120
run_case native  d3_nodup tests/bench_fc_d3_nodup_count.metta           120
run_case pathmap d3_nodup tests/bench_fc_d3_pathmap_nodup_count.metta   120

echo "FC backend matrix written to: $tsv"
column -t -s $'\t' "$tsv"
