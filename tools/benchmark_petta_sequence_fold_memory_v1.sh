#!/usr/bin/env bash

set -u

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$script_dir/.." && pwd)
timeout_seconds=${PETTA_SEQUENCE_FOLD_BENCH_TIMEOUT:-120}
cetta_bin=${CETTA_BIN:-$root/cetta}
swi_root=${GSLT2PARSE_PETTA_ROOT:-${PETTA_SWI_ROOT:-$root/../PeTTa}}
require_conformance=${PETTA_SEQUENCE_FOLD_BENCH_REQUIRE_CONFORMANCE:-0}

if [[ "$#" -eq 0 ]]; then
    set -- 512
fi
if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ||
        ! "$require_conformance" =~ ^[01]$ ]]; then
    echo "usage: $0 [POSITIVE_SEQUENCE_LENGTH ...]" >&2
    exit 2
fi
for count in "$@"; do
    if [[ ! "$count" =~ ^[1-9][0-9]*$ ]]; then
        echo "usage: $0 [POSITIVE_SEQUENCE_LENGTH ...]" >&2
        exit 2
    fi
done
if [[ ! -x "$cetta_bin" || ! -x /usr/bin/time ]]; then
    echo "the benchmark requires an executable CeTTa binary and /usr/bin/time" >&2
    exit 2
fi

