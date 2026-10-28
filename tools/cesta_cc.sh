#!/bin/sh
# The C compiler driver of this tree: the pinned Cesta tool over the native
# compiler.  Plain C reaches the native compiler unchanged; Cesta units are
# compiled by Cesta.  The Makefile names the tool (CESTA) and checks its
# sha256 against the pin before it builds anything.
tool=${CESTA:-$(dirname "$0")/../runtime/cesta/cesta}
exec "$tool" cc --cc "${CESTA_NATIVE_CC:-gcc}" "$@"
