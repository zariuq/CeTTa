#!/bin/sh

set -eu

: "${CETTA_LANGDEF_ROOT:?CETTA_LANGDEF_ROOT is required}"
: "${CETTA_LANGDEF_BIN:?CETTA_LANGDEF_BIN is required}"
: "${CETTA_LANGDEF_MANIFEST:?CETTA_LANGDEF_MANIFEST is required}"
: "${CETTA_LANGDEF_PROOF_BACKEND:?CETTA_LANGDEF_PROOF_BACKEND is required}"

if [ "$#" -ne 1 ]; then
    echo "usage: $0 SOURCE" >&2
    exit 2
fi

registry_root=$(realpath "$CETTA_LANGDEF_ROOT") || exit 2
runtime_bin=$(realpath "$CETTA_LANGDEF_BIN") || exit 2
manifest=$(realpath "$CETTA_LANGDEF_MANIFEST") || exit 2
source_file=$(realpath "$1") || exit 2
language_name=$(basename "$(dirname "$manifest")")
expected_manifest=$(
    realpath "$registry_root/$language_name/langdef.metta"
) || exit 2

if [ ! -d "$registry_root" ] || [ ! -x "$runtime_bin" ] || \
        [ ! -f "$manifest" ] || [ ! -f "$source_file" ] || \
        [ "$manifest" != "$expected_manifest" ]; then
    echo "langdef proof verdict driver received an invalid resource" >&2
    exit 2
fi

case "$CETTA_LANGDEF_PROOF_BACKEND" in
    authority|frame-cache-diagnostic-v1|generated-relational-audit-v1)
        ;;
    *)
        echo "langdef proof verdict driver received an unknown backend" >&2
        exit 2
        ;;
esac

set +e
output=$(
    cd "$(dirname "$runtime_bin")"
    CETTA_LANGDEF_ROOT="$registry_root" \
        "$runtime_bin" --lang "$language_name" \
        --langdef-proof-backend "$CETTA_LANGDEF_PROOF_BACKEND" \
        "$source_file" 2>&1
)
status=$?
set -e

case "$status" in
    0)
        exit 0
        ;;
    2)
        exit 1
        ;;
    *)
        printf '%s\n' "$output" >&2
        exit 2
        ;;
esac
