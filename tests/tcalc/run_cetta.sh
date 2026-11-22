#!/bin/sh
# run_cetta.sh BIN [PROFILE] FILE: the cetta binary on a tcalc fixture.
bin=$1; shift
if [ $# -eq 2 ]; then exec "$bin" --lang tcalc --profile "$1" "$2"; fi
exec "$bin" --lang tcalc "$1"
