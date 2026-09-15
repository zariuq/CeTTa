#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 && $# != 3 )); then
    echo 'usage: check_ebnf_workbench_v1.sh RUNTIME EVIDENCE_DIRECTORY [ADMISSION_PROGRAM]' >&2
    exit 2
fi
ebnf_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
ebnf_runtime=$(realpath "$1")
mkdir -p "$2"
ebnf_evidence=$(realpath "$2")
test ! -e "$ebnf_evidence/inputs.sha256"
cd "$ebnf_root"
ebnf_program=${3:-langdef/petta/generated/plain_bnf_semantic_admission_v1.metta}
sha256sum "$ebnf_runtime" "$ebnf_program" lib/lib_bnf.metta lib/langdef.metta \
    langdef/bnf/ebnf_source_v1.bnf langdef/bnf/ebnf_self_v1.ebnf \
    langdef/bnf/ebnf_grammar_v1.metta langdef/bnf/ebnf_cst_projection_v1.metta \
    langdef/bnf/ebnf_declaration_view_v1.metta langdef/bnf/ebnf_lowering_v1.metta \
    langdef/bnf/ebnf_derivation_projection_v1.metta \
    langdef/bnf/ebnf_diagnostic_projection_v1.metta \
    native/langdef_module.c native/deterministic_equation_plan_v1.c \
    experiments/gslt2parse_foundation/native/parser_pack_native_v1.c \
    experiments/gslt2parse_foundation/native/parser_pack_native_v1.h \
    langdef/bnf/plain_bnf_source_v1.metta langdef/bnf/plain_bnf_parser_profile_v1.metta \
    langdef/bnf/plain_bnf_grammar_v1.metta langdef/bnf/plain_bnf_cst_projection_v1.metta \
    langdef/bnf/plain_bnf_denotation_v1.metta \
    langdef/bnf/language_def_parser_compiler_v1.metta \
    tests/langdef/bnf/ebnf_arithmetic_v1.ebnf \
    tests/langdef/bnf/ebnf_records_v1.ebnf \
    tests/langdef/bnf/ebnf_records_escaped_v1.input \
    tests/langdef/bnf/ebnf_dangling_else_v1.ebnf \
    tests/langdef/bnf/ebnf_ho_syntax_v1.ebnf \
    benchmarks/bnf/generate_ebnf_workbench.c \
    tests/support/check_ebnf_workbench_v1.sh > "$ebnf_evidence/inputs.sha256"
ebnf_count=0
for ebnf_fixture in observations integration equivalence projection diagnostics cycles helper_names namespace_freshness applications; do
    ebnf_test="tests/langdef/bnf/ebnf_${ebnf_fixture}_v1.metta"
    ebnf_expected="tests/langdef/bnf/ebnf_${ebnf_fixture}_v1.expected"
    sha256sum "$ebnf_test" "$ebnf_expected" >> "$ebnf_evidence/inputs.sha256"
    CETTA_PETTA_SEARCH_MACHINE=1 "$ebnf_runtime" --lang petta "$ebnf_program" "$ebnf_test" \
        > "$ebnf_evidence/$ebnf_fixture.out" 2> "$ebnf_evidence/$ebnf_fixture.stderr"
    test ! -s "$ebnf_evidence/$ebnf_fixture.stderr"
    cmp "$ebnf_expected" "$ebnf_evidence/$ebnf_fixture.out"
    ebnf_count=$((ebnf_count + 1))
done

