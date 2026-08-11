#!/bin/sh

set -eu

: "${CETTA_LANGDEF_ROOT:?CETTA_LANGDEF_ROOT is required}"
: "${CETTA_LANGDEF_BIN:?CETTA_LANGDEF_BIN is required}"
: "${CETTA_LANGDEF_MANIFEST:?CETTA_LANGDEF_MANIFEST is required}"

if [ "$#" -ne 1 ]; then
    echo "usage: $0 SOURCE" >&2
    exit 2
fi

runtime_root=$(realpath "$CETTA_LANGDEF_ROOT") || exit 2
runtime_bin=$(realpath "$CETTA_LANGDEF_BIN") || exit 2
manifest=$(realpath "$CETTA_LANGDEF_MANIFEST") || exit 2
source_file=$(realpath "$1") || exit 2

if [ ! -d "$runtime_root" ] || [ ! -x "$runtime_bin" ] || \
        [ ! -f "$manifest" ] || [ ! -f "$source_file" ]; then
    echo "langdef verdict driver received an invalid resource" >&2
    exit 2
fi

case "$manifest$source_file" in
    *\"*|*\\*)
        echo "langdef verdict driver cannot quote this path" >&2
        exit 2
        ;;
esac

if ! output=$(
    cd "$runtime_root"
    "$runtime_bin" --lang prime \
        -e '!(import! &self langdef)' \
        -e "!(bind! &verdict-langdef (langdef:load \"$manifest\"))" \
        -e "!(println! (langdef:import-file &verdict-langdef \"$source_file\"))"
); then
    echo "langdef verdict runtime failed" >&2
    exit 2
fi

case "$output" in
    *"(LangDef:Imported "*|*"(LangDef:StageAcceptedIncomplete "*)
        exit 0
        ;;
    *"(LangDef:ParseRejected "*|*"(LangDef:StageRejected "*)
        exit 1
        ;;
    *"(LangDef:ParseIncomplete "*|*"(LangDef:StageIncomplete "*|\
    *"(LangDef:StageUnsupported "*|*"(Error "*)
        printf '%s\n' "$output" >&2
        exit 2
        ;;
    *)
        printf '%s\n' "$output" >&2
        exit 2
        ;;
esac
