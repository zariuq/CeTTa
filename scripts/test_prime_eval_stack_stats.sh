#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=${CETTA_BIN:-"$ROOT/runtime/cetta-main-runtime-stats"}

# This gate requires the optional persistent Need index.  The index became
# runtime-selectable after the gate was introduced, so make the qualification
# mode explicit instead of depending on an ambient shell setting.
export CETTA_PRIME_NEED_HEAP_INDEX=1

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
        -e '(= (mam:sum $n) (if (< $n 1) 0 (+ (superpose (1)) (mam:sum (- $n 1)))))' \
        -e "!(mam:sum (force (delay ${n})))" \
        >"$stdout_file" 2>"$stats_file"

    if [[ $(<"$stdout_file") != "[${n}]" ]]; then
        echo "FAIL: sum(${n}) changed under the explicit Prime stack" >&2
        cat "$stdout_file" >&2
        exit 1
    fi

    local roots pushes bind_tasks call_tasks normalize_tasks poisoned_tasks work
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
    poisoned_tasks=$(counter "$stats_file" prime-eval-stack-poisoned-task)
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
    if ((poisoned_tasks != 0)); then
        echo "FAIL: sum(${n}) entered a recursive poisoned task" >&2
        exit 1
    fi
    # Needed-equation search now contributes one generated-root decision
    # frame per recursive step.  Count that scheduler unit honestly while
    # retaining a tight linear envelope over the complete explicit machine.
    if ((work < n || work > 22 * n + 16)); then
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

if ((work_800 - work_400 > 22 * 400 + 16 ||
    work_1600 - work_800 > 22 * 800 + 16)); then
    echo "FAIL: explicit-stack work increments are not linearly bounded" >&2
    exit 1
fi
if ((evacuated_800 > 3 * evacuated_400 + 1048576 ||
    evacuated_1600 > 3 * evacuated_800 + 1048576)); then
    echo "FAIL: continuation evacuation escaped the linear doubling envelope" >&2
    exit 1
fi

# The first collection used to rehome an empty branch-state identity onto the
# moving survivor semispace.  Its first later StateCell event then allocated
# there, so the next collection had to abort safely.  Exercise nested active
# environments on both sides of the write and require at least two successful
# collections with exactly the GC-off result.
state_gc_stdout="$probe_dir/state-gc.stdout"
state_gc_stats="$probe_dir/state-gc.stats"
state_nogc_stdout="$probe_dir/state-nogc.stdout"
state_nogc_stats="$probe_dir/state-nogc.stats"
state_definition='(= (mam:state-spine $n) (if (< $n 1) (get-state &mam:gc-state) (if (== $n 800) (let $_ (change-state! &mam:gc-state 1) (mam:state-spine (- $n 1))) (+ (superpose (0)) (mam:state-spine (- $n 1))))))'
for gc in 0 1; do
    stdout_file="$state_nogc_stdout"
    stats_file="$state_nogc_stats"
    if [[ $gc == 1 ]]; then
        stdout_file="$state_gc_stdout"
        stats_file="$state_gc_stats"
    fi
    CETTA_GC=$gc CETTA_GC_BUDGET_MB=1 \
    "$BIN" --emit-runtime-stats --lang prime \
        -e '!(bind! &mam:gc-state (new-state 0))' \
        -e "$state_definition" \
        -e '!(mam:state-spine 1600)' \
        >"$stdout_file" 2>"$stats_file"
done
if ! cmp -s "$state_nogc_stdout" "$state_gc_stdout" ||
   [[ $(<"$state_gc_stdout") != $'[()]\n[1]' ]]; then
    echo "FAIL: GC changed stateful Prime continuation semantics" >&2
    diff -u "$state_nogc_stdout" "$state_gc_stdout" >&2 || true
    exit 1
