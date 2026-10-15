#!/usr/bin/env bash
set -euo pipefail

# 2026-07-03 bug: appending 7 unused equations to lib/system.metta made
# tests/test_gparse_native_dispatch.metta consume 24 GiB (unmemoized
# DAG-to-tree copies in the equation-query path). This test reproduces that
# setup and asserts exit 0, the 23 expected output lines, and peak RSS under
# 1 GB.
#
# The padded library is private to this run.  The runtime takes its library
# root from the real path of its executable, so the run uses a hard link of
# the binary inside a private root whose lib/ holds a padded copy of
# system.metta and links to every other library entry.  The shared
# lib/system.metta is never written, so concurrent runs cannot disturb it.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$script_dir/.." && pwd)"
bin="${1:-${CETTA_BIN:-$root/cetta}}"
bin="$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin")"
system_file="$root/lib/system.metta"
expected="$root/tests/test_gparse_native_dispatch.expected"
workload="$root/tests/test_gparse_native_dispatch.metta"
scratch_dir="$(mktemp -d "$root/runtime/test_stdlib_growth_memory_regression.XXXXXX")"
private_root="$scratch_dir/root"
private_bin="$private_root/cetta"
probe="$scratch_dir/padding_probe.metta"
stdout_raw="$scratch_dir/stdout.raw"
stdout_filtered="$scratch_dir/stdout.filtered"
stderr_file="$scratch_dir/stderr"
time_file="$scratch_dir/time"
shared_before="$(sha256sum "$system_file" | cut -d' ' -f1)"

cleanup() {
  status=$?
  if [ -d "$private_root/lib" ]; then
    find "$private_root/lib" -mindepth 1 -maxdepth 1 \( -type l -o -type f \) -delete
    rmdir "$private_root/lib" 2>/dev/null || true
  fi
  rm -f "$private_bin" "$probe" "$stdout_raw" "$stdout_filtered" \
    "$stderr_file" "$time_file"
  rmdir "$private_root" "$scratch_dir" 2>/dev/null || true
  if [ "$status" -eq 0 ] &&
     [ "$(sha256sum "$system_file" | cut -d' ' -f1)" != "$shared_before" ]; then
    echo "FAIL: the shared lib/system.metta changed during the run" >&2
    exit 1
  fi
  exit "$status"
}
trap cleanup EXIT INT TERM

mkdir -p "$private_root/lib"
for entry in "$root"/lib/* "$root"/lib/.[!.]*; do
  [ -e "$entry" ] || continue
  name="$(basename "$entry")"
  [ "$name" = system.metta ] && continue
  ln -s "$entry" "$private_root/lib/$name"
done
cp "$system_file" "$private_root/lib/system.metta"
ln "$bin" "$private_bin" 2>/dev/null || cp "$bin" "$private_bin"

cat >> "$private_root/lib/system.metta" <<'PAD'

; stdlib growth memory regression padding: intentionally unused equations.
(= (system:stdlib-growth-memory-regression-pad-0) ())
(= (system:stdlib-growth-memory-regression-pad-1) ())
(= (system:stdlib-growth-memory-regression-pad-2) ())
(= (system:stdlib-growth-memory-regression-pad-3) ())
(= (system:stdlib-growth-memory-regression-pad-4) ())
(= (system:stdlib-growth-memory-regression-pad-5) ())
(= (system:stdlib-growth-memory-regression-pad-6) ())
PAD

# Positive control: the run must load the padded private copy.
printf '%s\n' '!(import! &self system)' \
  '!(system:stdlib-growth-memory-regression-pad-6)' > "$probe"
probe_out="$("$private_bin" --profile extended --lang he "$probe" 2>&1)"
if [ "$(printf '%s\n' "$probe_out" | tail -1)" != "[()]" ]; then
  echo "FAIL: the padded private system library was not the one loaded" >&2
  printf '%s\n' "$probe_out" >&2
  exit 1
fi

if ! (
  /usr/bin/time -f '%M' -o "$time_file" "$private_bin" --profile extended --lang he "$workload" >"$stdout_raw" 2>"$stderr_file"
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
