#!/usr/bin/env bash

set -u

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$script_dir/.." && pwd)
count=${1:-10000}
timeout_seconds=${PETTA_READER_BENCH_TIMEOUT:-300}
cetta_bin=${CETTA_BIN:-$root/cetta}
swi_root=${GSLT2PARSE_PETTA_ROOT:-${PETTA_SWI_ROOT:-$root/../PeTTa}}

case "$count" in
    ''|*[!0-9]*)
        echo "usage: $0 [POSITIVE_EQUATION_COUNT]" >&2
        exit 2
        ;;
esac
if [[ "$count" -eq 0 || ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
    echo "usage: $0 [POSITIVE_EQUATION_COUNT]" >&2
    exit 2
fi
if [[ ! -x "$cetta_bin" || ! -x /usr/bin/time ]]; then
    echo "the benchmark requires an executable CeTTa binary and /usr/bin/time" >&2
    exit 2
fi

work=$(mktemp -d "$root/runtime/petta-reader-memory-v1.XXXXXX") || exit 2
program="$work/program.metta"
expected="$work/expected"

awk -v count="$count" 'BEGIN {
    for (i = 0; i < count; ++i) {
        print "(= (PetTaReaderMemoryProbeV1 KeyV1-" i \
            ") (ValueV1 " i "))"
    }
    print "!(PetTaReaderMemoryProbeV1 KeyV1-0)"
}' >"$program"
printf '%s\n' '(ValueV1 0)' >"$expected"

program_bytes=$(wc -c <"$program" | tr -d '[:space:]')
program_sha256=$(sha256sum "$program" | cut -d' ' -f1)
relative_work=${work#"$root/"}

printf '%s\n' \
    "benchmark=petta-reader-memory-v1" \
    "equations=$count" \
    "program_bytes=$program_bytes" \
    "program_sha256=$program_sha256" \
    "artifacts=$relative_work"
printf 'runner\tstatus\toutput\twall_seconds\tmax_rss_kb\n'

failed=0
measure() {
    local runner=$1
    shift
    local stdout_file="$work/$runner.stdout"
    local stderr_file="$work/$runner.stderr"
    local time_file="$work/$runner.time"
    local status wall_seconds='-' max_rss_kb='-' timed_status='-'
    local output_status

    /usr/bin/time -f '%e\t%M\t%x' -o "$time_file" \
        timeout "$timeout_seconds" "$@" \
        >"$stdout_file" 2>"$stderr_file"
    status=$?
    IFS=$'\t' read -r wall_seconds max_rss_kb timed_status <"$time_file"

    output_status=match
    if ! cmp -s "$expected" "$stdout_file"; then
        output_status=mismatch
        failed=1
    fi
    if [[ "$status" -ne 0 || "$timed_status" -ne "$status" ]]; then
        failed=1
    fi
    printf '%s\t%s\t%s\t%s\t%s\n' \
        "$runner" "$status" "$output_status" \
        "$wall_seconds" "$max_rss_kb"
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
    printf 'swi-petta\tskipped\tnot-configured\t-\t-\n'
fi

if [[ "$failed" -ne 0 ]]; then
    echo "one or more runners failed or changed the expected answer" >&2
    exit 1
fi
