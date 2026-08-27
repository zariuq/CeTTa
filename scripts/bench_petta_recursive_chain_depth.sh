#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"
# The default pair is the smallest current success/failure bracket.  Override
# PETTA_RECURSIVE_CHAIN_DEPTHS for a wider scaling ladder after a fix lands.
DEPTHS="${PETTA_RECURSIVE_CHAIN_DEPTHS:-64 128}"
TIMEOUT_SECONDS="${PETTA_RECURSIVE_CHAIN_TIMEOUT_SECONDS:-5}"
TEMPLATE="$ROOT/benchmarks/petta/recursive_chain_depth.metta.in"

die() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

[[ -x "$CETTA_BIN" ]] || die "CeTTa binary not found or not executable: $CETTA_BIN"
[[ -f "$TEMPLATE" ]] || die "benchmark template not found: $TEMPLATE"
[[ "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]] || \
    die "timeout must be a positive integer"

mkdir -p "$ROOT/runtime"
work="$(mktemp -d "$ROOT/runtime/bench_petta_recursive_chain_depth.XXXXXX")"
trap 'rm -rf "$work"' EXIT INT TERM

printf 'depth\tstatus\texit\twall_seconds\trss_kib\n'
have_exact=0
unexpected=0

for depth in $DEPTHS; do
    [[ "$depth" =~ ^[1-9][0-9]*$ ]] || die "depth must be a positive integer: $depth"
    source="$work/depth_${depth}.metta"
    stdout="$work/depth_${depth}.out"
    stderr="$work/depth_${depth}.err"
    timing="$work/depth_${depth}.time"
    sed "s/@DEPTH@/$depth/g" "$TEMPLATE" >"$source"

    set +e
    /usr/bin/time -f '%e\t%M' -o "$timing" \
        timeout "$TIMEOUT_SECONDS" \
        env CETTA_PETTA_SEARCH_MACHINE=1 \
            CETTA_PETTA_CLAUSE_BODY_ACTIVATION=0 \
            "$CETTA_BIN" --quiet --lang petta "$source" \
            >"$stdout" 2>"$stderr"
    rc=$?
    set -e

    wall=NA
    rss=NA
    if [[ -s "$timing" ]]; then
        IFS=$'\t' read -r wall rss < <(tail -n 1 "$timing") || true
    fi

    actual="$(tr -d '\r\n' <"$stdout")"
    if [[ "$rc" -eq 0 && "$actual" == "$depth" ]]; then
        status=exact
        have_exact=1
    elif [[ "$rc" -eq 124 ]]; then
        status=timeout
    elif rg -q 'Stack overflow|AddressSanitizer: stack-overflow' \
            "$stdout" "$stderr"; then
        status=stack-overflow
    elif [[ "$rc" -eq 0 ]]; then
        status=wrong-result
        unexpected=1
    else
        status="exit-$rc"
        unexpected=1
    fi

    printf '%s\t%s\t%s\t%s\t%s\n' \
        "$depth" "$status" "$rc" "$wall" "$rss"
done

[[ "$have_exact" -eq 1 ]] || die "no depth produced an exact result"
[[ "$unexpected" -eq 0 ]] || die "benchmark encountered a semantic or runtime failure"
