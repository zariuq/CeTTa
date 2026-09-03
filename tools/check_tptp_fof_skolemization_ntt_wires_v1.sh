#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <Mettapedia-root>" >&2
    exit 2
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mettapedia_root=$1
generated_dir="$project_root/runtime/bootstrap"
source_wire="$project_root/langdef/tptp/fof_skolemization_v1.metta"
generated_language_wire="$project_root/langdef/tptp/generated/fof_skolemization_ntt_language_v1.metta"
generated_inference_wire="$project_root/langdef/tptp/generated/fof_skolemization_ntt_inference_v1.metta"
generated_source_wire="$project_root/langdef/tptp/generated/fof_skolemization_ntt_source_v1.metta"

mkdir -p "$generated_dir"
source_candidate=$(mktemp "$generated_dir/tptp-fof-skolemization.XXXXXX")
language_candidate=$(mktemp "$generated_dir/tptp-fof-skolemization-ntt-language.XXXXXX")
inference_candidate=$(mktemp "$generated_dir/tptp-fof-skolemization-ntt-inference.XXXXXX")
source_package_candidate=$(mktemp "$generated_dir/tptp-fof-skolemization-ntt-source.XXXXXX")
trap 'rm -f "$source_candidate" "$language_candidate" "$inference_candidate" "$source_package_candidate"' EXIT INT TERM

cd "$mettapedia_root/lean/mettapedia"
lake env lean --run \
    Mettapedia/OSLF/Tools/ExportTptpFofSkolemizationNTT.lean \
    "$source_candidate" "$language_candidate" "$inference_candidate" \
    "$source_package_candidate"

if ! cmp -s "$source_candidate" "$source_wire"; then
    echo "generated TPTP FOF Skolemization language differs from the checked artifact" >&2
    exit 1
fi

if ! cmp -s "$language_candidate" "$generated_language_wire"; then
    echo "generated TPTP FOF Skolemization NTT language differs from the checked artifact" >&2
    exit 1
fi

if ! cmp -s "$inference_candidate" "$generated_inference_wire"; then
    echo "generated TPTP FOF Skolemization NTT inference layer differs from the checked artifact" >&2
    exit 1
fi

if ! cmp -s "$source_package_candidate" "$generated_source_wire"; then
    echo "generated TPTP FOF Skolemization checked-source package differs from the checked artifact" >&2
    exit 1
fi

expected_object_digest=$(
    grep -o 'sha256:[0-9a-f]\{64\}' "$source_package_candidate" | head -n 1
)
actual_object_digest="sha256:$(sha256sum "$language_candidate" | awk '{print $1}')"
if [ "$expected_object_digest" != "$actual_object_digest" ]; then
    echo "generated TPTP FOF Skolemization source package is stale for its object-language wire" >&2
    exit 1
fi

echo "PASS: TPTP FOF Skolemization NTT wires are byte-identical and revision-bound to one Lean definition"
