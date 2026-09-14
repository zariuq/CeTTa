#!/bin/sh
set -eu

if [ "$#" -ne 4 ]; then
    echo 'usage: test_match_decision_policy_v1.sh COMPILER CHART CC EVIDENCE_DIR' >&2
    exit 2
fi
compiler=$1
chart=$2
cc=$3
evidence=$4
mkdir -p "$evidence"
policy=experiments/gslt2parse_foundation/presentations/core/match_decision_policy_v1.metta
reflection=experiments/gslt2parse_foundation/presentations/reflection/finite_horn_reflection_v1.metta
matcher=langdef/shared/finite_horn_quote_match_v1.metta
transform=experiments/gslt2parse_foundation/presentations/compiler/match_decision_policy_table_v1.metta
header=src/generated/match_decision_policy_v1.generated.h
checks=0

pass() {
    checks=$((checks + 1))
    printf 'PASS: %s\n' "$1"
}
fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}
answers() {
    "$compiler" answers --chart "$chart" \
        --source "$reflection" --source "$matcher" --source "$transform" \
        --reflect-source "$1" --query '(md-compile ?fact)' --out "$2"
}
emit() {
    "$compiler" match-policy-header --policy "$1" --answers "$2" --out "$3"
}
probe() {
    "$cc" -std=c11 -Wall -Wextra -Werror -include stdint.h -include "$1" \
        tests/support/match_decision_policy_header_probe_v1.c -o "$evidence/probe"
    "$evidence/probe"
}
reject_stream() {
    cp "$header" "$evidence/rejected.generated.h"
    if emit "$1" "$2" "$evidence/rejected.generated.h" > "$3" 2>&1; then
        fail 'invalid compilation result was emitted'
    fi
    grep -F "$4" "$3" > /dev/null || fail 'rejection reason differs'
    cmp "$header" "$evidence/rejected.generated.h" || fail 'failure replaced the output'
}
reject_policy() {
    answers "$evidence/$1.metta" "$evidence/$1.answers" > "$evidence/$1.chart.log" 2>&1
    reject_stream "$evidence/$1.metta" "$evidence/$1.answers" "$evidence/$1.emit.log" "$2"
    pass "$3"
}

answers "$policy" "$evidence/original.answers" > "$evidence/original.chart.log" 2>&1
emit "$policy" "$evidence/original.answers" "$evidence/original.generated.h"
cmp "$header" "$evidence/original.generated.h" || fail 'runtime header is stale'
pass 'native source-driven regeneration is byte-identical'
[ "$(probe "$evidence/original.generated.h")" = '1 1' ] || fail 'original policy probe'
pass 'generated C retains the equal-head outcome and direct-key eligibility'

sed 's/expression expression-head equal equal keep/expression expression-head equal equal refute/' \
    "$policy" > "$evidence/changed.metta"
answers "$evidence/changed.metta" "$evidence/changed.answers" > "$evidence/changed.chart.log" 2>&1
emit "$evidence/changed.metta" "$evidence/changed.answers" "$evidence/changed.generated.h"
[ "$(probe "$evidence/changed.generated.h")" = '2 0' ] || fail 'changed policy probe'
pass 'authored outcome change changes C and revokes direct-key eligibility'

sed 's/unknown ?key ?arity ?identity fallback/unknown ?key ?same ?same fallback/' \
    "$policy" > "$evidence/repeated.metta"
reject_policy repeated 'no occurrence' 'shared variables constrain matching rather than acting as independent wildcards'

for outcome in fallback keep; do
    sed "/(rule policy-unknown/i\
    (rule overlapping-occurrence (head (candidate-policy unknown wildcard equal equal $outcome)) (body))" \
        "$policy" > "$evidence/overlap-$outcome.metta"
    reject_policy "overlap-$outcome" 'occurrences overlap' "overlapping occurrences are refused even with outcome $outcome"
done

sed 's/unknown ?key ?arity ?identity fallback/42 ?key ?arity ?identity fallback/' \
    "$policy" > "$evidence/unsupported.metta"
reject_policy unsupported 'unadmitted source occurrence' 'unsupported source shape cannot disappear from admission'
sed 's/unknown ?key ?arity ?identity fallback/unknown bogus-key ?arity ?identity fallback/' \
    "$policy" > "$evidence/outside-domain.metta"
reject_policy outside-domain 'unadmitted source occurrence' 'out-of-domain constants are refused'
sed '/(rule policy-unknown/i\
    (rule body-bearing-occurrence (head (candidate-policy unknown wildcard equal equal fallback)) (body (candidate-policy absent wildcard equal equal keep)))' \
    "$policy" > "$evidence/body-bearing.metta"
reject_policy body-bearing 'unadmitted source occurrence' 'premise-bearing rules are not silently treated as facts'

reject_stream "$evidence/changed.metta" "$evidence/original.answers" \
    "$evidence/source-mismatch.log" 'foreign source occurrence'
pass 'answers cannot be reused with a changed source rule'
awk '!/\(md-source /' "$evidence/original.answers" > "$evidence/omitted-source.answers"
reject_stream "$policy" "$evidence/omitted-source.answers" \
    "$evidence/omitted-source.log" 'omitted or unadmitted'
pass 'source coverage is mandatory'
awk '!/\(md-admitted /' "$evidence/original.answers" > "$evidence/omitted-admission.answers"
reject_stream "$policy" "$evidence/omitted-admission.answers" \
    "$evidence/omitted-admission.log" 'omitted or unadmitted'
pass 'admission evidence for every source rule is mandatory'
awk '!/\(md-cell / || removed++ > 0' "$evidence/original.answers" > "$evidence/omitted-cell.answers"
reject_stream "$policy" "$evidence/omitted-cell.answers" \
    "$evidence/omitted-cell.log" 'no occurrence'
pass 'an omitted input cell is not filled from a default table'
sed 's/(md-source /(unknown-result /' "$evidence/original.answers" > "$evidence/unknown-result.answers"
reject_stream "$policy" "$evidence/unknown-result.answers" \
    "$evidence/unknown-result.log" 'unknown policy compilation result'
pass 'unknown result constructors are refused'
printf '(MatchDecisionPolicyNativeV1Summary %s 0)\n' "$checks"
