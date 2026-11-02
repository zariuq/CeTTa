#!/usr/bin/env bash
# Retained frozen-profile experiment; no change to installed libraries or C.
set -Eeuo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=$(realpath "${1:-$ROOT/cetta}")
OUTPUT=${2:-$ROOT/runtime/ic-pln-mvp}
SOURCE_SHA=6b980321bbe9b49e5b12e2fcee0479ab1d5c7550c2104a67b0722a5b473da4ee
test -x "$BIN"
if [[ $(sha256sum "$ROOT/lib/petta/lib_pln.metta" | cut -d ' ' -f1) != "$SOURCE_SHA" ]]; then
    printf 'REFUSED: this frozen experiment requires its recorded PLN source\n' >&2
    exit 2
fi
mkdir -p "$OUTPUT"
OUTPUT=$(realpath "$OUTPUT")
RUN=$(mktemp -d "$OUTPUT/run.XXXXXX")
trap 'status=$?; printf "FAIL: see %s (exit %s)\n" "$RUN" "$status" >&2; exit "$status"' ERR
mkdir -p "$RUN/profile/tests/petta" "$RUN/profile/scripts" "$RUN/profile/examples/petta"
cp -a "$ROOT/lib" "$RUN/profile/lib"
cp "$BIN" "$RUN/profile/cetta"
cp "$ROOT/examples/petta/ic_pln_poc.metta" "$RUN/profile/poc.metta"
cp "$ROOT/examples/petta/ic_pln_poc.metta" "$RUN/profile/examples/petta/"
cp "$HERE/profile.metta" "$RUN/profile/profile.metta"
cp "$HERE/observations.metta" "$RUN/observations.metta"
cp "$HERE/replay.sh" "$RUN/replay.sh"
cp "$ROOT/tests/petta/ic_library_cost.metta" "$ROOT/tests/petta/ic_rule_heads.metta" "$RUN/profile/tests/petta/"
cp "$ROOT/scripts/check_ic_rule_heads.sh" "$RUN/profile/scripts/"
cp "$ROOT/tests/support/count_metta_nodes.awk" "$RUN/count_metta_nodes.awk"
cp "$ROOT/lib/petta/lib_pln.metta" "$RUN/original.metta"
patch --batch --fuzz=0 -p1 -d "$RUN/profile" < "$HERE/profile.patch" > "$RUN/profile-patch.log"
cd "$RUN"
printf '%s\n' 'IC-PLN-FROZEN-SAFE-1' \
    'Observation: normalized source + fixed public-call panel in fresh processes' \
    'Call: /safe arity 2; exact authored definition checked before each search' \
    'Search: same-head ranked seed pairs; limits 4 then 1; Nodes objective' \
    'Encoding: experimental LC2/LM1 payloads; matching profile required' \
    'Not a general-purpose, independently profile-describing archive format' > PROFILE.txt
{
    find profile/lib -type f -print0 | sort -z | xargs -0 sha256sum
    sha256sum profile/cetta profile/poc.metta profile/profile.metta \
        profile/examples/petta/ic_pln_poc.metta PROFILE.txt
} > PROFILE.sha256
sha256sum original.metta observations.metta count_metta_nodes.awk replay.sh > INPUTS.sha256