fi
state_gc_collections=$(
    counter "$state_gc_stats" prime-eval-stack-gc-frame-safe-point
)
state_writes=$(counter "$state_gc_stats" prime-need-branch-state-write)
if ((state_gc_collections < 2 || state_writes < 1)); then
    echo "FAIL: stateful Prime canary missed the write or second collection" >&2
    printf '%s\n' \
        "collections=$state_gc_collections writes=$state_writes" >&2
    exit 1
fi

observation_stdout="$probe_dir/observation.stdout"
observation_stats="$probe_dir/observation.stats"
CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
"$BIN" --emit-runtime-stats --lang prime --count-only \
    "$ROOT/tests/prime/prepared_pure_observation_stack.metta" \
    >"$observation_stdout" 2>"$observation_stats"
if [[ $(<"$observation_stdout") != 1 ]]; then
    echo "FAIL: deep Prime structural observation changed its result count" >&2
    cat "$observation_stdout" >&2
    exit 1
fi
observation_tasks=$(counter "$observation_stats" prime-eval-stack-task-normalize)
observation_poisoned=$(counter "$observation_stats" prime-eval-stack-poisoned-task)
observation_heap_peak=$(counter "$observation_stats" prime-eval-stack-frame-depth-peak)
observation_c_stack_peak=$(counter "$observation_stats" eval-c-stack-guard-depth-peak)
observation_gc=$(counter "$observation_stats" prime-eval-stack-gc-frame-safe-point)
observation_evacuated=$(counter "$observation_stats" prime-eval-stack-gc-evacuated-bytes)
if ((observation_tasks < 10000 || observation_poisoned != 0 ||
    observation_heap_peak < 10000 || observation_c_stack_peak > 8 ||
    observation_gc == 0 || observation_evacuated == 0)); then
    echo "FAIL: deep Prime observation did not remain on generated-rooted heap frames" >&2
    exit 1
fi

non_tail_stdout="$probe_dir/non-tail.stdout"
non_tail_stats="$probe_dir/non-tail.stats"
CETTA_GC=1 CETTA_GC_BUDGET_MB=1 \
"$BIN" --emit-runtime-stats --lang prime --count-only \
    "$ROOT/tests/prime/non_tail_symbolic_observation_stack.metta" \
    >"$non_tail_stdout" 2>"$non_tail_stats"
if [[ $(<"$non_tail_stdout") != 1 ]]; then
    echo "FAIL: non-tail Prime observation changed its result count" >&2
    cat "$non_tail_stdout" >&2
    exit 1
fi
non_tail_admissions=$(counter "$non_tail_stats" prepared-pure-call-admission)
non_tail_commits=$(counter "$non_tail_stats" prepared-pure-call-commit)
non_tail_declines=$(counter "$non_tail_stats" prepared-pure-call-decline)
non_tail_cache_hits=$(
    counter "$non_tail_stats" prepared-pure-call-program-cache-hit
)
non_tail_cache_stores=$(
    counter "$non_tail_stats" prepared-pure-call-program-cache-store
)
non_tail_normalize=$(counter "$non_tail_stats" prime-eval-stack-task-normalize)
non_tail_poisoned=$(counter "$non_tail_stats" prime-eval-stack-poisoned-task)
non_tail_heap_peak=$(counter "$non_tail_stats" prime-eval-stack-frame-depth-peak)
non_tail_c_stack_peak=$(counter "$non_tail_stats" eval-c-stack-guard-depth-peak)
if ((non_tail_admissions < 10001 ||
    non_tail_commits != non_tail_admissions || non_tail_declines != 0 ||
    non_tail_cache_hits < 10000 || non_tail_cache_stores != 1 ||
    non_tail_normalize < 20001 || non_tail_poisoned != 0 ||
    non_tail_heap_peak < 30000 || non_tail_c_stack_peak > 8)); then
    echo "FAIL: non-tail Prime observation escaped cached heap machinery" >&2
    printf '%s\n' \
        "admission=$non_tail_admissions commit=$non_tail_commits decline=$non_tail_declines" \
        "cache-hit=$non_tail_cache_hits cache-store=$non_tail_cache_stores" \
        "normalize=$non_tail_normalize heap=$non_tail_heap_peak c-stack=$non_tail_c_stack_peak poisoned=$non_tail_poisoned" >&2
    exit 1
