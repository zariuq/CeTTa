#!/usr/bin/env bash
# Native document evaluation on its fixtures: each FILE.mg, checked with the
# options in FILE.args (a theory flag; none for Egal), must print FILE.sexpr
# and exit with FILE.status, as `megalodon -sexprinfo` does: its rows, then
# its syntax error or failure line.  Each fixture is self-contained; most
# begin with the same small Egal preamble and end with the item they probe.
set -euo pipefail
tool=${1:?usage: run_elab_fixtures.sh MG_SEXPRINFO}
tool=$(cd "$(dirname "$tool")" && pwd)/$(basename "$tool")
dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/elab" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/mg-elab-fixtures.XXXXXX")
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
    if cmp -s "$work/out" "$dir/$name.sexpr" && [[ "$status" == "$expected" ]]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL %s (exit %s, expected %s)\n' "$name" "$status" "$expected"
        diff <(cut -c1-200 "$dir/$name.sexpr") <(cut -c1-200 "$work/out") | head -5 || true
    fi
done
printf '(MegalodonElabFixtures %d passed %d failed)\n' "$pass" "$fail"
((fail == 0))