quote() {
    local value=$1
    value=${value//\\/\\\\}; value=${value//\"/\\\"}
    value=${value//$'\n'/\\n}; value=${value//$'\r'/\\r}; value=${value//$'\t'/\\t}
    printf '"%s"' "$value"
}
stage() {
    local name=$1 expression=$2
    {
        printf '!(import! &self %s)\n' "$(quote "$RUN/profile/poc.metta")"
        printf '!(import! &self %s)\n' "$(quote "$RUN/profile/profile.metta")"
        printf '!%s\n' "$expression"
    } > "$name.metta"
    profile/cetta --lang petta "$RUN/$name.metta" > "$name.stdout" 2> "$name.stderr"
    test ! -s "$name.stderr"
    ! grep -q 'PLN:Fault' "$name.stdout"
}
binary() { awk '{for(i=1;i<=NF;i++)printf "%02x",$i}' "$1" | xxd -r -p > "$2"; }
learn() {
    local name=$1 input=$2 suffix=$3 split=$4 output=$5 limit=$6
    stage "$name" "(pln-profile:learn $(quote "$RUN/$input.dec") $(quote "$suffix") $split $(quote "$RUN/$output.dec") $(quote "$RUN/$output.helpers.metta") $limit)"
    binary "$output.dec" "$output.sic"
    grep '^[(]PLN:Stage ' "$name.stdout"
}
stage profile-controls '(pln-profile:controls)'
test "$(grep -c '^[(]ProfileCheck .* true)$' profile-controls.stdout)" -eq 4
stage pack "(pln-poc:pack $(quote "$RUN/original.metta") $(quote "$RUN/A0.dec") $(quote "$RUN/original.normalized.metta"))"
binary A0.dec A0.sic
learn first A0 a 0 A1 4
learn second A1 b 0 A2 1
grep -q '"a" 2207 2186 59537 59092 2 4 RegionExhausted IC:NoFault 0)' first.stdout
grep -q '"b" 2186 2180 59092 59059 1 1 RegionExhausted IC:NoFault 0)' second.stdout
grep -q '^[(]PLN:Uses "ic-learned:a" 4)' second.stdout
grep -q '^[(]PLN:Uses "ic-learned:b" 2)' second.stdout
for artifact in A1 A2; do
    stage "decode-$artifact" "(pln-poc:decode $(quote "$RUN/$artifact.dec") $(quote "$RUN/$artifact.restored.metta"))"
    cmp original.normalized.metta "$artifact.restored.metta"
    stage "export-$artifact" "(pln-poc:export $(quote "$RUN/$artifact.dec") $(quote "$RUN/$artifact.rewritten.metta"))"
done
n0=$(awk -f count_metta_nodes.awk original.normalized.metta)
n1=$(awk -f count_metta_nodes.awk A1.rewritten.metta)
n2=$(awk -f count_metta_nodes.awk A2.rewritten.metta)
test "$n0 $n1 $n2" = '2207 2186 2180'
test "$(wc -c < A0.sic) $(wc -c < A1.sic) $(wc -c < A2.sic)" = '59537 59092 59059'
for variant in original final; do
    source_file=original.normalized.metta
    [[ $variant != final ]] || source_file=A2.rewritten.metta
    { cat "$source_file"; printf '\n'; cat observations.metta; } > "$variant-execution.metta"
    "$BIN" --lang petta "$RUN/$variant-execution.metta" > "$variant.stdout" 2> "$variant.stderr"
    test ! -s "$variant.stderr"
    test "$(grep -c '^[(]PLNProbe ' "$variant.stdout")" -eq 33
    { cat "$source_file"; printf '\n!(TransitiveSimilarityStrength 0.5 0.6 0.7 "bad" 0.7)\n'; } > "$variant-fault.metta"
    if "$BIN" --lang petta "$RUN/$variant-fault.metta" > "$variant-fault.stdout" 2> "$variant-fault.stderr"; then
        status=0
    else status=$?; fi
    test "$status" -eq 2
    printf '%s\n' "$status" > "$variant-fault.status"
done
cmp original.stdout final.stdout
cmp original-fault.stdout final-fault.stdout
cmp original-fault.stderr final-fault.stderr
cmp original-fault.status final-fault.status
learn resumed-first A0 a 1 A1r 4
learn resumed-second A1r b 1 A2r 1
cmp A1.sic A1r.sic
cmp A2.sic A2r.sic
grep -q '^[(]PLN:Paused 1)' resumed-first.stdout
grep -q '^[(]PLN:Paused 1)' resumed-second.stdout
profile/cetta --lang petta profile/tests/petta/ic_library_cost.metta > cost-controls.stdout 2> cost-controls.stderr
test ! -s cost-controls.stderr
test "$(grep -c '^[(]CostCheck .* true)$' cost-controls.stdout)" -eq 17
bash profile/scripts/check_ic_rule_heads.sh "$RUN/profile/cetta" > rule-head-controls.stdout 2> rule-head-controls.stderr
test ! -s rule-head-controls.stderr
head -c -1 A2.sic > truncated.sic
od -An -v -tu1 truncated.sic > truncated.dec
stage truncated "(pln-poc:decode $(quote "$RUN/truncated.dec") $(quote "$RUN/truncated.restored.metta"))"
grep -q '^[(]PLN:Refused)' truncated.stdout
test ! -e truncated.restored.metta
sha256sum --strict --status -c PROFILE.sha256
sha256sum --strict --status -c INPUTS.sha256
sha256sum A0.sic A1.sic A2.sic A2.dec original.normalized.metta A1.rewritten.metta A2.rewritten.metta A2.helpers.metta > ARTIFACTS.sha256
bash replay.sh "$RUN" > replay-check.stdout
printf '%s\n' \
    'PASS: unchanged real PLN input; two generated features with 4 and 2 uses' \
    "Nodes: $n0 -> $n1 -> $n2" \
    'Archive bytes (profile supplied separately): 59537 -> 59092 -> 59059' \
    'PASS: both native reconstructions and independent AWK counts' \
    'PASS: 33 public-call observations and separate fault comparison' \
    'PASS: pause/resume at both stages yields identical artifacts' \
    'PASS: 17 cost controls, rule-head controls, 4 pinned-call controls, malformed refusal' \
    'PASS: retained-profile checksum verification and fresh-process replay' > result.txt
cat result.txt
cat A2.helpers.metta
printf 'Capsule: %s\n' "$RUN"
