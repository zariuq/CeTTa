#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CETTA_BIN="$ROOT/cetta"
PETTA_DIR="/home/zar/claude/hyperon/PeTTa"
PETTA_RUN="$PETTA_DIR/run.sh"
BACKENDS=(native-subst-tree native-candidate-exact pathmap-imported)

run_cetta() {
    local backend=$1
    local file=$2
    local expected=$3
    local out status count timing
    out=$(/usr/bin/time -f 'elapsed=%E rss=%MKB' bash -lc \
        "ulimit -v 6291456; '$CETTA_BIN' --space-match-backend '$backend' --count-only '$file'" \
        2>&1) || status=$?
    status=${status:-0}
    count=$(printf '%s\n' "$out" | grep -E '^[0-9]+$' | tail -1 || true)
    timing=$(printf '%s\n' "$out" | grep 'elapsed=' | tail -1 || true)
    if [[ $status -ne 0 || -z ${count:-} || $count != "$expected" ]]; then
        printf '%s\n' "$out" | tail -20 >&2
        echo "FAIL: CeTTa backend=$backend file=$file expected=$expected got=${count:-<none>}" >&2
        return 1
    fi
    printf '  cetta %-22s count=%-4s %s\n' "$backend" "$count" "$timing"
}

run_petta() {
    local file=$1
    local expected=$2
    local out status count timing
    out=$(/usr/bin/time -f 'elapsed=%E rss=%MKB' bash -lc \
        "cd '$PETTA_DIR'; ulimit -v 6291456; '$PETTA_RUN' '$file' --silent" \
        2>&1) || status=$?
    status=${status:-0}
    count=$(printf '%s\n' "$out" | grep -E '^[0-9]+$' | tail -1 || true)
    timing=$(printf '%s\n' "$out" | grep 'elapsed=' | tail -1 || true)
    if [[ $status -ne 0 || -z ${count:-} || $count != "$expected" ]]; then
        printf '%s\n' "$out" | tail -20 >&2
        echo "FAIL: PeTTa file=$file expected=$expected got=${count:-<none>}" >&2
        return 1
    fi
    printf '  petta %-22s count=%-4s %s\n' "$(basename "$file")" "$count" "$timing"
}

run_pair() {
    local label=$1
    local he_file=$2
    local he_expected=$3
    local petta_file=$4
    local petta_expected=$5
    echo "== $label =="
    for backend in "${BACKENDS[@]}"; do
        run_cetta "$backend" "$he_file" "$he_expected"
    done
    run_petta "$petta_file" "$petta_expected"
}

if [[ ! -x $CETTA_BIN ]]; then
    echo "error: $CETTA_BIN is missing or not executable" >&2
    exit 1
fi

run_pair \
  "dense-join" \
  "$ROOT/tests/bench_matchjoin_he.metta" \
  "216" \
  "$ROOT/tests/bench_matchjoin_petta.metta" \
  "216"

run_pair \
  "three-hop-join" \
  "$ROOT/tests/bench_matchjoin3_he.metta" \
  "625" \
  "$ROOT/tests/bench_matchjoin3_petta.metta" \
  "625"

run_pair \
  "backchain-family" \
  "$ROOT/tests/bench_backchain_he.metta" \
  "20" \
  "$ROOT/tests/bench_backchain_petta.metta" \
  "1"
