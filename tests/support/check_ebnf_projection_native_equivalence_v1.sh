#!/usr/bin/env bash
# Native-versus-authored EBNF projection equivalence gate.
#
# Standard mode runs the frozen small corpus, the 5 KB arithmetic/JSON/CSV
# families, the reachable-padded families, and the fabricated boundary
# receipts, EACH IN ITS OWN PROCESS (transient oracle memory cannot
# accumulate across cases), and requires every comparison verdict to agree.
# A negative-control variant with a test-local mutated oracle source must
# then be detected as a disagreement. Extended mode runs the ~17 KB
# documents, one process per family, with a test-only work allowance.
#
# usage: check_ebnf_projection_native_equivalence_v1.sh RUNTIME EVIDENCE_DIRECTORY [ADMISSION_PROGRAM] [standard|extended]
set -euo pipefail
if (( $# < 2 || $# > 4 )); then
    echo 'usage: check_ebnf_projection_native_equivalence_v1.sh RUNTIME EVIDENCE_DIRECTORY [ADMISSION_PROGRAM] [standard|extended]' >&2
    exit 2
fi

equiv_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
equiv_runtime=$(realpath "$1")
equiv_evidence="$2"
equiv_program=${3:-langdef/petta/generated/plain_bnf_semantic_admission_v1.metta}
equiv_mode=${4:-standard}
case "$equiv_mode" in standard|extended) ;; *) echo "invalid mode: $equiv_mode" >&2; exit 2;; esac
trap 'rc=$?; if (( rc != 0 )); then echo "EBNF equivalence gate failed (status $rc); evidence: $equiv_evidence" >&2; fi' EXIT
test ! -e "$equiv_evidence"
mkdir -p "$equiv_evidence/inputs"
equiv_evidence=$(realpath "$equiv_evidence")
cd "$equiv_root"

equiv_fixture_root="tests/langdef/bnf/native-equivalence"

cp -r "$equiv_fixture_root/grammars" "$equiv_evidence/grammars"
python3 "$equiv_fixture_root/generate_inputs.py" "$equiv_evidence/inputs" >/dev/null

# authority selectors -> metta authority expressions
authority_expr() {
    case "$1" in
    arith)  echo '(bnf:default-authority "expr")';;
    s)      echo '(bnf:default-authority "s")';;
    json)   echo '(equiv:json-authority)';;
    csv)    echo '(equiv:csv-authority)';;
    *)      echo "unknown authority selector: $1" >&2; exit 2;;
    esac
}

# Emit one equivalence program for a corpus case.
# $1 name  $2 authority-selector  $3 grammar file  $4 input file  $5 allowance  $6 outfile
emit_case() {
    sed -e "s|@LIB_BNF@|$equiv_root/lib/lib_bnf.metta|" \
        -e "s|@PROJECTION_SOURCE@|langdef/bnf/ebnf_derivation_projection_v1.metta|" \
        "$equiv_fixture_root/ebnf_equiv_lib_v1.metta" > "$6"
    local expected=accepted
    [[ "$1" != *-bad ]] || expected=rejected
    {
        echo "!(println! (EbnfProjectionNativeEquivalenceV1 $1"
        echo "    (equiv:compare \"$equiv_evidence/grammars/$3\" $(authority_expr "$2") \"$equiv_evidence/inputs/$4\" $5 $expected)))"
    } >> "$6"
}

# Require exactly one verdict per expected case, including its tag and value.
# A contradictory or duplicate verdict must not be hidden by a successful grep.
assert_verdicts() {
    python3 - "$@" <<'PY_VERDICTS'
import pathlib, sys
lines = pathlib.Path(sys.argv[1]).read_text().splitlines()
actual = [line for line in lines if line.startswith('(Ebnf')]
expected = sys.argv[2:]
if actual != expected or any('(Error ' in line for line in lines):
    print(f"incorrect verdicts: expected {expected!r}, received {actual!r}", file=sys.stderr)
    sys.exit(1)
PY_VERDICTS
}

run_and_assert_true() {
    local out="$1.out" rc=0
    CETTA_PETTA_SEARCH_MACHINE=1 "$equiv_runtime" --lang petta "$equiv_program" \
        "$1" > "$out" 2> "$1.stderr" || rc=$?
    if (( rc != 0 )) || [[ -s "$1.stderr" ]]; then
        echo "case $2: runtime failed (status $rc)" >&2
        return 1
    fi
    assert_verdicts "$out" "(EbnfProjectionNativeEquivalenceV1 $2 true)" || return 1
    cat "$out" >> "$equiv_evidence/all-verdicts.out"
}

