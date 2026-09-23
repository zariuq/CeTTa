#!/usr/bin/env bash
set -euo pipefail
if (( $# < 2 || $# > 3 )); then
    echo 'usage: check_gslt_support_profile_native_v1.sh TOOL EVIDENCE_DIRECTORY [REFERENCE_DIRECTORY]' >&2
    exit 2
fi
profile_tool=$(realpath "$1")
profile_script=$(realpath "$0")
profile_repo=$(cd "$(dirname "$profile_script")/../.." && pwd -P)
test -x "$profile_tool"
mkdir -p "$2"
profile_evidence=$(realpath "$2")
test ! -e "$profile_evidence/inputs.sha256"
cd "$profile_repo"
profile_reference=$(realpath "${3:-src/generated}")
profile_tool_sha=$(sha256sum "$profile_tool")
profile_tool_sha=${profile_tool_sha%% *}
profile_manifest=langdef/mm2/gslt_profile_v1.metta
profile_fixture=tests/support/gslt_support_profile_native_v1
profile_sources=(src/gslt_support_profile_v1.c native/operational_language_def_v1.c
    src/lib_parse_native_grammar.c src/gslt_dense_bitset_v1.c src/atom.c src/binding/frame_identity.c src/symbol.c
    src/name_key.c src/native_sha256.c src/parser.c src/match.c src/binding/frame_schema.c src/binding/slot_store.c src/binding/activation_view.c
    src/gslt_support_transform_runtime.c src/gslt_pure_provider_v1.c src/mm2_lower.c
    src/prime_need.c src/variant_shape.c)
profile_observer=tests/support/test_gslt_support_profile_emission_v1.c
profile_unit=tests/support/test_gslt_support_profile_v1.c
profile_runtime=tests/support/test_gslt_support_transform_runtime.c
sha256sum "$profile_tool" "$profile_script" "$profile_manifest" \
    "$profile_fixture/empty_utf8.metta" "$profile_fixture/unavailable.metta" \
    "$profile_observer" "$profile_unit" "$profile_runtime" "${profile_sources[@]}" \
    src/gslt_support_profile_v1.h src/gslt_support_transform_runtime.h \
    native/operational_language_def_v1.h src/generated/cetta_execution_contracts.generated.h \
    "$profile_reference/mm2_gslt_profile_v1.generated.c" \
    "$profile_reference/mm2_gslt_profile_v1.generated.h" > "$profile_evidence/inputs.sha256"
profile_cc=${CC:-cc}
read -r -a profile_cflags <<< "${CFLAGS:--O2 -Wall -Werror}"
profile_cflags+=(-std=c11 -pthread -ffunction-sections -fdata-sections -Isrc -I.
    -DCETTA_BUILD_WITH_GMP=0 -DCETTA_BUILD_WITH_RUNTIME_STATS=0)
profile_objects=()
for profile_source in "${profile_sources[@]}"; do
    profile_object="$profile_evidence/$(basename "${profile_source%.c}").o"
    "$profile_cc" "${profile_cflags[@]}" -c "$profile_source" -o "$profile_object" \
        >> "$profile_evidence/compile.log" 2>&1
    profile_objects+=("$profile_object")
done
profile_link() {
    "$profile_cc" "${profile_cflags[@]}" -Wl,--gc-sections "$@" \
        "${profile_objects[@]}" -lm -o "$profile_evidence/$profile_binary" \
        >> "$profile_evidence/compile.log" 2>&1
}
profile_binary=retained-unit
profile_link "$profile_unit" "$profile_reference/mm2_gslt_profile_v1.generated.c"
"$profile_evidence/$profile_binary" "$profile_manifest" > "$profile_evidence/retained-unit.log" 2>&1
for profile_case in actual empty_utf8 unavailable; do
    profile_input="$profile_fixture/$profile_case.metta"
    if [[ $profile_case == actual ]]; then profile_input=$profile_manifest; fi
    for profile_trial in first second; do
        profile_output="$profile_evidence/$profile_case/$profile_trial"
        mkdir -p "$profile_output/generated"
        "$profile_tool" --manifest "$profile_input" \
            --header "$profile_output/generated/mm2_gslt_profile_v1.generated.h" \
            --source "$profile_output/generated/mm2_gslt_profile_v1.generated.c" \
            --symbol cetta_mm2_gslt_profile_v1 \
            --header-include generated/mm2_gslt_profile_v1.generated.h \
            > "$profile_output/generate.stdout" 2> "$profile_output/generate.stderr"
        test ! -s "$profile_output/generate.stderr"
    done
    profile_output="$profile_evidence/$profile_case/first"
    for profile_extension in c h; do
        cmp "$profile_output/generated/mm2_gslt_profile_v1.generated.$profile_extension" \
            "$profile_evidence/$profile_case/second/generated/mm2_gslt_profile_v1.generated.$profile_extension"
    done
    profile_binary="$profile_case/observe"
    profile_link -iquote "$profile_output" "$profile_observer" \
        "$profile_output/generated/mm2_gslt_profile_v1.generated.c"
    profile_work=()
    if [[ $profile_case == unavailable ]]; then
        # Literal MeTTa variables, not shell expansion.
        # shellcheck disable=SC2016
        profile_work=('(seed a) (work (from (BTM (seed $x))) (do (put (seen $x))) (0 future))')
    fi
    "$profile_evidence/$profile_binary" "$profile_input" "$profile_tool_sha" "${profile_work[@]}" \
        > "$profile_evidence/$profile_case/observe.log" 2>&1
done
profile_output="$profile_evidence/actual/first"
profile_binary=emitted-unit
profile_link -iquote "$profile_output" "$profile_unit" "$profile_output/generated/mm2_gslt_profile_v1.generated.c"
"$profile_evidence/$profile_binary" "$profile_manifest" > "$profile_evidence/emitted-unit.log" 2>&1
profile_binary=runtime
profile_link -iquote "$profile_output" "$profile_runtime" "$profile_output/generated/mm2_gslt_profile_v1.generated.c"
"$profile_evidence/$profile_binary" > "$profile_evidence/runtime.log" 2>&1

profile_controls="$profile_evidence/controls"
mkdir -p "$profile_controls"
cp "$profile_manifest" "$profile_controls/source.metta"
ln "$profile_controls/source.metta" "$profile_controls/source-hardlink"
ln -s source.metta "$profile_controls/source-symlink"
ln -s absent "$profile_controls/dangling"
profile_refusals=0
profile_refuse() {
    local label=$1 header=$2 source=$3 symbol=$4 include=$5 diagnostic=$6
    if "$profile_tool" --manifest "$profile_controls/source.metta" \
        --header "$header" --source "$source" --symbol "$symbol" --header-include "$include" \
        > "$profile_controls/$label.stdout" 2> "$profile_controls/$label.stderr"; then
        echo "unexpected support-profile CLI acceptance: $label" >&2; exit 1
    fi
    rg -Fq "$diagnostic" "$profile_controls/$label.stderr"
    cmp "$profile_manifest" "$profile_controls/source.metta"
    test ! -e "$profile_controls/generated.h"
    test ! -e "$profile_controls/generated.c"
    test -L "$profile_controls/dangling"
    test "$(readlink "$profile_controls/dangling")" = absent
    test ! -e "$profile_controls/absent"
    profile_refusals=$((profile_refusals + 1))
}
for profile_alias in source.metta source-hardlink source-symlink dangling; do
    profile_refuse "header-$profile_alias" "$profile_controls/$profile_alias" "$profile_controls/generated.c" \
        valid generated.h 'output is absent or aliases'
    profile_refuse "source-$profile_alias" "$profile_controls/generated.h" "$profile_controls/$profile_alias" \
        valid generated.h 'output is absent or aliases'
done
profile_refuse same "$profile_controls/generated.c" "$profile_controls/./generated.c" valid generated.h 'output is absent or aliases'
profile_refuse executable "$profile_tool" "$profile_controls/generated.c" valid generated.h 'output is absent or aliases'
profile_refuse symbol "$profile_controls/generated.h" "$profile_controls/generated.c" 'bad-symbol' generated.h 'usage:'
profile_refuse keyword "$profile_controls/generated.h" "$profile_controls/generated.c" return generated.h 'usage:'
profile_refuse include "$profile_controls/generated.h" "$profile_controls/generated.c" valid 'bad"header' 'usage:'
sha256sum -c "$profile_evidence/inputs.sha256" > "$profile_evidence/inputs-unchanged.log"
printf 'Native support-profile CLI: 3 compiled descriptors, deterministic regeneration, runtime controls, %s expected refusals.\n' "$profile_refusals"
