#!/usr/bin/env bash
# Cross-engine battery ladder: finds the max passing size per (system, program)
# under T seconds and 8 GB.  Systems: cetta extended / prime / petta, SWI-PeTTa.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PETTA="${PETTA_ROOT:-$(cd "$ROOT/../PeTTa" 2>/dev/null && pwd)}"
OUT="${1:-$ROOT/runtime/battery_ladder.csv}"
T="${BATTERY_TIMEOUT:-25}"
mkdir -p "$ROOT/runtime"
SCRATCH="$(mktemp -d "$ROOT/runtime/battery-ladder.XXXXXX")"
trap 'rm -rf "$SCRATCH"' EXIT
echo "system,program,n,status,seconds,rss_kb,answer" > "$OUT"
run_one() { # sys prog n file
  local sys="$1" prog="$2" n="$3" file="$4" t0 t1 rc out ans rss
  t0=$(date +%s.%N)
  if [ "$sys" = swi ]; then
    out=$( (ulimit -v 8388608; cd "$PETTA" && timeout $T ./run.sh "$file" --silent) 2>&1 )
  else
    out=$( (ulimit -v 8388608; cd "$ROOT" && timeout $T ./cetta --lang $sys "$file") 2>&1 )
  fi
  rc=$?
  t1=$(date +%s.%N)
  ans=$(printf '%s\n' "$out" | grep -vE '^\s*$' | tail -1 | tr -d ',"' | cut -c1-40)
  local sec; sec=$(echo "$t1 $t0" | awk '{printf "%.2f", $1-$2}')
  if [ $rc -eq 124 ]; then echo "timeout,$sec"; return; fi
  if printf '%s' "$out" | grep -qi "stack overflow"; then echo "stack,$sec"; return; fi
  if [ $rc -ne 0 ]; then echo "error$rc,$sec"; return; fi
  echo "ok,$sec,$ans"
}
ladder() { # prog M list-of-N...
  local prog="$1" M="$2"; shift 2
  for sys in "he --profile extended" prime petta swi; do
    local name; case "$sys" in he*) name=cetta-he-ext;; prime) name=cetta-prime;; petta) name=cetta-petta;; swi) name=swi-petta;; esac
    for n in "$@"; do
      sed "s/@M@/$M/g; s/@N@/$n/g" "$prog.metta.in" > "$SCRATCH/x.metta"
      local res; res=$(run_one "$sys" "$prog" "$n" "$SCRATCH/x.metta")
      local status sec ans; status=${res%%,*}; rest=${res#*,}; sec=${rest%%,*}; ans=${rest#*,}
      echo "$name,$prog$([ "$M" != x ] && echo "-m$M"),$n,$status,$sec,,$ans" >> "$OUT"
      echo "[$name $prog M=$M N=$n] $status ${sec}s $ans"
      [ "$status" != ok ] && break
    done
  done
}
cd "$(dirname "$0")"
ladder ack 3        3 5 7 8 9 10 11 12
ladder peano_ack 2  100 300 1000 3000 10000 30000 100000
ladder peano_ack 3  2 3 4 5 6
ladder nrev x       100 300 1000 3000 10000 30000 100000
ladder deriv x      2 4 6 8 10 12 14
ladder queens x     5 6 7 8 9 10 11
echo DONE >> "$OUT"
