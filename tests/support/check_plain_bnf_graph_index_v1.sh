#!/bin/sh
set -eu
if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo 'usage: check_plain_bnf_graph_index_v1.sh COMPILER RUNTIME EVIDENCE_DIRECTORY [indexed|worklist]' >&2
    exit 2
fi
compiler=$(realpath "$1")
runtime=$(realpath "$2")
selected=${4:-indexed}
case $selected in indexed|worklist) :;; *) echo 'invalid candidate graph route' >&2; exit 2;; esac
mkdir -p "$3"
evidence=$(realpath "$3")
test ! -e "$evidence/program.metta"
root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
sources='experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta
experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta
experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta
experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta
langdef/bnf/plain_bnf_semantic_admission_v1.metta
langdef/bnf/plain_bnf_graph_analysis_v1.metta
langdef/bnf/plain_bnf_graph_index_v1.metta'
if [ "$selected" = worklist ]; then
    sources="$sources
langdef/bnf/plain_bnf_graph_discovery_v1.metta"
fi
set --
for source in $sources; do set -- "$@" --source "$source"; done
if [ "$selected" = worklist ]; then
    set -- "$@" --closed-entry-mode BNFAnalyzeDocumentWorklistV1:1110 \
        --closed-entry-mode BNFValidateGrammarDiscoveryV1:10 \
        --closed-entry-mode BNFDiscoveryRankNextV1:10 \
        --closed-entry-mode BNFDiscoveryRankCompareV1:110 \
        --closed-entry-mode BNFDiscoveryHeapMergeV1:110 \
        --closed-entry-mode BNFDiscoveryHeapCombineV1:10 \
        --closed-entry-mode BNFGraphTriePutV1:1110
