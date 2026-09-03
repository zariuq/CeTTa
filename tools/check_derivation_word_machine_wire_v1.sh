#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <Mettapedia-root>" >&2
    exit 2
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mettapedia_root=$1
generated_dir="$project_root/runtime/bootstrap"
checked_wire="$project_root/langdef/logic/derivation_word_machine_v1.metta"

mkdir -p "$generated_dir"
candidate=$(mktemp "$generated_dir/derivation-word-machine.XXXXXX")
trap 'rm -f "$candidate"' EXIT INT TERM

(
    cd "$mettapedia_root/lean/mettapedia"
    printf '%s\n' \
        'import Mettapedia.GSLT.LanguageDef.DerivationWordMachineLanguageDef' \
        'open Mettapedia.GSLT.LanguageDef' \
        '#eval IO.println DerivationWordMachineLanguageDef.wire' |
        lake env lean --stdin
) >"$candidate"

if ! cmp -s "$candidate" "$checked_wire"; then
    echo "generated derivation-word-machine wire differs from the checked artifact" >&2
    exit 1
fi

echo "PASS: derivation-word-machine wire is byte-identical to its Lean definition"
