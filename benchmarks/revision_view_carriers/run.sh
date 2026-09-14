#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
binary=$(mktemp "${TMPDIR:-/tmp}/cetta-revision-view-tournament.XXXXXX")
trap 'rm -f -- "$binary"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -O3 -Wall -Wextra -Werror -pedantic \
    ${CETTA_REVISION_VIEW_CFLAGS:-} \
    "$script_dir/bench_realizations.c" -o "$binary"
exec "$binary" "${1:-5}"
