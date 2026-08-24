#!/usr/bin/env bash

set -u

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$script_dir/.." && pwd)
timeout_seconds=${RHOMETTA_RHOCALC_BENCH_TIMEOUT:-30}
cetta_bin=${CETTA_BIN:-$root/cetta}

if [[ "$#" -lt 2 || ! -f "$1" || ! -f "$2" ||
        ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ||
        ! -x "$cetta_bin" || ! -x /usr/bin/time ]]; then
    echo "usage: $0 RHOMETTA_PROGRAM PROCESS_QUERY [REDUCTION_LIMIT ...]" >&2
    exit 2
fi

program=$(realpath "$1") || exit 2
query=$(realpath "$2") || exit 2
shift 2
if [[ "$#" -eq 0 ]]; then
    set -- 1 10 100 1000
fi
for limit in "$@"; do
    if [[ ! "$limit" =~ ^[1-9][0-9]*$ ]]; then
        echo "reduction limits must be positive integers" >&2
        exit 2
    fi
done

work=$(mktemp -d "$root/runtime/rhometta-rhocalc-profile-v1.XXXXXX") || exit 2
relative_work=${work#"$root/"}
combined="$work/program.metta"
manifest="$work/manifest.tsv"
results="$work/results.tsv"

awk '1' "$program" "$query" >"$combined"
{
    printf 'field\tvalue\n'
    printf 'benchmark\trhometta-rhocalc-profile-v1\n'
    printf 'program_sha256\t%s\n' "$(sha256sum "$program" | cut -d' ' -f1)"
    printf 'query_sha256\t%s\n' "$(sha256sum "$query" | cut -d' ' -f1)"
    printf 'combined_bytes\t%s\n' "$(wc -c <"$combined" | tr -d '[:space:]')"
    printf 'combined_sha256\t%s\n' "$(sha256sum "$combined" | cut -d' ' -f1)"
    printf 'reduction_limits\t%s\n' "$*"
    printf 'timeout_seconds\t%s\n' "$timeout_seconds"
    printf 'cetta_sha256\t%s\n' "$(sha256sum "$cetta_bin" | cut -d' ' -f1)"
} >"$manifest"

printf 'reduction_limit\tstatus\tcompletion\twall_seconds\tmax_rss_kb\tobserved_values\n' >"$results"
printf '%s\n' \
    "benchmark=rhometta-rhocalc-profile-v1" \
    "combined_bytes=$(wc -c <"$combined" | tr -d '[:space:]')" \
    "combined_sha256=$(sha256sum "$combined" | cut -d' ' -f1)" \
    "artifacts=$relative_work"
printf 'reduction_limit\tstatus\tcompletion\twall_seconds\tmax_rss_kb\tobserved_values\n'

for limit in "$@"; do
    stdout_file="$work/$limit.stdout"
    stderr_file="$work/$limit.stderr"
    time_file="$work/$limit.time"
    /usr/bin/time -q -f '%e\t%M\t%x' -o "$time_file" \
        timeout "$timeout_seconds" "$cetta_bin" \
        --rho-reduction-limit "$limit" \
        --lang rhocalc --profile rhometta --syntax metta "$combined" \
        >"$stdout_file" 2>"$stderr_file"
    status=$?
    IFS=$'\t' read -r wall_seconds max_rss_kb timed_status <"$time_file"
    if [[ "$timed_status" != "$status" ]]; then
        status="$status/$timed_status"
    fi
    case "$status" in
        0) completion=quiescent ;;
        3) completion=bounded-prefix ;;
        124) completion=timeout ;;
        *) completion=failed ;;
    esac
    observed_values=$(rg -o -F '(gslt-rho:observed-v1' "$stdout_file" | wc -l)
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$limit" "$status" "$completion" "$wall_seconds" \
        "$max_rss_kb" "$observed_values" | tee -a "$results"
done
