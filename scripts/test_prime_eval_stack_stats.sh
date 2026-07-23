#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=${CETTA_BIN:-"$ROOT/runtime/cetta-main-runtime-stats"}

if [[ ! -x "$BIN" ]]; then
    echo "FAIL: runtime-stats CeTTa binary is unavailable" >&2
    exit 1
fi

work_400=0
work_800=0
work_1600=0
evacuated_400=0
evacuated_800=0
evacuated_1600=0
probe_dir=$(mktemp -d)
trap 'rm -f "$probe_dir"/*; rmdir "$probe_dir"' EXIT

counter() {
    local stats_file=$1
    local name=$2
    local value
    value=$(sed -n "s/^runtime-counter ${name} //p" "$stats_file")
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: missing runtime counter ${name}" >&2
        exit 1
    fi
    printf '%s\n' "$value"
}

run_sum_probe() {
    local n=$1
    local stdout_file="$probe_dir/sum-${n}.stdout"
    local stats_file="$probe_dir/sum-${n}.stats"

    CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
    "$BIN" --emit-runtime-stats --lang prime \
        -e '(= (mam:sum $n) (if (< $n 1) 0 (+ 1 (mam:sum (- $n 1)))))' \
        -e "!(mam:sum ${n})" >"$stdout_file" 2>"$stats_file"

    if [[ $(<"$stdout_file") != "[${n}]" ]]; then
        echo "FAIL: sum(${n}) changed under the explicit Prime stack" >&2
        cat "$stdout_file" >&2
        exit 1
    fi

    local roots pushes bind_tasks call_tasks normalize_tasks work
    local strict_frames if_frames force_frames finish_frames
    local heap_peak c_stack_peak trip_eval trip_bind trip_typed
    local gc_frame_safe_points gc_evacuated_bytes gc_fresh_budget_peak
    local ancestor_queries ancestor_index_steps ancestor_log_steps
    local storage_key_scan_frames
    roots=$(counter "$stats_file" prime-eval-stack-root-run)
    pushes=$(counter "$stats_file" prime-eval-stack-frame-push)
    bind_tasks=$(counter "$stats_file" prime-eval-stack-task-bind)
    call_tasks=$(counter "$stats_file" prime-eval-stack-task-call)
    normalize_tasks=$(counter "$stats_file" prime-eval-stack-task-normalize)
    strict_frames=$(counter "$stats_file" prime-eval-stack-frame-strict)
    if_frames=$(counter "$stats_file" prime-eval-stack-frame-if)
    force_frames=$(counter "$stats_file" prime-eval-stack-frame-force)
    finish_frames=$(counter "$stats_file" prime-eval-stack-frame-bind-finish)
    heap_peak=$(counter "$stats_file" prime-eval-stack-frame-depth-peak)
    gc_frame_safe_points=$(
        counter "$stats_file" prime-eval-stack-gc-frame-safe-point
    )
    gc_evacuated_bytes=$(
        counter "$stats_file" prime-eval-stack-gc-evacuated-bytes
    )
    gc_fresh_budget_peak=$(
        counter "$stats_file" prime-eval-stack-gc-fresh-budget-peak
    )
    ancestor_queries=$(
        counter "$stats_file" prime-need-ancestor-query
    )
    ancestor_index_steps=$(
        counter "$stats_file" prime-need-ancestor-index-step
    )
    ancestor_log_steps=$(
        counter "$stats_file" prime-need-ancestor-log-step
    )
    storage_key_scan_frames=$(
        counter "$stats_file" prime-need-storage-key-scan-frame
    )
    c_stack_peak=$(counter "$stats_file" eval-c-stack-guard-depth-peak)
    trip_eval=$(counter "$stats_file" eval-c-stack-guard-trip-eval)
    trip_bind=$(counter "$stats_file" eval-c-stack-guard-trip-bind)
    trip_typed=$(counter "$stats_file" eval-c-stack-guard-trip-bind-typed)

    work=$((pushes + bind_tasks + call_tasks + normalize_tasks))
    if ((roots != 1)); then
        echo "FAIL: sum(${n}) installed ${roots} root drivers, expected one" >&2
        exit 1
    fi
    if ((strict_frames == 0 || if_frames == 0 || force_frames == 0 ||
        finish_frames == 0)); then
        echo "FAIL: sum(${n}) did not exercise every admitted frame kind" >&2
        exit 1
    fi
    if ((work < n || work > 20 * n + 16)); then
        echo "FAIL: explicit-stack work ${work} escaped the linear sum(${n}) envelope" >&2
        exit 1
    fi
    if ((heap_peak <= n || gc_frame_safe_points == 0 ||
        gc_evacuated_bytes == 0 || gc_fresh_budget_peak < 1048576)); then
        echo "FAIL: sum(${n}) did not witness heap continuation ownership" >&2
        exit 1
    fi
    if ((c_stack_peak > 8 || trip_eval != 0 || trip_bind != 0 ||
        trip_typed != 0)); then
        echo "FAIL: sum(${n}) returned to recursive C-stack growth" >&2
        exit 1
    fi
    if ((ancestor_queries == 0 || ancestor_index_steps == 0 ||
        ancestor_log_steps != 0 || storage_key_scan_frames != 0)); then
        echo "FAIL: sum(${n}) left a linear-scan Need metadata path" >&2
        exit 1
    fi

    printf -v "work_${n}" '%d' "$work"
    printf -v "evacuated_${n}" '%d' "$gc_evacuated_bytes"
    printf 'sum(%d): work=%d heap-peak=%d c-stack-peak=%d gc=%d evacuated=%d\n' \
        "$n" "$work" "$heap_peak" "$c_stack_peak" \
        "$gc_frame_safe_points" "$gc_evacuated_bytes"
}

