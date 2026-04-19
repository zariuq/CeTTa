#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/cetta"
RESULTS_DIR="$ROOT/results"
RUNTIME_DIR="$ROOT/runtime"

source "$ROOT/scripts/cetta_bench_build.sh"

HISTORY_FILE="$RESULTS_DIR/bench_weird_history.tsv"
LATEST_FILE="$RESULTS_DIR/bench_weird_latest.tsv"
BEST_FILE="$RESULTS_DIR/bench_weird_best.tsv"

RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
LATEST_TMP="$RESULTS_DIR/.bench_weird_latest_${RUN_ID}.tsv"

METAMATH_TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-180}"
DEFAULT_VM_LIMIT_KB="${DEFAULT_VM_LIMIT_KB:-6291456}"
MORK_VM_LIMIT_KB="${MORK_VM_LIMIT_KB:-10485760}"
D4_PROBE_TIMEOUT="${D4_PROBE_TIMEOUT:-60}"
WEIRD_FILTER="${WEIRD_FILTER:-}"

BRANCH="$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)"
COMMIT="$(git -C "$ROOT" rev-parse HEAD)"
DIRTY="0"
if [[ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]]; then
    DIRTY="1"
fi

mkdir -p "$RESULTS_DIR" "$RUNTIME_DIR"

cetta_ensure_build_mode "$ROOT" "$BIN" pathmap "weird benchmark audit"

header='run_id	timestamp_utc	benchmark	classification	system	backend	input	status	expected	count	elapsed_sec	rss_kb	timeout_sec	vm_limit_kb	branch	commit	dirty	note'

ensure_header() {
    local file="$1"
    if [[ ! -f "$file" ]]; then
        printf '%s\n' "$header" > "$file"
    fi
}

sanitize() {
    printf '%s' "${1:-}" | tr '\t\r\n' '   '
}

append_record() {
    local benchmark="$1"
    local classification="$2"
    local system="$3"
    local backend="$4"
    local input="$5"
    local status="$6"
    local expected="$7"
    local count="$8"
    local elapsed_sec="$9"
    local rss_kb="${10}"
    local timeout_sec="${11}"
    local vm_limit_kb="${12}"
    local note="${13}"
    local timestamp_utc

    timestamp_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$RUN_ID" \
        "$timestamp_utc" \
        "$(sanitize "$benchmark")" \
        "$(sanitize "$classification")" \
        "$(sanitize "$system")" \
        "$(sanitize "$backend")" \
        "$(sanitize "$input")" \
        "$(sanitize "$status")" \
        "$(sanitize "$expected")" \
        "$(sanitize "$count")" \
        "$(sanitize "$elapsed_sec")" \
        "$(sanitize "$rss_kb")" \
        "$(sanitize "$timeout_sec")" \
        "$(sanitize "$vm_limit_kb")" \
        "$(sanitize "$BRANCH")" \
        "$(sanitize "$COMMIT")" \
        "$(sanitize "$DIRTY")" \
        "$(sanitize "$note")" \
        | tee -a "$HISTORY_FILE" >> "$LATEST_TMP"
}

is_selected() {
    local lane="$1"
    if [[ -z "$WEIRD_FILTER" ]]; then
        return 0
    fi
    case ",$WEIRD_FILTER," in
        *,"$lane",*) return 0 ;;
        *) return 1 ;;
    esac
}

