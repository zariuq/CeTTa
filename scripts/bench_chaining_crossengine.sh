#!/usr/bin/env bash
# Cross-engine chaining benchmark driver for CeTTa (--lang he) vs PeTTa (run.sh).
# Chainers from github.com/ngeiswei/chaining and github.com/rTreutlein/PeTTaChainer;
# PeTTa engine from github.com/patham9/PeTTa.
# Runs each manifest row on both engines, checks the per-row observable contract
# CROSS-ENGINE (never CeTTa's own output as golden), and reports wall-clock + result
# count. Modeled on bench_compare_cetta_petta.sh / bench_metamath_d5.sh.
#
# NOTE: timings are single-run unless REPEATS>1; witness rows compare result VALUES.
#
# Usage: bench_chaining_crossengine.sh [--repeats N] [--only ID] [--timeout SEC]
#        Roman generated rows also accept a small depth/branch grid (see below).
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CETTA="${CETTA_BIN:-$ROOT/cetta}"
PETTA_DIR="${PETTA_DIR:-$ROOT/../PeTTa}"
PETTA_RUN="$PETTA_DIR/run.sh"
BENCH="$ROOT/benchmarks/chaining_crossengine"
MANIFEST="$BENCH/manifest.tsv"
REPEATS="${REPEATS:-1}"
TIMEOUT="${TIMEOUT:-300}"
ONLY="${ONLY:-}"

[[ -x "$CETTA" ]] || { echo "error: cetta not found at $CETTA" >&2; exit 1; }
[[ -x "$PETTA_RUN" ]] || { echo "error: PeTTa run.sh not found at $PETTA_RUN" >&2; exit 1; }

# Min wall-clock over REPEATS for: cetta count-only on $1
cetta_time(){ local f="$1" best="" t; for ((r=0;r<REPEATS;r++)); do
  t=$( { /usr/bin/time -f '%e' timeout "$TIMEOUT" "$CETTA" --lang he --count-only "$f" >/dev/null; } 2>&1 )
  [[ -z "$best" || $(awk -v a="$t" -v b="$best" 'BEGIN{print (a<b)}') == 1 ]] && best="$t"
done; echo "$best"; }
cetta_count(){ timeout "$TIMEOUT" "$CETTA" --lang he --count-only "$1" 2>/dev/null | tail -1; }
petta_time(){ local f="$1" best="" t; for ((r=0;r<REPEATS;r++)); do
  t=$( cd "$PETTA_DIR" && { /usr/bin/time -f '%e' timeout "$TIMEOUT" ./run.sh "$f" --silent >/dev/null; } 2>&1 | tail -1 )
  [[ -z "$best" || $(awk -v a="$t" -v b="$best" 'BEGIN{print (a<b)}') == 1 ]] && best="$t"
done; echo "$best"; }
petta_count(){ ( cd "$PETTA_DIR" && timeout "$TIMEOUT" ./run.sh "$1" --silent 2>/dev/null | grep -cE '\(: ' || true ); }
# Extract meaningful result lines (drop unit/true/empty noise); strip CeTTa's [ ] wrapper.
cetta_results(){ timeout "$TIMEOUT" "$CETTA" --lang he "$1" 2>/dev/null \
  | sed -E 's/^\[(.*)\]$/\1/' | sed -E 's/\), \(/)\n(/g' \
  | grep -vE '^\(\)$|^true$|^$|^"' | sort; }
petta_results(){ ( cd "$PETTA_DIR" && timeout "$TIMEOUT" ./run.sh "$1" --silent 2>/dev/null ) \
  | grep -vE '^\(\)$|^true$|^$|MORK|^"' | sort; }
# Witness gate: same meaningful result VALUES on both engines (value diff, not count).
witness_gate(){ local f="$1"; if diff -q <(cetta_results "$f") <(petta_results "$f") >/dev/null 2>&1; then echo OK; else echo DIFFER; fi; }

printf "%-22s %-10s %-8s %-8s %-9s %-9s %-7s %s\n" id contract cetta_obs petta_obs gate cetta_s petta_s notes
# static-file rows
while IFS=$'\t' read -r id author style file contract expected leatta notes; do
  [[ "$id" == \#* || "$id" == "id" || -z "$id" ]] && continue
  [[ "$file" == *.sh ]] && continue                         # generated rows handled below
  [[ -n "$ONLY" && "$ONLY" != "$id" ]] && continue
  f="$BENCH/$file"
  avail=$(free -m | awk 'NR==2{print $7}')
  if (( avail < 15000 )); then echo "PAUSE: MemAvailable ${avail}MB < 15000MB; skipping $id" >&2; continue; fi
  cn=$(cetta_count "$f"); ct=$(cetta_time "$f")
  if [[ "$contract" == "cetta_only" ]]; then
    pn="n/a"; pt="n/a"; gate="CETTA-ONLY"
  else
    pn=$(petta_count "$f"); pt=$(petta_time "$f")
    case "$contract" in
      count)   gate=$([[ "$cn" == "$pn" && "$cn" == "$expected" ]] && echo OK || echo MISMATCH) ;;
      witness) gate=$(witness_gate "$f"); cn="by-val"; pn="by-val" ;;
      *)       gate="?" ;;
    esac
  fi
  printf "%-22s %-10s %-8s %-8s %-9s %-9s %-7s %s\n" "$id" "$contract" "$cn" "$pn" "$gate" "$ct" "$pt" "$notes"
done < "$MANIFEST"

# Roman generated rows: small depth/branch grid
echo "--- Roman generated grid (gen scripts) ---"
GEN="$BENCH/08_roman_chain_noise"
run_gen(){ local label="$1" script="$2" d="$3" b="$4" expr="$5"
  local f="$GEN/_drv_${label}_d${d}_b${b}.metta"; "$GEN/$script" "$d" "$b" > "$f"
  local exp; exp=$(awk -v b="$b" -v d="$d" "BEGIN{print $expr}")
  local cn ct pn pt; cn=$(cetta_count "$f"); ct=$(cetta_time "$f"); pn=$(petta_count "$f"); pt=$(petta_time "$f")
  local gate; gate=$([[ "$cn" == "$pn" && "$cn" == "$exp" ]] && echo OK || echo MISMATCH)
  printf "%-22s d=%-2s b=%-3s proofs=%-9s cetta_n=%-9s petta_n=%-9s %-9s cetta=%-7s petta=%s\n" \
    "$label" "$d" "$b" "$exp" "$cn" "$pn" "$gate" "$ct" "$pt"
}
run_gen chain_bwd  gen_kb.sh        25 8  "1"
run_gen chain_fwd  gen_kb_forward.sh 10 8  "(d+1)+d*b"
run_gen branchy    gen_kb_branchy.sh 10 1  "(1+b)^d"
echo "done."
