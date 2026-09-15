#!/usr/bin/env bash
set -euo pipefail

if (( $# < 4 || $# > 5 )); then
    echo 'usage: check_gslt_integer_native_types_v1.sh COMPILER RUNTIME LEAN_PROJECT EVIDENCE_DIRECTORY [SWI_PETTA_SOURCE]' >&2
    exit 2
fi
bnf_repo=$(pwd -P)
bnf_compiler=$(realpath "$1")
bnf_runtime=$(realpath "$2")
bnf_lean=$(realpath "$3")
mkdir -p "$4"
bnf_evidence=$(realpath "$4")
test ! -e "$bnf_evidence/plain.native-types.metta"
bnf_exporter="$bnf_repo/tests/langdef/bnf/integer_provider_native_type_export_v1.lean"
bnf_sources=(
    "$bnf_repo/experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta"
    "$bnf_repo/experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta"
    "$bnf_repo/tests/petta/gslt_integer_native_types_v1.metta")
bnf_entries=()
for bnf_name in Pair Use Gap Absent Duplicate Float Symbol Overflow; do
    bnf_entries+=(--closed-entry-mode "IntegerLiteral${bnf_name}V1:0")
done
bnf_entries+=(--closed-entry-mode IntegerVariableCallV1:10)
sha256sum "$bnf_compiler" "$bnf_runtime" "$bnf_exporter" "${bnf_sources[@]}" \
    > "$bnf_evidence/inputs.sha256"

bnf_compile() {
    local label=$1 packet=$2 output=$3
    local args=() source
    for source in "${bnf_sources[@]}"; do args+=(--source "$source"); done
    if [[ $packet != absent ]]; then args+=(--native-types "$packet"); fi
    "$bnf_compiler" petta-direct "${args[@]}" "${bnf_entries[@]}" \
        --out "$output" > "$bnf_evidence/$label.compile.txt" \
        2> "$bnf_evidence/$label.compile.stderr"
}

bnf_generate_case() {
    local label=$1
    (cd "$bnf_lean" && lake env lean --run "$bnf_exporter" "${bnf_sources[@]}") \
        > "$bnf_evidence/$label.native-types.metta" \
        2> "$bnf_evidence/$label.export.stderr"
    bnf_compile "$label.raw" absent "$bnf_evidence/$label.raw.metta"
    bnf_compile "$label.typed" "$bnf_evidence/$label.native-types.metta" \
        "$bnf_evidence/$label.typed.metta"
}

bnf_run() {
    local label=$1 query=$2
    CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta \
        "$bnf_evidence/$label.metta" "$query" \
        > "$bnf_evidence/$label.out" 2> "$bnf_evidence/$label.stderr"
    test ! -s "$bnf_evidence/$label.stderr"
}

bnf_mode() {
    local file=$1 relation=$2 cardinality=$3
    rg -q "^; gslt-binding-mode $relation/.* functional=$cardinality specialized=yes$" "$file"
}

bnf_generate_case plain
for bnf_name in Pair Use Gap Absent; do
    bnf_mode "$bnf_evidence/plain.raw.metta" "IntegerLiteral${bnf_name}V1" relational
    bnf_mode "$bnf_evidence/plain.typed.metta" "IntegerLiteral${bnf_name}V1" semidet
done
for bnf_name in Duplicate Float Symbol Overflow; do
    bnf_mode "$bnf_evidence/plain.typed.metta" "IntegerLiteral${bnf_name}V1" relational
done
bnf_mode "$bnf_evidence/plain.typed.metta" IntegerVariableCallV1 relational
bnf_query="$bnf_repo/tests/petta/gslt_integer_native_types_query_v1.metta"
bnf_run plain.raw "$bnf_query"
bnf_run plain.typed "$bnf_query"
diff -u "$bnf_repo/tests/petta/gslt_integer_native_types_v1.expected" "$bnf_evidence/plain.raw.out"
diff -u "$bnf_evidence/plain.raw.out" "$bnf_evidence/plain.typed.out"
# The ablation changes executed call syntax, not only a comment/certificate.
rg -q '\(superpose \(collapse \(gslt:mode:IntegerLiteralPairV1:0\)\)\)' "$bnf_evidence/plain.raw.metta"
rg -q '\(let .*\(gslt:fn:IntegerLiteralPairV1:0\)' "$bnf_evidence/plain.typed.metta"

bnf_pair_query="$bnf_repo/tests/petta/gslt_integer_native_types_pair_query_v1.metta"
for bnf_case in duplicate broad shadow nullary_override; do
    bnf_sources+=("$bnf_repo/tests/petta/gslt_integer_native_types_${bnf_case}_v1.metta")
    bnf_generate_case "$bnf_case"
    bnf_mode "$bnf_evidence/$bnf_case.typed.metta" IntegerLiteralPairV1 relational
    bnf_mode "$bnf_evidence/$bnf_case.typed.metta" IntegerLiteralUseV1 relational
    bnf_run "$bnf_case.raw" "$bnf_pair_query"
    bnf_run "$bnf_case.typed" "$bnf_pair_query"
    diff -u "$bnf_evidence/$bnf_case.raw.out" "$bnf_evidence/$bnf_case.typed.out"
    unset 'bnf_sources[3]'
done

# Each mutant is a source transformation, followed by fresh NTT inference.
bnf_original_sources=("${bnf_sources[@]}")
for bnf_case in renamed same-polarity wrong-operands; do
    mkdir -p "$bnf_evidence/$bnf_case.sources"
    for bnf_i in 0 1 2; do
        bnf_changed="$bnf_evidence/$bnf_case.sources/$bnf_i.metta"
        case $bnf_case in
            renamed)
                sed 's/ground-integer-less/renamed-integer-comparison-v1/g' \
                    "${bnf_original_sources[$bnf_i]}" > "$bnf_changed" ;;
            same-polarity)
                sed 's/(>= ?left ?right)/(< ?left ?right)/g' \
                    "${bnf_original_sources[$bnf_i]}" > "$bnf_changed" ;;
            wrong-operands)
                sed 's/(ground-integer-not-less 0 2)/(ground-integer-not-less 2 0)/g' \
                    "${bnf_original_sources[$bnf_i]}" > "$bnf_changed" ;;
        esac
        bnf_sources[$bnf_i]=$bnf_changed
    done
    bnf_generate_case "$bnf_case"
    if [[ $bnf_case == renamed ]]; then
        bnf_mode "$bnf_evidence/$bnf_case.typed.metta" IntegerLiteralPairV1 semidet
    else
        bnf_mode "$bnf_evidence/$bnf_case.typed.metta" IntegerLiteralPairV1 relational
    fi
    bnf_run "$bnf_case.raw" "$bnf_pair_query"
    bnf_run "$bnf_case.typed" "$bnf_pair_query"
    diff -u "$bnf_evidence/$bnf_case.raw.out" "$bnf_evidence/$bnf_case.typed.out"