: > "$equiv_evidence/all-verdicts.out"

if [[ "$equiv_mode" == "standard" ]]; then
    #        name                        authority grammar                       input                            allowance
    corpus_cases=(
        "arithmetic-small            arith  arithmetic.ebnf            arithmetic_small.input            100000000"
        "duplicate                   s      duplicate.ebnf             duplicate.input                   100000000"
        "nullable                    s      nullable.ebnf              nullable.input                    100000000"
        "ambiguous                   s      ambiguous.ebnf             ambiguous.input                   100000000"
        "control-target              s      control_target.ebnf        control_target.input              100000000"
        "left-recursive              s      left_recursive.ebnf        left_recursive.input              100000000"
        "json-small                  json   json.ebnf                  json_small.input                  100000000"
        "arithmetic-5000             arith  arithmetic.ebnf            arithmetic_5000.input             1000000000"
        "json-5000                   json   json.ebnf                  json_5000.input                   1000000000"
        "csv-5000                    csv    csv.ebnf                   csv_5000.input                    1000000000"
        "arithmetic-bad              arith  arithmetic.ebnf            arithmetic_bad.input              100000000"
        "json-bad                    json   json.ebnf                  json_bad.input                    100000000"
    )
    for row in "${corpus_cases[@]}"; do
        read -r name auth grammar input allowance <<< "$row"
        program="$equiv_evidence/case-$name.metta"
        emit_case "$name" "$auth" "$grammar" "$input" "$allowance" "$program"
        run_and_assert_true "$program" "$name"
    done

    # Fabricated boundary receipts: one cheap program, eight parity verdicts.
    sed -e "s|@LIB_BNF@|$equiv_root/lib/lib_bnf.metta|" \
        -e "s|@PROJECTION_SOURCE@|langdef/bnf/ebnf_derivation_projection_v1.metta|" \
        "$equiv_fixture_root/ebnf_equiv_lib_v1.metta" > "$equiv_evidence/boundary.metta"
    cat >> "$equiv_evidence/boundary.metta" <<'EOF_BOUNDARY'
!(println! (EbnfProjectionNativeEquivalenceV1 span-max-representable
      (equiv:parity value (equiv:literal-receipt (equiv:text 120))
        ((CstRuleV1 "s#" (- (equiv:max-i64) 1) (equiv:max-i64))))))
!(println! (EbnfProjectionNativeEquivalenceV1 span-overflow-literal
      (equiv:parity fault (equiv:literal-receipt (equiv:text 120))
        ((CstRuleV1 "s#" (equiv:max-i64) (equiv:min-i64))))))
!(println! (EbnfProjectionNativeEquivalenceV1 span-overflow-lexical
      (equiv:parity fault (equiv:lexical-receipt)
        ((CstRuleV1 "TokenLabelT" (equiv:max-i64) (equiv:min-i64) (cp 116))))))
!(println! (EbnfProjectionNativeEquivalenceV1 literal-nul
      (equiv:parity fault (equiv:literal-receipt (equiv:text 0))
        ((CstRuleV1 "s#" 0 1)))))
!(println! (EbnfProjectionNativeEquivalenceV1 literal-surrogate
      (equiv:parity fault (equiv:literal-receipt (equiv:text 55296))
        ((CstRuleV1 "s#" 0 1)))))
!(println! (EbnfProjectionNativeEquivalenceV1 literal-negative
      (equiv:parity fault (equiv:literal-receipt (equiv:text -1))
        ((CstRuleV1 "s#" 0 1)))))
!(println! (EbnfProjectionNativeEquivalenceV1 literal-above-range
      (equiv:parity fault (equiv:literal-receipt (equiv:text 1114112))
        ((CstRuleV1 "s#" 0 1)))))
!(println! (EbnfProjectionNativeEquivalenceV1 literal-multibyte-valid
      (equiv:parity value (equiv:literal-receipt (bnf-v1:text-cons 955 (bnf-v1:text-cons 38634 (bnf-v1:text-cons 33 (bnf-v1:text-nil)))))
        ((CstRuleV1 "s#" 0 3)))))
