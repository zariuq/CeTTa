#!/bin/sh
# The C compiler driver of this tree: the Cesta tool over the native
# compiler.  Plain C reaches the native compiler unchanged; Cesta units are
# compiled by Cesta.  The Makefile names the tool (CESTA) and the Cesta source
# it is built from (CESTA_SRC_DIR); the first compile that needs the tool
# builds it.
set -eu
native=0
if [ "${1:-}" = --native ]; then native=1; shift; fi
if [ "${CETTA_LTO:-0}" = 1 ]; then
    link=1 wrap=0 response=0
    for arg do
        case "$arg" in
            -c|-S|-E|-fsyntax-only|-M|-MM) link=0 ;;
            -Wl,*--wrap*|--wrap|--wrap=*) wrap=1 ;;
            @*) response=1 ;;
        esac
    done
    if [ "$response" = 1 ]; then
        wrap=0
        if python3 "$(dirname "$0")/cetta_wrap_link.py" "$@"; then wrap=1; fi
    fi
    # Failure-injection links consume the native half of fat LTO objects.
    if [ "$link" = 1 ] && [ "$wrap" = 1 ]; then set -- "$@" -fno-lto; fi
fi
if [ "$native" = 1 ]; then exec "${CESTA_NATIVE_CC:-gcc}" "$@"; fi
if [ "${CESTA_READ_WARNINGS:-0}" = 1 ]; then set -- --read-warnings "$@"; fi
: "${CESTA:?CESTA names the Cesta tool; build through the Makefile}"
if [ ! -x "$CESTA" ]; then
    "$(dirname "$0")/cesta_tool.sh" build "${CESTA_SRC_DIR:?}" "$CESTA" || exit 1
fi
exec "$CESTA" cc --cc "${CESTA_NATIVE_CC:-gcc}" "$@"
