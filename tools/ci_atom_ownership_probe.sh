#!/bin/sh
# Compile the ownership core independently of the interpreter's POSIX bootstrap.
set -eu
cd "$(dirname "$0")/.."
case $(uname -s) in
    MINGW*|MSYS*|CYGWIN*) suffix=.exe ;;
    *) suffix= ;;
esac
mkdir -p runtime/portability
probe=runtime/portability/atom-ownership$suffix
set -- $(pkg-config --cflags --libs gmp)
"${CC:-cc}" -O2 -Wall -Werror -std=c11 -pthread -Isrc \
    -DCETTA_BUILD_WITH_GMP=1 -DCETTA_MANAGED_SYMBOLS=1 \
    -DCETTA_TEST_HOOKS=1 -DCETTA_RUNTIME_STATS_NOOP=1 \
    tests/test_atom_constructor_fields.c src/symbol.c src/scalar_trie.c \
    src/atom.c src/binding/frame_identity.c src/name_key.c src/atom_blob.c \
    src/term_canon.c "$@" -lm -o "$probe"
"$probe"
