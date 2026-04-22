#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND_SCRIPT="$ROOT/scripts/bench_space_backend_matrix.sh"
TRANSFER_SCRIPT="$ROOT/scripts/bench_space_transfer_matrix.sh"
SUITE="${1:-light}"
FACT_COUNT_ARG="${2:-}"
MATCH_ROUNDS_ARG="${3:-}"
RUNTIME_DIR="$ROOT/runtime/bench_ffi_friction"

usage() {
    cat <<EOF
Usage: $(basename "$0") [light|basic|stress|heavy] [FACT_COUNT] [MATCH_ROUNDS]

Suites:
  light   Small regular witness over native, pathmap, and MORK lanes.
  basic   10k Roman-style local diagnostic suite.
  stress  50k full diagnostic suite.
  heavy   Guarded 100k suite; requires BENCH_FFI_ALLOW_HEAVY=1.

Environment overrides:
  BENCH_FFI_BACKEND_MODE       backend matrix mode, default all
  BENCH_FFI_TRANSFER_MODE      transfer matrix mode, default all
  BENCH_FFI_BACKEND_SCENARIOS  backend scenario list
  BENCH_FFI_TRANSFER_SCENARIOS transfer scenario list
  BENCH_FFI_SKIP_BACKEND=1     skip backend matrix
  BENCH_FFI_SKIP_TRANSFER=1    skip transfer matrix
EOF
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

case "$SUITE" in
    light)
        FACT_COUNT="${FACT_COUNT_ARG:-1000}"
        MATCH_ROUNDS="${MATCH_ROUNDS_ARG:-1}"
        DEFAULT_BACKEND_SCENARIOS="insert_only exact_hit_after_insert full_scan_after_insert post_remove_count_after_insert suite_total"
        DEFAULT_TRANSFER_SCENARIOS="copy_only exact_hit_after_copy full_scan_after_copy suite_total"
        ;;
    basic)
        FACT_COUNT="${FACT_COUNT_ARG:-10000}"
        MATCH_ROUNDS="${MATCH_ROUNDS_ARG:-3}"
        DEFAULT_BACKEND_SCENARIOS="insert_only exact_hit_after_insert full_scan_after_insert post_remove_count_after_insert suite_total"
        DEFAULT_TRANSFER_SCENARIOS="copy_only exact_hit_after_copy full_scan_after_copy suite_total"
        ;;
    stress)
        FACT_COUNT="${FACT_COUNT_ARG:-50000}"
        MATCH_ROUNDS="${MATCH_ROUNDS_ARG:-3}"
        DEFAULT_BACKEND_SCENARIOS="insert_only exact_hit_after_insert full_scan_after_insert post_remove_count_after_insert suite_total"
        DEFAULT_TRANSFER_SCENARIOS="copy_only exact_hit_after_copy full_scan_after_copy suite_total"
        ;;
    heavy)
        FACT_COUNT="${FACT_COUNT_ARG:-100000}"
        MATCH_ROUNDS="${MATCH_ROUNDS_ARG:-3}"
        DEFAULT_BACKEND_SCENARIOS="suite_total"
        DEFAULT_TRANSFER_SCENARIOS="suite_total"
        [ "${BENCH_FFI_ALLOW_HEAVY:-0}" = "1" ] || die "heavy suite requires BENCH_FFI_ALLOW_HEAVY=1"
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        die "Unknown suite: $SUITE"
        ;;
esac

[[ "$FACT_COUNT" =~ ^[0-9]+$ ]] || die "FACT_COUNT must be a non-negative integer"
[[ "$MATCH_ROUNDS" =~ ^[0-9]+$ ]] || die "MATCH_ROUNDS must be a non-negative integer"
[ "$FACT_COUNT" -gt 42 ] || die "FACT_COUNT must be greater than 42 because the workload probes/removes (friend sam 42)"
[ "$MATCH_ROUNDS" -gt 0 ] || die "MATCH_ROUNDS must be positive"
[ -x "$BACKEND_SCRIPT" ] || die "Missing executable $BACKEND_SCRIPT"
[ -x "$TRANSFER_SCRIPT" ] || die "Missing executable $TRANSFER_SCRIPT"

BACKEND_MODE="${BENCH_FFI_BACKEND_MODE:-all}"
TRANSFER_MODE="${BENCH_FFI_TRANSFER_MODE:-all}"
BACKEND_SCENARIOS="${BENCH_FFI_BACKEND_SCENARIOS:-$DEFAULT_BACKEND_SCENARIOS}"
TRANSFER_SCENARIOS="${BENCH_FFI_TRANSFER_SCENARIOS:-$DEFAULT_TRANSFER_SCENARIOS}"
SKIP_BACKEND="${BENCH_FFI_SKIP_BACKEND:-0}"
SKIP_TRANSFER="${BENCH_FFI_SKIP_TRANSFER:-0}"

mkdir -p "$RUNTIME_DIR"
LOG_DIR="$RUNTIME_DIR/${SUITE}_${FACT_COUNT}_$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$LOG_DIR"
summary_log="$LOG_DIR/summary.tsv"

printf 'bench_suite\tphase\tkind\tcase_id\tsource_kind\ttarget_kind\troute_class\tscenario\tfact_count\tmatch_rounds\tcount\ttime_s\trss_kb\n' \
    | tee "$summary_log"

if [ "$SKIP_BACKEND" != "1" ]; then
    backend_log="$LOG_DIR/backend.tsv"
    backend_err="$LOG_DIR/backend.stderr"
    set +e
    "$BACKEND_SCRIPT" "$BACKEND_MODE" "$FACT_COUNT" "$MATCH_ROUNDS" "$BACKEND_SCENARIOS" \
        2>"$backend_err" \
        | tee "$backend_log" \
        | awk -F '\t' -v suite="$SUITE" '
            BEGIN { OFS = "\t" }
            $1 == "backend" {
                print suite, "backend", $1, $2, $2, $2, "local", $3, $4, $5, $6, $7, $8
            }
        ' \
        | tee -a "$summary_log"
    backend_status=${PIPESTATUS[0]}
    set -e
    if [ "$backend_status" -ne 0 ]; then
        printf 'backend matrix failed: status=%s log=%s stderr=%s\n' \
            "$backend_status" "$backend_log" "$backend_err" >&2
        [ ! -s "$backend_err" ] || tail -80 "$backend_err" >&2 || true
        exit "$backend_status"
    fi
fi

if [ "$SKIP_TRANSFER" != "1" ]; then
    transfer_log="$LOG_DIR/transfer.tsv"
    transfer_err="$LOG_DIR/transfer.stderr"
    set +e
    "$TRANSFER_SCRIPT" "$TRANSFER_MODE" "$FACT_COUNT" "$MATCH_ROUNDS" "$TRANSFER_SCENARIOS" \
        2>"$transfer_err" \
        | tee "$transfer_log" \
        | awk -F '\t' -v suite="$SUITE" '
            BEGIN { OFS = "\t" }
            $1 == "transfer" {
                print suite, "transfer", $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11
            }
        ' \
        | tee -a "$summary_log"
    transfer_status=${PIPESTATUS[0]}
    set -e
    if [ "$transfer_status" -ne 0 ]; then
        printf 'transfer matrix failed: status=%s log=%s stderr=%s\n' \
            "$transfer_status" "$transfer_log" "$transfer_err" >&2
        [ ! -s "$transfer_err" ] || tail -80 "$transfer_err" >&2 || true
        exit "$transfer_status"
    fi
fi

printf 'logs\t%s\nsummary\t%s\n' "$LOG_DIR" "$summary_log" >&2
