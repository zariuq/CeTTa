#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <Mettapedia-root>" >&2
    exit 2
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mettapedia_root=$1
candidate_dir=$(mktemp -d \
    "$project_root/runtime/bootstrap/tptp-fof-transformations.XXXXXX")
lock_file="$project_root/langdef/tptp/generated/fof_transformation_language_defs_v1.sha256"
trap 'rm -rf "$candidate_dir"' EXIT INT TERM

cd "$mettapedia_root/lean/mettapedia"
lake env lean --run \
    Mettapedia/OSLF/Tools/ExportTptpFofTransformationLanguageDefs.lean \
    "$candidate_dir"

artifact_names='official_fof_to_named_v1.metta
named_fof_to_resolved_v1.metta
fof_normalization_v1.metta
fof_prenex_normalization_v1.metta
fof_skolemization_v1.metta
fof_definitional_naming_v1.metta
fof_definitional_cnf_generation_v1.metta
fof_clausification_batch_generation_v1.metta
fof_cnf_name_allocation_v1.metta
fof_cnf_official_ast_v1.metta'

# The metadata projection is already a validated LanguageDef but is not yet
# part of the aggregate exporter. Render it from the same Lean authority so
# the checked CeTTa wire remains generated rather than transcribed.
projection_artifact_name=official_fof_batch_projection_v1.metta
(
    cd "$mettapedia_root/lean/mettapedia"
    printf '%s\n' \
        'import Mettapedia.GSLT.LanguageDef.TptpOfficialFofBatchProjectionLanguageDef' \
        'open Mettapedia.OSLF.MeTTaIL.Syntax' \
        'open Mettapedia.GSLT.LanguageDef' \
        '#eval IO.println ((CanonicalWire.renderLanguage? TptpOfficialFofBatchProjectionLanguageDef.language).getD "")' |
        lake env lean --stdin
) >"$candidate_dir/$projection_artifact_name"

artifact_names="$artifact_names
$projection_artifact_name"

artifact_count=0
for artifact_name in $artifact_names; do
    artifact_count=$((artifact_count + 1))
    candidate="$candidate_dir/$artifact_name"
    checked="$project_root/langdef/tptp/$artifact_name"
    if [ ! -s "$candidate" ]; then
        echo "Lean exporter omitted $artifact_name" >&2
        exit 1
    fi
    if ! cmp -s "$candidate" "$checked"; then
        echo "generated TPTP FOF transformation wire differs: $artifact_name" >&2
        exit 1
    fi
done

generated_count=$(find "$candidate_dir" -maxdepth 1 -type f | wc -l)
lock_count=$(wc -l <"$lock_file")
if [ "$artifact_count" -ne 11 ] || [ "$generated_count" -ne 11 ] || \
        [ "$lock_count" -ne 11 ]; then
    echo "TPTP FOF transformation artifact inventory is not exactly eleven" >&2
    exit 1
fi

cd "$project_root"
sha256sum -c "$lock_file" >/dev/null

lock_probe="$candidate_dir/lock-probe"
mkdir -p "$lock_probe/langdef/tptp"
for artifact_name in $artifact_names; do
    cp "$candidate_dir/$artifact_name" \
        "$lock_probe/langdef/tptp/$artifact_name"
done
if ! (cd "$lock_probe" && sha256sum -c "$lock_file" >/dev/null); then
    echo "freshly generated TPTP FOF transformation wires fail their digest lock" >&2
    exit 1
fi
sed 's/TPTPFOFNormalization/TPTPFOFNormalizationMutant/' \
    "$lock_probe/langdef/tptp/fof_normalization_v1.metta" \
    >"$lock_probe/langdef/tptp/fof_normalization_v1.metta.mutant"
mv "$lock_probe/langdef/tptp/fof_normalization_v1.metta.mutant" \
    "$lock_probe/langdef/tptp/fof_normalization_v1.metta"
if (cd "$lock_probe" && sha256sum -c "$lock_file" >/dev/null 2>&1); then
    echo "mutated TPTP FOF normalization wire escaped its digest lock" >&2
    exit 1
fi

echo "PASS: eleven TPTP FOF transformation LanguageDefs are byte-identical to Lean, digest-locked, and mutation-sensitive"
