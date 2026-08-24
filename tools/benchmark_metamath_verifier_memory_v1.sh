#!/usr/bin/env bash

set -u

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$script_dir/.." && pwd)
timeout_seconds=${METAMATH_VERIFIER_BENCH_TIMEOUT:-1800}
python_bin=${PYTHON_BIN:-$(command -v python3 || true)}
mmverify=${MMVERIFY:-$root/../metamath/mmverify/mmverify.py}
mmlean4=${MM_LEAN4:-$root/../../Mettapedia/lean/standalone/mm-lean4/.lake/build/bin/mm-lean4}
metamath_exe=${METAMATH_EXE:-$root/../../repos/metamath-exe/metamath}
metamath_knife=${METAMATH_KNIFE:-$(command -v metamath-knife || true)}
direct_driver=${CETTA_DIRECT_PETTA_DRIVER:-}
direct_bin=${CETTA_DIRECT_PETTA_BIN:-}
direct_program=${CETTA_DIRECT_PETTA_PROGRAM:-}
direct_query=${CETTA_DIRECT_PETTA_QUERY:-}
direct_manifest=${CETTA_DIRECT_PETTA_MANIFEST:-}

if [[ "$#" -ne 1 || ! -f "$1" ||
        ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ||
        ! -x /usr/bin/time ]]; then
    echo "usage: $0 SET_MM" >&2
    exit 2
fi

database=$(realpath "$1") || exit 2
work=$(mktemp -d "$root/runtime/metamath-verifier-memory-v1.XXXXXX") || exit 2
relative_work=${work#"$root/"}
manifest="$work/manifest.tsv"
results="$work/results.tsv"
commands="$work/metamath-exe.commands"

printf 'set scroll continuous\nverify proof *\nexit\n' >"$commands"

{
    printf 'field\tvalue\n'
    printf 'benchmark\tmetamath-verifier-memory-v1\n'
    printf 'database_bytes\t%s\n' "$(wc -c <"$database" | tr -d '[:space:]')"
    printf 'database_sha256\t%s\n' "$(sha256sum "$database" | cut -d' ' -f1)"
    printf 'timeout_seconds\t%s\n' "$timeout_seconds"
} >"$manifest"

printf 'runner\tstatus\twall_seconds\tmax_rss_kb\n' >"$results"

printf '%s\n' \
    "benchmark=metamath-verifier-memory-v1" \
    "database_bytes=$(wc -c <"$database" | tr -d '[:space:]')" \
    "database_sha256=$(sha256sum "$database" | cut -d' ' -f1)" \
    "artifacts=$relative_work"
printf 'runner\tstatus\twall_seconds\tmax_rss_kb\n'

record_skipped() {
    local runner=$1
    printf '%s\tskipped\t-\t-\n' "$runner" | tee -a "$results"
}

measure() {
    local runner=$1
    shift
    local stdout_file="$work/$runner.stdout"
    local stderr_file="$work/$runner.stderr"
    local time_file="$work/$runner.time"
    local status wall_seconds='-' max_rss_kb='-' timed_status='-'

    printf '%s_sha256\t%s\n' "$runner" \
        "$(sha256sum "$1" | cut -d' ' -f1)" >>"$manifest"
    /usr/bin/time -q -f '%e\t%M\t%x' -o "$time_file" \
        timeout "$timeout_seconds" "$@" \
        >"$stdout_file" 2>"$stderr_file"
    status=$?
    IFS=$'\t' read -r wall_seconds max_rss_kb timed_status <"$time_file"
    if [[ "$timed_status" != "$status" ]]; then
        status="${status}/${timed_status}"
    fi
    printf '%s\t%s\t%s\t%s\n' \
        "$runner" "$status" "$wall_seconds" "$max_rss_kb" | \
        tee -a "$results"
}

if [[ -n "$direct_driver" && -x "$direct_driver" &&
      -n "$direct_bin" && -x "$direct_bin" &&
      -n "$direct_program" && -f "$direct_program" &&
      -n "$direct_query" && -f "$direct_query" &&
      -n "$direct_manifest" && -f "$direct_manifest" ]]; then
    {
        printf 'cetta_petta_driver_sha256\t%s\n' \
            "$(sha256sum "$direct_driver" | cut -d' ' -f1)"
        printf 'cetta_petta_program_sha256\t%s\n' \
            "$(sha256sum "$direct_program" | cut -d' ' -f1)"
        printf 'cetta_petta_query_sha256\t%s\n' \
            "$(sha256sum "$direct_query" | cut -d' ' -f1)"
        printf 'cetta_petta_manifest_sha256\t%s\n' \
            "$(sha256sum "$direct_manifest" | cut -d' ' -f1)"
    } >>"$manifest"
    measure cetta-petta /usr/bin/env \
        CETTA_DIRECT_PETTA_ROOT="$root" \
        CETTA_DIRECT_PETTA_BIN="$direct_bin" \
        CETTA_DIRECT_PETTA_PROGRAM="$direct_program" \
        CETTA_DIRECT_PETTA_QUERY="$direct_query" \
        CETTA_DIRECT_PETTA_MANIFEST="$direct_manifest" \
        CETTA_DIRECT_PETTA_TIMEOUT="$((timeout_seconds + 1))" \
        "$direct_driver" "$database"
else
    record_skipped cetta-petta
fi

if [[ -n "$python_bin" && -x "$python_bin" && -f "$mmverify" ]]; then
    printf 'mmverify_script_sha256\t%s\n' \
        "$(sha256sum "$mmverify" | cut -d' ' -f1)" >>"$manifest"
    measure mmverify /usr/bin/env -C "$(dirname "$mmverify")" \
        "$python_bin" "$mmverify" "$database"
else
    record_skipped mmverify
fi

if [[ -x "$mmlean4" ]]; then
    measure mm-lean4 "$mmlean4" "$database" --mode=zar
else
    record_skipped mm-lean4
fi

if [[ -n "$metamath_knife" && -x "$metamath_knife" ]]; then
    measure metamath-knife "$metamath_knife" "$database" --verify
else
    record_skipped metamath-knife
fi

if [[ -x "$metamath_exe" ]]; then
    measure metamath-exe "$metamath_exe" "$database" <"$commands"
else
    record_skipped metamath-exe
fi
