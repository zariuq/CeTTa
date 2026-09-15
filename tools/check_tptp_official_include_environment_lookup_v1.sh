#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <Mettapedia-root>" >&2
    exit 2
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mettapedia_root=$1
candidate_dir=$(mktemp -d \
    "$project_root/runtime/bootstrap/tptp-include-environment-lookup.XXXXXX")
candidate="$candidate_dir/official_include_environment_lookup_v1.metta"
checked="$project_root/langdef/tptp/official_include_environment_lookup_v1.metta"
lock_file="$project_root/langdef/tptp/generated/official_include_environment_lookup_v1.sha256"
trap 'rm -rf "$candidate_dir"' EXIT INT TERM

cd "$mettapedia_root/lean/mettapedia"
lake env lean --run \
    Mettapedia/OSLF/Tools/ExportTptpOfficialIncludeEnvironmentLookupLanguageDef.lean \
    "$candidate"

if [ ! -s "$candidate" ]; then
    echo "Lean omitted the official include-environment lookup LanguageDef" >&2
    exit 1
fi
if ! cmp -s "$candidate" "$checked"; then
    echo "generated include-environment lookup LanguageDef differs from the checked artifact" >&2
    exit 1
fi

cd "$project_root"
sha256sum -c "$lock_file" >/dev/null

lock_probe="$candidate_dir/lock-probe"
mkdir -p "$lock_probe/langdef/tptp"
cp "$candidate" \
    "$lock_probe/langdef/tptp/official_include_environment_lookup_v1.metta"
if ! (cd "$lock_probe" && sha256sum -c "$lock_file" >/dev/null); then
    echo "freshly generated include-environment lookup LanguageDef fails its digest lock" >&2
    exit 1
fi
sed 's/TptpOfficialIncludeEnvironmentLookupV1/TptpOfficialIncludeEnvironmentLookupMutantV1/' \
    "$lock_probe/langdef/tptp/official_include_environment_lookup_v1.metta" \
    >"$lock_probe/langdef/tptp/official_include_environment_lookup_v1.metta.mutant"
mv "$lock_probe/langdef/tptp/official_include_environment_lookup_v1.metta.mutant" \
    "$lock_probe/langdef/tptp/official_include_environment_lookup_v1.metta"
if (cd "$lock_probe" && sha256sum -c "$lock_file" >/dev/null 2>&1); then
    echo "mutated include-environment lookup LanguageDef escaped its digest lock" >&2
    exit 1
fi

echo "PASS: official include-environment lookup LanguageDef is byte-identical to Lean, digest-locked, and mutation-sensitive"
