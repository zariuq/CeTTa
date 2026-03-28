#!/bin/bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/cetta"
BACKEND="${1:-native-subst-tree}"
PROBE="$ROOT/tests/bench_tilepuzzle_probe.metta"

if [ ! -x "$BIN" ]; then
    echo "error: missing executable $BIN" >&2
    echo "hint: run 'make cetta' first" >&2
    exit 1
fi

out_file="$ROOT/runtime/tile_runtime_probe_${BACKEND}.out"
time_file="$ROOT/runtime/tile_runtime_probe_${BACKEND}.time"
mkdir -p "$ROOT/runtime"

/usr/bin/time -v -o "$time_file" \
    bash -lc "ulimit -v 6291456 && cd '$ROOT' && ./cetta --count-only --profile he_extended --space-match-backend '$BACKEND' --lang he '$PROBE'" \
    > "$out_file"

mapfile -t lines < "$out_file"
if [ "${#lines[@]}" -ne 23 ]; then
    echo "error: unexpected output shape for tile runtime probe" >&2
    cat "$out_file" >&2
    exit 1
fi

elapsed="$(grep -F 'Elapsed (wall clock) time' "$time_file" | awk -F': ' '{print $2}')"
rss="$(grep -F 'Maximum resident set size' "$time_file" | awk -F': ' '{print $2}')"

printf 'backend=%s\n' "$BACKEND"
printf 'steps=%s\n' "${lines[2]}"
printf 'seen=%s\n' "${lines[3]}"
printf 'query_equations=%s\n' "${lines[4]}"
printf 'query_candidates=%s\n' "${lines[5]}"
printf 'query_legacy_candidates=%s\n' "${lines[6]}"
printf 'query_subst_candidates=%s\n' "${lines[7]}"
printf 'rename_vars=%s\n' "${lines[8]}"
printf 'hash_exact_lookup=%s\n' "${lines[9]}"
printf 'hash_exact_hit=%s\n' "${lines[10]}"
printf 'space_push=%s\n' "${lines[11]}"
printf 'space_pop=%s\n' "${lines[12]}"
printf 'queue_compact=%s\n' "${lines[13]}"
printf 'resolve_registry_hit=%s\n' "${lines[14]}"
printf 'resolve_registry_rewrite=%s\n' "${lines[15]}"
printf 'resolve_registry_noop=%s\n' "${lines[16]}"
printf 'bindings_clone=%s\n' "${lines[17]}"
printf 'bindings_apply=%s\n' "${lines[18]}"
printf 'bindings_free=%s\n' "${lines[19]}"
printf 'query_subst_emitted=%s\n' "${lines[20]}"
printf 'query_subst_candidate_fallback=%s\n' "${lines[21]}"
printf 'query_subst_bucket_fallback=%s\n' "${lines[22]}"
printf 'wall=%s\n' "$elapsed"
printf 'rss_kb=%s\n' "$rss"
