#!/usr/bin/env bash
set -euo pipefail
if (( $# != 3 )); then
    echo 'usage: check_plain_bnf_admission_domain_v1.sh COMPILER LEAN_PROJECT EVIDENCE_DIRECTORY' >&2
    exit 2
fi
domain_compiler=$(realpath "$1")
domain_lean=$(realpath "$2")
domain_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
mkdir -p "$3"
domain_evidence=$(realpath "$3")
test ! -e "$domain_evidence/inputs.sha256"
cd "$domain_root"
domain_language=langdef/bnf/plain_bnf_grammar_v1.metta
domain_recipe=tests/support/generate_plain_bnf_typed_discovery_v1.sh
domain_sources=(
    experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta
    langdef/bnf/plain_bnf_semantic_admission_v1.metta
    langdef/bnf/plain_bnf_graph_analysis_v1.metta
    langdef/bnf/plain_bnf_graph_index_v1.metta
    langdef/bnf/plain_bnf_graph_discovery_v1.metta)
sha256sum "$domain_compiler" "$domain_language" "$domain_recipe" \
    tests/support/check_plain_bnf_admission_domain_v1.sh \
    tests/langdef/bnf/integer_provider_native_type_export_v1.lean \
    tests/langdef/bnf/generated/plain_bnf_typed_discovery_v1.metta \
    "${domain_sources[@]}" > "$domain_evidence/inputs.sha256"

domain_generate() {
    local label=$1
    shift
    bash "$domain_recipe" "$domain_compiler" "$domain_lean" \
        "$domain_evidence/$label.types.metta" "$domain_evidence/$label.program.metta" "$@" \
        > "$domain_evidence/$label.generate.log" 2>&1
}

# Change a single declared parameter, never the generated artifact. Keeping the
# Integer declaration intact distinguishes path transport from a mere scan for
# the existence of an Integer type anywhere in the schema.
domain_mutate() {
    local label=$1 rule=$2 parameter=$3 before=$4 after=$5
    awk -v rule="$rule" -v parameter="$parameter" -v before="$before" -v after="$after" '
        /\(GrammarRule / { active = index($0, "(GrammarRule \"" rule "\"") != 0 }
        {
            if (active && index($0, "(TermSimple \"" parameter "\"")) {
                old = "(TBase \"" before "\")"
                position = index($0, old)
                if (!position) exit 3
                $0 = substr($0, 1, position - 1) "(TBase \"" after "\")" substr($0, position + length(old))
                changed++
            }
            print
        }
        END { if (changed != 1) exit 4 }
    ' "$domain_language" > "$domain_evidence/$label.language.metta"
    if cmp -s "$domain_language" "$domain_evidence/$label.language.metta"; then
        echo 'domain mutation did not change the declared parameter' >&2
        exit 1
    fi
    rg -Fq '(TypeDecl "Integer" CarrierBuiltinInt)' "$domain_evidence/$label.language.metta"
    domain_generate "$label" "$domain_evidence/$label.language.metta"
    cmp "$domain_evidence/base.types.metta" "$domain_evidence/$label.types.metta"
    # Require changed executable control, not a changed descriptive label alone.
    test "$(rg -c '^; gslt-binding-mode BNFScalarOrderDiagnosticV1/4 input=1110 success=1111 functional=relational specialized=yes$' \
        "$domain_evidence/$label.program.metta")" -eq 2
    rg -Fq '(superpose (collapse (admitted:gslt:mode:BNFScalarOrderDiagnosticV1:1110 ' \
        "$domain_evidence/$label.program.metta"
    if rg -Fq '(= (admitted:gslt:fn:BNFScalarOrderDiagnosticV1:1110 ' \
        "$domain_evidence/$label.program.metta"; then
        echo 'missing input-domain path incorrectly licensed typed scalar control' >&2
        exit 1
    fi
    printf '%s\tno-integer-specialization\n' "$label" >> "$domain_evidence/observations.tsv"
}

domain_generate base
cmp tests/langdef/bnf/generated/plain_bnf_typed_discovery_v1.metta "$domain_evidence/base.program.metta"
rg -Fq '(once (admitted:gslt:fn:BNFScalarOrderDiagnosticV1:1110 ' "$domain_evidence/base.program.metta"
printf 'base\ttyped-scalar-control\n' > "$domain_evidence/observations.tsv"
domain_mutate scalar-head-string bnf-v1:scalars-cons head Integer String
domain_mutate scalar-tail-text bnf-v1:scalars-cons tail BnfScalarList BnfText
domain_mutate authority-lexical-text bnf-v1:grammar-authority lexical BnfLexicalEnvironment BnfText
domain_mutate matcher-points-text bnf-v1:lexical-points scalars BnfScalarList BnfText
sha256sum -c "$domain_evidence/inputs.sha256" > "$domain_evidence/inputs-after.log"
printf '(PlainBnfAdmissionDomainV1Summary baseline=1 broken-paths=4)\n'
