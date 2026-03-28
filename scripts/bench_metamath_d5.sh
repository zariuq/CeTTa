#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CETTA_BIN="$ROOT/cetta"
CASE_FILE="$ROOT/tests/bench_metamath_d5.metta"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-180}"
VM_LIMIT_KB="${VM_LIMIT_KB:-6291456}"

if [[ ! -x "$CETTA_BIN" ]]; then
    echo "error: $CETTA_BIN is missing or not executable" >&2
    exit 1
fi

if [[ ! -f "$CASE_FILE" ]]; then
    echo "error: benchmark case $CASE_FILE is missing" >&2
    exit 1
fi

run_out=""
status=0
run_out=$(/usr/bin/time -f 'elapsed=%E rss=%MKB' bash -lc \
    "ulimit -v $VM_LIMIT_KB; timeout $TIMEOUT_SECONDS '$CETTA_BIN' --count-only --lang he '$CASE_FILE'" \
    2>&1) || status=$?

time_line=$(printf '%s\n' "$run_out" | grep 'elapsed=' | tail -1 || true)
final_count=$(printf '%s\n' "$run_out" | grep -E '^[0-9]+$' | tail -1 || true)

summary_status="ok"
note=""
if [[ $status -eq 124 ]]; then
    summary_status="timeout"
    note="timeout ${TIMEOUT_SECONDS}s"
elif printf '%s\n' "$run_out" | grep -Fq 'fatal: out of memory'; then
    summary_status="oom"
    note=$(printf '%s\n' "$run_out" | grep -F 'fatal: out of memory' | tail -1)
elif [[ $status -ne 0 ]]; then
    summary_status="error"
    note="exit_status=$status"
fi

printf 'benchmark=metamath-depth5\n'
printf 'file=%s\n' "$CASE_FILE"
printf 'vm_limit_kb=%s\n' "$VM_LIMIT_KB"
printf 'timeout_seconds=%s\n' "$TIMEOUT_SECONDS"
printf 'status=%s\n' "$summary_status"
printf 'final_count=%s\n' "${final_count:-<none>}"
printf 'timing=%s\n' "${time_line:-<none>}"
if [[ -n "$note" ]]; then
    printf 'note=%s\n' "$note"
fi