run_cetta_count_probe() {
    local benchmark="$1"
    local classification="$2"
    local backend="$3"
    local input_file="$4"
    local expected="$5"
    local timeout_seconds="$6"
    local vm_limit_kb="$7"
    local extra_args="$8"

    local cmd out status time_line elapsed_sec rss_kb count checkpoint result_status note

    cmd="cd '$ROOT' && '$BIN'"
    if [[ -n "$backend" ]]; then
        cmd="$cmd --space-engine '$backend'"
    fi
    if [[ -n "$extra_args" ]]; then
        cmd="$cmd $extra_args"
    fi
    cmd="$cmd --count-only '$input_file'"

    out=""
    status=0
    out=$(/usr/bin/time -f 'elapsed_sec=%e rss_kb=%M' bash -lc \
        "ulimit -v $vm_limit_kb; timeout $timeout_seconds bash -lc \"$cmd\"" \
        2>&1) || status=$?

    time_line="$(printf '%s\n' "$out" | grep -E 'elapsed_sec=|rss_kb=' | tail -1 || true)"
    elapsed_sec="$(printf '%s\n' "$time_line" | sed -n 's/.*elapsed_sec=\([0-9.]*\).*/\1/p')"
    rss_kb="$(printf '%s\n' "$time_line" | sed -n 's/.*rss_kb=\([0-9][0-9]*\).*/\1/p')"
    count="$(printf '%s\n' "$out" | grep -E '^[0-9]+$' | tail -1 || true)"
    checkpoint="$(printf '%s\n' "$out" | grep '\[chain\]' | tail -1 || true)"

    result_status="error"
    note=""
    if [[ $status -eq 124 && -n "$checkpoint" ]]; then
        result_status="checkpoint"
        note="$checkpoint"
    elif [[ $status -eq 124 ]]; then
        result_status="timeout"
        note="timeout ${timeout_seconds}s"
    elif printf '%s\n' "$out" | grep -Fq 'fatal: out of memory'; then
        result_status="oom"
        note="$(printf '%s\n' "$out" | grep -F 'fatal: out of memory' | tail -1)"
    elif [[ $status -ne 0 ]]; then
        result_status="error"
        note="exit_status=$status"
    elif [[ -n "$count" ]]; then
        if [[ -n "$expected" && "$count" = "$expected" ]]; then
            result_status="exact_match"
        elif [[ -n "$expected" ]]; then
            result_status="count_mismatch"
            note="expected=$expected"
        else
            result_status="count"
        fi
    elif [[ -n "$checkpoint" ]]; then
        result_status="checkpoint"
        note="$checkpoint"
    else
        result_status="error"
        note="$(printf '%s\n' "$out" | tail -3 | tr '\n' ' ' | sed 's/  */ /g')"
    fi

    append_record \
        "$benchmark" "$classification" "cetta" "$backend" "$input_file" \
        "$result_status" "$expected" "$count" "$elapsed_sec" "$rss_kb" \
        "$timeout_seconds" "$vm_limit_kb" "$note"

    printf '%-24s %-20s status=%-14s count=%-8s elapsed=%-8s rss=%s\n' \
        "$benchmark" "${backend:-default}" "$result_status" "${count:-<none>}" "${elapsed_sec:-<na>}" "${rss_kb:-<na>}"
}

run_eqtl_act_modes() {
    local out status mode benchmark wall rss note
    local flush_mode

    out=""
    status=0
    out=$(bash -lc "cd '$ROOT' && ./scripts/bench_mork_act_eqtl.sh all" 2>&1) || status=$?

    if [[ $status -ne 0 ]]; then
        append_record \
            "bio-eqtl-act" "benchmark" "cetta" "act" "benchmarks/bench_bio_eqtl_mork_*" \
            "error" "" "" "" "" "" "$MORK_VM_LIMIT_KB" \
            "$(printf '%s\n' "$out" | tail -5 | tr '\n' ' ' | sed 's/  */ /g')"
        printf '%-24s %-20s status=%s\n' "bio-eqtl-act" "act" "error"
        return
    fi

    mode=""
    wall=""
    rss=""
    benchmark=""
    flush_mode() {
        if [[ -n "$mode" ]]; then
            note="prepared_via_mork_dump"
            append_record \
                "bio-eqtl-act-$mode" "benchmark" "cetta" "act" "$benchmark" \
                "ok" "" "" "$wall" "$rss" "" "$MORK_VM_LIMIT_KB" "$note"
            printf '%-24s %-20s status=%-14s elapsed=%-8s rss=%s\n' \
                "bio-eqtl-act-$mode" "act" "ok" "${wall:-<na>}" "${rss:-<na>}"
        fi
        mode=""
        wall=""
        rss=""
        benchmark=""
    }

    while IFS= read -r line; do
        case "$line" in
            mode=*)
                mode="${line#mode=}"
                ;;
            benchmark=*)
                benchmark="${line#benchmark=}"
                ;;
            wall=*)
                wall="${line#wall=}"
                ;;
            rss_kb=*)
                rss="${line#rss_kb=}"
                ;;
            '')
                flush_mode
                ;;
        esac
    done <<< "$out"
    flush_mode
}

record_unavailable() {
    local benchmark="$1"
    local classification="$2"
    local note="$3"
    append_record "$benchmark" "$classification" "cetta" "" "" "unavailable" "" "" "" "" "" "" "$note"
    printf '%-24s %-20s status=%s\n' "$benchmark" "-" "unavailable"
}