# Each namespace must contribute to freshness. Mutate the authored scan,
# execute lowering normally, and demand the specific capture observation.
# Generated helper spellings and counts are not normative fixture outputs.
for ebnf_namespace in declaration nested-reference lexical; do
    awk -v target="$ebnf_namespace" '
        /\(rule width-rule$/ && target == "declaration" {selected=1}
        /\(rule width-reference$/ && target == "nested-reference" {selected=1}
        /\(rule width-lexical$/ && target == "lexical" {selected=1}
        selected && /\(ebnf-v1:text-width \?name\)/ {
            sub(/\(ebnf-v1:text-width \?name\)/, "(ebnf-v1:z)")
            changed++; selected=0
        }
        {print}
        END {if (changed != 1) exit 1}
    ' langdef/bnf/ebnf_lowering_v1.metta > "$ebnf_evidence/namespace-$ebnf_namespace.metta"
    sed "s|langdef/bnf/ebnf_lowering_v1.metta|$ebnf_evidence/namespace-$ebnf_namespace.metta|g" \
        lib/lib_bnf.metta > "$ebnf_evidence/namespace-$ebnf_namespace-library.metta"
    sed "s|../../../lib/lib_bnf.metta|$ebnf_evidence/namespace-$ebnf_namespace-library.metta|" \
        tests/langdef/bnf/ebnf_namespace_freshness_v1.metta \
        > "$ebnf_evidence/namespace-$ebnf_namespace-test.metta"
    CETTA_PETTA_SEARCH_MACHINE=1 "$ebnf_runtime" --lang petta "$ebnf_program" \
        "$ebnf_evidence/namespace-$ebnf_namespace-test.metta" \
        > "$ebnf_evidence/namespace-$ebnf_namespace.out" \
        2> "$ebnf_evidence/namespace-$ebnf_namespace.stderr"
    test ! -s "$ebnf_evidence/namespace-$ebnf_namespace.stderr"
    test "$(wc -l < "$ebnf_evidence/namespace-$ebnf_namespace.out")" -eq 4
    grep -Fxq "(EbnfNamespaceFreshV1 $ebnf_namespace false)" \
        "$ebnf_evidence/namespace-$ebnf_namespace.out"
    for ebnf_other in declaration nested-reference lexical; do
        if [[ "$ebnf_other" != "$ebnf_namespace" ]]; then
            grep -Fxq "(EbnfNamespaceFreshV1 $ebnf_other true)" \
                "$ebnf_evidence/namespace-$ebnf_namespace.out"
        fi
    done
    sha256sum "$ebnf_evidence/namespace-$ebnf_namespace.metta" \
        "$ebnf_evidence/namespace-$ebnf_namespace-library.metta" \
        "$ebnf_evidence/namespace-$ebnf_namespace-test.metta" >> "$ebnf_evidence/inputs.sha256"
    ebnf_count=$((ebnf_count + 1))
done

# Dropping carry propagation makes helper names repeat after the first two
# allocations. Require declaration refusal, not a loader or evaluator fault.
awk '
    /\(rule counter-one$/ {selected=1}
    selected && /\(bnf-v1:text-cons 48 \(ebnf-v1:next-bits \?tail\)\)/ {
        sub(/\(ebnf-v1:next-bits \?tail\)/, "?tail")
        changed++; selected=0
    }
    {print}
    END {if (changed != 1) exit 1}
' langdef/bnf/ebnf_lowering_v1.metta > "$ebnf_evidence/mutated-helper-counter.metta"
sed "s|langdef/bnf/ebnf_lowering_v1.metta|$ebnf_evidence/mutated-helper-counter.metta|g" \
    lib/lib_bnf.metta > "$ebnf_evidence/counter-mutated-library.metta"
sed "s|../../../lib/lib_bnf.metta|$ebnf_evidence/counter-mutated-library.metta|" \
    tests/langdef/bnf/ebnf_helper_names_v1.metta > "$ebnf_evidence/counter-mutated-test.metta"
CETTA_PETTA_SEARCH_MACHINE=1 "$ebnf_runtime" --lang petta "$ebnf_program" \
    "$ebnf_evidence/counter-mutated-test.metta" > "$ebnf_evidence/counter-mutation.out" \
    2> "$ebnf_evidence/counter-mutation.stderr"
test ! -s "$ebnf_evidence/counter-mutation.stderr"
test "$(wc -l < "$ebnf_evidence/counter-mutation.out")" -eq 4
for ebnf_helpers in 129 257 33; do
    grep -Fxq "(EbnfHelperNamesRefusedV1 $ebnf_helpers)" "$ebnf_evidence/counter-mutation.out"
done
sha256sum "$ebnf_evidence/mutated-helper-counter.metta" \
    "$ebnf_evidence/counter-mutated-library.metta" \
    "$ebnf_evidence/counter-mutated-test.metta" >> "$ebnf_evidence/inputs.sha256"
ebnf_count=$((ebnf_count + 1))

# A fresh source mutation removes the empty branch of star. It is executed
# by the same lowering mechanism; no generated target artifact is patched.
awk '
    /\(rule expression-zero-or-more$/ {selected=1}
    selected && /\(bnf-v1:alternative \(bnf-v1:elements-nil\)/ {
        sub(/\(bnf-v1:elements-nil\)/,
            "(bnf-v1:elements-cons ?body (bnf-v1:elements-nil))")
        changed++; selected=0
    }
    {print}
    END {if (changed != 1) exit 1}
' langdef/bnf/ebnf_lowering_v1.metta > "$ebnf_evidence/mutated-lowering.metta"
sed "s|langdef/bnf/ebnf_lowering_v1.metta|$ebnf_evidence/mutated-lowering.metta|g" \
    lib/lib_bnf.metta > "$ebnf_evidence/mutated-library.metta"
sed "s|../../../lib/lib_bnf.metta|$ebnf_evidence/mutated-library.metta|" \
    tests/langdef/bnf/ebnf_observations_v1.metta > "$ebnf_evidence/mutated-test.metta"
CETTA_PETTA_SEARCH_MACHINE=1 "$ebnf_runtime" --lang petta "$ebnf_program" \
    "$ebnf_evidence/mutated-test.metta" > "$ebnf_evidence/mutation.out" \
    2> "$ebnf_evidence/mutation.stderr"
test ! -s "$ebnf_evidence/mutation.stderr"
if cmp -s tests/langdef/bnf/ebnf_observations_v1.expected "$ebnf_evidence/mutation.out"; then
    echo 'star mutation escaped the observation gate' >&2
    exit 1
fi
# The selected fourth probe is empty input under star. Require an actual
# grammar rejection there, not an unrelated loader/evaluator failure.
test "$(sed -n '5p' "$ebnf_evidence/mutation.out")" = '(rejected 0)'
ebnf_count=$((ebnf_count + 1))

# Erase a group alternative index in the authored projection. Parse counts
# alone would miss this: all four trees would still be returned, but their
# independent source choices would have been identified incorrectly.
# The production projection is the native realization; the authored
# transformation remains the independently executable oracle. The mutated
# library therefore routes observations through the authored oracle entry,
# so this canary verifies the oracle source is live and semantics-bearing.
awk '
    /\(rule node-group$/ {selected=1}
    selected && /\(EBNF:Group / {
        sub(/\?index/, "0"); changed++; selected=0
    }
    {print}
    END {if (changed != 1) exit 1}
' langdef/bnf/ebnf_derivation_projection_v1.metta > "$ebnf_evidence/mutated-projection.metta"
sed -e "s|langdef/bnf/ebnf_derivation_projection_v1.metta|$ebnf_evidence/mutated-projection.metta|g" \
    -e "s|(case (bnf:project-ebnf-trees \$receipt|(case (bnf:project-ebnf-trees-authored \$receipt|g" \
    lib/lib_bnf.metta > "$ebnf_evidence/projection-mutated-library.metta"
sed "s|../../../lib/lib_bnf.metta|$ebnf_evidence/projection-mutated-library.metta|" \
    tests/langdef/bnf/ebnf_projection_v1.metta > "$ebnf_evidence/projection-mutated-test.metta"
CETTA_PETTA_SEARCH_MACHINE=1 "$ebnf_runtime" --lang petta "$ebnf_program" \
    "$ebnf_evidence/projection-mutated-test.metta" > "$ebnf_evidence/projection-mutation.out" \
    2> "$ebnf_evidence/projection-mutation.stderr"
test ! -s "$ebnf_evidence/projection-mutation.stderr"
test "$(sed -n '3p' "$ebnf_evidence/projection-mutation.out")" = '(EbnfChoiceMultiplicityV1 4 0 0 0)'
ebnf_count=$((ebnf_count + 1))

# Keeping the left-recursive accumulator in traversal order must be caught
# as a source-order defect, even though parser acceptance is unchanged.
# As with the group-index canary, the mutated library routes observations
# through the authored oracle entry so the oracle stays executed.
awk '
    /\(rule iterations-source-order-cons$/ {selected=1}
    selected && /\(ebnf-v1:iterations-source-order \?rest$/ {
        print "        (EBNF:IterationsCons (ebnf-v1:source-order ?head)"
        if (getline <= 0) exit 1
        print "          (ebnf-v1:iterations-source-order ?rest ?tail))))"
        changed++; selected=0; next
    }
    {print}
    END {if (changed != 1) exit 1}
' langdef/bnf/ebnf_derivation_projection_v1.metta > "$ebnf_evidence/mutated-iteration-order.metta"
sed -e "s|langdef/bnf/ebnf_derivation_projection_v1.metta|$ebnf_evidence/mutated-iteration-order.metta|g" \
    -e "s|(case (bnf:project-ebnf-trees \$receipt|(case (bnf:project-ebnf-trees-authored \$receipt|g" \
    lib/lib_bnf.metta > "$ebnf_evidence/order-mutated-library.metta"
sed "s|../../../lib/lib_bnf.metta|$ebnf_evidence/order-mutated-library.metta|" \
    tests/langdef/bnf/ebnf_projection_v1.metta > "$ebnf_evidence/order-mutated-test.metta"
CETTA_PETTA_SEARCH_MACHINE=1 "$ebnf_runtime" --lang petta "$ebnf_program" \
    "$ebnf_evidence/order-mutated-test.metta" > "$ebnf_evidence/order-mutation.out" \
    2> "$ebnf_evidence/order-mutation.stderr"
test ! -s "$ebnf_evidence/order-mutation.stderr"
# Require the specific swapped Unicode iterations, not an unrelated fault.
grep -Fq '(EbnfUnicodeLeavesV1 (LCons (literal "雪" (EBNF:InputSpan 1 2)) (LCons (literal "λ" (EBNF:InputSpan 0 1))' \
    "$ebnf_evidence/order-mutation.out"
ebnf_count=$((ebnf_count + 1))

# Erasing the owner at helper creation must change source-level diagnostics,
# even though grammar recognition and helper spans are unchanged.
awk '
    /\(rule lower-entry-rule$/ {selected=1}
    selected && /\(ebnf-v1:rule-origin \?name \?span\)/ {
        sub(/\(ebnf-v1:rule-origin \?name \?span\)/,
            "(ebnf-v1:rule-origin (bnf-v1:text-nil) ?span)")
        changed++; selected=0
    }
    {print}
    END {if (changed != 1) exit 1}
' langdef/bnf/ebnf_lowering_v1.metta > "$ebnf_evidence/mutated-owner.metta"
sed "s|langdef/bnf/ebnf_lowering_v1.metta|$ebnf_evidence/mutated-owner.metta|g" \
    lib/lib_bnf.metta > "$ebnf_evidence/owner-mutated-library.metta"
sed "s|../../../lib/lib_bnf.metta|$ebnf_evidence/owner-mutated-library.metta|" \
    tests/langdef/bnf/ebnf_diagnostics_v1.metta > "$ebnf_evidence/owner-mutated-test.metta"
CETTA_PETTA_SEARCH_MACHINE=1 "$ebnf_runtime" --lang petta "$ebnf_program" \
    "$ebnf_evidence/owner-mutated-test.metta" > "$ebnf_evidence/owner-mutation.out" \
    2> "$ebnf_evidence/owner-mutation.stderr"
test ! -s "$ebnf_evidence/owner-mutation.stderr"
grep -Fq '(expression unreachable "" group' "$ebnf_evidence/owner-mutation.out"
grep -Fq '(EbnfUnexpectedOwnersV1 (EBNF:Diagnostics' "$ebnf_evidence/owner-mutation.out"
if grep -Fq '(EbnfDiagnosticOwnersV1 ' "$ebnf_evidence/owner-mutation.out"; then
    echo 'owner-erasure mutation escaped the diagnostic gate' >&2
    exit 1
fi
ebnf_count=$((ebnf_count + 1))

# The original plain-BNF public API remains unchanged.
CETTA_PETTA_SEARCH_MACHINE=1 "$ebnf_runtime" --lang petta "$ebnf_program" \
    tests/langdef/bnf/plain_bnf_public_entry_v1.metta \
    > "$ebnf_evidence/plain-public.out" 2> "$ebnf_evidence/plain-public.stderr"
test ! -s "$ebnf_evidence/plain-public.stderr"
cmp tests/langdef/bnf/plain_bnf_public_entry_v1.expected "$ebnf_evidence/plain-public.out"
ebnf_count=$((ebnf_count + 1))
sha256sum tests/langdef/bnf/plain_bnf_public_entry_v1.metta \
    tests/langdef/bnf/plain_bnf_public_entry_v1.expected \
    "$ebnf_evidence/mutated-lowering.metta" "$ebnf_evidence/mutated-library.metta" \
    "$ebnf_evidence/mutated-test.metta" \
    "$ebnf_evidence/mutated-projection.metta" \
    "$ebnf_evidence/projection-mutated-library.metta" \
    "$ebnf_evidence/projection-mutated-test.metta" \
    "$ebnf_evidence/mutated-owner.metta" \
    "$ebnf_evidence/owner-mutated-library.metta" \
    "$ebnf_evidence/owner-mutated-test.metta" \
    "$ebnf_evidence/mutated-iteration-order.metta" \
    "$ebnf_evidence/order-mutated-library.metta" \
    "$ebnf_evidence/order-mutated-test.metta" >> "$ebnf_evidence/inputs.sha256"
# The public path must also transport a substantial grammar value. This
# specimen previously exhausted the evaluator stack before reaching GLL.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    benchmarks/bnf/generate_ebnf_workbench.c -o "$ebnf_evidence/generate-capacity"
"$ebnf_evidence/generate-capacity" 50 project > "$ebnf_evidence/capacity.metta"
CETTA_PETTA_SEARCH_MACHINE=1 "$ebnf_runtime" --lang petta "$ebnf_program" \
    lib/lib_bnf.metta "$ebnf_evidence/capacity.metta" \
    > "$ebnf_evidence/capacity.out" 2> "$ebnf_evidence/capacity.stderr"
test ! -s "$ebnf_evidence/capacity.stderr"
test "$(grep -Fxc EbnfBenchProjected "$ebnf_evidence/capacity.out")" -eq 1
if grep -Eq 'Unexpected|Error|Incomplete' "$ebnf_evidence/capacity.out"; then
    echo 'expanded grammar did not complete its public projection' >&2
    exit 1
fi
ebnf_count=$((ebnf_count + 1))
sha256sum "$ebnf_evidence/capacity.metta" >> "$ebnf_evidence/inputs.sha256"
sha256sum -c "$ebnf_evidence/inputs.sha256" > "$ebnf_evidence/inputs-after.txt"
printf '(EbnfWorkbenchV1Summary %s 0)\n' "$ebnf_count" | tee "$ebnf_evidence/summary.txt"
