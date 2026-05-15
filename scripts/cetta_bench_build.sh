#!/usr/bin/env bash

set -euo pipefail

cetta_canonical_build_mode() {
    case "$1" in
        full)
            printf 'main\n'
            ;;
        pathmap)
            printf 'mork\n'
            ;;
        *)
            printf '%s\n' "$1"
            ;;
    esac
}

cetta_build_mode() {
    local bin="$1"
    "$bin" -v 2>/dev/null | sed -n 's/.*(\([^)]*\)).*/\1/p'
}

cetta_mode_satisfies() {
    local mode="$1"
    local required="$2"
    case "$required" in
        core)
            [[ "$mode" == "core" || "$mode" == "python" || "$mode" == "mork" || "$mode" == "main" || "$mode" == "pathmap" || "$mode" == "full" ]]
            ;;
        python)
            [[ "$mode" == "python" || "$mode" == "main" || "$mode" == "full" ]]
            ;;
        mork)
            [[ "$mode" == "mork" || "$mode" == "main" || "$mode" == "pathmap" || "$mode" == "full" ]]
            ;;
        main)
            [[ "$mode" == "main" || "$mode" == "full" ]]
            ;;
        pathmap)
            [[ "$mode" == "mork" || "$mode" == "main" || "$mode" == "pathmap" || "$mode" == "full" ]]
            ;;
        full)
            [[ "$mode" == "main" || "$mode" == "full" ]]
            ;;
        *)
            return 1
            ;;
    esac
}

cetta_ensure_build_mode() {
    local root="$1"
    local bin="$2"
    local required="$3"
    local reason="${4:-requested benchmark surface}"
    local required_canon=""
    local mode=""
    required_canon="$(cetta_canonical_build_mode "$required")"

    if [[ -x "$bin" ]]; then
        mode="$(cetta_build_mode "$bin" || true)"
        if [[ -n "$mode" ]] && cetta_mode_satisfies "$mode" "$required"; then
            return 0
        fi
    fi

    if [[ "$bin" != "$root/cetta" ]]; then
        printf '%s\n' "error: $reason requires BUILD=$required_canon (or compatible), but custom CETTA_BIN '$bin' is not compatible" >&2
        printf '%s\n' "hint: rebuild that binary with BUILD=$required_canon or unset CETTA_BIN" >&2
        exit 1
    fi

    printf '%s\n' "building CeTTa with BUILD=$required_canon for $reason" >&2
    (
        make -C "$root" -j1 BUILD="$required_canon" cetta
    )
}
