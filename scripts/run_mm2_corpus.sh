#!/usr/bin/env bash
# Sweep the MM2 corpus through CeTTa's MM2 lane and compare against the
# recorded manifest status.
#
# The corpus deliberately mixes provenance: CeTTa's own examples, tests and
# conformance fixtures, MORK's upstream kernel resources, and mm2-chainer's
# examples pinned to a DIFFERENT MORK fork (marked fork-skip: their kernel is
# not ours, so a failure there says nothing about CeTTa).  External files are
# referenced in place rather than vendored, so
# rows whose source checkout is absent are reported as `missing`, not failed.
#
# Requires a bridge-enabled binary: build with `make BUILD=mork cetta`.
# Without it every row reports a bridge error, which the sweep reveals
# rather than silently timing.
#
#   usage: scripts/run_mm2_corpus.sh [--update]
#          --update  rewrite manifest.tsv status/atoms from this run
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$root/benchmarks/mm2_corpus/manifest.tsv"
bin="$root/cetta"
: "${AIHUB:=$(cd "$root/../.." && pwd)}"
export AIHUB

[ -x "$bin" ] || { echo "no cetta binary at $bin (make BUILD=mork cetta)" >&2; exit 2; }
[ -f "$manifest" ] || { echo "no manifest at $manifest" >&2; exit 2; }

update=0
[ "${1:-}" = "--update" ] && update=1

pass=0; regress=0; missing=0; skipped=0; total=0
rows=""

while IFS=$'\t' read -r name origin path want_status want_atoms; do
    [ "$name" = "name" ] && continue
    total=$((total + 1))
    if [ "$want_status" = "fork-skip" ]; then
        skipped=$((skipped + 1))
        rows+="$name\t$origin\t$path\tfork-skip\t0\n"
        continue
    fi
    file="$(eval echo "$path")"
    if [ ! -f "$file" ]; then
        missing=$((missing + 1))
        printf '%-44s %-18s MISSING\n' "$name" "$origin"
        rows+="$name\t$origin\t$path\tmissing\t0\n"
        continue
    fi
    out="$(timeout 60 "$bin" --lang mm2 "$file" 2>/dev/null | tr -d "\0")"
    if [ $? -ne 0 ]; then
        status=mork-panic; atoms=0
    else
        status=runs; atoms="$(printf '%s' "$out" | grep -c .)"
    fi
    rows+="$name\t$origin\t$path\t$status\t$atoms\n"
    if [ "$status" = "$want_status" ] && [ "$atoms" = "$want_atoms" ]; then
        pass=$((pass + 1))
    else
        regress=$((regress + 1))
        printf '%-44s %-18s %s/%s -> %s/%s\n' \
            "$name" "$origin" "$want_status" "$want_atoms" "$status" "$atoms"
    fi
done < "$manifest"

if [ "$update" = 1 ]; then
    { printf 'name\torigin\tpath\tstatus\tatoms\n'; printf "$rows"; } > "$manifest"
    echo "manifest updated"
fi

echo "(Mm2CorpusSummary total=$total agree=$pass differ=$regress missing=$missing skipped=$skipped)"
[ "$regress" -eq 0 ]