fi
# The fixed source manifest above is whitespace-separated and contains no globs.
# shellcheck disable=SC2086
sha256sum "$compiler" "$runtime" $sources lib/lib_bnf.metta lib/langdef.metta \
    langdef/bnf/*.metta langdef/bnf/*.bnf tests/langdef/bnf/*.bnf \
    langdef/petta/generated/plain_bnf_semantic_admission_v1.metta \
    tests/langdef/bnf/plain_bnf_graph_index_v1.metta \
    tests/langdef/bnf/plain_bnf_graph_trie_v1.metta \
    tests/support/check_plain_bnf_graph_index_v1.sh \
    > "$evidence/inputs.sha256"
if [ "$selected" = worklist ]; then
    sha256sum tests/langdef/bnf/plain_bnf_graph_discovery_v1.metta \
        tests/langdef/bnf/plain_bnf_graph_discovery_v1.expected \
        tests/langdef/bnf/plain_bnf_discovery_validation_v1.metta \
        tests/langdef/bnf/plain_bnf_discovery_validation_v1.expected \
        tests/langdef/bnf/plain_bnf_discovery_queue_v1.metta \
        tests/langdef/bnf/plain_bnf_discovery_queue_v1.expected >> "$evidence/inputs.sha256"
fi
for output in program regenerated; do
    "$compiler" petta-direct "$@" \
        --closed-entry-mode BNFValidateGrammarV1:10 \
        --closed-entry-mode BNFAnalyzeDocumentV1:1110 \
        --closed-entry-mode BNFAnalyzeDocumentIndexedV1:1110 \
        --closed-entry-mode BNFGraphTrieLookupV1:110 \
        --closed-entry-mode BNFGraphTrieInsertFirstV1:1110 \
        --closed-entry-mode BNFNameLookupV1:110 \
        --closed-entry-mode BNFAppendNameV1:110 \
        --closed-entry-mode BNFIndexedNamesObserveV1:10 \
        --out "$evidence/$output.metta" \
        > "$evidence/$output.compile.txt" 2> "$evidence/$output.compile.stderr"
    test ! -s "$evidence/$output.compile.stderr"
done
cmp "$evidence/program.metta" "$evidence/regenerated.metta"
# Test-only controls over the current orchestration: select the original
# declaration/graph entries and remove the checked-call selection, but keep
# the public structural refusal and resource boundary. No algorithms are
# implemented here and no generated equations are patched.
# Dollar names below are MeTTa variables, not shell variables.
# shellcheck disable=SC2016
sed -e 's/gslt:admitted-entry:BNFValidateGrammarDiscoveryV1:10 $language $input/gslt:entry:BNFValidateGrammarV1:10 $input/' \
    -e 's/BNFValidateGrammarDiscoveryV1/BNFValidateGrammarV1/g' \
    -e 's/BNFAnalyzeDocumentWorklistV1/BNFAnalyzeDocumentV1/g' \
    lib/lib_bnf.metta > "$evidence/reference-library.metta"
if rg -q 'gslt:admitted-entry:|BNFValidateGrammarDiscoveryV1|BNFAnalyzeDocumentWorklistV1' \
    "$evidence/reference-library.metta"; then
    echo 'reference orchestration retained a candidate entry' >&2
    exit 1
fi
if [ "$selected" = worklist ]; then
    sed -e 's/BNFValidateGrammarV1/BNFValidateGrammarDiscoveryV1/g' \
        -e 's/BNFAnalyzeDocumentV1/BNFAnalyzeDocumentWorklistV1/g' \
        "$evidence/reference-library.metta" > "$evidence/candidate-library.metta"
else
    cp "$evidence/reference-library.metta" "$evidence/candidate-library.metta"
fi
sha256sum "$evidence/reference-library.metta" "$evidence/candidate-library.metta" \
    >> "$evidence/inputs.sha256"
fixtures='graph_trie graph_index admission_oracle public_outcomes public_entry edit_document lexical_domain'
if [ "$selected" = worklist ]; then fixtures="$fixtures graph_discovery discovery_queue discovery_validation"; fi
for fixture in $fixtures; do
    sha256sum "tests/langdef/bnf/plain_bnf_${fixture}_v1.metta" \
        "tests/langdef/bnf/plain_bnf_${fixture}_v1.expected" >> "$evidence/inputs.sha256"
done
sha256sum tests/langdef/bnf/plain_bnf_denotation_v1.metta >> "$evidence/inputs.sha256"
count=0
for fixture in $fixtures; do
    sed -e "s|../../../lib/lib_bnf.metta|$evidence/reference-library.metta|" \
        "tests/langdef/bnf/plain_bnf_${fixture}_v1.metta" > "$evidence/reference-$fixture.metta"
    sha256sum "$evidence/reference-$fixture.metta" >> "$evidence/inputs.sha256"
    CETTA_PETTA_SEARCH_MACHINE=1 "$runtime" --lang petta \
        "$evidence/program.metta" "$evidence/reference-$fixture.metta" \
        > "$evidence/$fixture.out" 2> "$evidence/$fixture.stderr"
    test ! -s "$evidence/$fixture.stderr"
    diff -u "tests/langdef/bnf/plain_bnf_${fixture}_v1.expected" "$evidence/$fixture.out"
    count=$((count + 1))
done
if [ "$selected" = worklist ]; then
    # The independently specified diagnostic outputs also apply to the new
    # collector. In particular, equal duplicate spans remain two occurrences.
    sed 's/BNFValidateGrammarV1/BNFValidateGrammarDiscoveryV1/g' \
        tests/langdef/bnf/plain_bnf_admission_oracle_v1.metta \
        > "$evidence/discovery-admission-oracle.metta"
    CETTA_PETTA_SEARCH_MACHINE=1 "$runtime" --lang petta \
        "$evidence/program.metta" "$evidence/discovery-admission-oracle.metta" \
        > "$evidence/discovery-admission-oracle.out" 2> "$evidence/discovery-admission-oracle.stderr"
    test ! -s "$evidence/discovery-admission-oracle.stderr"
    diff -u tests/langdef/bnf/plain_bnf_admission_oracle_v1.expected \
        "$evidence/discovery-admission-oracle.out"
    count=$((count + 1))
    # Loading additional raw entries does not itself select the worklist.
    if rg -q 'BNFValidateGrammarV1|BNFAnalyzeDocumentV1' "$evidence/candidate-library.metta"; then
        echo 'candidate orchestration retained a reference entry' >&2
        exit 1
    fi
    for fixture in public_outcomes public_entry edit_document lexical_domain denotation; do
        sed -e "s|../../../lib/lib_bnf.metta|$evidence/candidate-library.metta|" \
            "tests/langdef/bnf/plain_bnf_${fixture}_v1.metta" \
            > "$evidence/candidate-$fixture.metta"
        sha256sum "$evidence/candidate-$fixture.metta" >> "$evidence/inputs.sha256"
        if [ "$fixture" != denotation ]; then
            CETTA_PETTA_SEARCH_MACHINE=1 "$runtime" --lang petta \
                "$evidence/program.metta" "$evidence/candidate-$fixture.metta" \
                > "$evidence/candidate-$fixture.out" 2> "$evidence/candidate-$fixture.stderr"
            test ! -s "$evidence/candidate-$fixture.stderr"
            diff -u "tests/langdef/bnf/plain_bnf_${fixture}_v1.expected" \
                "$evidence/candidate-$fixture.out"
            count=$((count + 1))
        fi
    done
fi
if [ "$selected" = indexed ]; then
    sed -e "s|../../../lib/lib_bnf.metta|$evidence/candidate-library.metta|" \
        tests/langdef/bnf/plain_bnf_denotation_v1.metta > "$evidence/candidate-denotation.metta"
fi
for route in public candidate; do
    program=langdef/petta/generated/plain_bnf_semantic_admission_v1.metta
    fixture=tests/langdef/bnf/plain_bnf_denotation_v1.metta
    if [ "$route" = candidate ]; then
        program="$evidence/program.metta"
        fixture="$evidence/candidate-denotation.metta"
    fi
    CETTA_PETTA_SEARCH_MACHINE=1 "$runtime" --lang petta "$program" \
        "$fixture" \
        > "$evidence/denotation-$route.out" 2> "$evidence/denotation-$route.stderr"
    test ! -s "$evidence/denotation-$route.stderr"
    if rg -q '^[(](Error|assertEqual|PlainBnfUnexpected)' "$evidence/denotation-$route.out"; then
        echo 'denotation reported an error or unexpected result' >&2
        exit 1
    fi
    rg -F -x -q PlainBnfSecondStageDenotationFixedPointExactV1 "$evidence/denotation-$route.out"
done
cmp "$evidence/denotation-public.out" "$evidence/denotation-candidate.out"
sha256sum -c "$evidence/inputs.sha256" > "$evidence/inputs-after.txt"
printf '(PlainBnfGraphIndexCandidateV1Summary %s %s 0)\n' "$selected" "$count" \
    | tee "$evidence/summary.txt"
