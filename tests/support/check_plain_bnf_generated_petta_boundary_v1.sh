#!/usr/bin/env bash
set -euo pipefail
if (( $# != 4 )); then
    echo 'usage: check_plain_bnf_generated_petta_boundary_v1.sh COMPILER CETTA_RUNTIME SWI_PETTA_DIRECTORY EVIDENCE_DIRECTORY' >&2
    exit 2
fi
boundary_compiler=$(realpath "$1")
boundary_runtime=$(realpath "$2")
boundary_swi=$(realpath "$3")
boundary_python_libdir=${PYTHON312_LIBDIR:-/dev/null}
mkdir -p "$4"
boundary_evidence=$(realpath "$4")
test ! -e "$boundary_evidence/inputs.sha256"
boundary_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
cd "$boundary_root"
boundary_fixture=tests/langdef/bnf/plain_bnf_generated_petta_boundary_v1.metta
boundary_occurrences=tests/langdef/bnf/plain_bnf_generated_petta_occurrences_v1.metta
boundary_sources=(
    experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta
    langdef/bnf/plain_bnf_semantic_admission_v1.metta
    langdef/bnf/plain_bnf_graph_analysis_v1.metta
    langdef/bnf/plain_bnf_graph_index_v1.metta
    langdef/bnf/plain_bnf_graph_discovery_v1.metta)
boundary_modes=(
    BNFGraphTrieLookupV1:110 BNFGraphTrieInsertFirstV1:1110
    BNFDiscoveryReverseDefinitionsV1:110 BNFDiscoveryCollectDefinitionsV1:1000)
sha256sum "$boundary_compiler" "$boundary_runtime" "$boundary_swi/run.sh" \
    "$boundary_swi/src/translator.pl" "$boundary_swi/src/metta.pl" \
    "$boundary_swi/src/spaces.pl" "$boundary_swi/src/filereader.pl" \
    "$(command -v swipl)" "${boundary_sources[@]}" "$boundary_fixture" "$boundary_occurrences" \
    tests/langdef/bnf/plain_bnf_generated_petta_*v1.expected "$0" \
    > "$boundary_evidence/inputs.sha256"
git -C "$boundary_swi" rev-parse HEAD > "$boundary_evidence/swi-revision.txt"
swipl --version > "$boundary_evidence/swi-version.txt"
printf '%s\n' "$boundary_python_libdir" > "$boundary_evidence/swi-python-libdir.txt"
if [[ -r $boundary_python_libdir/libpython3.12.so.1.0 ]]; then
    # Upstream SWI-PeTTa loads Janus even though this fixture makes no Python call.
    sha256sum "$boundary_python_libdir/libpython3.12.so.1.0" >> "$boundary_evidence/inputs.sha256"
fi

boundary_compile() {
    local boundary_name=$1 boundary_index=$2
    local boundary_source boundary_mode
    local boundary_args=()
    for boundary_source in "${boundary_sources[@]}"; do
        if [[ $boundary_source == langdef/bnf/plain_bnf_graph_index_v1.metta ]]; then
            boundary_args+=(--source "$boundary_index")
        else
            boundary_args+=(--source "$boundary_source")
        fi
    done
    for boundary_mode in "${boundary_modes[@]}"; do
        boundary_args+=(--closed-entry-mode "$boundary_mode")
    done
    "$boundary_compiler" petta-direct "${boundary_args[@]}" \
        --out "$boundary_evidence/$boundary_name.generated.metta" \
        > "$boundary_evidence/$boundary_name.compile.stdout" \
        2> "$boundary_evidence/$boundary_name.compile.stderr"
    test ! -s "$boundary_evidence/$boundary_name.compile.stderr"
    # These are the real closed generated entries, without native handle imports.
    if rg -q 'import!|gslt:admitted-entry:' "$boundary_evidence/$boundary_name.generated.metta"; then
        echo 'unexpected native library boundary in selected generated family' >&2
        exit 1
    fi
}

boundary_run() {
    local boundary_name=$1 boundary_client=$2 boundary_expected=$3 boundary_status
    # SWI main accepts one file. Concatenate whole files without changing a
    # generated equation, so both engines see exactly the same installed program.
    awk '1' "$boundary_evidence/$boundary_name.generated.metta" "$boundary_client" \
        > "$boundary_evidence/$boundary_name.run.metta"
    sha256sum "$boundary_evidence/$boundary_name.generated.metta" \
        "$boundary_evidence/$boundary_name.run.metta" \
        >> "$boundary_evidence/artifacts.sha256"
    if CETTA_PETTA_SEARCH_MACHINE=1 "$boundary_runtime" --lang petta \
        "$boundary_evidence/$boundary_name.run.metta" \
        > "$boundary_evidence/$boundary_name.cetta.stdout" \
        2> "$boundary_evidence/$boundary_name.cetta.stderr"; then
        boundary_status=0
    else boundary_status=$?; fi
    printf '%s\n' "$boundary_status" > "$boundary_evidence/$boundary_name.cetta.exit"
    test "$boundary_status" = 0
    # Avoid run.sh's optional Python subprocess used only to discover LIBDIR.
    # No Python or foreign operation occurs in the generated component program.
    if PYTHON312_LIBDIR="$boundary_python_libdir" \
        bash "$boundary_swi/run.sh" --silent "$boundary_evidence/$boundary_name.run.metta" \
        > "$boundary_evidence/$boundary_name.swi.stdout" \
        2> "$boundary_evidence/$boundary_name.swi.stderr"; then
        boundary_status=0
    else boundary_status=$?; fi
    printf '%s\n' "$boundary_status" > "$boundary_evidence/$boundary_name.swi.exit"
    test "$boundary_status" = 0
    test ! -s "$boundary_evidence/$boundary_name.cetta.stderr"
    test ! -s "$boundary_evidence/$boundary_name.swi.stderr"
    diff -u "$boundary_expected" "$boundary_evidence/$boundary_name.cetta.stdout" \
        > "$boundary_evidence/$boundary_name.expected.diff"
    diff -u "$boundary_evidence/$boundary_name.cetta.stdout" \
        "$boundary_evidence/$boundary_name.swi.stdout" \
        > "$boundary_evidence/$boundary_name.differential.diff"
}

boundary_compile base langdef/bnf/plain_bnf_graph_index_v1.metta
boundary_compile regenerated langdef/bnf/plain_bnf_graph_index_v1.metta
cmp "$boundary_evidence/base.generated.metta" "$boundary_evidence/regenerated.generated.metta"
boundary_run base "$boundary_fixture" tests/langdef/bnf/plain_bnf_generated_petta_boundary_v1.expected

for boundary_case in equal ordered; do
    # Duplicate one complete actual source rule, changing its rule name only
    # (equal) or additionally its output (ordered), then compile afresh.
    awk -v mutation="$boundary_case" '
        /^    \(rule bnf-graph-trie-lookup-empty-v1$/ {
            if (active || found) exit 3
            active = 1; found++; saved = ""; depth = 0
        }
        {
            print
            if (active) {
                saved = saved $0 "\n"
                line = $0; opens = gsub(/\(/, "", line)
                line = $0; closes = gsub(/\)/, "", line)
                depth += opens - closes
                if (depth == 0) {
                    active = 0
                    if (gsub(/bnf-graph-trie-lookup-empty-v1/,
                        "bnf-graph-trie-lookup-empty-mutation-v1", saved) != 1) exit 4
                    if (mutation == "ordered" &&
                        gsub(/BNFIndexMissingV1/, "(BNFIndexFoundV1 MutationSecond)", saved) != 1) exit 5
                    printf "%s", saved
                }
            }
        }
        END { if (active || found != 1) exit 6 }
    ' langdef/bnf/plain_bnf_graph_index_v1.metta \
        > "$boundary_evidence/$boundary_case.source.metta"
    sha256sum "$boundary_evidence/$boundary_case.source.metta" \
        >> "$boundary_evidence/artifacts.sha256"
    boundary_compile "$boundary_case" "$boundary_evidence/$boundary_case.source.metta"
    rg -q '^; gslt-binding-mode BNFGraphTrieLookupV1/3 .*functional=relational specialized=yes$' \
        "$boundary_evidence/$boundary_case.generated.metta"
    boundary_run "$boundary_case" "$boundary_occurrences" \
        "tests/langdef/bnf/plain_bnf_generated_petta_${boundary_case}_occurrences_v1.expected"
done
sha256sum -c "$boundary_evidence/inputs.sha256" > "$boundary_evidence/inputs-after.txt"
sha256sum -c "$boundary_evidence/artifacts.sha256" > "$boundary_evidence/artifacts-after.txt"
printf '(PlainBnfGeneratedPeTTaBoundaryV1Summary 3 0)\n' | tee "$boundary_evidence/summary.txt"
