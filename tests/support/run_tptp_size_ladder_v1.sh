#!/usr/bin/env bash
# Per-stage time and peak RSS on the frozen TPTP reader size ladder.
set -euo pipefail
if (( $# != 4 )); then
  echo 'usage: run_tptp_size_ladder_v1.sh LADDER SNAPSHOT CORPUS_ROOT OUT_TSV' >&2
  exit 2
fi
root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
ladder=$(realpath "$1")
snapshot=$(realpath "$2")
corpus=$(realpath "$3")
out_parent=$(dirname -- "$4")
mkdir -p "$out_parent"
out_parent=$(cd -- "$out_parent" && pwd -P)
out="$out_parent/$(basename -- "$4")"
cd "$root"

puz="$corpus/Problems/PUZ/PUZ001-1.p"
kb30="$corpus/Problems/ITP/ITP002_1.p"
itp="$corpus/Problems/ITP/ITP022+5.p"
mb1="$corpus/Problems/ITP/ITP024+5.p"
deep="$corpus/Problems/SYO/SYO587+1.p"
large="$corpus/Problems/HWV/HWV134-1.p"
temporary=$(mktemp "$out_parent/tptp-size-ladder-v1.XXXXXX")
load_log="${out%.tsv}.load.log"
trap 'rm -f "$temporary"' EXIT INT TERM
: > "$load_log"

run_one() {
  local label="$1" file="$2"
  test -f "$file"
  if [[ -n "${TPTP_SIZE_LADDER_TIMEOUT_SECONDS:-}" ]]; then
    timeout --signal=KILL "$TPTP_SIZE_LADDER_TIMEOUT_SECONDS" \
      "$ladder" "$snapshot" "$file" \
      > "$temporary" 2>> "$load_log"
  else
    "$ladder" "$snapshot" "$file" \
      > "$temporary" 2>> "$load_log"
  fi
  if [[ ! -s "$out" ]]; then
    head -n 1 "$temporary" > "$out"
  fi
  tail -n +2 "$temporary" >> "$out"
  printf '%s\t%s\n' "$label" "$file" >> "$load_log"
}

rm -f "$out"
run_one puz001-1 "$puz"
run_one itp002 "$kb30"
run_one itp022 "$itp"
run_one itp024 "$mb1"
run_one syo587 "$deep"
run_one hwv134 "$large"
