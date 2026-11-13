#!/bin/sh
# Keep compilation and linking on the same pinned frontend and compiler.
set -eu
: "${CESTA:?the build must select a Cesta tool}"
: "${CESTA_NATIVE_CC:?the build must select a native compiler}"
test -x "$CESTA" || { echo "Cesta tool is not built: $CESTA" >&2; exit 1; }
exec "$CESTA" cc --cc "$CESTA_NATIVE_CC" "$@"
