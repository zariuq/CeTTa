#!/usr/bin/env bash
set -euo pipefail
if (( $# < 2 || $# > 3 )); then
    echo 'usage: check_nik_authority_catalog_native_v1.sh TOOL EVIDENCE_DIRECTORY [REFERENCE_SOURCE]' >&2
    exit 2
fi
nik_tool=$(realpath "$1")
nik_script=$(realpath "$0")
nik_repo=$(cd "$(dirname "$nik_script")/../.." && pwd -P)
mkdir -p "$2"
nik_evidence=$(realpath "$2")
test ! -e "$nik_evidence/inputs.sha256"
cd "$nik_repo"
nik_reference=$(realpath "${3:-src/generated/prime_nik_authorities_v1.generated.c}")
nik_catalog=langdef/prime/nik_authority_catalog_v1.metta
nik_template=tests/support/nik_authority_catalog_native_v1.metta
nik_scope_template=tests/support/nik_authority_catalog_scope_v1.metta
nik_observer=tests/support/test_nik_authority_catalog_native_v1.c
nik_sources=(src/nik_runtime.c src/inference_checker.c src/inference_side_condition_provider.c
    src/abt.c src/gslt_provider_runtime.c src/parser.c src/atom.c src/binding/frame_identity.c src/symbol.c
    src/name_key.c src/native_sha256.c)
sha256sum "$nik_tool" "$nik_script" "$nik_reference" "$nik_catalog" "$nik_template" "$nik_scope_template" \
    "$nik_observer" tools/nik_authority_catalog_v1.c tools/gslt_native_emission_v1.h \
    native/operational_language_def_v1.c src/nik_runtime_internal.h src/nik_runtime.h \
    "${nik_sources[@]}" > "$nik_evidence/inputs.sha256"
nik_cc=${CC:-cc}
read -r -a nik_flags <<< "${CFLAGS:--O2 -Wall -Werror}"
nik_flags+=(-std=c11 -g -pthread -ffunction-sections -fdata-sections -I. -Isrc
    -DCETTA_BUILD_WITH_GMP=0 -DCETTA_BUILD_WITH_RUNTIME_STATS=0)
nik_objects=()
for nik_src in "${nik_sources[@]}"; do
    nik_obj="$nik_evidence/$(basename "${nik_src%.c}").o"
    "$nik_cc" "${nik_flags[@]}" -c "$nik_src" -o "$nik_obj" >> "$nik_evidence/compile.log" 2>&1
    nik_objects+=("$nik_obj")
done

# Fill only the digest field of the authored test template. The presentation
# line is already in the shared Lean wire renderer's canonical form.
nik_fill_digest() {
    local input=$1 output=$2 presentation digest
    presentation=$(sed -n '/^[[:space:]]*(GInferenceLanguageV1 /s/^[[:space:]]*//p' "$input")
    test -n "$presentation"
    digest=$(printf '%s' "$presentation" | sha256sum); digest=${digest%% *}
    sed "s/DIGEST/$digest/" "$input" > "$output"
}

for nik_case in original identity toy empty changed-proof scoped; do
    nik_dir="$nik_evidence/$nik_case"
    mkdir -p "$nik_dir/first" "$nik_dir/second"
    case "$nik_case" in
        original) cp "$nik_catalog" "$nik_dir/catalog.metta" ;;
        identity) sed 's/prime.dtt.calibration/prime.dtt.calibration.changed/' "$nik_catalog" > "$nik_dir/catalog.metta" ;;
        *)
            cp "$nik_template" "$nik_dir/template.metta"
            if [[ $nik_case == scoped ]]; then
                cp "$nik_scope_template" "$nik_dir/template.metta"
            elif [[ $nik_case == empty ]]; then
                sed 's/(GProof (GRuleInst "ax" LNil) PrNil)/""/' "$nik_template" > "$nik_dir/template.metta"
            elif [[ $nik_case == changed-proof ]]; then
                sed 's/(GRuleInst "ax"/(GRuleInst "changed"/' "$nik_template" > "$nik_dir/template.metta"
            fi
            nik_fill_digest "$nik_dir/template.metta" "$nik_dir/catalog.metta"
            ;;
    esac
    for nik_trial in first second; do
        "$nik_tool" --catalog "$nik_dir/catalog.metta" --header "$nik_dir/$nik_trial/catalog.h" \
            --source "$nik_dir/$nik_trial/catalog.c" --symbol cetta_prime_nik_authorities_v1 \
            --header-include catalog.h > "$nik_dir/$nik_trial/generate.stdout" 2> "$nik_dir/$nik_trial/generate.stderr"
        test ! -s "$nik_dir/$nik_trial/generate.stderr"
    done
    cmp "$nik_dir/first/catalog.h" "$nik_dir/second/catalog.h"
    cmp "$nik_dir/first/catalog.c" "$nik_dir/second/catalog.c"
    nik_sha=$(sha256sum "$nik_dir/catalog.metta"); nik_sha=${nik_sha%% *}
    rg -q "Source SHA-256: $nik_sha" "$nik_dir/first/catalog.c"
    "$nik_cc" "${nik_flags[@]}" "$nik_observer" "${nik_objects[@]}" \
        "-DNIK_CATALOG_REFERENCE_C=\"$nik_reference\"" \
        "-DNIK_CATALOG_GENERATED_C=\"$nik_dir/first/catalog.c\"" \
        -Wl,--gc-sections -lm -o "$nik_dir/observe" >> "$nik_evidence/compile.log" 2>&1
    "$nik_dir/observe" "$nik_case" > "$nik_dir/observe.log" 2>&1
    cat "$nik_dir/observe.log"
