#!/usr/bin/env bash
set -euo pipefail

if (( $# < 2 || $# > 4 )); then
    echo 'usage: check_gslt_provider_catalog_native_v1.sh TOOL EVIDENCE_DIRECTORY [REFERENCE_DIRECTORY [all|petta|nik|zero]]' >&2
    exit 2
fi

case "${4:-all}" in
    all) catalog_indices=(0 1 2) ;;
    petta) catalog_indices=(0) ;;
    nik) catalog_indices=(1) ;;
    zero) catalog_indices=(2) ;;
    *) echo 'unknown provider-catalog client' >&2; exit 2 ;;
esac

catalog_tool=$(realpath "$1")
catalog_script=$(realpath "$0")
catalog_repo=$(cd "$(dirname "$catalog_script")/../.." && pwd -P)
test -x "$catalog_tool"
catalog_tool_sha=$(sha256sum "$catalog_tool")
catalog_tool_sha=${catalog_tool_sha%% *}
mkdir -p "$2"
catalog_evidence=$(realpath "$2")
test ! -e "$catalog_evidence/inputs.sha256"
cd "$catalog_repo"
catalog_reference=$(realpath "${3:-src/generated}")

catalog_sources=(
    langdef/petta/typecheck_v3_core_provider_catalog_v1.metta
    langdef/prime/nik_side_condition_provider_catalog_v1.metta
    langdef/zero/interact_provider_catalog_v1.metta)
catalog_manifests=(
    langdef/petta/typecheck_v3_core_runtime_v1.metta
    langdef/prime/nik_runtime_v1.metta
    langdef/zero/langdef.metta)
catalog_stems=(petta_typecheck_v3_core_provider_catalog_v1
               prime_nik_side_condition_provider_catalog_v1
               zero_interact_provider_catalog_v1)
catalog_semantics=(
    langdef/petta/typecheck_v3_core_v1.metta
    langdef/prime/nik_authority_runtime_v1.metta
    langdef/shared/finite_horn_quote_match_v1.metta
    langdef/zero/semantics/query_kernel_v1.metta
    langdef/zero/semantics/closed_bag_observation_v1.metta
    langdef/zero/semantics/open_substitution_v1.metta
    langdef/zero/semantics/support_indexed_abt_match_v1.metta
    langdef/zero/semantics/revisioned_interact_v1.metta)
catalog_pinned=("$catalog_tool" "$catalog_script")
for catalog_index in "${catalog_indices[@]}"; do
    catalog_stem=${catalog_stems[$catalog_index]}
    catalog_pinned+=("${catalog_sources[$catalog_index]}"
                    "${catalog_manifests[$catalog_index]}"
                    "$catalog_reference/$catalog_stem.generated.c"
                    "$catalog_reference/$catalog_stem.generated.h")
    case "$catalog_index" in
        0|1) catalog_pinned+=("${catalog_semantics[$catalog_index]}") ;;
        2) catalog_pinned+=("${catalog_semantics[@]:2}") ;;
    esac
done
catalog_native_sources=(src/gslt_provider_runtime.c src/parser.c src/atom.c
                        src/symbol.c src/name_key.c src/native_sha256.c)
catalog_observer=tests/support/test_gslt_provider_catalog_descriptor_v1.c
catalog_fixture_dir=tests/support/gslt_provider_catalog_native_v1
catalog_fixtures=("$catalog_fixture_dir/manifest.metta"
                  "$catalog_fixture_dir/semantics.metta"
                  "$catalog_fixture_dir/catalog.metta"
                  "$catalog_fixture_dir/catalog_utf8.metta")
catalog_pinned+=("${catalog_native_sources[@]}" src/gslt_provider_runtime.h
                src/generated/cetta_execution_contracts.generated.h
                "$catalog_observer" "${catalog_fixtures[@]}")
sha256sum "${catalog_pinned[@]}" > "$catalog_evidence/inputs.sha256"

catalog_cc=${CC:-cc}
read -r -a catalog_cflags <<< "${CFLAGS:--O2 -Wall -Werror}"
catalog_cflags+=(-std=c11 -pthread -ffunction-sections -fdata-sections
                 -Isrc -I. -DCETTA_BUILD_WITH_GMP=0
                 -DCETTA_BUILD_WITH_RUNTIME_STATS=0)
