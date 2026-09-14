#!/usr/bin/env bash
set -euo pipefail

width="${1:-20000}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$script_dir/.." && pwd)"
cetta="${CETTA_BIN:-$root/cetta}"
input="$root/runtime/test_wide_typed_call_stack_${width}.$$.metta"
expected="$root/runtime/test_wide_typed_call_stack_${width}.$$.expected"
he_stdout="$root/runtime/test_wide_typed_call_stack_${width}.$$.he.out"
he_stderr="$root/runtime/test_wide_typed_call_stack_${width}.$$.he.err"
prime_stdout="$root/runtime/test_wide_typed_call_stack_${width}.$$.prime.out"
prime_stderr="$root/runtime/test_wide_typed_call_stack_${width}.$$.prime.err"
mkdir -p "$root/runtime"

cleanup() {
  rm -f "$input" "$expected" \
    "$he_stdout" "$he_stderr" "$prime_stdout" "$prime_stderr"
}
trap cleanup EXIT

{
  printf '(: mam:wide (->'
  i=0
  while [ "$i" -le "$width" ]; do
    printf ' Atom'
    i=$((i + 1))
  done
  printf '))\n'
  printf '!(mam:wide'
  i=0
  while [ "$i" -lt "$width" ]; do
    printf ' a'
    i=$((i + 1))
  done
  printf ')\n'
  printf '(: mam:branch (-> Number Number Atom))\n'
  printf '!(mam:branch (superpose (1 2)) (superpose (3 4)))\n'
  printf '!(mam:branch (superpose (1 1)) (superpose (3 3)))\n'
  printf '!(let $x (superpose (1 2)) (mam:branch $x $x))\n'
  printf '!(mam:branch "bad" 3)\n'
} >"$input"

{
  printf '[(mam:wide'
  i=0
  while [ "$i" -lt "$width" ]; do
    printf ' a'
    i=$((i + 1))
  done
  printf ')]\n'
  printf '[(mam:branch 1 3), (mam:branch 1 4), (mam:branch 2 3), (mam:branch 2 4)]\n'
  printf '[(mam:branch 1 3), (mam:branch 1 3), (mam:branch 1 3), (mam:branch 1 3)]\n'
  printf '[(mam:branch 1 1), (mam:branch 2 2)]\n'
  printf '[(Error (mam:branch "bad" 3) (BadArgType 1 Number String))]\n'
} >"$expected"

run_case() {
  label="$1"
  stdout="$2"
  stderr="$3"
  shift 3
  if ! "$cetta" --quiet "$@" "$input" >"$stdout" 2>"$stderr"; then
    echo "FAIL: $label wide typed call exited non-zero" >&2
    cat "$stderr" >&2
    exit 1
  fi
  if grep -Eq 'Stack overflow|StackOverflow' "$stdout" "$stderr"; then
    echo "FAIL: $label wide typed call reported stack overflow" >&2
    cat "$stderr" >&2
    exit 1
  fi
  if ! cmp -s "$expected" "$stdout"; then
    echo "FAIL: $label wide typed call changed or truncated the result" >&2
    echo "expected bytes: $(wc -c <"$expected")" >&2
    echo "actual bytes:   $(wc -c <"$stdout")" >&2
    echo "actual prefix:" >&2
    head -c 256 "$stdout" >&2
    printf '\n' >&2
    cat "$stderr" >&2
    exit 1
  fi
}

run_case HE "$he_stdout" "$he_stderr" --lang he --profile extended
run_case Prime "$prime_stdout" "$prime_stderr" --lang prime

echo "PASS: wide typed-call stack regression width=$width (HE and Prime)"
