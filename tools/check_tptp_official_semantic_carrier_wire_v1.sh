#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <Mettapedia-root>" >&2
    exit 2
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mettapedia_root=$1
generated_dir="$project_root/runtime/bootstrap"
checked_wire="$project_root/langdef/tptp/official_semantic_carrier_v9200.metta"

mkdir -p "$generated_dir"
candidate=$(mktemp "$generated_dir/tptp-official-semantic-carrier.XXXXXX")
trap 'rm -f "$candidate"' EXIT INT TERM

cd "$mettapedia_root/lean/mettapedia"
lake build Mettapedia.GSLT.LanguageDef.TptpOfficialSemanticCarrierNTT
lake env lean --run Mettapedia/OSLF/Tools/ExportTptpOfficialSemanticCarrier.lean \
    "$candidate"

if ! cmp -s "$candidate" "$checked_wire"; then
    echo "generated official TPTP semantic-carrier wire differs from the checked artifact" >&2
    exit 1
fi

echo "PASS: official TPTP semantic-carrier wire is byte-identical to its Lean projection"
