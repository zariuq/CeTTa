#!/usr/bin/env bash
# The native kernel's verdicts on their fixtures: mg-check on each FILE.mg,
# with the options in FILE.args, must print FILE.out and exit with
# FILE.status.  The expected reports are written from the verdict rules
# (mg_check.c), not recorded: what is verified, what rests on which of the
# document's axioms, and why a theorem has no authority.
set -euo pipefail
tool=${1:?usage: run_verdict_fixtures.sh MG_CHECK}
tool=$(cd "$(dirname "$tool")" && pwd)/$(basename "$tool")
dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/verdict" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/mg-verdict-fixtures.XXXXXX")
cleanup() {
    rm -f -- "$work/out"
    rmdir -- "$work"
}
trap cleanup EXIT
pass=0
fail=0
for input in "$dir"/*.mg; do
    name=$(basename "$input" .mg)
    args=()
    [[ -f "$dir/$name.args" ]] && read -r -a args < "$dir/$name.args"
    status=0
    (cd "$dir" && "$tool" "${args[@]}" "$name.mg") > "$work/out" 2>/dev/null || status=$?
    expected=$(cat "$dir/$name.status")
    if cmp -s "$work/out" "$dir/$name.out" && [[ "$status" == "$expected" ]]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL %s (exit %s, expected %s)\n' "$name" "$status" "$expected"
        diff "$dir/$name.out" "$work/out" | head -8 || true
    fi
done
printf '(MegalodonVerdictFixtures %d passed %d failed)\n' "$pass" "$fail"
((fail == 0))
