#!/usr/bin/env bash
set -euo pipefail
if (( $# != 4 && $# != 5 )); then
    echo 'usage: generate_plain_bnf_typed_discovery_v1.sh COMPILER LEAN_PROJECT NATIVE_TYPES_OUTPUT PROGRAM_OUTPUT [ADMISSION_LANGUAGE]' >&2
    exit 2
fi
typed_compiler=$(realpath "$1")
typed_lean=$(realpath "$2")
typed_types=$(realpath -m "$3")
typed_output=$(realpath -m "$4")
typed_script=$(realpath "$0")
typed_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
cd "$typed_root"
typed_language=${5:-langdef/bnf/plain_bnf_grammar_v1.metta}
test -f "$typed_language"
typed_sources=(
    experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta
    langdef/bnf/plain_bnf_semantic_admission_v1.metta
    langdef/bnf/plain_bnf_graph_analysis_v1.metta
    langdef/bnf/plain_bnf_graph_index_v1.metta
    langdef/bnf/plain_bnf_graph_discovery_v1.metta)
typed_modes=(
    BNFAnalyzeDocumentWorklistV1:1110 BNFValidateGrammarDiscoveryV1:10
    BNFDiscoveryRankNextV1:10 BNFDiscoveryRankCompareV1:110
    BNFDiscoveryHeapMergeV1:110 BNFDiscoveryHeapCombineV1:10 BNFGraphTriePutV1:1110
    BNFValidateGrammarV1:10 BNFAnalyzeDocumentV1:1110 BNFAnalyzeDocumentIndexedV1:1110
    BNFGraphTrieLookupV1:110 BNFGraphTrieInsertFirstV1:1110
    BNFNameLookupV1:110 BNFAppendNameV1:110 BNFIndexedNamesObserveV1:10)
typed_args=()
typed_absolute_sources=()
for typed_source in "${typed_sources[@]}"; do
    typed_args+=(--source "$typed_source")
    typed_absolute_sources+=("$typed_root/$typed_source")
done
typed_protected=("$typed_compiler" "$typed_root/tests/langdef/bnf/integer_provider_native_type_export_v1.lean"
    "$typed_root/langdef/bnf/plain_bnf_grammar_v1.metta" "$typed_language" "$typed_script"
    "${typed_absolute_sources[@]}")
if [[ "$typed_types" == "$typed_output" ]]; then
    echo 'native-type and program outputs must be distinct' >&2
    exit 2
fi
for typed_destination in "$typed_types" "$typed_output"; do
    for typed_input in "${typed_protected[@]}"; do
        if [[ "$typed_destination" == "$(realpath "$typed_input")" ]] ||
           [[ -e "$typed_destination" && "$typed_destination" -ef "$typed_input" ]]; then
            echo 'output must not alias a generation input' >&2
            exit 2
        fi
    done
done
if [[ -e "$typed_types" && -e "$typed_output" && "$typed_types" -ef "$typed_output" ]]; then
    echo 'native-type and program outputs must not alias' >&2
    exit 2
fi
for typed_mode in "${typed_modes[@]}"; do
    typed_args+=(--closed-entry-mode "$typed_mode")
done
# This is a generation input, not a post-hoc qualification receipt. Rebuild
# its transitive Lean dependencies before computing the packet from sources.
(cd "$typed_lean" && lake build Mettapedia.GSLT.Parsing.IntegerProviderNativeTypeExport) >&2
# Publish complete files only. A failed export or lowering must not truncate
# the installed program. Failed intermediate files remain available to inspect.
typed_staged_types=$(mktemp "$typed_types.new.XXXXXX")
typed_staged_output=$(mktemp "$typed_output.new.XXXXXX")
(cd "$typed_lean" && lake env lean --run \
    "$typed_root/tests/langdef/bnf/integer_provider_native_type_export_v1.lean" \
    "${typed_absolute_sources[@]}") > "$typed_staged_types"
"$typed_compiler" petta-direct "${typed_args[@]}" \
    --native-types "$typed_staged_types" \
    --admission-language "$typed_language" \
    --admission-entry BNFValidateGrammarDiscoveryV1:BnfGrammarInput \
    --out "$typed_staged_output"
# Preserve installed permissions; fresh data files follow the caller's umask
# instead of inheriting mktemp's private-only mode.
if [[ -e "$typed_types" ]]; then
    chmod --reference="$typed_types" "$typed_staged_types"
else
    chmod =rw "$typed_staged_types"
fi
if [[ -e "$typed_output" ]]; then
    chmod --reference="$typed_output" "$typed_staged_output"
else
    chmod =rw "$typed_staged_output"
fi
mv -- "$typed_staged_types" "$typed_types"
mv -- "$typed_staged_output" "$typed_output"
