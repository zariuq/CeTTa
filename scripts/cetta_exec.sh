#!/usr/bin/env bash
set -euo pipefail

bin="${CETTA_WRAPPED_BIN:-}"

if [[ -z "$bin" && $# -gt 0 ]]; then
    bin="$1"
    shift
fi

if [[ -z "$bin" ]]; then
    echo "usage: scripts/cetta_exec.sh <binary> [args...]" >&2
    exit 2
fi

if [[ "${CETTA_SANITIZER_REPEATABLE:-${CETTA_ASAN_REPEATABLE:-0}}" == "1" ]]; then
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    if command -v setarch >/dev/null 2>&1; then
        exec setarch "$(uname -m)" -R "$bin" "$@"
    fi
fi

exec "$bin" "$@"
