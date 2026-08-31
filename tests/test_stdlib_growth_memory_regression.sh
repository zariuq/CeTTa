#!/usr/bin/env bash
set -euo pipefail

# 2026-07-03 bug: appending 7 unused equations to lib/system.metta made
# tests/test_gparse_native_dispatch.metta consume 24 GiB (unmemoized
# DAG-to-tree copies in the equation-query path). This test reproduces that
# setup and asserts exit 0, the 23 expected output lines, and peak RSS under
# 1 GB.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$script_dir/.." && pwd)"
bin="${1:-${CETTA_BIN:-$root/cetta}}"
system_file="$root/lib/system.metta"
expected="$root/tests/test_gparse_native_dispatch.expected"
workload="$root/tests/test_gparse_native_dispatch.metta"
scratch_dir="$root/runtime/test_stdlib_growth_memory_regression_$$"
snapshot="$scratch_dir/system.metta.snapshot"
stdout_raw="$scratch_dir/stdout.raw"
stdout_filtered="$scratch_dir/stdout.filtered"
stderr_file="$scratch_dir/stderr"
time_file="$scratch_dir/time"

restore_status=0
cleanup() {
  status=$?
  if [ -f "$snapshot" ]; then
    if ! cp "$snapshot" "$system_file"; then
      restore_status=1
      echo "FAIL: could not restore lib/system.metta" >&2
    elif ! cmp -s "$snapshot" "$system_file"; then
      restore_status=1
      echo "FAIL: lib/system.metta restore was not byte-identical" >&2
    fi
  fi
  rm -f "$snapshot" "$stdout_raw" "$stdout_filtered" "$stderr_file" "$time_file"
  rmdir "$scratch_dir" 2>/dev/null || true
  if [ "$restore_status" -ne 0 ]; then
    exit "$restore_status"
  fi
  exit "$status"
}
trap cleanup EXIT INT TERM

mkdir -p "$scratch_dir"
cp "$system_file" "$snapshot"

cat >> "$system_file" <<'PAD'

; stdlib growth memory regression padding: intentionally unused equations.
(= (system:stdlib-growth-memory-regression-pad-0) ())
(= (system:stdlib-growth-memory-regression-pad-1) ())
(= (system:stdlib-growth-memory-regression-pad-2) ())
(= (system:stdlib-growth-memory-regression-pad-3) ())
(= (system:stdlib-growth-memory-regression-pad-4) ())
(= (system:stdlib-growth-memory-regression-pad-5) ())
(= (system:stdlib-growth-memory-regression-pad-6) ())
PAD

if ! (
  /usr/bin/time -f '%M' -o "$time_file" "$bin" --profile extended --lang he "$workload" >"$stdout_raw" 2>"$stderr_file"
); then
  echo "FAIL: stdlib growth memory regression exited non-zero" >&2
  cat "$stderr_file" >&2
  exit 1
fi

grep -v '^Failed to create stream fd:' "$stdout_raw" > "$stdout_filtered"

line_count="$(wc -l < "$stdout_filtered" | tr -d ' ')"
if [ "$line_count" != "23" ]; then
  echo "FAIL: expected 23 output lines, got $line_count" >&2
  cat "$stdout_filtered" >&2
  exit 1
fi

if ! cmp -s "$expected" "$stdout_filtered"; then
  echo "FAIL: stdlib growth memory regression output changed" >&2
  diff -u "$expected" "$stdout_filtered" | head -80 >&2
  exit 1
fi

rss_kb="$(tail -1 "$time_file" | tr -d '[:space:]')"
case "$rss_kb" in
  ''|*[!0-9]*)
    echo "FAIL: could not parse peak RSS from /usr/bin/time output" >&2
    cat "$time_file" >&2
    exit 1
    ;;
esac

if [ "$rss_kb" -ge 1048576 ]; then
  echo "FAIL: peak RSS ${rss_kb}KB exceeded 1 GB" >&2
  exit 1
fi

echo "PASS: stdlib growth memory regression rss_kb=$rss_kb"