EOF_BOUNDARY
    CETTA_PETTA_SEARCH_MACHINE=1 "$equiv_runtime" --lang petta "$equiv_program" \
        "$equiv_evidence/boundary.metta" > "$equiv_evidence/boundary.out" \
        2> "$equiv_evidence/boundary.stderr"
    test ! -s "$equiv_evidence/boundary.stderr"
    boundary_verdicts=()
    for boundary_case in span-max-representable span-overflow-literal \
            span-overflow-lexical literal-nul literal-surrogate \
            literal-negative literal-above-range literal-multibyte-valid; do
        boundary_verdicts+=("(EbnfProjectionNativeEquivalenceV1 $boundary_case true)")
    done
    assert_verdicts "$equiv_evidence/boundary.out" "${boundary_verdicts[@]}"
    cat "$equiv_evidence/boundary.out" >> "$equiv_evidence/all-verdicts.out"

    # Negative control: a mutated oracle source must be detected on the
    # control-target case. The gate fails if the comparison still agrees.
    awk '
        /\(rule node-group$/ {selected=1}
        selected && /\(EBNF:Group / {
            sub(/\?index/, "0"); changed++; selected=0
        }
        {print}
        END {if (changed != 1) exit 1}
    ' langdef/bnf/ebnf_derivation_projection_v1.metta \
        > "$equiv_evidence/mutated-projection.metta"
    sed -e "s|@LIB_BNF@|$equiv_root/lib/lib_bnf.metta|" \
        -e "s|@PROJECTION_SOURCE@|$equiv_evidence/mutated-projection.metta|" \
        "$equiv_fixture_root/ebnf_equiv_lib_v1.metta" > "$equiv_evidence/control.metta"
    echo "!(println! (EbnfProjectionNativeEquivalenceV1 control-target" >> "$equiv_evidence/control.metta"
    echo "    (equiv:compare \"$equiv_evidence/grammars/control_target.ebnf\" $(authority_expr s) \"$equiv_evidence/inputs/control_target.input\" 100000000 accepted)))" >> "$equiv_evidence/control.metta"
    CETTA_PETTA_SEARCH_MACHINE=1 "$equiv_runtime" --lang petta "$equiv_program" \
        "$equiv_evidence/control.metta" > "$equiv_evidence/control.out" \
        2> "$equiv_evidence/control.stderr"
    test ! -s "$equiv_evidence/control.stderr"
    assert_verdicts "$equiv_evidence/control.out" "(EbnfProjectionNativeEquivalenceV1 control-target false)"
    ebnf_cases=20
else
    # Extended cases are required checks. Runtime failure, exhaustion, missing
    # verdicts and disagreement all fail the gate; none counts as a pass.
    ext_cases=(
        "arithmetic-reachable-5000   arith  arithmetic_reachable.ebnf  arithmetic_reachable_5000.input   2000000000"
        "json-reachable-5000         json   json_reachable.ebnf        json_reachable_5000.input         2000000000"
        "arithmetic-17000            arith  arithmetic.ebnf            arithmetic_17000.input            4000000000"
        "json-17000                 json   json.ebnf                  json_17000.input                  4000000000"
    )
    ebnf_cases=0
    ebnf_failures=0
    for row in "${ext_cases[@]}"; do
        read -r name auth grammar input allowance <<< "$row"
        program="$equiv_evidence/case-$name.metta"
        emit_case "$name" "$auth" "$grammar" "$input" "$allowance" "$program"
        if run_and_assert_true "$program" "$name"; then
            ebnf_cases=$((ebnf_cases + 1))
        else
            ebnf_failures=$((ebnf_failures + 1))
            echo "(EbnfProjectionNativeEquivalenceV1 $name failed)" \
                >> "$equiv_evidence/all-verdicts.out"
        fi
    done
fi

sha256sum "$equiv_runtime" "$equiv_program" \
    "$equiv_fixture_root/ebnf_equiv_lib_v1.metta" \
    "$equiv_fixture_root/generate_inputs.py" \
    "$equiv_fixture_root/grammars/"*.ebnf \
    lib/lib_bnf.metta lib/langdef.metta \
    langdef/bnf/ebnf_derivation_projection_v1.metta \
    native/ebnf_derivation_projection_native_v1.c \
    > "$equiv_evidence/inputs.sha256"

echo "(EbnfProjectionNativeEquivalenceV1Summary $ebnf_cases ${ebnf_failures:-0})"
(( ${ebnf_failures:-0} == 0 ))
