#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <Mettapedia-root>" >&2
    exit 2
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mettapedia_root=$1
generated_dir="$project_root/runtime/bootstrap"
checked_data="$project_root/tests/langdef/tptp/official_ground_resolution_refutation_v9200.semantic.metta"

mkdir -p "$generated_dir"
candidate=$(mktemp "$generated_dir/tptp-ground-resolution-semantic-canary.XXXXXX")
trap 'rm -f "$candidate"' EXIT INT TERM

(
    cd "$mettapedia_root/lean/mettapedia"
    lake env lean --run \
        Mettapedia/OSLF/Tools/ExportTptpOfficialGroundResolutionCanary.lean \
        "$candidate"
)

if ! cmp -s "$candidate" "$checked_data"; then
    echo "generated official ground-resolution semantic canary differs from the checked artifact" >&2
    exit 1
fi

echo "PASS: official ground-resolution semantic canary is byte-identical to its Lean projection"