write_best_file() {
    python3 - "$HISTORY_FILE" "$BEST_FILE" <<'PY'
import csv
import sys
from pathlib import Path

history_path = Path(sys.argv[1])
best_path = Path(sys.argv[2])

rank = {
    "exact_match": 7,
    "ok": 6,
    "count": 5,
    "count_mismatch": 4,
    "checkpoint": 3,
    "timeout": 2,
    "oom": 1,
    "error": 0,
    "unavailable": -1,
}

def parse_int(value: str) -> int:
    try:
        return int(value)
    except Exception:
        return -1

def parse_float(value: str) -> float:
    try:
        return float(value)
    except Exception:
        return float("inf")

def score(row):
    return (
        rank.get(row["status"], -2),
        parse_int(row["count"]),
        -parse_float(row["elapsed_sec"]),
    )

with history_path.open(newline="") as f:
    reader = csv.DictReader(f, delimiter="\t")
    rows = list(reader)

best = {}
for row in rows:
    key = (row["benchmark"], row["system"], row["backend"])
    current = best.get(key)
    if current is None or score(row) > score(current):
        best[key] = row

fieldnames = reader.fieldnames
with best_path.open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames, delimiter="\t")
    writer.writeheader()
    for key in sorted(best):
        writer.writerow(best[key])
PY
}

ensure_header "$HISTORY_FILE"
printf '%s\n' "$header" > "$LATEST_TMP"

echo "== weird benchmark audit =="
echo "branch=$BRANCH commit=$COMMIT dirty=$DIRTY"

if is_selected "metamath-d5"; then
    run_cetta_count_probe \
        "metamath-depth5" "target" "" "benchmarks/bench_metamath_d5.metta" \
        "" "$METAMATH_TIMEOUT_SECONDS" "$DEFAULT_VM_LIMIT_KB" "--lang he"
fi

if is_selected "d3-nodup"; then
    run_cetta_count_probe \
        "nil-depth3-nodup" "probe" "" "tests/nil_pc_fc_d3_nodup.metta" \
        "" "120" "$DEFAULT_VM_LIMIT_KB" ""
fi

if is_selected "d3-nodup-backends"; then
    for backend in native native-candidate-exact pathmap; do
        run_cetta_count_probe \
            "nil-depth3-nodup" "probe" "$backend" "tests/nil_pc_fc_d3_nodup.metta" \
            "" "120" "$DEFAULT_VM_LIMIT_KB" ""
    done
fi

if is_selected "d4"; then
    run_cetta_count_probe \
        "nil-depth4" "probe" "" "tests/nil_pc_fc_d4.metta" \
        "" "600" "$DEFAULT_VM_LIMIT_KB" ""
fi

if is_selected "d4-nodup"; then
    run_cetta_count_probe \
        "nil-depth4-nodup" "probe" "" "tests/nil_pc_fc_d4_nodup.metta" \
        "" "600" "$DEFAULT_VM_LIMIT_KB" ""
fi

if is_selected "d4-backends"; then
    for backend in native native-candidate-exact pathmap; do
        run_cetta_count_probe \
            "nil-depth4" "probe" "$backend" "tests/nil_pc_fc_d4.metta" \
            "" "$D4_PROBE_TIMEOUT" "$DEFAULT_VM_LIMIT_KB" ""
    done
fi

if is_selected "d4-nodup-backends"; then
    for backend in native native-candidate-exact pathmap; do
        run_cetta_count_probe \
            "nil-depth4-nodup" "probe" "$backend" "tests/nil_pc_fc_d4_nodup.metta" \
            "" "$D4_PROBE_TIMEOUT" "$DEFAULT_VM_LIMIT_KB" ""
    done
fi

if is_selected "bio-eqtl-act"; then
    run_eqtl_act_modes
fi

if is_selected "bio-1m-act"; then
    record_unavailable \
        "bio-1m-act" "local-benchmark" \
        "requires tracked source data and prep harness cleanup before it is a reproducible benchmark"
fi

mv "$LATEST_TMP" "$LATEST_FILE"
write_best_file

echo
echo "history: $HISTORY_FILE"
echo "latest : $LATEST_FILE"
echo "best   : $BEST_FILE"
