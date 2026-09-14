#!/usr/bin/env bash
set -euo pipefail
if (( $# != 4 )); then
    echo 'usage: check_gslt_admitted_boundary_v1.sh COMPILER RUNTIME LEAN_PROJECT EVIDENCE_DIRECTORY' >&2
    exit 2
fi
bnf_repo=$(pwd -P)
bnf_compiler=$(realpath "$1")
bnf_runtime=$(realpath "$2")
bnf_lean=$(realpath "$3")
mkdir -p "$4"
bnf_evidence=$(realpath "$4")
test ! -e "$bnf_evidence/inputs.sha256"
bnf_source_base=(
    "$bnf_repo/experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta"
    "$bnf_repo/experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta"
    "$bnf_repo/tests/petta/gslt_admitted_integer_alignment_v1.metta")
bnf_exporter="$bnf_repo/tests/langdef/bnf/integer_provider_native_type_export_v1.lean"
bnf_language="$bnf_repo/langdef/bnf/plain_bnf_grammar_v1.metta"
bnf_query="$bnf_repo/tests/petta/gslt_admitted_integer_alignment_query_v1.metta"
bnf_controls=(
    "$bnf_repo/tests/petta/gslt_integer_native_types_duplicate_v1.metta"
    "$bnf_repo/tests/petta/gslt_admitted_boundary_shadow_v1.metta"
    "$bnf_repo/tests/petta/gslt_admitted_constructor_shadow_v1.metta")
sha256sum "$bnf_compiler" "$bnf_runtime" "$bnf_exporter" "$bnf_language" \
    "$bnf_query" "${bnf_source_base[@]}" "${bnf_controls[@]}" \
    tests/petta/gslt_admitted_integer_alignment_v1.expected \
    tests/petta/gslt_admitted_integer_duplicate_v1.expected "$0" \
    > "$bnf_evidence/inputs.sha256"
for bnf_case in alignment duplicate boundary constructor; do
    bnf_sources=("${bnf_source_base[@]}")
    case $bnf_case in
        duplicate) bnf_sources+=("${bnf_controls[0]}") ;;
        boundary) bnf_sources+=("${bnf_controls[1]}") ;;
        constructor) bnf_sources+=("${bnf_controls[2]}") ;;
    esac
    (cd "$bnf_lean" && lake env lean --run "$bnf_exporter" "${bnf_sources[@]}") \
        > "$bnf_evidence/$bnf_case.types.metta" 2> "$bnf_evidence/$bnf_case.export.stderr"
    bnf_args=()
    for bnf_source in "${bnf_sources[@]}"; do bnf_args+=(--source "$bnf_source"); done
    bnf_status=0
    "$bnf_compiler" petta-direct "${bnf_args[@]}" \
        --closed-entry-mode AdmittedIntegerAlignmentV1:10 \
        --native-types "$bnf_evidence/$bnf_case.types.metta" \
        --admission-language "$bnf_language" \
        --admission-entry AdmittedIntegerAlignmentV1:BnfScalarList \
        --out "$bnf_evidence/$bnf_case.program.metta" \
        > "$bnf_evidence/$bnf_case.compile.out" \
        2> "$bnf_evidence/$bnf_case.compile.stderr" || bnf_status=$?
    if [[ $bnf_case == boundary || $bnf_case == constructor ]]; then
        test "$bnf_status" -ne 0
        test ! -e "$bnf_evidence/$bnf_case.program.metta"
        rg -q '^LangDefCompileFailed: authored target equation overlaps the admitted entry boundary$' \
            "$bnf_evidence/$bnf_case.compile.stderr"
        test "$(wc -l < "$bnf_evidence/$bnf_case.compile.stderr")" -eq 1
        continue
    fi
    test "$bnf_status" -eq 0
    test ! -s "$bnf_evidence/$bnf_case.compile.stderr"
    # Both raw and admitted families must retain the two eligible clauses.
    test "$(rg -c '^; gslt-binding-mode AdmittedIntegerAlignmentV1/2 .*functional=relational specialized=yes$' \
        "$bnf_evidence/$bnf_case.program.metta")" -eq 2
    bnf_head=admitted:gslt:entry:AdmittedIntegerAlignmentV1:10
    CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_QUERY_TRACE="$bnf_head" \
        "$bnf_runtime" --lang petta "$bnf_evidence/$bnf_case.program.metta" "$bnf_query" \
        > "$bnf_evidence/$bnf_case.out" 2> "$bnf_evidence/$bnf_case.trace"
    rg -q "^\\[petta-query\\].*head=$bnf_head bindings=" "$bnf_evidence/$bnf_case.trace"
    if rg -n -v "^\\[petta-query\\].*head=$bnf_head bindings=" "$bnf_evidence/$bnf_case.trace"; then
        echo 'unexpected admitted-boundary runtime diagnostic' >&2
        exit 1
    fi
    diff -u "$bnf_repo/tests/petta/gslt_admitted_integer_${bnf_case}_v1.expected" \
        "$bnf_evidence/$bnf_case.out"
done
sha256sum -c "$bnf_evidence/inputs.sha256" > "$bnf_evidence/inputs-after.txt"
echo '(GsltAdmittedBoundaryV1 passed)'
