#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: $0 MEGALODON_ROOT [OUTPUT_DIRECTORY]" >&2
    exit 2
fi

megalodon_root=$(realpath "$1")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output_directory=${2:-"$repo_root/.generated/megalodon-oracle"}
mkdir -p "$output_directory"
output_directory=$(realpath "$output_directory")

required_sources=(
    src/hashbtcstub.c
    src/hashbtc.ml
    src/ser.mli
    src/ser.ml
    src/hashaux.mli
    src/hashaux.ml
    src/hash.mli
    src/hash.ml
    src/hashold.mli
    src/hashold.ml
    src/mathdata.mli
    src/mathdata.ml
    src/mathdatapfg.mli
    src/mathdatapfg.ml
    src/syntax.mli
    src/syntax.ml
    src/parser.mli
    src/parser.ml
    src/lexer.mll
    secp256k1/secp256k1.h
    secp256k1/hash_impl.h
)

for source in "${required_sources[@]}"; do
    if [[ ! -f "$megalodon_root/$source" ]]; then
        echo "missing Megalodon reference source: $source" >&2
        exit 1
    fi
done

ocamlc \
    -ccopt "-O2 -fPIC -I $megalodon_root/secp256k1" \
    -c "$megalodon_root/src/hashbtcstub.c" \
    -o "$output_directory/hashbtcstub.o"

ocamlopt -I "$output_directory" \
    -c "$megalodon_root/src/hashbtc.ml" \
    -o "$output_directory/hashbtc.cmx"

for module in ser hashaux hash hashold mathdata mathdatapfg syntax parser; do
    ocamlopt -I "$output_directory" \
        -c "$megalodon_root/src/$module.mli" \
        -o "$output_directory/$module.cmi"
    ocamlopt -I "$output_directory" \
        -c "$megalodon_root/src/$module.ml" \
        -o "$output_directory/$module.cmx"
done

ocamllex -o "$output_directory/lexer.ml" "$megalodon_root/src/lexer.mll"

ocamlopt -I "$output_directory" \
    -c "$output_directory/lexer.ml" \
    -o "$output_directory/lexer.cmx"

# Two oracles over Megalodon's own lexer and parser: its tokens, and its
# document items and proof tactics.
for oracle in token parse; do
    ocamlopt -I "$output_directory" \
        -c "$repo_root/tests/support/megalodon_reference_${oracle}_oracle.ml" \
        -o "$output_directory/megalodon_reference_${oracle}_oracle.cmx"
    ocamlopt -I +unix -I "$output_directory" \
        -o "$output_directory/megalodon-reference-${oracle}-oracle" \
        unix.cmxa \
        "$output_directory/hashbtcstub.o" \
        "$output_directory/hashbtc.cmx" \
        "$output_directory/ser.cmx" \
        "$output_directory/hashaux.cmx" \
        "$output_directory/hash.cmx" \
        "$output_directory/hashold.cmx" \
        "$output_directory/mathdata.cmx" \
        "$output_directory/mathdatapfg.cmx" \
        "$output_directory/syntax.cmx" \
        "$output_directory/parser.cmx" \
        "$output_directory/lexer.cmx" \
        "$output_directory/megalodon_reference_${oracle}_oracle.cmx"
    printf '%s\n' "$output_directory/megalodon-reference-${oracle}-oracle"
done
