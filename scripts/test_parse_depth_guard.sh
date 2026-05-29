#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="${CETTA_BIN:-$root/cetta}"
depth="${CETTA_PARSE_DEPTH_TEST_DEPTH:-5000}"

mkdir -p "$root/runtime"
input="$root/runtime/test_parse_depth_guard_$$.metta"
stdout="$root/runtime/test_parse_depth_guard_$$.out"
stderr="$root/runtime/test_parse_depth_guard_$$.err"
trap 'rm -f "$input" "$stdout" "$stderr"' EXIT

deep=""
for ((i = 0; i < depth; i++)); do
    deep="${deep}("
done
deep="${deep}A"
for ((i = 0; i < depth; i++)); do
    deep="${deep})"
done

for surface in parse parse-first; do
    printf '!(%s "%s")\n' "$surface" "$deep" > "$input"

    if ! "$bin" --profile he-extended --lang he "$input" > "$stdout" 2> "$stderr"; then
        cat "$stderr" >&2
        echo "FAIL: $surface depth guard process exited non-zero" >&2
        exit 1
    fi

    if grep -Eiq 'stack overflow|StackOverflow' "$stdout" "$stderr"; then
        cat "$stderr" >&2
        echo "FAIL: $surface depth guard reached process stack guard" >&2
        exit 1
    fi

    if ! grep -Fq 'ParseFailed' "$stdout"; then
        echo "FAIL: deeply nested $surface did not fail cleanly" >&2
        head -20 "$stdout" | cut -c1-240 >&2
        exit 1
    fi
done

echo "PASS: parse depth guard"
