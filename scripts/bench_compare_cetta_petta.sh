#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"
BACKENDS_STR="${BACKENDS_STR:-native native-candidate-exact pathmap}"
IFS=' ' read -r -a BACKENDS <<< "$BACKENDS_STR"

source "$ROOT/scripts/cetta_bench_build.sh"

discover_petta_dir() {
    local candidate
    for candidate in \
        "${PETTA_DIR:-}" \
        "$ROOT/../PeTTa" \
        "$ROOT/../../hyperon/PeTTa"
    do
        if [[ -n "$candidate" && -x "$candidate/run.sh" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

PETTA_DIR="$(discover_petta_dir || true)"
PETTA_RUN="${PETTA_DIR:+$PETTA_DIR/run.sh}"

extract_numeric_tail() {
    python3 -c '
import re
import sys

lines = [line.strip() for line in sys.stdin.read().splitlines() if line.strip()]
for line in reversed(lines):
    if line.startswith("\"") and line.endswith("\"") and len(line) >= 2:
        line = line[1:-1].strip()
    m = re.fullmatch(r"\[?\s*(\d+)\s*\]?", line)
    if m:
        print(m.group(1))
        break
'
}

run_cetta() {
    local backend=$1
    local file=$2
    local expected=$3
    local out status count timing
    out=$(/usr/bin/time -f 'elapsed=%E rss=%MKB' bash -lc \
        "ulimit -v 6291456; '$CETTA_BIN' --space-engine '$backend' --count-only '$file'" \
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

run_cetta_value() {
    local backend=$1
    local file=$2
    local expected=$3
    local out status value timing
    out=$(/usr/bin/time -f 'elapsed=%E rss=%MKB' bash -lc \
        "ulimit -v 6291456; '$CETTA_BIN' --space-engine '$backend' --lang he '$file'" \
        2>&1) || status=$?
    status=${status:-0}
    value=$(printf '%s\n' "$out" | extract_numeric_tail || true)
    timing=$(printf '%s\n' "$out" | grep 'elapsed=' | tail -1 || true)
    if [[ $status -ne 0 || -z ${value:-} || $value != "$expected" ]]; then
        printf '%s\n' "$out" | tail -20 >&2
        echo "FAIL: CeTTa backend=$backend file=$file expected-value=$expected got=${value:-<none>}" >&2
        return 1
    fi
    printf '  cetta %-22s value=%-4s %s\n' "$backend" "$value" "$timing"
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

run_petta_value() {
    local file=$1
    local expected=$2
    local out status value timing
    out=$(/usr/bin/time -f 'elapsed=%E rss=%MKB' bash -lc \
        "cd '$PETTA_DIR'; ulimit -v 6291456; '$PETTA_RUN' '$file' --silent" \
        2>&1) || status=$?
    status=${status:-0}
    value=$(printf '%s\n' "$out" | extract_numeric_tail || true)
    timing=$(printf '%s\n' "$out" | grep 'elapsed=' | tail -1 || true)
    if [[ $status -ne 0 || -z ${value:-} || $value != "$expected" ]]; then
        printf '%s\n' "$out" | tail -20 >&2
        echo "FAIL: PeTTa file=$file expected-value=$expected got=${value:-<none>}" >&2
        return 1
    fi
    printf '  petta %-22s value=%-4s %s\n' "$(basename "$file")" "$value" "$timing"
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

run_pair_value() {
    local label=$1
    local he_file=$2
    local he_expected=$3
    local petta_file=$4
    local petta_expected=$5
    echo "== $label =="
    for backend in "${BACKENDS[@]}"; do
        run_cetta_value "$backend" "$he_file" "$he_expected"
    done
    run_petta_value "$petta_file" "$petta_expected"
}

if [[ ! -x $CETTA_BIN ]]; then
    cetta_ensure_build_mode "$ROOT" "$CETTA_BIN" pathmap "CeTTa vs PeTTa comparison benchmarks"
fi

for backend in "${BACKENDS[@]}"; do
    if [[ "$backend" == "pathmap" ]]; then
        cetta_ensure_build_mode "$ROOT" "$CETTA_BIN" pathmap "CeTTa vs PeTTa comparison benchmarks"
        break
    fi
done

if [[ ! -x $CETTA_BIN ]]; then
    echo "error: $CETTA_BIN is missing or not executable" >&2
    exit 1
fi

if [[ -z "${PETTA_DIR:-}" || ! -x "${PETTA_RUN:-}" ]]; then
    echo "error: unable to locate a runnable PeTTa checkout" >&2
    echo "hint: set PETTA_DIR=/path/to/PeTTa" >&2
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

run_pair_value \
  "foldall-spacecount" \
  "$ROOT/benchmarks/bench_foldallspacecount_he.metta" \
  "3" \
  "$ROOT/benchmarks/bench_foldallspacecount_petta.metta" \
  "3"

run_pair_value \
  "foldall-match" \
  "$ROOT/benchmarks/bench_foldallmatch_he.metta" \
  "5" \
  "$ROOT/benchmarks/bench_foldallmatch_petta.metta" \
  "5"
