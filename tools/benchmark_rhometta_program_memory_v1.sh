#!/usr/bin/env bash

set -u

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$script_dir/.." && pwd)
timeout_seconds=${RHOMETTA_PROGRAM_BENCH_TIMEOUT:-300}
cetta_bin=${CETTA_BIN:-$root/cetta}

if [[ "$#" -ne 1 || ! -f "$1" ||
        ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ||
        ! -x "$cetta_bin" || ! -x /usr/bin/time ]]; then
    echo "usage: $0 RHOMETTA_PROGRAM" >&2
    exit 2
fi

program=$(realpath "$1") || exit 2
work=$(mktemp -d "$root/runtime/rhometta-program-memory-v1.XXXXXX") || exit 2
relative_work=${work#"$root/"}
manifest="$work/manifest.tsv"
results="$work/results.tsv"
stdout_file="$work/cetta.stdout"
stderr_file="$work/cetta.stderr"
time_file="$work/cetta.time"

{
    printf 'field\tvalue\n'
    printf 'benchmark\trhometta-program-memory-v1\n'
    printf 'program_bytes\t%s\n' "$(wc -c <"$program" | tr -d '[:space:]')"
    printf 'program_sha256\t%s\n' "$(sha256sum "$program" | cut -d' ' -f1)"
    printf 'timeout_seconds\t%s\n' "$timeout_seconds"
    printf 'cetta_sha256\t%s\n' "$(sha256sum "$cetta_bin" | cut -d' ' -f1)"
} >"$manifest"

/usr/bin/time -q -f '%e\t%M\t%x' -o "$time_file" \
    timeout "$timeout_seconds" "$cetta_bin" "$program" \
    >"$stdout_file" 2>"$stderr_file"
status=$?
IFS=$'\t' read -r wall_seconds max_rss_kb timed_status <"$time_file"
observations=$(rg -o -F '(gslt-rho:observed-v1' "$stdout_file" | wc -l)

{
    printf 'runner\tstatus\twall_seconds\tmax_rss_kb\tobservations\n'
    printf 'cetta-rhometta\t%s\t%s\t%s\t%s\n' \
        "$status" "$wall_seconds" "$max_rss_kb" "$observations"
} >"$results"

printf '%s\n' \
    "benchmark=rhometta-program-memory-v1" \
    "program_bytes=$(wc -c <"$program" | tr -d '[:space:]')" \
    "program_sha256=$(sha256sum "$program" | cut -d' ' -f1)" \
    "artifacts=$relative_work"
printf 'runner\tstatus\twall_seconds\tmax_rss_kb\tobservations\n'
printf 'cetta-rhometta\t%s\t%s\t%s\t%s\n' \
    "$status" "$wall_seconds" "$max_rss_kb" "$observations"

if [[ "$timed_status" != "$status" ]]; then
    echo "timeout and measured process statuses disagree" >&2
    exit 1
fi
