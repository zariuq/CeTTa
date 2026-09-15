#!/bin/sh
set -eu

if [ "$#" -ne 4 ]; then
    echo 'usage: qualify_match_decision_policy_lean_native_v1.sh COMPILER CHART LEAN_ROOT EVIDENCE_DIR' >&2
    exit 2
fi
compiler=$1
chart=$2
lean_root=$(cd "$3" && pwd)
mkdir -p "$4"
evidence=$(cd "$4" && pwd)
policy=experiments/gslt2parse_foundation/presentations/core/match_decision_policy_v1.metta
client=tests/langdef/bnf/match_decision_policy_source_qualification_v1.lean
result=runtime/generated/match_decision_policy_v1.result.metta
answers=runtime/generated/match_decision_policy_v1.answers
checks=0
sha256sum "$policy" "$result" "$answers" "$client" "$compiler" "$chart" \
    > "$evidence/input-digests.txt"

pass() {
    checks=$((checks + 1))
    printf 'PASS: %s\n' "$1"
}
fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}
copy_case() {
    mkdir -p "$evidence/$1/$(dirname "$policy")" \
        "$evidence/$1/$(dirname "$client")" "$evidence/$1/$(dirname "$result")"
    cp "$policy" "$evidence/$1/$policy"
    cp "$client" "$evidence/$1/$client"
    cp "$answers" "$evidence/$1/$answers"
}
check_case() {
    (cd "$lean_root" && nice -n 19 lake env lean "$evidence/$1/$client") \
        > "$evidence/$1/lean.log" 2>&1
}
require_case() {
    if ! check_case "$1"; then
        tail -n 50 "$evidence/$1/lean.log" >&2
        fail "$2"
    fi
    if grep -E 'sorryAx|uses .sorry.|^.*error:' "$evidence/$1/lean.log"; then
        fail 'successful qualification contained an unchecked proof'
    fi
    pass "$2"
}
reject_case() {
    if check_case "$1"; then
        fail 'Lean accepted a corrupted compilation result'
    fi
    grep -F "$3" "$evidence/$1/lean.log" > /dev/null || {
        tail -n 50 "$evidence/$1/lean.log" >&2
        fail 'negative case lacks its discriminating diagnostic'
    }
    proof_line=$(awk -v theorem="$2" '$0 ~ "^theorem " theorem "([[:space:]]|:)" { found = 1 }
        found && /:= by/ { print NR; exit }' "$evidence/$1/$client")
    test -n "$proof_line" || fail 'relevant theorem proof was not found'
    awk -v line="$proof_line" '/error:/ {
        ++errors
        if (index($0, ":" line ":") == 0) wrong = 1
    } END { exit !(errors == 1 && !wrong) }' "$evidence/$1/lean.log" || {
        tail -n 50 "$evidence/$1/lean.log" >&2
        fail 'negative case has an error outside its designated theorem'
    }
    pass "$4"
}

(cd "$lean_root" && LAKE_JOBS=3 nice -n 19 lake build \
    Mettapedia.GSLT.Parsing.FiniteFactTable \
    Mettapedia.GSLT.Parsing.CanonicalSourceOperationalGSLT \
    Mettapedia.OSLF.MeTTaIL.MeTTaSyntaxQuotation) \
    > "$evidence/lean-build.log" 2>&1 || {
        tail -n 50 "$evidence/lean-build.log" >&2
        fail 'qualification dependencies did not build'
    }

copy_case original
cp "$result" "$evidence/original/$result"
require_case original 'actual native result is occurrence-exact against actual authored source'

# Mutate only the emitted outcome, not the attached source-rule quotation.
sed 's/unknown wildcard equal equal fallback))/unknown wildcard equal equal keep))/' \
    "$answers" > "$evidence/forged.answers"
cmp -s "$answers" "$evidence/forged.answers" && fail 'outcome mutation did not apply'
"$compiler" match-policy-header --policy "$policy" --answers "$evidence/forged.answers" \
    --out "$evidence/forged.generated.h" > "$evidence/forged-emitter.log" 2>&1 || \
    fail 'control no longer crosses the structural-emitter trust boundary'
pass 'a structurally valid forged outcome reaches independent semantic qualification'
copy_case forged
cp "$evidence/forged.answers" "$evidence/forged/$answers"
"$compiler" answer-facts --source "$evidence/forged.answers" \
    --out "$evidence/forged/$result" > "$evidence/forged-framing.log" 2>&1
reject_case forged native_result_checks '(MatchDecisionPolicySemanticCheckV1 false)' \
    'Lean rejects the forged outcome against unchanged source semantics'

# A valid framed packet cannot conceal a different, corrupted emitter input.
copy_case raw-only
cp "$result" "$evidence/raw-only/$result"
cp "$evidence/forged.answers" "$evidence/raw-only/$answers"
reject_case raw-only raw_answers_match_framed_result '(MatchDecisionPolicyFramingCheckV1 false)' \
    'a valid framed packet cannot hide a forged raw answer stream'

# Changing both the authored fact and its generated result must remain legal.
copy_case changed
sed 's/expression expression-head equal equal keep/expression expression-head equal equal refute/' \
    "$policy" > "$evidence/changed/$policy"
cmp -s "$policy" "$evidence/changed/$policy" && fail 'source mutation did not apply'
"$compiler" answers --chart "$chart" \
    --source experiments/gslt2parse_foundation/presentations/reflection/finite_horn_reflection_v1.metta \
    --source langdef/shared/finite_horn_quote_match_v1.metta \
    --source experiments/gslt2parse_foundation/presentations/compiler/match_decision_policy_table_v1.metta \
    --reflect-source "$evidence/changed/$policy" --query '(md-compile ?fact)' \
    --out "$evidence/changed.answers" > "$evidence/changed-chart.log" 2>&1
cp "$evidence/changed.answers" "$evidence/changed/$answers"
"$compiler" answer-facts --source "$evidence/changed.answers" \
    --out "$evidence/changed/$result" > "$evidence/changed-framing.log" 2>&1
require_case changed 'qualification follows a changed authored outcome after regeneration'

copy_case reordered
# The answer-stream codec requires sorted input. Permute the already framed
# result rows instead, where the semantic check explicitly permits reordering.
# This fixture mutation only accepts the emitter's three-line fact layout;
# the unchanged Lean source decoder still checks the resulting structured data.
awk '
/^    \(rule / {
    row = $0 ORS
    if (getline <= 0 || $0 !~ /^      \(head /) exit 2
    row = row $0 ORS
    if (getline <= 0 || $0 != "      (body))") exit 2
    rows[++count] = row $0 ORS
    next
}
$0 == "  ))" {
    if (count == 0) exit 2
    for (i = count; i > 0; --i) printf "%s", rows[i]
}
{ print }
' "$result" > "$evidence/reordered/$result"
cmp -s "$result" "$evidence/reordered/$result" && fail 'row-order mutation did not apply'
require_case reordered 'table emission order is not mistaken for source occurrence multiplicity'
printf '(MatchDecisionPolicyLeanNativeV1Summary %s 0)\n' "$checks"
