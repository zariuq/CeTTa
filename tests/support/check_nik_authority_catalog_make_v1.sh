#!/usr/bin/env bash
set -euo pipefail
if (( $# < 3 )); then
    echo 'usage: check_nik_authority_catalog_make_v1.sh TOOL EVIDENCE_DIRECTORY MAKE [MAKE_ARGUMENT ...]' >&2
    exit 2
fi
nik_tool=$(realpath "$1")
mkdir -p "$2"
nik_evidence=$(realpath "$2")
nik_make=$3
shift 3
nik_script=$(realpath "$0")
nik_repo=$(cd "$(dirname "$nik_script")/../.." && pwd -P)
cd "$nik_repo"
test ! -e "$nik_evidence/inputs.sha256"
sha256sum "$nik_tool" "$nik_script" Makefile \
    langdef/prime/nik_authority_catalog_v1.metta > "$nik_evidence/inputs.sha256"
cp langdef/prime/nik_authority_catalog_v1.metta "$nik_evidence/catalog.metta"
nik_header="$nik_evidence/catalog.h"
nik_source="$nik_evidence/catalog.c"
nik_make_args=(--no-print-directory "$@" -o "$nik_tool"
    "NIK_AUTHORITY_CATALOG_NATIVE_V1_BIN=$nik_tool"
    "PRIME_NIK_AUTHORITY_CATALOG_V1=$nik_evidence/catalog.metta"
    "PRIME_NIK_AUTHORITIES_GENERATED_H=$nik_header"
    "PRIME_NIK_AUTHORITIES_GENERATED_C=$nik_source")

# Request both grouped outputs, as the public compilation and native gate do.
# Each missing-peer check retains the displaced file for inspection.
"$nik_make" "${nik_make_args[@]}" "$nik_header" "$nik_source" \
    > "$nik_evidence/initial.log" 2>&1
test -s "$nik_header" && test -s "$nik_source"
cp "$nik_header" "$nik_evidence/expected.h"
cp "$nik_source" "$nik_evidence/expected.c"
mv "$nik_header" "$nik_evidence/displaced.h"
"$nik_make" "${nik_make_args[@]}" "$nik_header" "$nik_source" \
    > "$nik_evidence/missing-header.log" 2>&1
cmp "$nik_header" "$nik_evidence/expected.h"
cmp "$nik_source" "$nik_evidence/expected.c"
mv "$nik_source" "$nik_evidence/displaced.c"
"$nik_make" "${nik_make_args[@]}" "$nik_header" "$nik_source" \
    > "$nik_evidence/missing-source.log" 2>&1
cmp "$nik_header" "$nik_evidence/expected.h"
cmp "$nik_source" "$nik_evidence/expected.c"

# A real admission failure must not publish either staged output.
sed 's/GInferenceLanguageV1/GUnsupportedPresentationV1/g' \
    langdef/prime/nik_authority_catalog_v1.metta > "$nik_evidence/invalid.metta"
if "$nik_make" "${nik_make_args[@]}" \
    "PRIME_NIK_AUTHORITY_CATALOG_V1=$nik_evidence/invalid.metta" \
    -W "$nik_evidence/invalid.metta" "$nik_header" "$nik_source" \
    > "$nik_evidence/refusal.log" 2>&1; then
    echo 'unexpected Make publication for an invalid authority catalog' >&2
    exit 1
fi
rg -q 'NikAuthorityCatalogProjectionError:' "$nik_evidence/refusal.log"
cmp "$nik_header" "$nik_evidence/expected.h"
cmp "$nik_source" "$nik_evidence/expected.c"
sha256sum -c "$nik_evidence/inputs.sha256" > "$nik_evidence/inputs-unchanged.log"
printf 'Native NIK registry Make publication: both missing peers repaired; failed generation preserves both outputs.\n'
