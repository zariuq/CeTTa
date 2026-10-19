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
if [ "$(uname -s)" = Darwin ]; then
    # Mach-O needs an explicit undefined-symbol allowance even for optional
    # weak imports. Keep it restricted to providers this probe omits; every
    # other missing definition remains a link error.
    for symbol in space_module_display_name term_graph_ref term_graph_retain \
        term_graph_release term_graph_value_eq term_graph_view_hash \
        term_graph_print term_graph_open_value; do
        set -- "$@" "-Wl,-U,_$symbol"
    done
fi
"${CC:-cc}" -O2 -Wall -Werror -std=c11 -pthread -Isrc \
    -DCETTA_BUILD_WITH_GMP=1 -DCETTA_MANAGED_SYMBOLS=1 \
    -DCETTA_TEST_HOOKS=1 -DCETTA_RUNTIME_STATS_NOOP=1 \
    tests/test_atom_constructor_fields.c tests/support/atom_ownership_profile.c \
    src/symbol.c src/scalar_trie.c \
    src/atom.c src/binding/frame_identity.c src/name_key.c src/atom_blob.c \
    src/term_canon.c src/tensor.c "$@" -lm -o "$probe"
"$probe"
