#!/bin/sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build=$(mktemp -d)
trap 'rm -f -- "$build/bench"; rmdir -- "$build"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -O3 -Wall -Wextra -Werror -pedantic "$here/bench.c" -o "$build/bench"
"$build/bench" "${1:-3}"
