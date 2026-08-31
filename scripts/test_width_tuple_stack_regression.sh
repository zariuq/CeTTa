#!/usr/bin/env bash
set -euo pipefail

width="${1:-50000}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$script_dir/.." && pwd)"
cetta="${CETTA_BIN:-$root/cetta}"
input="$root/runtime/test_width_tuple_stack_${width}.$$.metta"
stdout="$root/runtime/test_width_tuple_stack_${width}.$$.out"
stderr="$root/runtime/test_width_tuple_stack_${width}.$$.err"
expected="$root/runtime/test_width_tuple_stack_${width}.$$.expected"
mkdir -p "$root/runtime"

cleanup() {
  rm -f "$input" "$stdout" "$stderr" "$expected"
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

{
  printf '[((wide'
  i=0
  while [ "$i" -lt "$width" ]; do
    printf ' a'
    i=$((i + 1))
  done
  printf '))]\n'
} > "$expected"

if ! "$cetta" --quiet --profile extended --lang he "$input" >"$stdout" 2>"$stderr"; then
  echo "FAIL: width-tuple stack regression exited non-zero" >&2
  cat "$stderr" >&2
  exit 1
fi

if grep -Eq 'Stack overflow|StackOverflow' "$stdout" "$stderr"; then
  echo "FAIL: width-tuple stack regression reported stack overflow" >&2
  cat "$stderr" >&2
  exit 1
fi

if ! cmp -s "$expected" "$stdout"; then
  echo "FAIL: width-tuple stack regression changed or truncated the tuple" >&2
  echo "expected bytes: $(wc -c < "$expected")" >&2
  echo "actual bytes:   $(wc -c < "$stdout")" >&2
  echo "actual prefix:" >&2
  head -c 256 "$stdout" >&2
  printf '\n' >&2
  cat "$stderr" >&2
  exit 1
fi

echo "PASS: width-tuple stack regression width=$width"