fi

# Structural normalization must retain only the logical support of the value
# crossing a continuation frame.  A recursively represented sequence exposes
# the old failure sharply: copying the full accumulated environment makes the
# released binding capacity superlinear even though the observable result is a
# single integer.  Two scales pin both the exact projection path and its
# linear-retention envelope without constraining a future whole-fold fusion.
support_released_100=0
support_released_200=0
run_support_probe() {
    local n=$1
    local stdout_file="$probe_dir/support-${n}.stdout"
    local stats_file="$probe_dir/support-${n}.stats"

    "$BIN" --emit-runtime-stats --lang prime \
        -e '!(import! &self clist)' \
        -e "!(clist:len (clist:range 0 ${n}))" \
        >"$stdout_file" 2>"$stats_file"

    local expected
    expected=$(printf '[()]\n[%d]' "$n")
    if [[ $(<"$stdout_file") != "$expected" ]]; then
        echo "FAIL: represented length(${n}) changed under support projection" >&2
        cat "$stdout_file" >&2
        exit 1
    fi

    local queries applied fallback elided released active_peak
    queries=$(counter "$stats_file" prime-eval-stack-support-projection-query)
    applied=$(counter "$stats_file" prime-eval-stack-support-projection-applied)
    fallback=$(counter "$stats_file" prime-eval-stack-support-projection-fallback)
    elided=$(counter "$stats_file" prime-eval-stack-support-item-elided)
    released=$(counter "$stats_file" bindings-released-entry-capacity)
    active_peak=$(counter "$stats_file" bindings-entry-active-bytes-peak)

    if ((queries == 0 || applied != queries || fallback != 0 ||
        elided == 0)); then
        echo "FAIL: length(${n}) did not exercise exact value-support projection" >&2
        printf '%s\n' \
            "queries=$queries applied=$applied fallback=$fallback elided=$elided" >&2
        exit 1
    fi
    if ((released > 256 * n + 4096 || active_peak > 4096 * n + 1048576)); then
        echo "FAIL: length(${n}) retained a world-sized normalization environment" >&2
        printf '%s\n' "released=$released active-peak=$active_peak" >&2
        exit 1
    fi

    printf -v "support_released_${n}" '%d' "$released"
}

run_support_probe 100
run_support_probe 200
if ((support_released_200 > 3 * support_released_100 + 4096)); then
    echo "FAIL: value-support retention escaped the linear doubling envelope" >&2
    exit 1
fi

# A branch-heavy complete bag keeps many initialized State/receipt carriers
# whose event DAG is empty.  Those carriers are the algebraic identity: they
# own no source-arena pointer and must not block exact evacuation.  Compare the
# complete output with GC disabled so every setup occurrence and the final
# 2^10 proof count remain part of the oracle.
branch_stdout="$probe_dir/branch.stdout"
branch_no_gc_stdout="$probe_dir/branch-no-gc.stdout"
branch_stats="$probe_dir/branch.stats"
bash "$ROOT/benchmarks/chaining/roman_chain_noise/gen_kb_branchy.sh" 10 1 |
    CETTA_GC=1 CETTA_GC_BUDGET_MB=64 \
    "$BIN" --emit-runtime-stats --lang prime --count-only /dev/stdin \
        >"$branch_stdout" 2>"$branch_stats"
bash "$ROOT/benchmarks/chaining/roman_chain_noise/gen_kb_branchy.sh" 10 1 |
    CETTA_GC=0 "$BIN" --lang prime --count-only /dev/stdin \
        >"$branch_no_gc_stdout"
if ! cmp -s "$branch_no_gc_stdout" "$branch_stdout" ||
   [[ $(tail -n 1 "$branch_stdout") != 1024 ]]; then
    echo "FAIL: empty branch-state carrier changed complete-bag multiplicity" >&2
    diff -u "$branch_no_gc_stdout" "$branch_stdout" >&2 || true
    exit 1