done
bnf_sources=("${bnf_original_sources[@]}")

# Packet mutations are untrusted input, never edits to generated programs.
sed 's/binary-integer-completion-v1 6 ground-integer-less less direct/binary-integer-completion-v1 6 ground-integer-less not-less direct/' \
    "$bnf_evidence/plain.native-types.metta" > "$bnf_evidence/false-polarity.metta"
sed 's/(binary-integer-completion-v1 6 ground-integer-less less direct)//' \
    "$bnf_evidence/plain.native-types.metta" > "$bnf_evidence/missing-occurrence.metta"
sed 's/(ground-integer-less 0 2)/(ground-integer-less 0 2.0)/g' \
    "$bnf_evidence/plain.native-types.metta" > "$bnf_evidence/float-source.metta"
sed 's/binary-integer-completion-v1 6 ground-integer-less less direct/binary-integer-completion-v1 6 ground-integer-less less successor/' \
    "$bnf_evidence/plain.native-types.metta" > "$bnf_evidence/false-successor.metta"
sed 's/(types$/(types (binary-integer-completion-v1 6 ground-integer-less less direct)/' \
    "$bnf_evidence/plain.native-types.metta" > "$bnf_evidence/extra-occurrence.metta"
for bnf_case in false-polarity missing-occurrence float-source false-successor extra-occurrence; do
    if cmp -s "$bnf_evidence/plain.native-types.metta" "$bnf_evidence/$bnf_case.metta"; then
        echo "mutation did not change its input: $bnf_case" >&2
        exit 1
    fi
    if bnf_compile "$bnf_case" "$bnf_evidence/$bnf_case.metta" "$bnf_evidence/$bnf_case.program.metta"; then
        echo "unexpected native-type mutation acceptance: $bnf_case" >&2
        exit 1
    fi
    test ! -e "$bnf_evidence/$bnf_case.program.metta"
done
if "$bnf_compiler" petta-direct --source "${bnf_sources[0]}" \
    --native-types "$bnf_evidence/plain.native-types.metta" \
    --epilogue "$bnf_query" --out "$bnf_evidence/epilogue.program.metta" \
    > "$bnf_evidence/epilogue.txt" 2> "$bnf_evidence/epilogue.stderr"; then
    echo 'unchecked native-type epilogue was accepted' >&2
    exit 1
fi
rg -q 'without an unchecked epilogue' "$bnf_evidence/epilogue.stderr"
test ! -e "$bnf_evidence/epilogue.program.metta"

if (( $# == 5 )); then
    bnf_petta=$(realpath "$5")
    sha256sum "$bnf_petta" "$(dirname "$bnf_petta")/translator.pl" \
        > "$bnf_evidence/swi-inputs.sha256"
    for bnf_version in raw typed; do
        swipl -q -s "$bnf_petta" \
            -g 'current_prolog_flag(argv, [_,Program,Query]), load_metta_file(Program, _), load_metta_file(Query, R), maplist(swrite,R,S), maplist(writeln,S), halt' \
            -- --silent "$bnf_evidence/plain.$bnf_version.metta" "$bnf_query" \
            > "$bnf_evidence/plain.$bnf_version.swi.out" \
            2> "$bnf_evidence/plain.$bnf_version.swi.stderr"
        test ! -s "$bnf_evidence/plain.$bnf_version.swi.stderr"
        diff -u "$bnf_evidence/plain.$bnf_version.out" "$bnf_evidence/plain.$bnf_version.swi.out"
    done
fi
echo '(IntegerNativeTypeConsumerV1 passed)'
