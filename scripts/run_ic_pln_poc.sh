#!/usr/bin/env bash
# Native structural-cost PLN example. Each stage starts in a fresh process.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=$(realpath "${1:-$ROOT/cetta}")
SEEDS=${IC_PLN_SEEDS:-64}
if [[ ! $SEEDS =~ ^[1-9][0-9]*$ || ${#SEEDS} -gt 4 || $SEEDS -gt 1000 ]]; then
    printf 'IC_PLN_SEEDS must be an integer from 1 to 1000\n' >&2
    exit 2
fi
OUTPUT=${2:-$ROOT/runtime/ic-pln-poc}
mkdir -p "$OUTPUT"
OUTPUT=$(realpath "$OUTPUT")
RUN=$(mktemp -d "$OUTPUT/run.XXXXXX")
cd "$ROOT"
GLUE=$ROOT/examples/petta/ic_pln_poc.metta
SOURCE=$ROOT/lib/petta/lib_pln.metta
declare -A RESULT DETAIL
current_stage=setup

quote() {
    local value=$1
    value=${value//\\/\\\\}; value=${value//\"/\\\"}
    value=${value//$'\n'/\\n}; value=${value//$'\r'/\\r}; value=${value//$'\t'/\\t}
    printf '"%s"' "$value"
}
run() {
    local name=$1
    current_stage=$name
    "$BIN" --lang petta "$RUN/$name.metta" > "$RUN/$name.stdout" 2> "$RUN/$name.stderr"
    test ! -s "$RUN/$name.stderr"
    if grep -q 'PLN:Fault' "$RUN/$name.stdout"; then
        cat "$RUN/$name.stdout"
        return 1
    fi
}
stage() {
    local name=$1 expression=$2
    printf '!(import! &self %s)\n!%s\n' "$(quote "$GLUE")" "$expression" > "$RUN/$name.metta"
    run "$name"
}
binary() { awk '{for(i=1;i<=NF;i++)printf "%02x",$i}' "$1" | xxd -r -p > "$2"; }
decimal() { od -An -v -tu1 "$1" | tr -s ' \n' ' ' > "$2"; }
field() { awk -v n="$2" '/^\(PLN:Stage / {gsub(/[()]/," ");print $(n+1)}' "$RUN/$1.stdout"; }
record() { RESULT[$1]=$2; DETAIL[$1]=$3; }
finish_table() {
    printf '\n| Check | Result | Evidence |\n|---|---|---|\n'
    local row failed=0
    for row in G1 G2 G3 G4 G5 G6; do
        printf '| %s | %s | %s |\n' "$row" "${RESULT[$row]:-FAIL}" "${DETAIL[$row]:-not reached}"
        [[ ${RESULT[$row]:-FAIL} == PASS ]] || failed=1
    done
    printf '\nArtifacts: %s\n' "${RUN#"$ROOT/"}"
    return "$failed"
}
on_error() {
    local status=$?
    trap - ERR
    record G1 FAIL "stage $current_stage exited $status; see its stderr"
    finish_table | tee "$RUN/result.txt" || true
    exit "$status"
}
trap on_error ERR

printf 'PLN structural compression\n' | tee "$RUN/summary.txt"
printf 'Candidate limit per stage: %s\n' "$SEEDS" | tee -a "$RUN/summary.txt"
sha256sum "$BIN" "$SOURCE" "$GLUE" scripts/run_ic_pln_poc.sh \
    lib/petta/lib_ic_library_cost.metta lib/petta/lib_ic_packed.metta \
    tests/petta/ic_library_cost.metta tests/support/count_metta_nodes.awk > "$RUN/provenance.sha256"
cp "$SOURCE" "$RUN/original.metta"
stage pack "(pln-poc:pack $(quote "$RUN/original.metta") $(quote "$RUN/A0.dec") $(quote "$RUN/original.normalized.metta"))"
binary "$RUN/A0.dec" "$RUN/A0.sic"

learn() {
    local label=$1 input=$2 suffix=$3 split=$4 output=$5
    stage "$label" "(pln-poc:learn $(quote "$RUN/$input.dec") $(quote "$suffix") $split $(quote "$RUN/$output.dec") $(quote "$RUN/$output.helpers.metta") $SEEDS)"
    binary "$RUN/$output.dec" "$RUN/$output.sic"
    grep '^[(]PLN:Stage ' "$RUN/$label.stdout" | tee -a "$RUN/summary.txt"
}
learn first A0 a 0 A1
learn second A1 b 0 A2
n0=$(field first 2); n1=$(field first 3); n2=$(field second 3)
a1=$(field first 6); a2=$(field second 6)
printf 'Nodes: %s -> %s -> %s\n' "$n0" "$n1" "$n2" | tee -a "$RUN/summary.txt"
printf 'Bytes: %s -> %s -> %s\n' "$(wc -c < "$RUN/A0.sic")" "$(wc -c < "$RUN/A1.sic")" "$(wc -c < "$RUN/A2.sic")" | tee -a "$RUN/summary.txt"
if [[ $n0 -gt $n1 && $n1 -gt $n2 && $a1 -gt 0 && $a2 -gt 0 ]] \
   && grep -q 'RegionExhausted IC:NoFault 0)' "$RUN/first.stdout" \
   && grep -q 'RegionExhausted IC:NoFault 0)' "$RUN/second.stdout"; then
    record G2 PASS "two improvements: $n0 -> $n1 -> $n2 nodes"
else
    record G2 FAIL "nodes $n0 -> $n1 -> $n2; accepted $a1 then $a2"
fi

stage decode "(pln-poc:decode $(quote "$RUN/A2.dec") $(quote "$RUN/decoded.metta"))"
stage export "(pln-poc:export $(quote "$RUN/A2.dec") $(quote "$RUN/rewritten.metta"))"
stage export-first "(pln-poc:export $(quote "$RUN/A1.dec") $(quote "$RUN/stage1-source.metta"))"
count0=$(awk -f tests/support/count_metta_nodes.awk "$RUN/original.normalized.metta")
count1=$(awk -f tests/support/count_metta_nodes.awk "$RUN/stage1-source.metta")
count2=$(awk -f tests/support/count_metta_nodes.awk "$RUN/rewritten.metta")
if cmp -s "$RUN/original.normalized.metta" "$RUN/decoded.metta" \
   && [[ $count0 -eq $n0 && $count1 -eq $n1 && $count2 -eq $n2 ]]; then
    record G3 PASS 'native reconstruction cmp-equal; separate node counts agree at every stage'
else record G3 FAIL 'reconstruction or independent node count differs'; fi

CALLS=(
 '(TransitiveSimilarityStrength 0.5 0.6 0.7 0.8 0.9)'
 '(TransitiveSimilarityStrength 0.3 0.4 0.5 0.6 0.7)'
 '(TransitiveSimilarityStrength 0.6 0.2 0.3 0.9 0.4)'
 '(TransitiveSimilarityStrength 0.4 1.0 0.4 0.5 0.5)'
 '(TransitiveSimilarityStrength 0.7 0.3 0.6 0.4 0.8)'
 '(TransitiveSimilarityStrength 0.15 0.25 0.35 0.45 0.55)'
 '(TransitiveSimilarityStrength 0.8 0.6 0.4 0.2 0.1)'
 '(Truth_transitiveSimilarity (stv 0.5 0.9) (stv 0.6 0.8) (stv 0.7 0.7) (stv 0.8 0.6) (stv 0.9 0.5))'
 '(Truth_transitiveSimilarity (stv 0.3 0.8) (stv 0.4 0.7) (stv 0.5 0.6) (stv 0.6 0.5) (stv 0.7 0.4))'
 '(Truth_ModusPonens (stv 0.8 0.9) (stv 0.7 0.8))'
 '(Truth_ModusPonens (stv 0.0 0.9) (stv 0.7 0.8))'
 '(Truth_Revision (stv 0.8 0.7) (stv 0.3 0.6))'
 '(Truth_Revision (stv 0.2 0.8) (stv 0.2 0.5))'
 '(Truth_Negation (stv 0.8 0.9))'
 '(Truth_Negation (stv 0.0 0.5))'
 '(Truth_Deduction (stv 0.5 0.9) (stv 0.5 0.8) (stv 0.5 0.7) (stv 0.6 0.9) (stv 0.7 0.8))'
 '(Truth_Deduction (stv 0.0 0.9) (stv 0.5 0.8) (stv 0.5 0.7) (stv 0.6 0.9) (stv 0.7 0.8))'
 '(Truth_equivalenceToImplication (stv 0.5 0.9) (stv 0.6 0.8) (stv 0.7 0.8))'
 '(/safe 1.0 0.0)'
)
same=0
for i in "${!CALLS[@]}"; do
    for variant in original rewritten; do
        { cat "$RUN/$variant.metta"; printf '\n!(println! (PLN:Answer %s (collapse %s)))\n' "$i" "${CALLS[$i]}"; } > "$RUN/call-$i-$variant.metta"
        run "call-$i-$variant"
    done
    if cmp -s "$RUN/call-$i-original.stdout" "$RUN/call-$i-rewritten.stdout" \
       && grep -q "^(PLN:Answer $i " "$RUN/call-$i-original.stdout" \
       && ! grep -q 'Error' "$RUN/call-$i-original.stdout"; then same=$((same+1)); fi
done
if [[ $same -eq ${#CALLS[@]} ]]; then record G4 PASS "$same original/rewritten PLN calls agree"
else record G4 FAIL "$same of ${#CALLS[@]} calls agree"; fi

learn resumed-first A0 a 5 A1r
learn resumed-second A1 b 5 A2r
if cmp -s "$RUN/A1.sic" "$RUN/A1r.sic" && cmp -s "$RUN/A2.sic" "$RUN/A2r.sic" \
   && grep -q 'PLN:Paused' "$RUN/resumed-first.stdout" && grep -q 'PLN:Paused' "$RUN/resumed-second.stdout"; then
    record G5 PASS 'both searches pause and resume to identical artifacts'
else record G5 FAIL 'resumption differs'; fi

"$BIN" --lang petta tests/petta/ic_library_cost.metta > "$RUN/cost-controls.stdout" 2> "$RUN/cost-controls.stderr"
control_count=$(grep -c '^[(]CostCheck .* true)$' "$RUN/cost-controls.stdout")
head -c -1 "$RUN/A2.sic" > "$RUN/truncated.sic"
decimal "$RUN/truncated.sic" "$RUN/truncated.dec"
stage truncated "(pln-poc:decode $(quote "$RUN/truncated.dec") $(quote "$RUN/truncated.decoded.metta"))"
if [[ $control_count -eq 17 ]] && [[ ! -s "$RUN/cost-controls.stderr" ]] \
   && grep -q '^[(]CostChecksFinished)' "$RUN/cost-controls.stdout" \
   && grep -q '^[(]PLN:Refused)' "$RUN/truncated.stdout" && [[ ! -e "$RUN/truncated.decoded.metta" ]]; then
    record G6 PASS '17 cost controls; malformed final archive refused'
else record G6 FAIL 'cost or malformed-artifact control failed'; fi

if sha256sum --status -c "$RUN/provenance.sha256"; then record G1 PASS 'one command; source and runtime unchanged'
else record G1 FAIL 'source or runtime changed during run'; fi
printf '\nLearned definitions:\n'
cat "$RUN/A2.helpers.metta"
grep '^[(]PLN:Uses ' "$RUN/second.stdout"
if finish_table | tee "$RUN/result.txt"; then exit 0; else exit 1; fi
