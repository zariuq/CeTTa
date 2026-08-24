#!/usr/bin/env bash

set -u

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$script_dir/.." && pwd)
timeout_seconds=${PETTA_PROGRAM_BENCH_TIMEOUT:-300}
cetta_bin=${CETTA_BIN:-$root/cetta}
swi_root=${GSLT2PARSE_PETTA_ROOT:-${PETTA_SWI_ROOT:-$root/../PeTTa}}
require_conformance=${PETTA_PROGRAM_REQUIRE_CONFORMANCE:-0}
allow_primary_failure=${PETTA_PROGRAM_ALLOW_PRIMARY_FAILURE:-0}

if [[ "$#" -ne 1 || ! -f "$1" ||
        ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ||
        ! "$require_conformance" =~ ^[01]$ ||
        ! "$allow_primary_failure" =~ ^[01]$ ]]; then
    echo "usage: $0 PETTA_PROGRAM" >&2
    exit 2
fi
if [[ ! -x "$cetta_bin" || ! -x /usr/bin/time ]]; then
    echo "the benchmark requires an executable CeTTa binary and /usr/bin/time" >&2
    exit 2
fi

program=$(realpath "$1") || exit 2
work=$(mktemp -d "$root/runtime/petta-program-memory-v1.XXXXXX") || exit 2
program_bytes=$(wc -c <"$program" | tr -d '[:space:]')
program_sha256=$(sha256sum "$program" | cut -d' ' -f1)
relative_work=${work#"$root/"}
manifest="$work/manifest.tsv"
results="$work/results.tsv"

{
    printf 'field\tvalue\n'
    printf 'benchmark\tpetta-program-memory-v1\n'
    printf 'program_bytes\t%s\n' "$program_bytes"
    printf 'program_sha256\t%s\n' "$program_sha256"
    printf 'timeout_seconds\t%s\n' "$timeout_seconds"
    printf 'cetta_sha256\t%s\n' "$(sha256sum "$cetta_bin" | cut -d' ' -f1)"
    if [[ -x "$swi_root/run.sh" ]]; then
        printf 'swi_petta_run_sha256\t%s\n' \
            "$(sha256sum "$swi_root/run.sh" | cut -d' ' -f1)"
    else
        printf 'swi_petta_run_sha256\tnot-configured\n'
    fi
} >"$manifest"

printf 'runner\tstatus\toutput\twall_seconds\tmax_rss_kb\n' >"$results"

printf '%s\n' \
    "benchmark=petta-program-memory-v1" \
    "program_bytes=$program_bytes" \
    "program_sha256=$program_sha256" \
    "artifacts=$relative_work"
printf 'runner\tstatus\toutput\twall_seconds\tmax_rss_kb\n'

reference_ready=0
nonconforming=0
measure() {
    local runner=$1
    shift
    local stdout_file="$work/$runner.stdout"
    local stderr_file="$work/$runner.stderr"
    local time_file="$work/$runner.time"
    local status wall_seconds='-' max_rss_kb='-' timed_status='-'
    local output_status

    /usr/bin/time -q -f '%e\t%M\t%x' -o "$time_file" \
        timeout "$timeout_seconds" "$@" \
        >"$stdout_file" 2>"$stderr_file"
    status=$?
    IFS=$'\t' read -r wall_seconds max_rss_kb timed_status <"$time_file"

    if [[ "$runner" == cetta-petta-search-machine ]]; then
        output_status=reference
        if [[ "$status" -eq 0 && "$timed_status" -eq "$status" ]]; then
            cp "$stdout_file" "$work/reference.stdout"
            reference_ready=1
        else
            output_status=failed-reference
            nonconforming=1
        fi
    elif [[ "$status" -ne 0 || "$timed_status" -ne "$status" ]]; then
        output_status=failed
        nonconforming=1
    elif [[ "$reference_ready" -ne 1 ]]; then
        output_status=uncompared
    elif cmp -s "$work/reference.stdout" "$stdout_file"; then
        output_status=match
    else
        output_status=mismatch
        nonconforming=1
    fi
    printf '%s\t%s\t%s\t%s\t%s\n' \
        "$runner" "$status" "$output_status" \
        "$wall_seconds" "$max_rss_kb" | tee -a "$results"
}

measure cetta-petta-search-machine \
    env CETTA_PETTA_SEARCH_MACHINE=1 \
    "$cetta_bin" --lang petta "$program"
measure cetta-petta-canonical \
    env CETTA_PETTA_SEARCH_MACHINE=0 \
    "$cetta_bin" --lang petta "$program"

if [[ -x "$swi_root/run.sh" ]]; then
    measure swi-petta sh "$swi_root/run.sh" --silent "$program"
else
    printf 'swi-petta\tskipped\tnot-configured\t-\t-\n' | \
        tee -a "$results"
fi

if [[ "$reference_ready" -ne 1 && "$allow_primary_failure" -ne 1 ]]; then
    echo "the primary c.Petta runner did not produce a reference result" >&2
    exit 1
fi
if [[ "$require_conformance" -eq 1 && "$nonconforming" -ne 0 ]]; then
    echo "one or more secondary runners failed or disagreed" >&2
    exit 1
fi
