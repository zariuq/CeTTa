#!/usr/bin/env bash
# Nesting depth has no bound in the reader: a deeply nested well-formed text
# parses to exactly the term it spells, and the same text with one closing
# parenthesis missing fails cleanly with ParseFailed.  The default depth is
# far past what a reader recursing on the native stack could hold.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="${CETTA_BIN:-$root/cetta}"
depth="${CETTA_PARSE_DEPTH_TEST_DEPTH:-200000}"

mkdir -p "$root/runtime"
input="$root/runtime/test_parse_depth_guard_$$.metta"
stdout="$root/runtime/test_parse_depth_guard_$$.out"
stderr="$root/runtime/test_parse_depth_guard_$$.err"
expected="$root/runtime/test_parse_depth_guard_$$.expected"
trap 'rm -f "$input" "$stdout" "$stderr" "$expected"' EXIT

opens=$(printf '%*s' "$depth" '' | tr ' ' '(')
closes=$(printf '%*s' "$depth" '' | tr ' ' ')')
deep="${opens}A${closes}"
unclosed="${opens}A${closes:1}"

run() {
    local label=$1
    if ! "$bin" --profile extended --lang he "$input" > "$stdout" 2> "$stderr"; then
        cat "$stderr" >&2
        echo "FAIL: $label exited non-zero" >&2
        exit 1
    fi
    if grep -Eiq 'stack overflow|StackOverflow' "$stdout" "$stderr"; then
        cat "$stderr" >&2
        echo "FAIL: $label reached the process stack guard" >&2
        exit 1
    fi
}

for syntax in parse parse-first; do
    printf '!(%s "%s")\n' "$syntax" "$deep" > "$input"
    run "deeply nested $syntax"
    printf '[%s]\n' "$deep" > "$expected"
    if ! cmp -s "$stdout" "$expected"; then
        echo "FAIL: deeply nested $syntax did not answer the term it spells" >&2
        head -c 240 "$stdout" >&2
        echo >&2
        exit 1
    fi

    printf '!(%s "%s")\n' "$syntax" "$unclosed" > "$input"
    run "deeply nested unclosed $syntax"
    if ! grep -Fq 'ParseFailed' "$stdout"; then
        echo "FAIL: deeply nested unclosed $syntax did not fail cleanly" >&2
        head -c 240 "$stdout" >&2
        echo >&2
        exit 1
    fi
done

echo "PASS: parse depth guard (depth $depth parses exactly; unclosed fails cleanly)"