work=$(mktemp -d "$root/runtime/petta-sequence-fold-memory-v1.XXXXXX") || exit 2
relative_work=${work#"$root/"}
manifest="$work/manifest.tsv"
programs="$work/programs.tsv"
results="$work/results.tsv"
expected="$work/expected.stdout"
variants=(head-unit head-build view-unit view-build)

printf '%s\n' ProbeAcceptedV1 >"$expected"
{
    printf 'field\tvalue\n'
    printf 'benchmark\tpetta-sequence-fold-memory-v1\n'
    printf 'benchmark_script_sha256\t%s\n' \
        "$(sha256sum "$script_dir/benchmark_petta_sequence_fold_memory_v1.sh" | cut -d' ' -f1)"
    printf 'sequence_lengths\t%s\n' "$*"
    printf 'variants\t%s\n' "${variants[*]}"
    printf 'timeout_seconds\t%s\n' "$timeout_seconds"
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

printf 'sequence_length\tvariant\tprogram_bytes\tprogram_sha256\n' >"$programs"
printf 'sequence_length\tvariant\trunner\tstatus\toutput\tfailure_class\twall_seconds\tmax_rss_kb\tmax_choice_depth\tmax_heap_live_bytes\tchoice_snapshots\theap_collections\n' >"$results"

generate_program() {
    local count=$1
    local variant=$2
    local program=$3

    awk -v count="$count" -v variant="$variant" '
        function emit_view_rules() {
            print "(= (probe:ValuesViewV1 (ValuesViewV1 (ValuesSequenceV1 ValuesTreeEmptyV1 ValuesTreeStackNilV1) ValuesEmptyViewV1))"
            print "   (ValuesViewV1 (ValuesSequenceV1 ValuesTreeEmptyV1 ValuesTreeStackNilV1) ValuesEmptyViewV1))"
            print "(= (probe:ValuesViewV1 (ValuesViewV1 (ValuesSequenceV1 ValuesTreeEmptyV1 (ValuesTreeStackConsV1 $tree $stack)) $view))"
            print "   (let $_ (probe:ValuesViewV1 (ValuesViewV1 (ValuesSequenceV1 $tree $stack) $view))"
            print "     (ValuesViewV1 (ValuesSequenceV1 ValuesTreeEmptyV1 (ValuesTreeStackConsV1 $tree $stack)) $view)))"
            print "(= (probe:ValuesViewV1 (ValuesViewV1 (ValuesSequenceV1 (ValuesTreeLeafV1 $value) $stack) (ValuesUnconsViewV1 $value (ValuesSequenceV1 ValuesTreeEmptyV1 $stack))))"
            print "   (ValuesViewV1 (ValuesSequenceV1 (ValuesTreeLeafV1 $value) $stack) (ValuesUnconsViewV1 $value (ValuesSequenceV1 ValuesTreeEmptyV1 $stack))))"
            print "(= (probe:ValuesViewV1 (ValuesViewV1 (ValuesSequenceV1 (ValuesTreeNodeV1 $left $right) $stack) $view))"
            print "   (let $_ (probe:ValuesViewV1 (ValuesViewV1 (ValuesSequenceV1 $left (ValuesTreeStackConsV1 $right $stack)) $view))"
            print "     (ValuesViewV1 (ValuesSequenceV1 (ValuesTreeNodeV1 $left $right) $stack) $view)))"
        }

        function emit_head_unit_rules() {
            print "(= (probe:HeadUnitV1 (HeadUnitV1 (ValuesSequenceV1 ValuesTreeEmptyV1 ValuesTreeStackNilV1) ProbeDoneV1))"
            print "   (HeadUnitV1 (ValuesSequenceV1 ValuesTreeEmptyV1 ValuesTreeStackNilV1) ProbeDoneV1))"
            print "(= (probe:HeadUnitV1 (HeadUnitV1 (ValuesSequenceV1 ValuesTreeEmptyV1 (ValuesTreeStackConsV1 $tree $stack)) ProbeDoneV1))"
            print "   (let $_ (probe:HeadUnitV1 (HeadUnitV1 (ValuesSequenceV1 $tree $stack) ProbeDoneV1))"
            print "     (HeadUnitV1 (ValuesSequenceV1 ValuesTreeEmptyV1 (ValuesTreeStackConsV1 $tree $stack)) ProbeDoneV1)))"
            print "(= (probe:HeadUnitV1 (HeadUnitV1 (ValuesSequenceV1 (ValuesTreeLeafV1 $value) $stack) ProbeDoneV1))"
            print "   (let $_ (probe:HeadUnitV1 (HeadUnitV1 (ValuesSequenceV1 ValuesTreeEmptyV1 $stack) ProbeDoneV1))"
            print "     (HeadUnitV1 (ValuesSequenceV1 (ValuesTreeLeafV1 $value) $stack) ProbeDoneV1)))"
            print "(= (probe:HeadUnitV1 (HeadUnitV1 (ValuesSequenceV1 (ValuesTreeNodeV1 $left $right) $stack) ProbeDoneV1))"
            print "   (let $_ (probe:HeadUnitV1 (HeadUnitV1 (ValuesSequenceV1 $left (ValuesTreeStackConsV1 $right $stack)) ProbeDoneV1))"
            print "     (HeadUnitV1 (ValuesSequenceV1 (ValuesTreeNodeV1 $left $right) $stack) ProbeDoneV1)))"
        }

        function emit_head_build_rules() {
            print "(= (probe:HeadBuildV1 (HeadBuildV1 (ValuesSequenceV1 ValuesTreeEmptyV1 ValuesTreeStackNilV1) ProbeLabelNilV1))"
            print "   (HeadBuildV1 (ValuesSequenceV1 ValuesTreeEmptyV1 ValuesTreeStackNilV1) ProbeLabelNilV1))"
            print "(= (probe:HeadBuildV1 (HeadBuildV1 (ValuesSequenceV1 ValuesTreeEmptyV1 (ValuesTreeStackConsV1 $tree $stack)) $labels))"
            print "   (let $_ (probe:HeadBuildV1 (HeadBuildV1 (ValuesSequenceV1 $tree $stack) $labels))"
            print "     (HeadBuildV1 (ValuesSequenceV1 ValuesTreeEmptyV1 (ValuesTreeStackConsV1 $tree $stack)) $labels)))"
            print "(= (probe:HeadBuildV1 (HeadBuildV1 (ValuesSequenceV1 (ValuesTreeLeafV1 $value) $stack) (ProbeLabelConsV1 $value $labels)))"
            print "   (let $_ (probe:HeadBuildV1 (HeadBuildV1 (ValuesSequenceV1 ValuesTreeEmptyV1 $stack) $labels))"
            print "     (HeadBuildV1 (ValuesSequenceV1 (ValuesTreeLeafV1 $value) $stack) (ProbeLabelConsV1 $value $labels))))"
            print "(= (probe:HeadBuildV1 (HeadBuildV1 (ValuesSequenceV1 (ValuesTreeNodeV1 $left $right) $stack) $labels))"
            print "   (let $_ (probe:HeadBuildV1 (HeadBuildV1 (ValuesSequenceV1 $left (ValuesTreeStackConsV1 $right $stack)) $labels))"
            print "     (HeadBuildV1 (ValuesSequenceV1 (ValuesTreeNodeV1 $left $right) $stack) $labels)))"
        }

        function emit_view_unit_rules() {
            emit_view_rules()
            print "(= (probe:ViewUnitV1 (ViewUnitV1 $values ProbeDoneV1))"
            print "   (let $_ (probe:ValuesViewV1 (ValuesViewV1 $values ValuesEmptyViewV1))"
            print "     (ViewUnitV1 $values ProbeDoneV1)))"
            print "(= (probe:ViewUnitV1 (ViewUnitV1 $values ProbeDoneV1))"
            print "   (let $_ (probe:ValuesViewV1 (ValuesViewV1 $values (ValuesUnconsViewV1 $value $rest)))"
            print "   (let $_ (probe:ViewUnitV1 (ViewUnitV1 $rest ProbeDoneV1))"
            print "     (ViewUnitV1 $values ProbeDoneV1))))"
        }

        function emit_view_build_rules() {
            emit_view_rules()
            print "(= (probe:ViewBuildV1 (ViewBuildV1 $values ProbeLabelNilV1))"
            print "   (let $_ (probe:ValuesViewV1 (ValuesViewV1 $values ValuesEmptyViewV1))"
            print "     (ViewBuildV1 $values ProbeLabelNilV1)))"
            print "(= (probe:ViewBuildV1 (ViewBuildV1 $values (ProbeLabelConsV1 $value $labels)))"
            print "   (let $_ (probe:ValuesViewV1 (ValuesViewV1 $values (ValuesUnconsViewV1 $value $rest)))"
            print "   (let $_ (probe:ViewBuildV1 (ViewBuildV1 $rest $labels))"
            print "     (ViewBuildV1 $values (ProbeLabelConsV1 $value $labels)))))"
        }

        BEGIN {
            for (i = 0; i < count; ++i)
                node[i] = "(ValuesTreeLeafV1 ProbeValueV1-" i ")"
            n = count
            while (n > 1) {
                next_n = 0
                for (i = 0; i < n; i += 2) {
                    if (i + 1 < n)
                        next_node[next_n++] = "(ValuesTreeNodeV1 " node[i] " " node[i + 1] ")"
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
            tree = node[0]
            if (variant == "head-unit") {
                emit_head_unit_rules()
                print "!(let $_ (probe:HeadUnitV1 (HeadUnitV1 (ValuesSequenceV1 " tree " ValuesTreeStackNilV1) ProbeDoneV1)) ProbeAcceptedV1)"
            } else if (variant == "head-build") {
                emit_head_build_rules()
                print "!(let $_ (probe:HeadBuildV1 (HeadBuildV1 (ValuesSequenceV1 " tree " ValuesTreeStackNilV1) $labels)) ProbeAcceptedV1)"
            } else if (variant == "view-unit") {
                emit_view_unit_rules()
                print "!(let $_ (probe:ViewUnitV1 (ViewUnitV1 (ValuesSequenceV1 " tree " ValuesTreeStackNilV1) ProbeDoneV1)) ProbeAcceptedV1)"
            } else if (variant == "view-build") {
                emit_view_build_rules()
                print "!(let $_ (probe:ViewBuildV1 (ViewBuildV1 (ValuesSequenceV1 " tree " ValuesTreeStackNilV1) $labels)) ProbeAcceptedV1)"
            }
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
    local output_status failure_class
    local max_choice_depth max_heap_live_bytes choice_snapshots heap_collections

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
    heap_collections=$(stat_field heap_collections "$stderr_file")
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$count" "$variant" "$runner" "$status" "$output_status" \
        "$failure_class" "$wall_seconds" "$max_rss_kb" \
        "${max_choice_depth:--}" "${max_heap_live_bytes:--}" \
        "${choice_snapshots:--}" "${heap_collections:--}" | tee -a "$results"
}

printf '%s\n' \
    "benchmark=petta-sequence-fold-memory-v1" \
    "sequence_lengths=$*" \
    "artifacts=$relative_work"
printf 'sequence_length\tvariant\trunner\tstatus\toutput\tfailure_class\twall_seconds\tmax_rss_kb\tmax_choice_depth\tmax_heap_live_bytes\tchoice_snapshots\theap_collections\n'

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
            printf '%s\t%s\tswi-petta\tskipped\tnot-configured\tnot-configured\t-\t-\t-\t-\t-\t-\n' \
                "$count" "$variant" | tee -a "$results"
        fi
    done
done

if [[ "$require_conformance" -eq 1 && "$failed" -ne 0 ]]; then
    echo "one or more sequence-fold benchmark runners failed or disagreed" >&2
    exit 1
fi