catalog_objects=()
for catalog_source in "${catalog_native_sources[@]}"; do
    catalog_object="$catalog_evidence/$(basename "${catalog_source%.c}").o"
    "$catalog_cc" "${catalog_cflags[@]}" -c "$catalog_source" -o "$catalog_object" \
        >> "$catalog_evidence/compile.stdout" 2>> "$catalog_evidence/compile.stderr"
    catalog_objects+=("$catalog_object")
done

"$catalog_cc" "${catalog_cflags[@]}" -c "$catalog_observer" \
    -o "$catalog_evidence/compare_catalogs.o" \
    >> "$catalog_evidence/compile.stdout" 2>> "$catalog_evidence/compile.stderr"

for catalog_index in "${catalog_indices[@]}"; do
    catalog_stem=${catalog_stems[$catalog_index]}
    catalog_symbol="cetta_$catalog_stem"
    catalog_dir="$catalog_evidence/$catalog_stem"
    mkdir -p "$catalog_dir/first" "$catalog_dir/second"
    for catalog_trial in first second; do
        "$catalog_tool" --catalog "${catalog_sources[$catalog_index]}" \
            --language-manifest "${catalog_manifests[$catalog_index]}" \
            --source-root langdef --symbol "$catalog_symbol" \
            --header "$catalog_dir/$catalog_trial/catalog.generated.h" \
            --source "$catalog_dir/$catalog_trial/catalog.generated.c" \
            --header-include catalog.generated.h \
            > "$catalog_dir/$catalog_trial/generate.stdout" \
            2> "$catalog_dir/$catalog_trial/generate.stderr"
        test ! -s "$catalog_dir/$catalog_trial/generate.stderr"
    done
    cmp "$catalog_dir/first/catalog.generated.h" "$catalog_dir/second/catalog.generated.h"
    cmp "$catalog_dir/first/catalog.generated.c" "$catalog_dir/second/catalog.generated.c"
    {
        "$catalog_cc" "${catalog_cflags[@]}" \
            "-D$catalog_symbol=metadata_native_catalog_v1" \
            -c "$catalog_dir/first/catalog.generated.c" -o "$catalog_dir/native.o"
        "$catalog_cc" "${catalog_cflags[@]}" \
            "-D$catalog_symbol=metadata_retained_catalog_v1" \
            -c "$catalog_reference/$catalog_stem.generated.c" -o "$catalog_dir/retained.o"
        "$catalog_cc" "${catalog_cflags[@]}" -Wl,--gc-sections \
            "$catalog_evidence/compare_catalogs.o" "$catalog_dir/native.o" \
            "$catalog_dir/retained.o" "${catalog_objects[@]}" -lm \
            -o "$catalog_dir/compare_catalogs"
    } >> "$catalog_evidence/compile.stdout" 2>> "$catalog_evidence/compile.stderr"
    "$catalog_dir/compare_catalogs" "$catalog_tool_sha" > "$catalog_dir/compare.stdout" \
        2> "$catalog_dir/compare.stderr"
    test ! -s "$catalog_dir/compare.stderr"
done

catalog_controls="$catalog_evidence/controls"
mkdir -p "$catalog_controls"
cp "${catalog_fixtures[@]}" "$catalog_controls/"

catalog_generate_control() {
    local label=$1 source=$2 manifest=$3 symbol=$4 include=$5
    local directory="$catalog_controls/$label"
    mkdir -p "$directory"
    "$catalog_tool" --catalog "$source" --language-manifest "$manifest" \
        --source-root "$catalog_controls" --symbol "$symbol" \
        --header "$directory/catalog.generated.h" \
        --source "$directory/catalog.generated.c" --header-include "$include" \
        > "$directory/generate.stdout" 2> "$directory/generate.stderr"
}