fi
branch_gc=$(counter "$branch_stats" prime-eval-stack-gc-frame-safe-point)
branch_evacuated=$(
    counter "$branch_stats" prime-eval-stack-gc-evacuated-bytes
)
if ((branch_gc == 0 || branch_evacuated == 0)); then
    echo "FAIL: branch-state identity probe did not cross moving GC" >&2
    exit 1
fi

# Cardinality is a universal observation: incomplete evaluation cannot publish
# a smaller count.  Complete emptiness, conversely, has the exact count zero.
count_zero_stdout="$probe_dir/count-zero.stdout"
"$BIN" --lang prime --count-only \
    "$ROOT/tests/prime/count_complete_zero.metta" >"$count_zero_stdout"
if [[ $(<"$count_zero_stdout") != 0 ]]; then
    echo "FAIL: complete empty bag did not publish count zero" >&2
    exit 1
fi
count_incomplete_stdout="$probe_dir/count-incomplete.stdout"
count_incomplete_stderr="$probe_dir/count-incomplete.stderr"
set +e
"$BIN" --lang prime --count-only --fuel 10 \
    "$ROOT/tests/prime/count_incomplete_loop.metta" \
    >"$count_incomplete_stdout" 2>"$count_incomplete_stderr"
count_incomplete_status=$?
set -e
if ((count_incomplete_status == 0)) ||
   [[ -s "$count_incomplete_stdout" ]] ||
   [[ $(<"$count_incomplete_stderr") != \
       "error: count observation incomplete: fuel-exhausted" ]]; then
    echo "FAIL: incomplete complete-bag observation published a count" >&2
    printf '%s\n' "status=$count_incomplete_status" >&2
    cat "$count_incomplete_stdout" "$count_incomplete_stderr" >&2
    exit 1
fi
for count_lane in prime petta; do
    count_partial_stdout="$probe_dir/count-partial-${count_lane}.stdout"
    count_partial_stderr="$probe_dir/count-partial-${count_lane}.stderr"
    set +e
    "$BIN" --lang "$count_lane" --count-only --fuel 20 \
        "$ROOT/tests/prime/count_incomplete_after_value.metta" \
        >"$count_partial_stdout" 2>"$count_partial_stderr"
    count_partial_status=$?
    set -e
    if ((count_partial_status == 0)) ||
       [[ -s "$count_partial_stdout" ]] ||
       [[ $(<"$count_partial_stderr") != \
           "error: count observation incomplete: fuel-exhausted" ]]; then
        echo "FAIL: ${count_lane} published a productive incomplete prefix" >&2
        printf '%s\n' "status=$count_partial_status" >&2
        cat "$count_partial_stdout" "$count_partial_stderr" >&2
        exit 1
    fi
done

he_stdout="$probe_dir/he.stdout"
he_stats="$probe_dir/he.stats"
"$BIN" --emit-runtime-stats --lang he --profile extended \
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
    prime-need-storage-key-scan-frame \
    prime-eval-stack-support-projection-query \
    prime-eval-stack-support-projection-applied \
    prime-eval-stack-support-projection-fallback \
    prime-eval-stack-support-item-elided \
    prime-need-source-argument-ref \
    prime-need-source-argument-universal-demand \
    prime-need-source-argument-universal-force \
    prime-need-source-argument-universal-cache-copy \
    prime-need-source-argument-universal-cache-bytes; do
    if (($(counter "$he_stats" "$name") != 0)); then
        echo "FAIL: Prime explicit-stack counter ${name} changed in HE" >&2
        exit 1
    fi
done

printf '%s\n' \
    "PrimeEvalStackStatsSummary PASS work=${work_400}/${work_800}/${work_1600} support=${support_released_100}/${support_released_200} non-tail-cache=${non_tail_cache_hits} HE=isolated"
