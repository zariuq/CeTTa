#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"
LANGUAGES="${MAM_LINEAR_FIB_LANGUAGES:-he prime petta}"
TIMEOUT_SECONDS="${MAM_LINEAR_FIB_TIMEOUT_SECONDS:-30}"
MAX_SECONDS="${MAM_LINEAR_FIB_MAX_SECONDS:-15}"
MAX_RSS_KIB="${MAM_LINEAR_FIB_MAX_RSS_KIB:-286720}"

N=999999
EXPECTED_DIGITS=208988
EXPECTED_SHA256=a82ec2dcbb7a34b3569a113fe476a6673b67d3168e2be54dbc805493ddce9b09

die() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

is_positive_number() {
    awk -v value="$1" 'BEGIN { exit !(value ~ /^[0-9]+([.][0-9]+)?$/ && value > 0) }'
}

is_positive_integer() {
    [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

[[ -x "$CETTA_BIN" ]] || die "CeTTa binary not found or not executable: $CETTA_BIN"
is_positive_number "$TIMEOUT_SECONDS" || die "timeout must be a positive number"
is_positive_number "$MAX_SECONDS" || die "time ceiling must be a positive number"
is_positive_integer "$MAX_RSS_KIB" || die "RSS ceiling must be a positive integer"

for language in $LANGUAGES; do
    case "$language" in
        he|prime|petta) ;;
        *) die "unsupported language in MAM_LINEAR_FIB_LANGUAGES: $language" ;;
    esac
done

mkdir -p "$ROOT/runtime/bench_mam_linear_fibonacci"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
run_dir="$(mktemp -d "$ROOT/runtime/bench_mam_linear_fibonacci/${stamp}.XXXXXX")"
source_file="$run_dir/linear_fibonacci_${N}.metta"
results="$run_dir/results.tsv"

sed "s/@N@/$N/g" \
    "$ROOT/benchmarks/mam_jetta/linear_fibonacci.metta.in" >"$source_file"
printf 'language\tn\tdigits\tsha256\twall_seconds\trss_kib\tstatus\n' >"$results"

overall_status=0
for language in $LANGUAGES; do
    log="$run_dir/${language}.out"
    timing="$run_dir/${language}.time"
    normalized="$run_dir/${language}.digits"
    status=ok
    rc=0

    language_args=(--lang "$language")
    if [[ "$language" == he ]]; then
        language_args+=(--profile extended)
    fi

    set +e
    /usr/bin/time -f '%e\t%M' -o "$timing" \
        timeout "$TIMEOUT_SECONDS" \
        "$CETTA_BIN" "${language_args[@]}" "$source_file" >"$log" 2>&1
    rc=$?
    set -e

    wall=NA
    rss=NA
    if [[ -s "$timing" ]]; then
        IFS=$'\t' read -r wall rss <"$timing" || true
    fi

    if [[ "$rc" -eq 124 ]]; then
        status=timeout
    elif [[ "$rc" -ne 0 ]]; then
        status="exit:$rc"
    fi

    : >"$normalized"
    if [[ "$status" == ok ]]; then
        if [[ "$language" == petta ]]; then
            sed -n 's/^\([0-9][0-9]*\)$/\1/p' "$log" >"$normalized"
        else
            sed -n 's/^\[\([0-9][0-9]*\)\]$/\1/p' "$log" >"$normalized"
        fi

        answer_count="$(wc -l <"$normalized")"
        if [[ "$answer_count" -ne 1 ]]; then
            status="answer-count:$answer_count"
        elif rg -q \
            -e '\(Error' \
            -e 'Stack overflow' \
            -e 'AddressSanitizer' \
            -e 'runtime error:' \
            "$log"; then
            status=runtime-error
        fi
    fi

    digits=NA
    actual_sha256=NA
    if [[ "$status" == ok ]]; then
        digits="$(awk 'NR == 1 { print length($0) }' "$normalized")"
        actual_sha256="$(tr -d '\n' <"$normalized" | sha256sum | awk '{ print $1 }')"
        if [[ "$digits" -ne "$EXPECTED_DIGITS" ]]; then
            status="wrong-digits:$digits"
        elif [[ "$actual_sha256" != "$EXPECTED_SHA256" ]]; then
            status=wrong-hash
        elif ! is_positive_number "$wall"; then
            status=missing-time
        elif ! is_positive_integer "$rss"; then
            status=missing-rss
        elif ! awk -v actual="$wall" -v limit="$MAX_SECONDS" \
            'BEGIN { exit !(actual <= limit) }'; then
            status="time-limit:$wall"
        elif (( rss > MAX_RSS_KIB )); then
            status="rss-limit:$rss"
        fi
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$language" "$N" "$digits" "$actual_sha256" "$wall" "$rss" "$status" \
        >>"$results"

    if [[ "$status" != ok ]]; then
        overall_status=1
        printf 'FAIL: linear fib(%s) %s: %s (log: %s)\n' \
            "$N" "$language" "$status" "${log#"$ROOT/"}" >&2
    fi
done

cat "$results"
if [[ "$overall_status" -ne 0 ]]; then
    exit "$overall_status"
fi

printf 'PASS: exact linear fib(%s), all requested dialects (artifacts: %s)\n' \
    "$N" "${run_dir#"$ROOT/"}"
