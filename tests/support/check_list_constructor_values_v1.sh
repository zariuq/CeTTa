#!/usr/bin/env bash
set -euo pipefail
if (( $# < 2 || $# > 3 )); then
    echo 'usage: check_list_constructor_values_v1.sh RUNTIME EVIDENCE_DIRECTORY [DEPTH]' >&2
    exit 2
fi
list_runtime=$(realpath "$1")
mkdir -p "$2"
list_evidence=$(realpath "$2")
list_depth=${3:-2048}
[[ "$list_depth" =~ ^[1-9][0-9]*$ ]] || exit 2
test ! -e "$list_evidence/inputs.sha256"
cd -- "$(dirname -- "$0")/../.."
sha256sum "$list_runtime" tests/petta/list_constructor_{values,typed,shadow}_v1.* \
    tests/gc/diagnostics/list_constructor_nonvalues_v1.* \
    tests/support/generate_list_constructor_depth_v1.c "$0" > "$list_evidence/inputs.sha256"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    tests/support/generate_list_constructor_depth_v1.c -o "$list_evidence/generate"
"$list_evidence/generate" "$list_depth" source > "$list_evidence/deep.metta"
"$list_evidence/generate" "$list_depth" bracketed > "$list_evidence/deep.expected"
list_count=0
for list_language in petta he prime; do
    for list_fixture in values typed shadow; do
        list_base=tests/petta/list_constructor_${list_fixture}_v1
        list_expected=$list_base.he.expected
        if [[ "$list_language" == petta ]]; then list_expected=$list_base.expected; fi
        if [[ "$list_language" == prime && -f "$list_base.prime.expected" ]]; then
            list_expected=$list_base.prime.expected
        fi
        CETTA_PETTA_SEARCH_MACHINE=1 "$list_runtime" --lang "$list_language" \
            "$list_base.metta" > "$list_evidence/$list_language-$list_fixture.out" \
            2> "$list_evidence/$list_language-$list_fixture.stderr"
        test ! -s "$list_evidence/$list_language-$list_fixture.stderr"
        cmp "$list_expected" "$list_evidence/$list_language-$list_fixture.out"
        list_count=$((list_count + 1))
    done
    if [[ "$list_language" != petta ]]; then
        # An unknown outer constructor still evaluates its nested fields.
        # PeTTa also exercises deep values through the public BNF workbench.
        "$list_runtime" --lang "$list_language" "$list_evidence/deep.metta" \
            > "$list_evidence/$list_language-deep.out" \
            2> "$list_evidence/$list_language-deep.stderr"
        test ! -s "$list_evidence/$list_language-deep.stderr"
        cmp "$list_evidence/deep.expected" "$list_evidence/$list_language-deep.out"
        list_count=$((list_count + 1))
        "$list_runtime" --lang "$list_language" \
            tests/gc/diagnostics/list_constructor_nonvalues_v1.metta \
            > "$list_evidence/$list_language-nonvalues.out" \
            2> "$list_evidence/$list_language-nonvalues.stderr"
        test ! -s "$list_evidence/$list_language-nonvalues.stderr"
        list_expected=tests/gc/diagnostics/list_constructor_nonvalues_v1.expected
        if [[ "$list_language" == prime ]]; then
            list_expected=tests/gc/diagnostics/list_constructor_nonvalues_v1.prime.expected
        fi
        cmp "$list_expected" \
            "$list_evidence/$list_language-nonvalues.out"
        list_count=$((list_count + 1))
    fi
done
# Error-shaped data and numeric exceptions are different observations.
# Run both automatic and explicitly selected PeTTa machine entry routes.
for list_fixture in list_constructor_pending_differentials list_constructor_error_shadow constructor_numeric_effect; do
    list_base=tests/petta/${list_fixture}_v1
    sha256sum "$list_base.metta" "$list_base.swi.expected" >> "$list_evidence/inputs.sha256"
    for list_mode in auto explicit; do
        list_command=(env -u CETTA_PETTA_SEARCH_MACHINE)
        if [[ "$list_mode" == explicit ]]; then
            list_command=(env CETTA_PETTA_SEARCH_MACHINE=1)
        fi
        "${list_command[@]}" "$list_runtime" --lang petta "$list_base.metta" \
            > "$list_evidence/$list_mode-$list_fixture.out" \
            2> "$list_evidence/$list_mode-$list_fixture.stderr"
        test ! -s "$list_evidence/$list_mode-$list_fixture.stderr"
        cmp "$list_base.swi.expected" "$list_evidence/$list_mode-$list_fixture.out"
        list_count=$((list_count + 1))
    done
done
sha256sum -c "$list_evidence/inputs.sha256" > "$list_evidence/inputs-after.txt"
printf '(ListConstructorValuesV1Summary %s 0)\n' "$list_count" | tee "$list_evidence/summary.txt"