run_sum_probe 400
run_sum_probe 800
run_sum_probe 1600

if ((work_800 - work_400 > 20 * 400 + 16 ||
    work_1600 - work_800 > 20 * 800 + 16)); then
    echo "FAIL: explicit-stack work increments are not linearly bounded" >&2
    exit 1
fi
if ((evacuated_800 > 3 * evacuated_400 + 1048576 ||
    evacuated_1600 > 3 * evacuated_800 + 1048576)); then
    echo "FAIL: continuation evacuation escaped the linear doubling envelope" >&2
    exit 1
fi

he_stdout="$probe_dir/he.stdout"
he_stats="$probe_dir/he.stats"
"$BIN" --emit-runtime-stats --lang he --profile he-extended \
    -e '!(+ 1 2)' >"$he_stdout" 2>"$he_stats"
if [[ $(<"$he_stdout") != '[3]' ]]; then
    echo "FAIL: HE control result changed" >&2
    exit 1
fi
for name in \
    prime-eval-stack-root-run \
    prime-eval-stack-frame-push \
    prime-eval-stack-frame-depth-peak \
    prime-eval-stack-task-bind \
    prime-eval-stack-task-call \
    prime-eval-stack-task-normalize \
    prime-eval-stack-frame-strict \
    prime-eval-stack-frame-if \
    prime-eval-stack-frame-force \
    prime-eval-stack-frame-bind-finish \
    prime-eval-stack-gc-frame-safe-point \
    prime-eval-stack-poisoned-task \
    prime-eval-stack-gc-evacuated-bytes \
    prime-eval-stack-gc-fresh-budget-peak \
    prime-need-ancestor-query \
    prime-need-ancestor-index-step \
    prime-need-ancestor-log-step \
    prime-need-storage-key-scan-frame; do
    if (($(counter "$he_stats" "$name") != 0)); then
        echo "FAIL: Prime explicit-stack counter ${name} changed in HE" >&2
        exit 1
    fi
done

printf '%s\n' \
    "PrimeEvalStackStatsSummary PASS work=${work_400}/${work_800}/${work_1600} HE=isolated"
