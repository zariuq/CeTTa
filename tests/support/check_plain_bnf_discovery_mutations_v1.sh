#!/bin/sh
set -eu
if [ "$#" -ne 3 ]; then
    echo 'usage: check_plain_bnf_discovery_mutations_v1.sh COMPILER RUNTIME EVIDENCE_DIRECTORY' >&2
    exit 2
fi
compiler=$(realpath "$1")
runtime=$(realpath "$2")
mkdir -p "$3"
evidence=$(realpath "$3")
test ! -e "$evidence/missing-reverse.source.metta"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
source=langdef/bnf/plain_bnf_graph_discovery_v1.metta
sha256sum "$source" "$compiler" "$runtime" \
    tests/langdef/bnf/plain_bnf_graph_discovery_v1.metta \
    tests/langdef/bnf/plain_bnf_graph_discovery_v1.expected \
    tests/support/check_plain_bnf_discovery_mutations_v1.sh > "$evidence/inputs.sha256"
# Mutate authored source and regenerate. Never patch emitted MeTTa.
sed 's/BNFDiscoveryProductiveV1 ?ranked ?reverse ?lexicals ?productiveIndex/BNFDiscoveryProductiveV1 ?ranked BNFGraphTrieEmptyV1 ?lexicals ?productiveIndex/' \
    "$source" > "$evidence/missing-reverse.source.metta"
sed '/(rule bnf-discovery-schedule-earlier-v1/,/ScheduleNextV1/s/BNFDiscoveryScheduleNextV1/BNFDiscoveryScheduleCurrentV1/' \
    "$source" > "$evidence/wrong-round.source.metta"
sed 's/(BNFDiscoveryHeapCombineV1 ?children ?remaining)/(BNFDiscoveryHeapMergeV1 ?children BNFDiscoveryHeapNilV1 ?remaining)/' \
    "$source" > "$evidence/lost-siblings.source.metta"
set --
for input in experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta \
    experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta \
    experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta \
    experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta \
    langdef/bnf/plain_bnf_semantic_admission_v1.metta langdef/bnf/plain_bnf_graph_analysis_v1.metta \
    langdef/bnf/plain_bnf_graph_index_v1.metta; do
    set -- "$@" --source "$input"
    sha256sum "$input" >> "$evidence/inputs.sha256"
done
for mutation in missing-reverse wrong-round lost-siblings; do
    ! cmp -s "$source" "$evidence/$mutation.source.metta"
    "$compiler" petta-direct "$@" --source "$evidence/$mutation.source.metta" \
        --closed-entry-mode BNFValidateGrammarV1:10 \
        --closed-entry-mode BNFAnalyzeDocumentV1:1110 \
        --closed-entry-mode BNFAnalyzeDocumentWorklistV1:1110 \
        --out "$evidence/$mutation.program.metta" \
        > "$evidence/$mutation.compile.txt" 2> "$evidence/$mutation.compile.stderr"
    test ! -s "$evidence/$mutation.compile.stderr"
    CETTA_PETTA_SEARCH_MACHINE=1 "$runtime" --lang petta \
        "$evidence/$mutation.program.metta" tests/langdef/bnf/plain_bnf_graph_discovery_v1.metta \
        > "$evidence/$mutation.out" 2> "$evidence/$mutation.stderr"
    test ! -s "$evidence/$mutation.stderr"
    ! cmp -s tests/langdef/bnf/plain_bnf_graph_discovery_v1.expected "$evidence/$mutation.out"
    rg -q '^[(]GraphDiscoveryDifference ' "$evidence/$mutation.out"
    ! rg -q '^[(](Error|GraphDiscoveryReadRefused|GraphDiscoveryInputRefused|GraphDiscoveryDeclarationRefused)' "$evidence/$mutation.out"
done
sha256sum -c "$evidence/inputs.sha256" > "$evidence/inputs-after.txt"
printf '(PlainBnfDiscoveryMutationsV1Summary 3 0)\n'
