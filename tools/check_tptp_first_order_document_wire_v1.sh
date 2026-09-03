#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <Mettapedia-root>" >&2
    exit 2
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mettapedia_root=$1
generated_dir="$project_root/runtime/bootstrap"
checked_wire="$project_root/langdef/logic/tptp_first_order_document_v1.metta"

mkdir -p "$generated_dir"
candidate=$(mktemp "$generated_dir/tptp-first-order-document.XXXXXX")
trap 'rm -f "$candidate"' EXIT INT TERM

cd "$mettapedia_root/lean/mettapedia"
lake env lean --run Mettapedia/OSLF/Tools/ExportTptpFirstOrderDocument.lean \
    "$candidate"

if ! cmp -s "$candidate" "$checked_wire"; then
    echo "generated TPTP first-order-document wire differs from the checked artifact" >&2
    exit 1
fi

echo "PASS: TPTP first-order-document wire is byte-identical to its Lean projection"
