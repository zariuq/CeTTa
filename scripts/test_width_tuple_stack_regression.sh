#!/usr/bin/env bash
set -euo pipefail

width="${1:-50000}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$script_dir/.." && pwd)"
input="$root/runtime/test_width_tuple_stack_${width}.$$.metta"
stdout="$root/runtime/test_width_tuple_stack_${width}.$$.out"
stderr="$root/runtime/test_width_tuple_stack_${width}.$$.err"
mkdir -p "$root/runtime"

cleanup() {
  rm -f "$input" "$stdout" "$stderr"
}
trap cleanup EXIT

{
  printf '!(('
  printf 'wide'
  i=0
  while [ "$i" -lt "$width" ]; do
    printf ' a'
    i=$((i + 1))
  done
  printf '))\n'
} > "$input"

if ! "$root/cetta" --quiet --profile he-extended --lang he "$input" >"$stdout" 2>"$stderr"; then
  echo "FAIL: width-tuple stack regression exited non-zero" >&2
  cat "$stderr" >&2
  exit 1
fi

if grep -Eq 'Stack overflow|StackOverflow' "$stdout" "$stderr"; then
  echo "FAIL: width-tuple stack regression reported stack overflow" >&2
  cat "$stderr" >&2
  exit 1
fi

if ! grep -Fq '(wide' "$stdout"; then
  echo "FAIL: width-tuple stack regression produced no expected tuple output" >&2
  cat "$stdout" >&2
  cat "$stderr" >&2
  exit 1
fi

echo "PASS: width-tuple stack regression width=$width"
