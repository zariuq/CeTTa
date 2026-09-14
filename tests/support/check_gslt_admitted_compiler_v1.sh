#!/usr/bin/env bash
set -euo pipefail

if (( $# != 4 )); then
    echo 'usage: check_gslt_admitted_compiler_v1.sh COMPILER RUNTIME LEAN_PROJECT EVIDENCE_DIRECTORY' >&2
    exit 2
fi

bnf_repo=$(pwd -P)
bnf_compiler=$(realpath "$1")
bnf_runtime=$(realpath "$2")
bnf_lean=$(realpath "$3")
bnf_script=$(realpath "$0")
test -x "$bnf_compiler" && test -x "$bnf_runtime"
mkdir -p "$4"
bnf_evidence=$(realpath "$4")
test ! -e "$bnf_evidence/inputs.sha256"
bnf_exporter="$bnf_repo/tests/langdef/bnf/integer_provider_native_type_export_v1.lean"
bnf_language="$bnf_repo/langdef/bnf/plain_bnf_grammar_v1.metta"
bnf_sources=(
    "$bnf_repo/experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta"
    "$bnf_repo/experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta"
    "$bnf_repo/experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta"
    "$bnf_repo/experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta"
    "$bnf_repo/langdef/bnf/plain_bnf_semantic_admission_v1.metta"
    "$bnf_repo/langdef/bnf/plain_bnf_graph_analysis_v1.metta")
bnf_source_args=()
for bnf_source in "${bnf_sources[@]}"; do
    bnf_source_args+=(--source "$bnf_source")
done
bnf_modes=(--closed-entry-mode BNFValidateGrammarV1:10
           --closed-entry-mode BNFAnalyzeDocumentV1:1110)
bnf_entry=BNFValidateGrammarV1:BnfGrammarInput
bnf_packet="$bnf_evidence/native-types.metta"
bnf_queries=(plain_bnf_public_entry_v1 plain_bnf_public_outcomes_v1
             plain_bnf_edit_document_v1 plain_bnf_admission_oracle_v1
             plain_bnf_lexical_domain_v1)
bnf_pinned=("$bnf_compiler" "$bnf_runtime" "$bnf_exporter" "$bnf_language"
            "$bnf_script" "${bnf_sources[@]}" "$bnf_repo/lib/lib_bnf.metta"
            "$bnf_repo/tests/langdef/bnf/plain_bnf_admitted_compiler_v1.metta"
            "$bnf_repo/tests/langdef/bnf/plain_bnf_admitted_compiler_v1.expected")
for bnf_query in "${bnf_queries[@]}"; do
    bnf_pinned+=("$bnf_repo/tests/langdef/bnf/$bnf_query.metta"
                "$bnf_repo/tests/langdef/bnf/$bnf_query.expected")
done
sha256sum "${bnf_pinned[@]}" > "$bnf_evidence/inputs.sha256"

bnf_compile() {
    local label=$1
    shift
    "$bnf_compiler" petta-direct "${bnf_source_args[@]}" \
        --out "$bnf_evidence/$label.metta" "$@" \
        > "$bnf_evidence/$label.compile.txt" \
        2> "$bnf_evidence/$label.compile.stderr"
    test ! -s "$bnf_evidence/$label.compile.stderr"
}

bnf_reject() {
    local label=$1 diagnostic=$2
    shift 2
    if "$bnf_compiler" petta-direct "${bnf_source_args[@]}" \
        --out "$bnf_evidence/$label.program.metta" "$@" \
        > "$bnf_evidence/$label.out" 2> "$bnf_evidence/$label.stderr"; then
        echo "unexpected admitted-compiler acceptance: $label" >&2
        exit 1
    fi
    test ! -e "$bnf_evidence/$label.program.metta"
    test ! -s "$bnf_evidence/$label.out"
    rg -q "$diagnostic" "$bnf_evidence/$label.stderr"
    printf '%s\trefused\n' "$label" >> "$bnf_evidence/refusals.tsv"
}

bnf_run() {
    local label=$1 program=$2 query=$3 expected=$4
    CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta \
        "$program" "$query" > "$bnf_evidence/$label.out" \
        2> "$bnf_evidence/$label.stderr"
    test ! -s "$bnf_evidence/$label.stderr"
    diff -u "$expected" "$bnf_evidence/$label.out"
}

bnf_split() {
    local label=$1
    local raw_size
    raw_size=$(wc -c < "$bnf_evidence/raw.metta")
    head -c "$raw_size" "$bnf_evidence/$label.metta" \
        > "$bnf_evidence/$label.raw-prefix.metta"
    cmp "$bnf_evidence/raw.metta" "$bnf_evidence/$label.raw-prefix.metta"
    tail -c "+$((raw_size + 2))" "$bnf_evidence/$label.metta" \
        > "$bnf_evidence/$label.specialized-suffix.metta"
}

bnf_changed() {
    if cmp -s "$1" "$2"; then
        echo 'source mutation did not change its input' >&2
        exit 1
    fi
}

(cd "$bnf_lean" && lake env lean --run "$bnf_exporter" "${bnf_sources[@]}") \
    > "$bnf_packet" 2> "$bnf_evidence/export.stderr"
bnf_compile no-types "${bnf_modes[@]}"
bnf_compile raw "${bnf_modes[@]}" --native-types "$bnf_packet"
# The existing six-source public program has no literal-only specialization.
cmp "$bnf_evidence/no-types.metta" "$bnf_evidence/raw.metta"
bnf_compile admitted "${bnf_modes[@]}" --native-types "$bnf_packet" \
    --admission-language "$bnf_language" --admission-entry "$bnf_entry"
bnf_split admitted
rg -q '^; gslt-binding-mode BNFScalarOrderDiagnosticV1/4 input=1110 success=1111 functional=relational specialized=yes$' \
    "$bnf_evidence/raw.metta"
rg -q '^; gslt-binding-mode BNFScalarOrderDiagnosticV1/4 input=1110 success=1111 functional=semidet specialized=yes$' \
    "$bnf_evidence/admitted.specialized-suffix.metta"
# Require a changed executable call, not just changed mode commentary.
rg -Fq '(superpose (collapse (gslt:mode:BNFScalarOrderDiagnosticV1:1110 ' \
    "$bnf_evidence/raw.metta"
rg -Fq '(admitted:gslt:fn:BNFScalarOrderDiagnosticV1:1110 ' \
    "$bnf_evidence/admitted.specialized-suffix.metta"
if rg -Fq '(superpose (collapse (admitted:gslt:mode:BNFScalarOrderDiagnosticV1:1110 ' \
    "$bnf_evidence/admitted.specialized-suffix.metta"; then
    echo 'typed scalar-order calls retained their relational round trip' >&2
    exit 1
fi

bnf_run checked "$bnf_evidence/admitted.metta" \
    "$bnf_repo/tests/langdef/bnf/plain_bnf_admitted_compiler_v1.metta" \
    "$bnf_repo/tests/langdef/bnf/plain_bnf_admitted_compiler_v1.expected"
# Observe actual clause selection without perturbing source or generated code.
# Output agreement alone would also pass if the wrapper always fell back.
bnf_trace_head=admitted:gslt:fn:BNFScalarOrderDiagnosticV1:1110
CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_QUERY_TRACE="$bnf_trace_head" \
    "$bnf_runtime" --lang petta "$bnf_evidence/admitted.metta" \
    "$bnf_repo/tests/langdef/bnf/plain_bnf_admitted_compiler_v1.metta" \
    > "$bnf_evidence/activation.out" 2> "$bnf_evidence/activation.stderr"
diff -u "$bnf_repo/tests/langdef/bnf/plain_bnf_admitted_compiler_v1.expected" \
    "$bnf_evidence/activation.out"
rg -q "^\\[petta-query\\] transitions=[0-9]+ head=$bnf_trace_head bindings=" \
    "$bnf_evidence/activation.stderr"
if rg -n -v "^\\[petta-query\\] transitions=[0-9]+ head=$bnf_trace_head bindings=" \
    "$bnf_evidence/activation.stderr"; then
    echo 'unexpected diagnostic during admitted-family activation trace' >&2
    exit 1
fi
# Existing public clients still call raw entries. These runs check that the
# combined program preserves that family, not that public integration changed.
for bnf_query in "${bnf_queries[@]}"; do
    for bnf_version in raw admitted; do
        bnf_run "$bnf_query.$bnf_version" "$bnf_evidence/$bnf_version.metta" \
            "$bnf_repo/tests/langdef/bnf/$bnf_query.metta" \
            "$bnf_repo/tests/langdef/bnf/$bnf_query.expected"
    done
    cmp "$bnf_evidence/$bnf_query.raw.out" "$bnf_evidence/$bnf_query.admitted.out"
done

bnf_typed=("${bnf_modes[@]}" --native-types "$bnf_packet")
bnf_admission=(--admission-language "$bnf_language" --admission-entry "$bnf_entry")
bnf_reject unpaired-language 'together' "${bnf_typed[@]}" --admission-language "$bnf_language"
bnf_reject unpaired-entry 'together' "${bnf_typed[@]}" --admission-entry "$bnf_entry"
bnf_reject missing-language-value 'lacks a value' "${bnf_typed[@]}" --admission-language
bnf_reject missing-entry-value 'lacks a value' "${bnf_typed[@]}" --admission-entry
bnf_reject repeated-language 'invalid or repeated' "${bnf_typed[@]}" \
    "${bnf_admission[@]}" --admission-language "$bnf_language"
bnf_reject repeated-entry 'invalid or repeated' "${bnf_typed[@]}" \
    "${bnf_admission[@]}" --admission-entry "$bnf_entry"
bnf_reject missing-types 'requires native types and closed entry modes' \
    "${bnf_modes[@]}" "${bnf_admission[@]}"
bnf_reject missing-closed-mode 'requires native types and closed entry modes' \
    --native-types "$bnf_packet" "${bnf_admission[@]}"
bnf_reject open-mode 'requires native types and closed entry modes' \
    --entry-mode BNFValidateGrammarV1:10 --native-types "$bnf_packet" "${bnf_admission[@]}"
bnf_reject epilogue 'without an unchecked epilogue' "${bnf_typed[@]}" \
    "${bnf_admission[@]}" --epilogue "$bnf_repo/tests/langdef/bnf/plain_bnf_public_entry_v1.metta"
bnf_reject unselected-entry 'not a declared closed entry' --native-types "$bnf_packet" \
    --closed-entry-mode BNFAnalyzeDocumentV1:1110 "${bnf_admission[@]}"
bnf_reject multi-input-entry 'exactly one declared single-input mode' \
    "${bnf_typed[@]}" --admission-language "$bnf_language" \
    --admission-entry BNFAnalyzeDocumentV1:BnfGrammarInput
bnf_reject unknown-type 'unknown admission type' "${bnf_typed[@]}" \
    --admission-language "$bnf_language" --admission-entry BNFValidateGrammarV1:MissingType

# Mutants are mechanically derived source inputs; generated programs are never patched.
sed 's/GSLTLanguageDefWireV1/MalformedLanguageDefControlV1/' "$bnf_language" \
    > "$bnf_evidence/malformed-language.metta"
bnf_changed "$bnf_language" "$bnf_evidence/malformed-language.metta"
bnf_reject malformed-language 'LanguageDef|language definition' "${bnf_typed[@]}" \
    --admission-language "$bnf_evidence/malformed-language.metta" --admission-entry "$bnf_entry"
sed 's/CarrierBuiltinInt/CarrierBuiltinString/g' "$bnf_language" \
    > "$bnf_evidence/string-carrier-language.metta"
bnf_changed "$bnf_language" "$bnf_evidence/string-carrier-language.metta"
if rg -q 'CarrierBuiltinInt' "$bnf_evidence/string-carrier-language.metta"; then
    echo 'carrier mutation left an integer carrier in the source' >&2
    exit 1
fi
bnf_compile string-carrier "${bnf_typed[@]}" \
    --admission-language "$bnf_evidence/string-carrier-language.metta" --admission-entry "$bnf_entry"
bnf_split string-carrier
rg -q '^; gslt-binding-mode BNFScalarOrderDiagnosticV1/4 input=1110 success=1111 functional=relational specialized=yes$' \
    "$bnf_evidence/string-carrier.specialized-suffix.metta"
rg -Fq '(superpose (collapse (admitted:gslt:mode:BNFScalarOrderDiagnosticV1:1110 ' \
    "$bnf_evidence/string-carrier.specialized-suffix.metta"
if rg -Fq '(= (admitted:gslt:fn:BNFScalarOrderDiagnosticV1:1110 ' \
    "$bnf_evidence/string-carrier.specialized-suffix.metta"; then
    echo 'string carrier incorrectly licensed integer scalar-order specialization' >&2
    exit 1
fi
# The unchanged query loads the original language, not this mutated schema.
# No checked call may cross into the mismatched authority's typed family.
CETTA_PETTA_SEARCH_MACHINE=1 \
    CETTA_PETTA_QUERY_TRACE=admitted:gslt:entry:BNFValidateGrammarV1:10 \
    "$bnf_runtime" --lang petta "$bnf_evidence/string-carrier.metta" \
    "$bnf_repo/tests/langdef/bnf/plain_bnf_admitted_compiler_v1.metta" \
    > "$bnf_evidence/authority-fallback.out" \
    2> "$bnf_evidence/authority-fallback.stderr"
test ! -s "$bnf_evidence/authority-fallback.stderr"
diff -u "$bnf_repo/tests/langdef/bnf/plain_bnf_admitted_compiler_v1.expected" \
    "$bnf_evidence/authority-fallback.out"

# The admitted route must retain authentication of source occurrences and type packets.
sed 's/ground-integer-less less direct)/ground-integer-less not-less direct)/' \
    "$bnf_packet" > "$bnf_evidence/false-polarity.packet.metta"
bnf_changed "$bnf_packet" "$bnf_evidence/false-polarity.packet.metta"
bnf_reject stale-packet 'native type disagrees with its source occurrence' \
    "${bnf_modes[@]}" --native-types "$bnf_evidence/false-polarity.packet.metta" "${bnf_admission[@]}"
sed 's/bnf-validate-grammar-v1/bnf-validate-grammar-stale-control-v1/g' "${bnf_sources[4]}" \
    > "$bnf_evidence/stale-source.metta"
bnf_changed "${bnf_sources[4]}" "$bnf_evidence/stale-source.metta"
bnf_source_args[9]="$bnf_evidence/stale-source.metta"
bnf_reject stale-source 'native-type source composition differs' "${bnf_typed[@]}" "${bnf_admission[@]}"

sha256sum -c "$bnf_evidence/inputs.sha256" > "$bnf_evidence/inputs-after.txt"
sha256sum "$bnf_packet" "$bnf_evidence/raw.metta" "$bnf_evidence/admitted.metta" \
    "$bnf_evidence/string-carrier.metta" > "$bnf_evidence/artifacts.sha256"
echo '(GsltAdmittedCompilerV1 passed)'
