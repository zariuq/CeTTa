#!/bin/sh

set -eu

"${METAMATH_CURSOR:?METAMATH_CURSOR is required}" \
    "$1" --recognition-verdict >/dev/null
exec "${MM_LEAN4:?MM_LEAN4 is required}" "$1"