catalog_reject() {
    local label=$1
    local diagnostic
    shift
    if catalog_generate_control "$label" "$@"; then
        echo "native catalog unexpectedly accepted $label" >&2
        exit 1
    fi
    test -s "$catalog_controls/$label/generate.stderr"
    test ! -e "$catalog_controls/$label/catalog.generated.h"
    test ! -e "$catalog_controls/$label/catalog.generated.c"
    case "$label" in
        unknown) diagnostic='provider is absent from authored signature' ;;
        authored-head) diagnostic='provider would override an authored rule' ;;
        duplicate) diagnostic='semantic-provider requirement is declared twice' ;;
        invalid-symbol|keyword-symbol|invalid-include) diagnostic='usage: gslt-provider-catalog-v1' ;;
        equations) diagnostic='provider source admission does not admit equations' ;;
        *) echo "unclassified native catalog refusal: $label" >&2; exit 1 ;;
    esac
    rg -Fq "$diagnostic" "$catalog_controls/$label/generate.stderr"
    if rg -q 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
        "$catalog_controls/$label/generate.stderr"; then
        echo "native catalog sanitizer fault during $label" >&2
        exit 1
    fi
    printf '%s\trefused\n' "$label" >> "$catalog_evidence/refusals.tsv"
}

catalog_generate_control positive "$catalog_controls/catalog.metta" \
    "$catalog_controls/manifest.metta" native_catalog_control catalog.generated.h
test ! -s "$catalog_controls/positive/generate.stderr"
"$catalog_cc" "${catalog_cflags[@]}" \
    -c "$catalog_controls/positive/catalog.generated.c" \
    -o "$catalog_controls/positive/catalog.o" \
    >> "$catalog_evidence/compile.stdout" 2>> "$catalog_evidence/compile.stderr"

catalog_generate_control utf8 "$catalog_controls/catalog_utf8.metta" \
    "$catalog_controls/manifest.metta" metadata_native_catalog_v1 catalog.generated.h
test ! -s "$catalog_controls/utf8/generate.stderr"
"$catalog_cc" "${catalog_cflags[@]}" -DCATALOG_UTF8_CONTROL=1 \
    "$catalog_observer" "$catalog_controls/utf8/catalog.generated.c" \
    "${catalog_objects[@]}" -Wl,--gc-sections -lm \
    -o "$catalog_controls/utf8/verify_catalog" \
    >> "$catalog_evidence/compile.stdout" 2>> "$catalog_evidence/compile.stderr"
"$catalog_controls/utf8/verify_catalog" "$catalog_tool_sha" > "$catalog_controls/utf8/verify.stdout" \
    2> "$catalog_controls/utf8/verify.stderr"
test ! -s "$catalog_controls/utf8/verify.stderr"

sed 's/(provider external /(provider unknown /' "$catalog_controls/catalog.metta" \
    > "$catalog_controls/unknown.metta"
sed 's/(provider external /(provider local /' "$catalog_controls/catalog.metta" \
    > "$catalog_controls/authored-head.metta"
sed 's/(provider external 1 "control.external.v1"))/(provider external 1 "control.external.v1") (provider external 1 "control.duplicate.v1"))/' \
    "$catalog_controls/catalog.metta" > "$catalog_controls/duplicate.metta"
for catalog_case in unknown authored-head duplicate; do
    cmp -s "$catalog_controls/catalog.metta" "$catalog_controls/$catalog_case.metta" && exit 1
    catalog_reject "$catalog_case" "$catalog_controls/$catalog_case.metta" \
        "$catalog_controls/manifest.metta" native_catalog_control catalog.generated.h
done
catalog_reject invalid-symbol "$catalog_controls/catalog.metta" \
    "$catalog_controls/manifest.metta" 'invalid-symbol' catalog.generated.h
catalog_reject keyword-symbol "$catalog_controls/catalog.metta" \
    "$catalog_controls/manifest.metta" static catalog.generated.h
catalog_reject invalid-include "$catalog_controls/catalog.metta" \
    "$catalog_controls/manifest.metta" native_catalog_control $'bad"\n#include <other.h>'

sed 's/(equations)/(equations (equation local-equality (local ?x) (external ?x)))/' \
    "$catalog_controls/semantics.metta" > "$catalog_controls/equations.metta"
sed 's/semantics.metta/equations.metta/' "$catalog_controls/manifest.metta" \
    > "$catalog_controls/equations-manifest.metta"
catalog_reject equations "$catalog_controls/catalog.metta" \
    "$catalog_controls/equations-manifest.metta" native_catalog_control catalog.generated.h

