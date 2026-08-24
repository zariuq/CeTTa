#!/usr/bin/env bash

set -u

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$script_dir/.." && pwd)
timeout_seconds=${RHOMETTA_RUNTIME_BENCH_TIMEOUT:-60}
reduction_limit=${RHOMETTA_RUNTIME_BENCH_REDUCTION_LIMIT:-1000}
cetta_bin=${CETTA_BIN:-$root/runtime/cetta-metamath-core-v1}
program=${CETTA_DIRECT_RHOMETTA_PROGRAM:-$root/langdef/rhometta/generated/metamath_state_proof_runtime_v1.metta}
query=${CETTA_DIRECT_RHOMETTA_QUERY:-$root/tests/rhometta_run/metamath_state_proof_runtime_input_v1.metta}
manifest=${CETTA_DIRECT_RHOMETTA_MANIFEST:-$root/langdef/metamath/direct_runtime_v1.metta}

if [[ "$#" -ne 1 || ! -f "$1" ||
        ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ||
        ! "$reduction_limit" =~ ^[1-9][0-9]*$ ||
        ! -x "$cetta_bin" || ! -f "$program" ||
        ! -f "$query" || ! -f "$manifest" ||
        ! -x /usr/bin/time ]]; then
    echo "usage: $0 MM_SOURCE" >&2
    exit 2
fi

mm_source=$(realpath "$1") || exit 2
work=$(mktemp -d "$root/runtime/metamath-direct-rhometta-runtime-v1.XXXXXX") || exit 2
relative_work=${work#"$root/"}
run_file="$work/run.metta"
benchmark_manifest="$work/manifest.tsv"
results="$work/results.tsv"
stdout_file="$work/cetta.stdout"
stderr_file="$work/cetta.stderr"
time_file="$work/cetta.time"

awk '1' "$program" "$query" >"$run_file"

{
    printf 'field\tvalue\n'
    printf 'benchmark\tmetamath-direct-rhometta-runtime-v1\n'
    printf 'program_bytes\t%s\n' "$(wc -c <"$program" | tr -d '[:space:]')"
    printf 'program_sha256\t%s\n' "$(sha256sum "$program" | cut -d' ' -f1)"
    printf 'query_sha256\t%s\n' "$(sha256sum "$query" | cut -d' ' -f1)"
    printf 'mm_bytes\t%s\n' "$(wc -c <"$mm_source" | tr -d '[:space:]')"
    printf 'mm_sha256\t%s\n' "$(sha256sum "$mm_source" | cut -d' ' -f1)"
    printf 'manifest_sha256\t%s\n' "$(sha256sum "$manifest" | cut -d' ' -f1)"
    printf 'reduction_limit\t%s\n' "$reduction_limit"
    printf 'timeout_seconds\t%s\n' "$timeout_seconds"
    printf 'cetta_sha256\t%s\n' "$(sha256sum "$cetta_bin" | cut -d' ' -f1)"
} >"$benchmark_manifest"

/usr/bin/time -q -f '%e\t%M\t%x' -o "$time_file" \
    timeout "$timeout_seconds" "$cetta_bin" \
    --rho-reduction-limit "$reduction_limit" \
    --lang rhocalc --profile rhometta --syntax metta \
    "$run_file" "$mm_source" "$manifest" \
    >"$stdout_file" 2>"$stderr_file"
status=$?
IFS=$'\t' read -r wall_seconds max_rss_kb timed_status <"$time_file"
outcomes=$(rg -o -F 'MetamathDirectRhoOutcomeV1' "$stdout_file" | wc -l)
limit_exhausted=$(rg -c -F 'rhocalc reduction limit exhausted' "$stderr_file" || true)

{
    printf 'runner\tstatus\twall_seconds\tmax_rss_kb\toutcomes\tlimit_exhausted\n'
    printf 'cetta-rhometta\t%s\t%s\t%s\t%s\t%s\n' \
        "$status" "$wall_seconds" "$max_rss_kb" "$outcomes" "$limit_exhausted"
} >"$results"

printf '%s\n' \
    "benchmark=metamath-direct-rhometta-runtime-v1" \
    "program_sha256=$(sha256sum "$program" | cut -d' ' -f1)" \
    "mm_sha256=$(sha256sum "$mm_source" | cut -d' ' -f1)" \
    "artifacts=$relative_work"
cat "$results"

if [[ "$timed_status" != "$status" ]]; then
    echo "timeout and measured process statuses disagree" >&2
    exit 1
fi
