#!/usr/bin/env bash

set -u

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$script_dir/.." && pwd)
timeout_seconds=${PETTA_GUARDED_DETERMINISM_BENCH_TIMEOUT:-120}
cetta_bin=${CETTA_BIN:-$root/cetta}
swi_root=${GSLT2PARSE_PETTA_ROOT:-${PETTA_SWI_ROOT:-$root/../PeTTa}}
require_conformance=${PETTA_GUARDED_DETERMINISM_BENCH_REQUIRE_CONFORMANCE:-0}

if [[ "$#" -eq 0 ]]; then
    set -- 16
fi
if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ||
        ! "$require_conformance" =~ ^[01]$ ]]; then
    echo "usage: $0 [POSITIVE_LEAF_COUNT ...]" >&2
    exit 2
fi
for count in "$@"; do
    if [[ ! "$count" =~ ^[1-9][0-9]*$ ]]; then
        echo "usage: $0 [POSITIVE_LEAF_COUNT ...]" >&2
        exit 2
    fi
done
if [[ ! -x "$cetta_bin" || ! -x /usr/bin/time ]]; then
    echo "the benchmark requires an executable CeTTa binary and /usr/bin/time" >&2
    exit 2
fi

work=$(mktemp -d "$root/runtime/petta-guarded-determinism-memory-v1.XXXXXX") || exit 2
relative_work=${work#"$root/"}
manifest="$work/manifest.tsv"
programs="$work/programs.tsv"
results="$work/results.tsv"
expected="$work/expected.stdout"
variants=(equal different)

printf '%s\n' ProbeAcceptedV1 >"$expected"
{
    printf 'field\tvalue\n'
    printf 'benchmark\tpetta-guarded-determinism-memory-v1\n'
    printf 'benchmark_script_sha256\t%s\n' \
        "$(sha256sum "$script_dir/benchmark_petta_guarded_determinism_memory_v1.sh" | cut -d' ' -f1)"
    printf 'leaf_counts\t%s\n' "$*"
    printf 'variants\t%s\n' "${variants[*]}"
    printf 'timeout_seconds\t%s\n' "$timeout_seconds"
    printf 'expected_output_rows\t1\n'
    printf 'expected_stdout_sha256\t%s\n' \
        "$(sha256sum "$expected" | cut -d' ' -f1)"
    printf 'cetta_sha256\t%s\n' "$(sha256sum "$cetta_bin" | cut -d' ' -f1)"
    if [[ -x "$swi_root/run.sh" ]]; then
        printf 'swi_petta_run_sha256\t%s\n' \
            "$(sha256sum "$swi_root/run.sh" | cut -d' ' -f1)"
        if git -C "$swi_root" rev-parse --verify HEAD >/dev/null 2>&1; then
            printf 'swi_petta_git_head\t%s\n' \
                "$(git -C "$swi_root" rev-parse HEAD)"
            if [[ -n "$(git -C "$swi_root" status --short --untracked-files=no)" ]]; then
                printf 'swi_petta_tracked_state\tdirty\n'
            else
                printf 'swi_petta_tracked_state\tclean\n'
            fi
        fi
    else
        printf 'swi_petta_run_sha256\tnot-configured\n'
    fi
} >"$manifest"

printf 'leaf_count\tvariant\tprogram_bytes\tprogram_sha256\n' >"$programs"
printf 'leaf_count\tvariant\trunner\tstatus\toutput\tfailure_class\twall_seconds\tmax_rss_kb\tstdout_rows\tstdout_bytes\tstdout_sha256\tmax_choice_depth\tmax_heap_live_bytes\tchoice_snapshots\n' >"$results"

generate_program() {
    local count=$1
    local variant=$2
    local program=$3

    awk -v count="$count" -v variant="$variant" '
        BEGIN {
            print "(= (probe:DifferentV1 (DifferentV1 $left $right))"
            print "   (if (== $left $right) (empty) (DifferentV1 $left $right)))"
            print "(= (probe:CompareV1 (CompareV1 $value $value ProbeEqualV1))"
            print "   (CompareV1 $value $value ProbeEqualV1))"
            print "(= (probe:CompareV1 (CompareV1 $left $right ProbeDifferentV1))"
            print "   (let $_ (probe:DifferentV1 (DifferentV1 $left $right))"
            print "     (CompareV1 $left $right ProbeDifferentV1)))"
            print "(= (probe:WalkV1 (WalkV1 (ProbeLeafV1 $left $right)))"
            print "   (let $_ (probe:CompareV1 (CompareV1 $left $right $comparison))"
            print "     (WalkV1 (ProbeLeafV1 $left $right))))"
            print "(= (probe:WalkV1 (WalkV1 (ProbeNodeV1 $left $right)))"
            print "   (let $_ (probe:WalkV1 (WalkV1 $left))"
            print "   (let $_ (probe:WalkV1 (WalkV1 $right))"
            print "     (WalkV1 (ProbeNodeV1 $left $right)))))"
            for (i = 0; i < count; ++i) {
                left = "ProbeValueV1-" i
                right = variant == "equal" ? left : "ProbeOtherV1-" i
                node[i] = "(ProbeLeafV1 " left " " right ")"
            }
            n = count
            while (n > 1) {
                next_n = 0
                for (i = 0; i < n; i += 2) {
                    if (i + 1 < n)
                        next_node[next_n++] = "(ProbeNodeV1 " node[i] " " node[i + 1] ")"
                    else
                        next_node[next_n++] = node[i]
                }
                delete node
                for (i = 0; i < next_n; ++i) {
                    node[i] = next_node[i]
                    delete next_node[i]
                }
                n = next_n
            }
            print "!(let $_ (probe:WalkV1 (WalkV1 " node[0] ")) ProbeAcceptedV1)"
        }
    ' >"$program"
}

stat_field() {
    local key=$1
    local stderr_file=$2
    awk -v key="$key" '
        $1 == "PETTA_MACHINE_STATS" {
            for (i = 2; i <= NF; ++i) {
                split($i, pair, "=")
                if (pair[1] == key) {
                    print pair[2]
                    exit
                }
            }
        }
    ' "$stderr_file"
}

failed=0
measure() {
    local count=$1
    local variant=$2
    local runner=$3
    local program=$4
    shift 4
    local prefix="$work/$count-$variant-$runner"
    local stdout_file="$prefix.stdout"
    local stderr_file="$prefix.stderr"
    local time_file="$prefix.time"
    local status wall_seconds='-' max_rss_kb='-' timed_status='-'
    local output_status failure_class stdout_rows stdout_bytes stdout_sha256
    local max_choice_depth max_heap_live_bytes choice_snapshots

    /usr/bin/time -q -f '%e\t%M\t%x' -o "$time_file" \
        timeout "$timeout_seconds" "$@" \
        >"$stdout_file" 2>"$stderr_file"
    status=$?
    IFS=$'\t' read -r wall_seconds max_rss_kb timed_status <"$time_file"

    output_status=match
    failure_class=none
    if [[ "$status" -eq 124 ]]; then
        output_status=failed
        failure_class=timeout
        failed=1
    elif [[ "$status" -ne 0 || "$timed_status" -ne "$status" ]]; then
        output_status=failed
        if rg -qi 'out of memory|cannot allocate memory|memory allocation' "$stderr_file"; then
            failure_class=out-of-memory
        elif rg -qi 'stack overflow|stack limit|recursion limit' "$stderr_file"; then
            failure_class=stack-overflow
        else
            failure_class="exit-$status"
        fi
        failed=1
    elif ! cmp -s "$expected" "$stdout_file"; then
        output_status=mismatch
        failure_class=wrong-output
        failed=1
    fi

    max_choice_depth=$(stat_field max_choice_depth "$stderr_file")
    max_heap_live_bytes=$(stat_field max_heap_live_bytes "$stderr_file")
    choice_snapshots=$(stat_field choice_continuation_snapshots "$stderr_file")
    stdout_rows=$(wc -l <"$stdout_file" | tr -d '[:space:]')
    stdout_bytes=$(wc -c <"$stdout_file" | tr -d '[:space:]')
    stdout_sha256=$(sha256sum "$stdout_file" | cut -d' ' -f1)
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$count" "$variant" "$runner" "$status" "$output_status" \
        "$failure_class" "$wall_seconds" "$max_rss_kb" \
        "$stdout_rows" "$stdout_bytes" "$stdout_sha256" \
        "${max_choice_depth:--}" "${max_heap_live_bytes:--}" \
        "${choice_snapshots:--}" | tee -a "$results"
}

printf '%s\n' \
    "benchmark=petta-guarded-determinism-memory-v1" \
    "leaf_counts=$*" \
    "artifacts=$relative_work"
printf 'leaf_count\tvariant\trunner\tstatus\toutput\tfailure_class\twall_seconds\tmax_rss_kb\tstdout_rows\tstdout_bytes\tstdout_sha256\tmax_choice_depth\tmax_heap_live_bytes\tchoice_snapshots\n'

for count in "$@"; do
    for variant in "${variants[@]}"; do
        program="$work/$count-$variant.metta"
        generate_program "$count" "$variant" "$program"
        program_bytes=$(wc -c <"$program" | tr -d '[:space:]')
        program_sha256=$(sha256sum "$program" | cut -d' ' -f1)
        printf '%s\t%s\t%s\t%s\n' \
            "$count" "$variant" "$program_bytes" "$program_sha256" >>"$programs"
        measure "$count" "$variant" cetta-petta-search-machine "$program" \
            env CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
            "$cetta_bin" --emit-runtime-stats --lang petta "$program"
        measure "$count" "$variant" cetta-petta-canonical "$program" \
            env CETTA_PETTA_SEARCH_MACHINE=0 \
            "$cetta_bin" --lang petta "$program"
        if [[ -x "$swi_root/run.sh" ]]; then
            measure "$count" "$variant" swi-petta "$program" \
                sh "$swi_root/run.sh" --silent "$program"
        else
            printf '%s\t%s\tswi-petta\tskipped\tnot-configured\tnot-configured\t-\t-\t-\t-\t-\t-\t-\t-\n' \
                "$count" "$variant" | tee -a "$results"
        fi
    done
done

if [[ "$require_conformance" -eq 1 && "$failed" -ne 0 ]]; then
    echo "one or more guarded-determinism benchmark runners failed or disagreed" >&2
    exit 1
fi