done

nik_controls="$nik_evidence/controls"
mkdir -p "$nik_controls"
cp "$nik_evidence/toy/catalog.metta" "$nik_controls/input.metta"
cp "$nik_evidence/toy/first/catalog.h" "$nik_controls/retained.h"
cp "$nik_evidence/toy/first/catalog.c" "$nik_controls/retained.c"
nik_refusals=0
nik_refuse() {
    local label=$1
    shift
    cp "$nik_controls/retained.h" "$nik_controls/output.h"
    cp "$nik_controls/retained.c" "$nik_controls/output.c"
    if "$nik_tool" "$@" > "$nik_controls/$label.stdout" 2> "$nik_controls/$label.stderr"; then
        echo "unexpected registry acceptance: $label" >&2; exit 1
    fi
    rg -q 'NikAuthorityCatalogProjectionError:|error:|usage:' "$nik_controls/$label.stderr"
    cmp "$nik_controls/output.h" "$nik_controls/retained.h"
    cmp "$nik_controls/output.c" "$nik_controls/retained.c"
    cmp "$nik_controls/input.metta" "$nik_evidence/toy/catalog.metta"
    nik_refusals=$((nik_refusals + 1))
}
nik_args=(--catalog "$nik_controls/input.metta" --header "$nik_controls/output.h"
    --source "$nik_controls/output.c" --symbol cetta_prime_nik_authorities_v1 --header-include catalog.h)
nik_mutant() {
    local label=$1 script=$2
    sed "$script" "$nik_template" > "$nik_controls/$label.template"
    if cmp -s "$nik_template" "$nik_controls/$label.template"; then
        echo "ineffective mutation: $label" >&2; exit 1
    fi
    nik_fill_digest "$nik_controls/$label.template" "$nik_controls/$label.metta"
    nik_refuse "$label" --catalog "$nik_controls/$label.metta" "${nik_args[@]:2}"
}
nik_mutant version 's/GInferenceLanguageV1 1/GInferenceLanguageV1 2/'
nik_mutant declaration-arity 's/(JDecl "J" 1)/(JDecl "J" 2)/'
nik_mutant negative-arity 's/(JDecl "J" 1)/(JDecl "J" -1)/'
nik_mutant overflowing-arity 's/(JDecl "J" 1)/(JDecl "J" 18446744073709551616)/'
nik_mutant malformed-list 's/(JDecl "J" 1) LNil/(JDecl "J" 1) BadTail/'
nik_mutant formal-unscoped 's/(GRuleV1 "ax" LNil LNil/(GRuleV1 "ax" (LCons (Formal "unused" 1) LNil) LNil/'
nik_mutant bad-conversion 's/GNoConversion/(GConversion "missing" "1")/'
nik_mutant empty-identity 's/"1" "DIGEST"/"" "DIGEST"/'
nik_mutant embedded-nul 's/test[.]/nul\\x00/'
nik_mutant extra-field 's/(authority TEST/(authority EXTRA TEST/'
nik_mutant bad-positive-tag 's/(positive /(not-positive /'
for nik_scope_case in unscoped-index named-binder; do
    case "$nik_scope_case" in
        unscoped-index) nik_scope_edit='s/(Var 0)/(Var 1)/g' ;;
        named-binder) nik_scope_edit='s/PLam BNone/PLam BNamed/g' ;;
    esac
    sed "$nik_scope_edit" "$nik_scope_template" > "$nik_controls/$nik_scope_case.template"
    nik_fill_digest "$nik_controls/$nik_scope_case.template" "$nik_controls/$nik_scope_case.metta"
    nik_refuse "$nik_scope_case" --catalog "$nik_controls/$nik_scope_case.metta" "${nik_args[@]:2}"
done
sed 's/"[0-9a-f]\{64\}"/"0000000000000000000000000000000000000000000000000000000000000000"/' \
    "$nik_controls/input.metta" > "$nik_controls/stale.metta"
nik_refuse stale-digest --catalog "$nik_controls/stale.metta" "${nik_args[@]:2}"
sed 's/(authority HOTG /(authority DTT /' "$nik_catalog" > "$nik_controls/duplicate.metta"
nik_refuse duplicate-alias --catalog "$nik_controls/duplicate.metta" "${nik_args[@]:2}"
nik_refuse no-args
nik_refuse unpaired --catalog
nik_refuse repeated "${nik_args[@]}" --catalog "$nik_controls/input.metta"
nik_refuse unknown "${nik_args[@]}" --semantic not-a-replay-generator
nik_refuse keyword "${nik_args[@]:0:6}" --symbol static --header-include catalog.h
nik_refuse bad-include "${nik_args[@]:0:8}" --header-include 'bad"include'
nik_refuse paired-alias --catalog "$nik_controls/input.metta" --header "$nik_controls/output.h" \
    --source "$nik_controls/output.h" "${nik_args[@]:6}"
ln "$nik_controls/input.metta" "$nik_controls/hardlink"
ln -s input.metta "$nik_controls/symlink"
ln -s absent "$nik_controls/dangling"
for nik_alias in input.metta hardlink symlink dangling; do
    nik_refuse "alias-$nik_alias" --catalog "$nik_controls/input.metta" --header "$nik_controls/$nik_alias" \
        --source "$nik_controls/output.c" "${nik_args[@]:6}"
done
test -L "$nik_controls/dangling"
test ! -e "$nik_controls/absent"
sha256sum -c "$nik_evidence/inputs.sha256" > "$nik_evidence/inputs-unchanged.log"
printf 'Native NIK registry projection: 6 deterministic cases, full ABI/native-check agreement, %s refusals.\n' "$nik_refusals"
