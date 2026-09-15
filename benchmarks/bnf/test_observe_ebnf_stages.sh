#!/usr/bin/env bash
set -euo pipefail
if (( $# != 1 )); then
    echo 'usage: test_observe_ebnf_stages.sh NEW_EVIDENCE_DIRECTORY' >&2
    exit 2
fi
mkdir -- "$1"
evidence=$(realpath "$1")
cd -- "$(dirname -- "$0")/../.."
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    benchmarks/bnf/observe_ebnf_stages.c -o "$evidence/observe"
emit() {
    local mode=$1 stage
    printf '%s\n' ordinary-output
    for stage in read declarations lower validate denote load parse project; do
        if [[ "$mode" != missing-start || "$stage" != read ]]; then
            printf '(EbnfStage begin %s)\n' "$stage"
        fi
        if [[ "$mode" == truncated && "$stage" == lower ]]; then return; fi
        if [[ "$mode" == wrong-order && "$stage" == declarations ]]; then
            printf '%s\n' '(EbnfStage end lower)'
        else
            printf '(EbnfStage end %s)\n' "$stage"
        fi
        if [[ "$mode" == duplicate && "$stage" == parse ]]; then
            printf '%s\n' '(EbnfStage end parse)'
        fi
    done
    if [[ "$mode" == extra ]]; then printf '%s\n' '(EbnfStage begin extra)'; fi
}
for mode in valid missing-start truncated wrong-order duplicate extra; do
    emit "$mode" > "$evidence/$mode.input"
    code=0
    "$evidence/observe" "$evidence/$mode.tsv" < "$evidence/$mode.input" \
        > "$evidence/$mode.output" || code=$?
    cmp "$evidence/$mode.input" "$evidence/$mode.output"
    if [[ "$mode" == valid ]]; then
        test "$code" -eq 0
        test "$(awk -F '\t' '$2 == "completed" {n++} END {print n+0}' "$evidence/$mode.tsv")" -eq 8
    else
        test "$code" -ne 0
    fi
done
grep -Fx $'lower\tincomplete\tNA' "$evidence/truncated.tsv"
sha256sum "$evidence/valid.tsv" > "$evidence/protected.sha256"
if "$evidence/observe" "$evidence/valid.tsv" < /dev/null > "$evidence/reuse.out" 2> "$evidence/reuse.stderr"; then
    exit 1
fi
sha256sum -c "$evidence/protected.sha256"
if "$evidence/observe" "$evidence/empty.tsv" < /dev/null > "$evidence/empty.out"; then
    exit 1
fi
printf '(EbnfStageObserverSummary 8 0)\n'
