#!/usr/bin/env bash

set -euo pipefail

: "${CETTA_DIRECT_PETTA_ROOT:?CETTA_DIRECT_PETTA_ROOT is required}"
: "${CETTA_DIRECT_PETTA_BIN:?CETTA_DIRECT_PETTA_BIN is required}"
: "${CETTA_DIRECT_PETTA_PROGRAM:?CETTA_DIRECT_PETTA_PROGRAM is required}"
: "${CETTA_DIRECT_PETTA_QUERY:?CETTA_DIRECT_PETTA_QUERY is required}"
: "${CETTA_DIRECT_PETTA_MANIFEST:?CETTA_DIRECT_PETTA_MANIFEST is required}"

if [[ "$#" -ne 1 ]]; then
    echo "usage: $0 SOURCE" >&2
    exit 2
fi

runtime_root=$(realpath "$CETTA_DIRECT_PETTA_ROOT") || exit 2
runtime_bin=$(realpath "$CETTA_DIRECT_PETTA_BIN") || exit 2
program=$(realpath "$CETTA_DIRECT_PETTA_PROGRAM") || exit 2
query=$(realpath "$CETTA_DIRECT_PETTA_QUERY") || exit 2
manifest=$(realpath "$CETTA_DIRECT_PETTA_MANIFEST") || exit 2
source_file=$(realpath "$1") || exit 2
timeout_seconds=${CETTA_DIRECT_PETTA_TIMEOUT:-120}

if [[ ! -d "$runtime_root/runtime" || ! -x "$runtime_bin" ||
        ! -f "$program" || ! -f "$query" || ! -f "$manifest" ||
        ! -f "$source_file" ||
        ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
    echo "direct PeTTa outcome driver received an invalid resource" >&2
    exit 2
fi
if ldd "$runtime_bin" 2>/dev/null | grep -Eiq 'python|libswipl|prolog'; then
    echo "direct PeTTa outcome driver requires a C-only runtime" >&2
    exit 2
fi

work=$(mktemp -d "$runtime_root/runtime/direct-petta-outcome.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM

set +e
timeout "$timeout_seconds" env CETTA_PETTA_SEARCH_MACHINE=1 \
    "$runtime_bin" --lang petta \
    "$program" "$query" "$source_file" "$manifest" \
    >"$work/runtime.stdout" 2>"$work/runtime.stderr"
runtime_status=$?
set -e
if [[ "$runtime_status" -eq 124 || "$runtime_status" -eq 137 ]]; then
    printf 'cetta-petta: exceeded %s-second stage bound\n' \
        "$timeout_seconds" >&2
    exit 2
elif [[ "$runtime_status" -ne 0 ]]; then
    sed 's/^/cetta-petta: /' "$work/runtime.stderr" >&2
    exit 2
fi

grep -E '^\(MetamathDirectPeTTaOutcomeV1 ' "$work/runtime.stdout" \
    >"$work/outcomes" || true
line_count=$(awk 'NF { count++ } END { print count + 0 }' \
    "$work/outcomes")
accepted_count=$(grep -Ec \
    '^\(MetamathDirectPeTTaOutcomeV1 AcceptedV1\)$' \
    "$work/outcomes" || true)
refused_count=$(grep -Ec \
    '^\(MetamathDirectPeTTaOutcomeV1 \(RefusedV1 .+\)\)$' \
    "$work/outcomes" || true)
parser_refused_count=$(grep -Ec \
    '^\(MetamathDirectPeTTaOutcomeV1 ParserRefusedV1\)$' \
    "$work/outcomes" || true)
parser_incomplete_count=$(grep -Ec \
    '^\(MetamathDirectPeTTaOutcomeV1 ParserIncompleteV1\)$' \
    "$work/outcomes" || true)

if [[ "$line_count" -eq 1 && "$accepted_count" -eq 1 &&
        "$refused_count" -eq 0 ]]; then
    printf '%s\n' '(MetamathDirectPeTTaOutcomeV1 AcceptedV1)'
elif [[ "$line_count" -eq 1 && "$accepted_count" -eq 0 &&
        "$refused_count" -eq 1 ]]; then
    cat "$work/outcomes"
elif [[ "$line_count" -eq 1 && "$parser_refused_count" -eq 1 ]]; then
    cat "$work/outcomes"
elif [[ "$line_count" -eq 1 && "$parser_incomplete_count" -eq 1 ]]; then
    cat "$work/outcomes"
elif [[ "$line_count" -eq 0 ]]; then
    printf '%s\n' \
        '(MetamathDirectPeTTaOutcomeV1 MissingSemanticOutcomeV1)'
else
    sed 's/^/unexpected-cetta-petta-output: /' \
        "$work/outcomes" >&2
    exit 2
fi