catalog_alias_reject() {
    local label=$1 header=$2 source=$3
    local directory="$catalog_controls/$label"
    local header_exists=0 source_exists=0
    local header_link='' source_link=''
    mkdir -p "$directory"
    if [[ -L $header ]]; then header_link=$(readlink "$header"); fi
    if [[ -L $source ]]; then source_link=$(readlink "$source"); fi
    if [[ -e $header ]]; then
        header_exists=1
        sha256sum "$header" > "$directory/header-before.sha256"
    fi
    if [[ -e $source ]]; then
        source_exists=1
        sha256sum "$source" > "$directory/source-before.sha256"
    fi
    if "$catalog_tool" --catalog "$catalog_controls/catalog.metta" \
        --language-manifest "$catalog_controls/manifest.metta" \
        --source-root "$catalog_controls" --symbol native_catalog_control \
        --header "$header" --source "$source" --header-include catalog.generated.h \
        > "$directory/generate.stdout" 2> "$directory/generate.stderr"; then
        echo "native catalog unexpectedly accepted output alias: $label" >&2
        exit 1
    fi
    rg -q '^usage:|aliases' "$directory/generate.stderr"
    if rg -q 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
        "$directory/generate.stderr"; then
        echo "native catalog sanitizer fault during $label" >&2
        exit 1
    fi
    if (( header_exists )); then
        sha256sum -c "$directory/header-before.sha256" > "$directory/header-after.txt"
    else
        test ! -e "$header"
    fi
    if (( source_exists )); then
        sha256sum -c "$directory/source-before.sha256" > "$directory/source-after.txt"
    else
        test ! -e "$source"
    fi
    if [[ -n $header_link ]]; then test "$(readlink "$header")" = "$header_link"; fi
    if [[ -n $source_link ]]; then test "$(readlink "$source")" = "$source_link"; fi
    sha256sum -c "$catalog_controls/source-inputs.sha256" > "$directory/inputs-after.txt"
    printf '%s\trefused-with-inputs-unchanged\n' "$label" >> "$catalog_evidence/refusals.tsv"
}

sha256sum "$catalog_controls/catalog.metta" "$catalog_controls/manifest.metta" \
    "$catalog_controls/semantics.metta" > "$catalog_controls/source-inputs.sha256"
catalog_alias_reject outputs-identical "$catalog_controls/same.out" "$catalog_controls/same.out"
mkdir -p "$catalog_controls/alias-parent"
catalog_alias_reject outputs-normalized "$catalog_controls/normalized.out" \
    "$catalog_controls/alias-parent/../normalized.out"
cp "$catalog_controls/catalog.metta" "$catalog_controls/hard-output.h"
ln "$catalog_controls/hard-output.h" "$catalog_controls/hard-output.c"
catalog_alias_reject outputs-hardlinked "$catalog_controls/hard-output.h" \
    "$catalog_controls/hard-output.c"
ln -s dangling-output.c "$catalog_controls/dangling-header.h"
catalog_alias_reject outputs-dangling "$catalog_controls/dangling-header.h" \
    "$catalog_controls/dangling-output.c"
for catalog_target in catalog manifest semantics; do
    ln "$catalog_controls/$catalog_target.metta" "$catalog_controls/$catalog_target-hardlink"
    for catalog_alias_kind in direct hardlink; do
        catalog_alias_target="$catalog_controls/$catalog_target.metta"
        if [[ $catalog_alias_kind == hardlink ]]; then
            catalog_alias_target="$catalog_controls/$catalog_target-hardlink"
        fi
        catalog_alias_reject "$catalog_target-$catalog_alias_kind-header" \
            "$catalog_alias_target" "$catalog_controls/unused-output.c"
        catalog_alias_reject "$catalog_target-$catalog_alias_kind-source" \
            "$catalog_controls/unused-output.h" "$catalog_alias_target"
    done
done

sha256sum -c "$catalog_evidence/inputs.sha256" > "$catalog_evidence/inputs-after.txt"
printf 'Native provider catalog: %s client descriptors equivalent, deterministic output, UTF-8/escaping preserved, %s refusals\n' \
    "${#catalog_indices[@]}" "$(wc -l < "$catalog_evidence/refusals.tsv")"
