#!/usr/bin/env bash
# Cross-engine chaining benchmark driver for CeTTa HE, CeTTa Prime, and PeTTa.
# Chainers from github.com/ngeiswei/chaining and github.com/rTreutlein/PeTTaChainer;
# PeTTa engine from github.com/patham9/PeTTa.
# Runs each manifest row across the declared engines, checks its observable contract
# CROSS-ENGINE (never one CeTTa lane's own output as golden), and reports
# wall-clock + result count. Modeled on bench_compare_cetta_petta.sh /
# bench_metamath_d5.sh.
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

while (( $# > 0 )); do
  case "$1" in
    --repeats)
      [[ $# -ge 2 ]] || { echo "error: --repeats requires a value" >&2; exit 2; }
      REPEATS="$2"; shift 2 ;;
    --only)
      [[ $# -ge 2 ]] || { echo "error: --only requires a manifest id" >&2; exit 2; }
      ONLY="$2"; shift 2 ;;
    --timeout)
      [[ $# -ge 2 ]] || { echo "error: --timeout requires seconds" >&2; exit 2; }
      TIMEOUT="$2"; shift 2 ;;
    --help|-h)
      sed -n '12p' "$0"; exit 0 ;;
    *)
      echo "error: unknown argument '$1'" >&2; exit 2 ;;
  esac
done

[[ "$REPEATS" =~ ^[1-9][0-9]*$ ]] || {
  echo "error: --repeats must be a positive integer" >&2; exit 2;
}
[[ "$TIMEOUT" =~ ^[1-9][0-9]*$ ]] || {
  echo "error: --timeout must be a positive integer" >&2; exit 2;
}

[[ -x "$CETTA" ]] || { echo "error: cetta not found at $CETTA" >&2; exit 1; }
[[ -x "$PETTA_RUN" ]] || { echo "error: PeTTa run.sh not found at $PETTA_RUN" >&2; exit 1; }

# Min wall-clock over REPEATS for one CeTTa lane and file.
cetta_time(){ local lane="$1" f="$2" best="" t; for ((r=0;r<REPEATS;r++)); do
  t=$( { /usr/bin/time -f '%e' timeout "$TIMEOUT" "$CETTA" --lang "$lane" --count-only "$f" >/dev/null; } 2>&1 )
  [[ -z "$best" || $(awk -v a="$t" -v b="$best" 'BEGIN{print (a<b)}') == 1 ]] && best="$t"
done; echo "$best"; }
cetta_count(){ local lane="$1" f="$2"; timeout "$TIMEOUT" "$CETTA" --lang "$lane" --count-only "$f" 2>/dev/null | tail -1; }
petta_time(){ local f="$1" best="" t; for ((r=0;r<REPEATS;r++)); do
  t=$( cd "$PETTA_DIR" && { /usr/bin/time -f '%e' timeout "$TIMEOUT" ./run.sh "$f" --silent >/dev/null; } 2>&1 | tail -1 )
  [[ -z "$best" || $(awk -v a="$t" -v b="$best" 'BEGIN{print (a<b)}') == 1 ]] && best="$t"
done; echo "$best"; }
petta_count(){ ( cd "$PETTA_DIR" && timeout "$TIMEOUT" ./run.sh "$1" --silent 2>/dev/null | grep -cE '\(: ' || true ); }
# Extract meaningful result lines (drop unit/true/empty noise); strip CeTTa's [ ] wrapper.
cetta_results(){ local lane="$1" f="$2"; timeout "$TIMEOUT" "$CETTA" --lang "$lane" "$f" 2>/dev/null \
  | sed -E 's/^\[(.*)\]$/\1/' | sed -E 's/\), \(/)\n(/g' \
  | grep -vE '^\(\)$|^true$|^$|^"' | sort; }
petta_results(){ ( cd "$PETTA_DIR" && timeout "$TIMEOUT" ./run.sh "$1" --silent 2>/dev/null ) \
  | grep -vE '^\(\)$|^true$|^$|MORK|^"' | sort; }
cetta_value_count(){ cetta_results "$1" "$2" | wc -l | tr -d ' '; }
# Witness gates run lanes sequentially. Some portfolio rows need several GiB in
# Prime, so process-substitution would turn a correctness check into an
# accidental concurrent memory benchmark.
witness_gate(){ local f="$1" d he prime petta verdict="DIFFER"; mkdir -p "$ROOT/runtime"
  d=$(mktemp -d "$ROOT/runtime/chaining-witness.XXXXXX")
  he="$d/he"; prime="$d/prime"; petta="$d/petta"
  if cetta_results he "$f" >"$he" &&
     cetta_results prime "$f" >"$prime" &&
     petta_results "$f" >"$petta" &&
     diff -q "$he" "$prime" >/dev/null 2>&1 &&
     diff -q "$he" "$petta" >/dev/null 2>&1; then
    verdict="OK"
  fi
  rm -f "$he" "$prime" "$petta"; rmdir "$d"
  echo "$verdict"
}
zero_witness_gate(){ local f="$1" zf="$2" expected="$3" d he prime petta zero verdict="DIFFER"; mkdir -p "$ROOT/runtime"
  d=$(mktemp -d "$ROOT/runtime/chaining-zero-witness.XXXXXX")
  he="$d/he"; prime="$d/prime"; petta="$d/petta"; zero="$d/zero"
  if cetta_results he "$f" >"$he" &&
     cetta_results prime "$f" >"$prime" &&
     petta_results "$f" >"$petta" &&
     cetta_results zero "$zf" >"$zero" &&
     diff -q "$he" "$prime" >/dev/null 2>&1 &&
     diff -q "$he" "$petta" >/dev/null 2>&1 &&
     diff -q "$he" "$zero" >/dev/null 2>&1 &&
     [[ "$(wc -l <"$zero" | tr -d ' ')" == "$expected" ]]; then
    verdict="OK"
  fi
  rm -f "$he" "$prime" "$petta" "$zero"; rmdir "$d"
  echo "$verdict"
}

printf "%-22s %-12s %-8s %-9s %-9s %-8s %-9s %-7s %-7s %-7s %-7s %s\n" \
  id contract he_obs prime_obs petta_obs zero_obs gate he_s prime_s petta_s zero_s notes
# static-file rows
while IFS=$'\t' read -r id author style file contract expected leatta notes; do
  [[ "$id" == \#* || "$id" == "id" || -z "$id" ]] && continue
  [[ "$file" == *.sh ]] && continue                         # generated rows handled below
  [[ -n "$ONLY" && "$ONLY" != "$id" ]] && continue
  f="$BENCH/$file"
  avail=$(free -m | awk 'NR==2{print $7}')
  if (( avail < 15000 )); then echo "PAUSE: MemAvailable ${avail}MB < 15000MB; skipping $id" >&2; continue; fi
  hn=$(cetta_count he "$f"); ht=$(cetta_time he "$f")
  qn=$(cetta_count prime "$f"); qt=$(cetta_time prime "$f")
  zn="n/a"; zt="n/a"
  if [[ "$contract" == "he_prime" ]]; then
    pn="n/a"; pt="n/a"
    gate=$([[ "$hn" == "$qn" && "$hn" == "$expected" ]] && echo OK || echo MISMATCH)
  else
    pn=$(petta_count "$f"); pt=$(petta_time "$f")
    case "$contract" in
      count)   gate=$([[ "$hn" == "$qn" && "$hn" == "$pn" && "$hn" == "$expected" ]] && echo OK || echo MISMATCH) ;;
      witness) gate=$(witness_gate "$f"); hn="by-val"; qn="by-val"; pn="by-val" ;;
      zero_witness)
        zf="${f%.metta}.zero.metta"
        [[ -f "$zf" ]] || { echo "error: missing Zero presentation $zf" >&2; exit 1; }
        zt=$(cetta_time zero "$zf")
        gate=$(zero_witness_gate "$f" "$zf" "$expected")
        hn="by-val"; qn="by-val"; pn="by-val"; zn="by-val" ;;
      *)       gate="?" ;;
    esac
  fi
  printf "%-22s %-12s %-8s %-9s %-9s %-8s %-9s %-7s %-7s %-7s %-7s %s\n" \
    "$id" "$contract" "$hn" "$qn" "$pn" "$zn" "$gate" "$ht" "$qt" "$pt" "$zt" "$notes"
done < "$MANIFEST"

# Roman generated rows: small depth/branch grid
GEN="$BENCH/08_roman_chain_noise"
run_gen(){ local id="$1" label="$2" script="$3" d="$4" b="$5" expr="$6"
  [[ -n "$ONLY" && "$ONLY" != "$id" ]] && return
  local f="$GEN/_drv_${label}_d${d}_b${b}.metta"; "$GEN/$script" "$d" "$b" > "$f"
  local exp; exp=$(awk -v b="$b" -v d="$d" "BEGIN{print $expr}")
  local hn ht qn qt pn pt
  hn=$(cetta_count he "$f"); ht=$(cetta_time he "$f")
  qn=$(cetta_count prime "$f"); qt=$(cetta_time prime "$f")
  pn=$(petta_count "$f"); pt=$(petta_time "$f")
  local gate; gate=$([[ "$hn" == "$qn" && "$hn" == "$pn" && "$hn" == "$exp" ]] && echo OK || echo MISMATCH)
  printf "%-22s d=%-2s b=%-3s proofs=%-9s he_n=%-9s prime_n=%-9s petta_n=%-9s %-9s he=%-7s prime=%-7s petta=%s\n" \
    "$label" "$d" "$b" "$exp" "$hn" "$qn" "$pn" "$gate" "$ht" "$qt" "$pt"
}
if [[ -z "$ONLY" || "$ONLY" == 08_chain_bwd ||
      "$ONLY" == 08c_chain_fwd || "$ONLY" == 08b_chain_branchy ]]; then
  echo "--- Roman generated grid (gen scripts) ---"
  run_gen 08_chain_bwd      chain_bwd gen_kb.sh          25 8 "1"
  run_gen 08c_chain_fwd     chain_fwd gen_kb_forward.sh  10 8 "(d+1)+d*b"
  run_gen 08b_chain_branchy branchy   gen_kb_branchy.sh  10 1 "(1+b)^d"
fi
echo "done."
