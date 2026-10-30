#!/usr/bin/env bash
# Success means the two documented counterexamples still reproduce.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BIN=$(realpath "${1:?usage: check_boundaries.sh CETTA [OUTPUT]}")
OUTPUT=${2:-$HERE/../../runtime/ic-pln-boundaries}
mkdir -p "$OUTPUT"
OUT=$(mktemp -d "$(realpath "$OUTPUT")/run.XXXXXX")
quote() {
    local value=$1
    value=${value//\\/\\\\}; value=${value//\"/\\\"}
    value=${value//$'\n'/\\n}; value=${value//$'\r'/\\r}; value=${value//$'\t'/\\t}
    printf '"%s"' "$value"
}
for mode in ambient reflection; do
    mkdir "$OUT/$mode"
    {
        printf '!(import! &self %s)\n' "$(quote "$HERE/boundaries.metta")"
        [[ $mode != ambient ]] || printf '(: audit-f (-> Number Atom))\n'
        printf '!(boundary:probe "%s" %s)\n' "$mode" "$(quote "$OUT/$mode")"
    } > "$OUT/$mode/probe.metta"
    "$BIN" --lang petta "$OUT/$mode/probe.metta" > "$OUT/$mode/probe.stdout" 2> "$OUT/$mode/probe.stderr"
    test ! -s "$OUT/$mode/probe.stderr"
    costs='75) (IC:Some 49)'
    [[ $mode != reflection ]] || costs='91) (IC:Some 65)'
    grep -Fq "(Boundary:Candidate 3 true (IC:Some $costs Accepted)" "$OUT/$mode/probe.stdout"
    for variant in original rewritten; do
        {
            [[ $mode != ambient ]] || printf '(: audit-f (-> Number Atom))\n'
            cat "$OUT/$mode/$variant.metta"
            if [[ $mode == ambient ]]; then printf '\n!(audit-f 3)\n'
            else printf '\n!(audit-inspect)\n'; fi
        } > "$OUT/$mode/$variant-run.metta"
        "$BIN" --lang petta "$OUT/$mode/$variant-run.metta" > "$OUT/$mode/$variant.stdout" 2> "$OUT/$mode/$variant.stderr"
        test ! -s "$OUT/$mode/$variant.stderr"
    done
    if cmp -s "$OUT/$mode/original.stdout" "$OUT/$mode/rewritten.stdout"; then
        printf 'FAIL: expected %s observation boundary did not reproduce\n' "$mode" >&2
        exit 1
    fi
done
grep -Fxq '(if (< 3 0) (- 0 3) (+ (* 3 3) (* 3 3)))' "$OUT/ambient/original.stdout"
grep -Fxq '(ic-learned:ambient 3)' "$OUT/ambient/rewritten.stdout"
grep -Fxq '4' "$OUT/reflection/original.stdout"
grep -Fxq '2' "$OUT/reflection/rewritten.stdout"
printf 'CONFIRMED LIMITS: ambient Atom result types and source reflection distinguish the rewrite\n'
printf 'Evidence: %s\n' "$OUT"
