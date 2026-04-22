#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND_SCRIPT="$ROOT/scripts/bench_space_backend_matrix.sh"
TRANSFER_SCRIPT="$ROOT/scripts/bench_space_transfer_matrix.sh"
SIZES_STR="${1:-100000 1000000 10000000}"
MATCH_ROUNDS="${2:-3}"
TIMEOUT_S="${TIMEOUT_S:-240}"
RUNTIME_DIR="$ROOT/runtime/bench_space_scale_ladder"
BACKEND_MODES_STR="${BACKEND_MODES_STR:-native pathmap mork-live mork-open-act mork-load-act}"
TRANSFER_CASES_STR="${TRANSFER_CASES_STR:-native-to-pathmap native-to-mork-live pathmap-to-native pathmap-to-mork-live mork-live-to-native mork-live-to-pathmap mork-live-to-open-act mork-live-to-load-act}"

[ -x "$BACKEND_SCRIPT" ] || { echo "error: missing $BACKEND_SCRIPT" >&2; exit 1; }
[ -x "$TRANSFER_SCRIPT" ] || { echo "error: missing $TRANSFER_SCRIPT" >&2; exit 1; }

mkdir -p "$RUNTIME_DIR"

capture_result() {
    local suite_kind=$1
    local case_id=$2
    local fact_count=$3
    local log=$4
    local status=$5
    local elapsed rss payload

    elapsed="$(awk -F= '/^ELAPSED=/{print $2}' "$log" | tail -1)"
    rss="$(awk -F= '/^RSS_KB=/{print $2}' "$log" | tail -1)"
    payload="$(awk 'NF { last = $0 } END { print last }' "$log")"
    payload="${payload//$'\t'/ }"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$suite_kind" "$case_id" "$fact_count" "$status" "${elapsed:-unknown}" \
        "${rss:-unknown}" "${payload:-}"
}

run_backend_case() {
    local mode=$1
    local fact_count=$2
    local log="$RUNTIME_DIR/backend_${mode}_${fact_count}.log"
    local shell_status=0

    (
        cd "$ROOT"
        ulimit -v 10485760
        /usr/bin/time -f 'ELAPSED=%E\nRSS_KB=%M' \
            timeout "$TIMEOUT_S" "$BACKEND_SCRIPT" "$mode" "$fact_count" "$MATCH_ROUNDS" "suite_total"
    ) >"$log" 2>&1 || shell_status=$?

    if [ "$shell_status" -eq 0 ]; then
        capture_result backend "$mode" "$fact_count" "$log" pass
    elif [ "$shell_status" -eq 124 ]; then
        capture_result backend "$mode" "$fact_count" "$log" timeout
    else
        capture_result backend "$mode" "$fact_count" "$log" fail
    fi
}

run_transfer_case() {
    local case_id=$1
    local fact_count=$2
    local log="$RUNTIME_DIR/transfer_${case_id}_${fact_count}.log"
    local shell_status=0

    (
        cd "$ROOT"
        ulimit -v 10485760
        /usr/bin/time -f 'ELAPSED=%E\nRSS_KB=%M' \
            timeout "$TIMEOUT_S" "$TRANSFER_SCRIPT" "$case_id" "$fact_count" "$MATCH_ROUNDS" "suite_total"
    ) >"$log" 2>&1 || shell_status=$?

    if [ "$shell_status" -eq 0 ]; then
        capture_result transfer "$case_id" "$fact_count" "$log" pass
    elif [ "$shell_status" -eq 124 ]; then
        capture_result transfer "$case_id" "$fact_count" "$log" timeout
    else
        capture_result transfer "$case_id" "$fact_count" "$log" fail
    fi
}

printf 'suite_kind\tcase_id\tfact_count\tstatus\telapsed\trss_kb\tlast_payload\n'

IFS=' ' read -r -a sizes <<< "$SIZES_STR"
IFS=' ' read -r -a backend_modes <<< "$BACKEND_MODES_STR"
IFS=' ' read -r -a transfer_cases <<< "$TRANSFER_CASES_STR"

for fact_count in "${sizes[@]}"; do
    for mode in "${backend_modes[@]}"; do
        run_backend_case "$mode" "$fact_count"
    done
    for case_id in "${transfer_cases[@]}"; do
        run_transfer_case "$case_id" "$fact_count"
    done
done
