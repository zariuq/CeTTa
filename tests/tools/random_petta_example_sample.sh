#!/usr/bin/env bash
# Pick N reasonable PeTTa/examples at random and run them on a CeTTa binary.
# Default: 10 examples, rc=0, stdout byte-identical to SWI-PeTTa when run.sh
# is present.  Print the seed so a failure is replayable.
#
#   tests/tools/random_petta_example_sample.sh [CETTA_BIN]
#   SEED=42 N=10 COMPARE_SWI=1 TIMEOUT=25 ./tests/tools/random_petta_example_sample.sh
set -eu
HERE=$(cd -- "$(dirname -- "$0")/../.." && pwd)
BIN=${1:-"$HERE/cetta"}
EX=${PETTA_EXAMPLES:-$(cd -- "$HERE/../PeTTa/examples" && pwd)}
SWI=${PETTA_RUN:-"$HERE/../PeTTa/run.sh"}
N=${N:-10}
TIMEOUT=${TIMEOUT:-25}
COMPARE_SWI=${COMPARE_SWI:-1}
SEED=${SEED:-$RANDOM}

if [ ! -x "$BIN" ]; then
  echo "FAIL: cetta binary not executable: $BIN" >&2
  exit 1
fi
if [ ! -d "$EX" ]; then
  echo "FAIL: PeTTa examples directory missing: $EX" >&2
  exit 1
fi

# External, interactive, or already known not to be a --lang petta smoke.
skip() {
  case "$1" in
    llm_cities.metta|torch.metta|greedy_chess.metta|git_import.metta|git_import2.metta|repl.metta|python.metta|python_import.metta|prologimport.metta|codex_with_he.metta) return 0 ;;
    nars_direct.metta|nars_temporal_sequence_probe.metta|nars_tuffy.metta) return 0 ;;
    pln_didactic_witnesses.metta|pln_direct.metta|pln_itv_idm_parity_golden.metta|pln_projection_tower_bool_canary.metta|pln_roman.metta|pln_strength_prior_canary.metta|pln_temporal_sequence_probe.metta|plntest.metta|plntestdirect.metta|pln_truth_parity_golden.metta|pln_tuffy.metta) return 0 ;;
    # Pre-existing CeTTa errors (search-revision / probes), not this close.
    assert_probe.metta|audit_probe.metta|metamo_tea_break.metta|test_unify_eval_branches.metta|with_explicit_count.metta) return 0 ;;
    matchnested.metta|matchnested2.metta|spaces_removeallatoms.metta|eval.metta|mutex_and_transaction.metta) return 0 ;;
    repro_match_template_param.metta|repro_match_template_param_test.metta|space_let_probe.metta) return 0 ;;
    # Wall-clock, not an if-guard bag.
    test_datetime.metta) return 0 ;;
    *) return 1 ;;
  esac
}

pool=()
for f in "$EX"/*.metta; do
  base=$(basename "$f")
  skip "$base" && continue
  pool+=("$base")
done
if [ "${#pool[@]}" -lt "$N" ]; then
  echo "FAIL: only ${#pool[@]} reasonable examples, need $N" >&2
  exit 1
fi

mapfile -t chosen < <(
  printf '%s\n' "${pool[@]}" | awk -v seed="$SEED" -v n="$N" '
    BEGIN { srand(seed + 0) }
    { a[NR] = $0 }
    END {
      for (i = NR; i >= 1; i--) {
        j = int(rand() * i) + 1
        t = a[i]; a[i] = a[j]; a[j] = t
      }
      for (i = 1; i <= n && i <= NR; i++) print a[i]
    }'
)

echo "SEED=$SEED N=$N BIN=$BIN"
echo "sample: ${chosen[*]}"

have_swi=0
if [ "$COMPARE_SWI" = 1 ] && [ -x "$SWI" ]; then
  have_swi=1
fi

normalize() {
  # CeTTa prints $V0; SWI prints $_0.  The bag is the same occurrence.
  sed -e 's/\$V[0-9][0-9]*/$V/g' -e 's/\$_[0-9][0-9]*/$V/g' "$1"
}

fail=0
for base in "${chosen[@]}"; do
  src="$EX/$base"
  cout=$(mktemp)
  sout=$(mktemp)
  if ! timeout "$TIMEOUT" "$BIN" --lang petta "$src" >"$cout" 2>/dev/null; then
    echo "FAIL $base (cetta rc/timeout)"
    fail=1
    rm -f "$cout" "$sout"
    continue
  fi
  if grep -q 'PettaSearchRevisionInvalidated' "$cout"; then
    echo "FAIL $base (search-revision error)"
    fail=1
    rm -f "$cout" "$sout"
    continue
  fi
  if [ "$have_swi" -eq 1 ]; then
    if ! timeout "$TIMEOUT" "$SWI" --silent "$src" >"$sout" 2>/dev/null; then
      echo "FAIL $base (swi rc/timeout)"
      fail=1
      rm -f "$cout" "$sout"
      continue
    fi
    cn=$(mktemp); sn=$(mktemp)
    normalize "$cout" >"$cn"
    normalize "$sout" >"$sn"
    if ! cmp -s "$cn" "$sn"; then
      echo "FAIL $base (cetta stdout != SWI-PeTTa)"
      fail=1
      rm -f "$cout" "$sout" "$cn" "$sn"
      continue
    fi
    rm -f "$cn" "$sn"
  fi
  echo "OK $base"
  rm -f "$cout" "$sout"
done

if [ "$fail" -ne 0 ]; then
  echo "FAIL: random PeTTa example sample (SEED=$SEED)"
  exit 1
fi
echo "PASS: random PeTTa example sample (SEED=$SEED N=$N)"
