#!/bin/sh
# The Cesta tool of this tree, built from Cesta source.
#
#   cesta_tool.sh id SOURCE              print the identity of a Cesta source tree
#   cesta_tool.sh build SOURCE OUTPUT    build the tool from SOURCE as OUTPUT
#
# Source identity excludes build products, tests, examples, Markdown and
# provenance metadata. Tool identity also names the compiler and build recipe.
# build runs in a private copy whose path is mapped out of the debug
# information: the same source and compiler give the same executable wherever
# they are built.  Concurrent builds of one output take turns, and the output
# appears only when it is complete.
set -eu

files() {
    python3 -c 'import sys; from pathlib import Path; sys.path.insert(0,sys.argv[1]); from cesta_source import files; root=Path(sys.argv[2]); sys.stdout.buffer.write(b"".join(str(p.relative_to(root)).encode()+b"\0" for p in files(root)))' "$(dirname "$0")" "$1"
}

case ${1:-} in
id)
    src=$2
    test -f "$src/Makefile" || { echo "$src: not a Cesta source tree" >&2; exit 1; }
    python3 "$(dirname "$0")/cesta_source.py" id "$src"
    ;;
tool-id)
    python3 "$(dirname "$0")/cesta_source.py" tool-id "$2" "${3:-${CESTA_NATIVE_CC:-gcc}}"
    ;;
build)
    src=$2 out=$3 cc=${CESTA_NATIVE_CC:-gcc}
    test -f "$src/Makefile" || { echo "$src: not a Cesta source tree" >&2; exit 1; }
    key=$(python3 "$(dirname "$0")/cesta_source.py" tool-id "$src" "$cc")
    current() {
        test -x "$out" && test -f "$out.identity" && \
            test "$(head -n 1 "$out.identity")" = "$key" && \
            test "$(tail -n 1 "$out.identity")" = "$(sha256sum "$out" | cut -d ' ' -f1)"
    }
    current && exit 0
    dir=$(dirname "$out")
    mkdir -p "$dir"
    if command -v flock >/dev/null 2>&1; then
        exec 9>"$out.lock"
        flock 9
        current && exit 0
    fi
    work=$(mktemp -d "$dir/build.XXXXXX")
    trap 'rm -rf "$work"' EXIT
    work=$(cd "$work" && pwd -P)
    echo "building the Cesta tool from $src" >&2
    files "$src" | (cd "$src" && tar -cf - --null -T -) | (cd "$work" && tar -xf -)
    if ! env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL -u MAKEOVERRIDES \
        make -C "$work" -s -j"${CESTA_TOOL_JOBS:-2}" build/cesta CC="$cc" CPPFLAGS= \
            CFLAGS="-O2 -g -ffile-prefix-map=$work=cesta" >"$work/build.log" 2>&1; then
        tail -n 40 "$work/build.log" >&2
        echo "building the Cesta tool from $src failed" >&2
        exit 1
    fi
    mv "$work/build/cesta" "$out.partial"
    mv -f "$out.partial" "$out"
    { printf '%s\n' "$key"; sha256sum "$out" | cut -d ' ' -f1; } > "$out.identity.partial"
    mv -f "$out.identity.partial" "$out.identity"
    ;;
*)
    echo "usage: cesta_tool.sh id SOURCE | build SOURCE OUTPUT" >&2
    exit 2
    ;;
esac
