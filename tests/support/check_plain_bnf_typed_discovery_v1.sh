#!/usr/bin/env bash
set -euo pipefail
if (( $# != 4 )); then
    echo 'usage: check_plain_bnf_typed_discovery_v1.sh COMPILER RUNTIME LEAN_PROJECT EVIDENCE_DIRECTORY' >&2
    exit 2
fi
bnf_root=$(pwd -P)
bnf_compiler=$(realpath "$1")
bnf_runtime=$(realpath "$2")
bnf_lean=$(realpath "$3")
mkdir -p "$4"
bnf_evidence=$(realpath "$4")
test ! -e "$bnf_evidence/inputs.sha256"
bnf_exporter="$bnf_root/tests/langdef/bnf/integer_provider_native_type_export_v1.lean"
bnf_adapter="$bnf_root/tests/langdef/bnf/plain_bnf_typed_discovery_orchestration_v1.metta"
bnf_language="$bnf_root/langdef/bnf/plain_bnf_grammar_v1.metta"
bnf_langdef_library=$(realpath "$bnf_root/lib/langdef.metta")
bnf_ownership="$bnf_root/tests/langdef/bnf/plain_bnf_typed_discovery_ownership_v1.metta"
bnf_sources=(
    experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta
    experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta
    langdef/bnf/plain_bnf_semantic_admission_v1.metta
    langdef/bnf/plain_bnf_graph_analysis_v1.metta
    langdef/bnf/plain_bnf_graph_index_v1.metta
    langdef/bnf/plain_bnf_graph_discovery_v1.metta)
bnf_modes=(
    BNFAnalyzeDocumentWorklistV1:1110 BNFValidateGrammarDiscoveryV1:10
    BNFDiscoveryRankNextV1:10 BNFDiscoveryRankCompareV1:110
    BNFDiscoveryHeapMergeV1:110 BNFDiscoveryHeapCombineV1:10 BNFGraphTriePutV1:1110
    BNFValidateGrammarV1:10 BNFAnalyzeDocumentV1:1110 BNFAnalyzeDocumentIndexedV1:1110
    BNFGraphTrieLookupV1:110 BNFGraphTrieInsertFirstV1:1110
    BNFNameLookupV1:110 BNFAppendNameV1:110 BNFIndexedNamesObserveV1:10)
bnf_args=()
bnf_absolute_sources=()
for bnf_source in "${bnf_sources[@]}"; do
    bnf_args+=(--source "$bnf_source")
    bnf_absolute_sources+=("$bnf_root/$bnf_source")
done
for bnf_mode in "${bnf_modes[@]}"; do bnf_args+=(--closed-entry-mode "$bnf_mode"); done
sha256sum "$bnf_compiler" "$bnf_runtime" "$bnf_exporter" "$bnf_adapter" "$bnf_language" "$bnf_ownership" \
    "${bnf_sources[@]}" lib/lib_bnf.metta "$bnf_langdef_library" "$0" \
    > "$bnf_evidence/inputs.sha256"

# Qualify the existing source-derived scalable candidate first. This is also
# the independent raw-prefix and complete public-output comparison input.
bash tests/support/check_plain_bnf_graph_index_v1.sh "$bnf_compiler" "$bnf_runtime" \
    "$bnf_evidence/raw" worklist > "$bnf_evidence/raw-gate.log" 2>&1
(cd "$bnf_lean" && lake env lean --run "$bnf_exporter" "${bnf_absolute_sources[@]}") \
    > "$bnf_evidence/native-types.metta" 2> "$bnf_evidence/export.stderr"
bnf_args+=(--native-types "$bnf_evidence/native-types.metta"
    --admission-language "$bnf_language"
    --admission-entry BNFValidateGrammarDiscoveryV1:BnfGrammarInput)
for bnf_output in program regenerated; do
    "$bnf_compiler" petta-direct "${bnf_args[@]}" --out "$bnf_evidence/$bnf_output.metta" \
        > "$bnf_evidence/$bnf_output.compile.txt" 2> "$bnf_evidence/$bnf_output.compile.stderr"
    test ! -s "$bnf_evidence/$bnf_output.compile.stderr"
done
cmp "$bnf_evidence/program.metta" "$bnf_evidence/regenerated.metta"
bnf_raw_size=$(wc -c < "$bnf_evidence/raw/program.metta")
head -c "$bnf_raw_size" "$bnf_evidence/program.metta" > "$bnf_evidence/raw-prefix.metta"
cmp "$bnf_evidence/raw/program.metta" "$bnf_evidence/raw-prefix.metta"
tail -c "+$((bnf_raw_size + 2))" "$bnf_evidence/program.metta" > "$bnf_evidence/typed-suffix.metta"
rg -q '^; gslt-binding-mode BNFScalarOrderDiagnosticV1/4 .*functional=semidet specialized=yes$' \
    "$bnf_evidence/typed-suffix.metta"
rg -Fq '(admitted:gslt:fn:BNFScalarOrderDiagnosticV1:1110 ' "$bnf_evidence/typed-suffix.metta"
if rg -Fq '(superpose (collapse (admitted:gslt:mode:BNFScalarOrderDiagnosticV1:1110 ' \
    "$bnf_evidence/typed-suffix.metta"; then
    echo 'typed discovery retained the untyped scalar-call wrapper' >&2
    exit 1
fi

# The checked route is now the public implementation. Preserve an exact
# snapshot for source-mutation probes; no replacement orchestration is added.
cp lib/lib_bnf.metta "$bnf_evidence/candidate-library.metta"
cmp lib/lib_bnf.metta "$bnf_evidence/candidate-library.metta"
test "$(rg -c '^\(= \(bnf:validate-document ' "$bnf_evidence/candidate-library.metta")" = 1
sha256sum "$bnf_evidence/candidate-library.metta" >> "$bnf_evidence/inputs.sha256"
bnf_count=0
CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta "$bnf_evidence/program.metta" \
    "$bnf_adapter" > "$bnf_evidence/installed-orchestration.out" \
    2> "$bnf_evidence/installed-orchestration.stderr"
test ! -s "$bnf_evidence/installed-orchestration.stderr"
printf 'PlainBnfInstalledTypedParserV1\nPlainBnfInstalledTypedMalformedRefusedV1\n' \
    > "$bnf_evidence/installed-orchestration.expected"
# Native-module registration may return an initial acknowledgment. It is not
# one of the two grammar-operation observations.
sed '1{/^true$/d;}' "$bnf_evidence/installed-orchestration.out" \
    > "$bnf_evidence/installed-orchestration.observation"
cmp "$bnf_evidence/installed-orchestration.expected" "$bnf_evidence/installed-orchestration.observation"
bnf_count=$((bnf_count + 1))
for bnf_fixture in public_outcomes public_entry edit_document lexical_domain denotation; do
    sed "s|$bnf_evidence/raw/candidate-library.metta|$bnf_evidence/candidate-library.metta|g" \
        "$bnf_evidence/raw/candidate-$bnf_fixture.metta" > "$bnf_evidence/candidate-$bnf_fixture.metta"
    CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta "$bnf_evidence/program.metta" \
        "$bnf_evidence/candidate-$bnf_fixture.metta" \
        > "$bnf_evidence/$bnf_fixture.out" 2> "$bnf_evidence/$bnf_fixture.stderr"
    test ! -s "$bnf_evidence/$bnf_fixture.stderr"
    bnf_expected="$bnf_evidence/raw/candidate-$bnf_fixture.out"
    if [[ $bnf_fixture == denotation ]]; then bnf_expected="$bnf_evidence/raw/denotation-candidate.out"; fi
    cmp "$bnf_expected" "$bnf_evidence/$bnf_fixture.out"
    bnf_count=$((bnf_count + 1))
done

# Compare complete lexical diagnostic payloads and their order, not only
# their public classification. Wrong-domain/authority fallback is also
# checked here at the generated entry, distinct from the public refusal gate.
sha256sum tests/langdef/bnf/plain_bnf_admitted_compiler_v1.metta \
    tests/langdef/bnf/plain_bnf_admitted_compiler_v1.expected \
    tests/langdef/bnf/plain_bnf_admission_oracle_v1.metta \
    tests/langdef/bnf/plain_bnf_admission_oracle_v1.expected \
    tests/langdef/bnf/plain_bnf_typed_discovery_oracle_v1.metta >> "$bnf_evidence/inputs.sha256"
sed -e 's/BNFValidateGrammarV1/BNFValidateGrammarDiscoveryV1/g' \
    -e "s|../../../lib/lib_bnf.metta|$bnf_evidence/candidate-library.metta|" \
    tests/langdef/bnf/plain_bnf_admitted_compiler_v1.metta \
    > "$bnf_evidence/ordered-lexicals.metta"
CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta "$bnf_evidence/program.metta" \
    "$bnf_evidence/ordered-lexicals.metta" \
    > "$bnf_evidence/ordered-lexicals.out" 2> "$bnf_evidence/ordered-lexicals.stderr"
test ! -s "$bnf_evidence/ordered-lexicals.stderr"
cmp tests/langdef/bnf/plain_bnf_admitted_compiler_v1.expected "$bnf_evidence/ordered-lexicals.out"
bnf_count=$((bnf_count + 1))

# Compare both complete answer streams against source-derived expectations,
# not just against one another. Repeated equal diagnostics and interleaved
# Unicode/order faults must retain every occurrence in the declared order.
bnf_scalar_oracle=tests/langdef/bnf/plain_bnf_typed_scalar_occurrences_v1.metta
bnf_scalar_expected=tests/langdef/bnf/plain_bnf_typed_scalar_occurrences_v1.expected
sha256sum "$bnf_scalar_oracle" "$bnf_scalar_expected" >> "$bnf_evidence/inputs.sha256"
CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta "$bnf_evidence/program.metta" \
    "$bnf_scalar_oracle" > "$bnf_evidence/scalar-occurrences.out" \
    2> "$bnf_evidence/scalar-occurrences.stderr"
test ! -s "$bnf_evidence/scalar-occurrences.stderr"
cmp "$bnf_scalar_expected" "$bnf_evidence/scalar-occurrences.out"
bnf_count=$((bnf_count + 1))

# The independently specified declaration oracle distinguishes first spans,
# definition order, and equal repeated diagnostic occurrences.
sed -e 's/BNFValidateGrammarV1/BNFValidateGrammarDiscoveryV1/g' \
    -e 's/gslt:entry:BNFValidateGrammarDiscoveryV1:10/typed-oracle:run/g' \
    tests/langdef/bnf/plain_bnf_admission_oracle_v1.metta > "$bnf_evidence/ordered-declarations.metta"
CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta "$bnf_evidence/program.metta" \
    tests/langdef/bnf/plain_bnf_typed_discovery_oracle_v1.metta \
    "$bnf_evidence/ordered-declarations.metta" \
    > "$bnf_evidence/ordered-declarations.out" 2> "$bnf_evidence/ordered-declarations.stderr"
test ! -s "$bnf_evidence/ordered-declarations.stderr"
sed '/^true$/d' "$bnf_evidence/ordered-declarations.out" > "$bnf_evidence/ordered-declarations.observation"
cmp tests/langdef/bnf/plain_bnf_admission_oracle_v1.expected "$bnf_evidence/ordered-declarations.observation"
bnf_count=$((bnf_count + 1))

# Require execution through the typed scalar family in a complete public path.
# Merely loading the larger program would leave all raw tests green too.
bnf_head=admitted:gslt:fn:BNFScalarOrderDiagnosticV1:1110
CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_QUERY_TRACE="$bnf_head" \
    "$bnf_runtime" --lang petta "$bnf_evidence/program.metta" \
    "$bnf_evidence/candidate-lexical_domain.metta" \
    > "$bnf_evidence/activation.out" 2> "$bnf_evidence/activation.trace"
cmp "$bnf_evidence/lexical_domain.out" "$bnf_evidence/activation.out"
rg -q "^\\[petta-query\\] transitions=[0-9]+ head=$bnf_head bindings=" "$bnf_evidence/activation.trace"
if rg -n -v "^\\[petta-query\\] transitions=[0-9]+ head=$bnf_head bindings=" "$bnf_evidence/activation.trace"; then
    echo 'unexpected diagnostic in typed discovery activation trace' >&2
    exit 1
fi

# Change the authored root, then export types and compile again. Never patch
# generated equations. These mutations exercise finite resource ownership and
# answer occurrences, not a new normative meaning for grammar admission.
for bnf_case in one zero two; do
    bnf_program="$bnf_evidence/program.metta"
    bnf_expected_count=1
    if [[ $bnf_case != one ]]; then
        bnf_mutant="$bnf_evidence/source-$bnf_case.metta"
        awk -v variant="$bnf_case" '
            /^    \(rule bnf-validate-grammar-discovery-v1$/ {
                if (active || found) exit 3
                active = 1; found = 1
            }
            {
                original = $0
                if (active) {
                    saved = saved original "\n"
                    line = original; opens = gsub(/\(/, "", line)
                    line = original; closes = gsub(/\)/, "", line)
                    depth += opens - closes
                    if (variant == "zero" &&
                        sub(/\(body \(BNFDiscoveryCollectDefinitionsV1/,
                            "(body (different ?entries ?entries)\n        (BNFDiscoveryCollectDefinitionsV1")) changes++
                }
                print
                if (active && depth == 0) {
                    active = 0
                    if (variant == "two") {
                        sub(/bnf-validate-grammar-discovery-v1/,
                            "bnf-validate-grammar-discovery-duplicate-v1", saved)
                        printf "%s", saved
                        changes++
                    }
                }
            }
            END { if (active || found != 1 || changes != 1) exit 4 }
        ' langdef/bnf/plain_bnf_graph_discovery_v1.metta > "$bnf_mutant"
        bnf_mutation_args=()
        bnf_mutation_sources=()
        for bnf_source in "${bnf_sources[@]:0:7}"; do
            bnf_mutation_args+=(--source "$bnf_source")
            bnf_mutation_sources+=("$bnf_root/$bnf_source")
        done
        bnf_mutation_args+=(--source "$bnf_mutant")
        bnf_mutation_sources+=("$bnf_mutant")
        for bnf_mode in "${bnf_modes[@]}"; do
            bnf_mutation_args+=(--closed-entry-mode "$bnf_mode")
        done
        (cd "$bnf_lean" && lake env lean --run "$bnf_exporter" "${bnf_mutation_sources[@]}") \
            > "$bnf_evidence/types-$bnf_case.metta" 2> "$bnf_evidence/export-$bnf_case.stderr"
        bnf_program="$bnf_evidence/program-$bnf_case.metta"
        "$bnf_compiler" petta-direct "${bnf_mutation_args[@]}" \
            --native-types "$bnf_evidence/types-$bnf_case.metta" \
            --admission-language "$bnf_language" \
            --admission-entry BNFValidateGrammarDiscoveryV1:BnfGrammarInput \
            --out "$bnf_program" > "$bnf_evidence/compile-$bnf_case.txt" \
            2> "$bnf_evidence/compile-$bnf_case.stderr"
        test ! -s "$bnf_evidence/compile-$bnf_case.stderr"
        bnf_expected_count=0
        if [[ $bnf_case == two ]]; then bnf_expected_count=2; fi
        sha256sum "$bnf_mutant" "$bnf_evidence/types-$bnf_case.metta" "$bnf_program" \
            >> "$bnf_evidence/inputs.sha256"
    fi
    CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta "$bnf_program" \
        "$bnf_evidence/candidate-library.metta" "$bnf_ownership" \
        > "$bnf_evidence/ownership-$bnf_case.out" 2> "$bnf_evidence/ownership-$bnf_case.stderr"
    test ! -s "$bnf_evidence/ownership-$bnf_case.stderr"
    printf '(PlainBnfTypedDiscoveryOwnershipV1 %s OwnedAnswersAcceptedV1 OwnedHandleClosedV1)\n' "$bnf_expected_count" \
        > "$bnf_evidence/ownership-$bnf_case.expected"
    # The library import may emit its own success value; do not count that
    # as a grammar result or pin how many internal imports were performed.
    sed '/^true$/d' "$bnf_evidence/ownership-$bnf_case.out" \
        > "$bnf_evidence/ownership-$bnf_case.observation"
    cmp "$bnf_evidence/ownership-$bnf_case.expected" "$bnf_evidence/ownership-$bnf_case.observation"
    bnf_count=$((bnf_count + 1))
done

# The FIFO is observed independently of grammar acceptance. Mutating the
# authored enqueue order must change this observation after regeneration.
bnf_fifo_test=tests/langdef/bnf/plain_bnf_reachable_queue_v1.metta
bnf_fifo_expected=tests/langdef/bnf/plain_bnf_reachable_queue_v1.expected
sha256sum "$bnf_fifo_test" "$bnf_fifo_expected" >> "$bnf_evidence/inputs.sha256"
CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta "$bnf_evidence/program.metta" \
    "$bnf_fifo_test" > "$bnf_evidence/fifo.out" 2> "$bnf_evidence/fifo.stderr"
test ! -s "$bnf_evidence/fifo.stderr"
sed '/^true$/d' "$bnf_evidence/fifo.out" > "$bnf_evidence/fifo.observation"
cmp "$bnf_fifo_expected" "$bnf_evidence/fifo.observation"
bnf_count=$((bnf_count + 1))
bnf_fifo_mutant="$bnf_evidence/source-fifo-order.metta"
awk '
    { if (sub(/BNFReachableReverseV1 \?references \?back \?nextBack/,
              "BNFReachableReverseV1 ?back ?references ?nextBack")) changed++; print }
    END {if (changed != 1) exit 1}
' langdef/bnf/plain_bnf_graph_analysis_v1.metta > "$bnf_fifo_mutant"
bnf_mutation_args=()
bnf_mutation_sources=()
for bnf_source in "${bnf_sources[@]}"; do
    bnf_input="$bnf_root/$bnf_source"
    if [[ "$bnf_source" == langdef/bnf/plain_bnf_graph_analysis_v1.metta ]]; then
        bnf_input="$bnf_fifo_mutant"
    fi
    bnf_mutation_args+=(--source "$bnf_input")
    bnf_mutation_sources+=("$bnf_input")
done
for bnf_mode in "${bnf_modes[@]}"; do
    bnf_mutation_args+=(--closed-entry-mode "$bnf_mode")
done
(cd "$bnf_lean" && lake env lean --run "$bnf_exporter" "${bnf_mutation_sources[@]}") \
    > "$bnf_evidence/types-fifo.metta" 2> "$bnf_evidence/export-fifo.stderr"
"$bnf_compiler" petta-direct "${bnf_mutation_args[@]}" \
    --native-types "$bnf_evidence/types-fifo.metta" \
    --admission-language "$bnf_language" \
    --admission-entry BNFValidateGrammarDiscoveryV1:BnfGrammarInput \
    --out "$bnf_evidence/program-fifo.metta" > "$bnf_evidence/compile-fifo.txt" \
    2> "$bnf_evidence/compile-fifo.stderr"
test ! -s "$bnf_evidence/compile-fifo.stderr"
CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta "$bnf_evidence/program-fifo.metta" \
    "$bnf_fifo_test" > "$bnf_evidence/fifo-mutant.out" 2> "$bnf_evidence/fifo-mutant.stderr"
test ! -s "$bnf_evidence/fifo-mutant.stderr"
test "$(grep -Fxc FifoDiscoveryOrderMismatchV1 "$bnf_evidence/fifo-mutant.out")" -eq 1
bnf_count=$((bnf_count + 1))
sha256sum -c "$bnf_evidence/inputs.sha256" > "$bnf_evidence/inputs-after.txt"
printf '(PlainBnfTypedDiscoveryV1Summary %s 0)\n' "$bnf_count" | tee "$bnf_evidence/summary.txt"
