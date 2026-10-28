#!/usr/bin/env python3
"""Write the Cesta project of the runtime executable from the Makefile's own
declaration of it.

The Makefile stays the one declaration of the runtime's sources and flags; this
derives the matching `cesta project/1` document for the active configuration.
`cesta build` compiles and links that document to the same executable as the
Makefile, and `cesta build --export-c` turns it into plain C with a Makefile.

The variables file has one NAME=VALUE line per variable, as the Makefile writes
it: NATIVE_CC, CPPFLAGS, CFLAGS, LDFLAGS, SRC, PARSER_BAKED_OBJ, BRIDGE_DEPS.
Words split as the shell splits the Makefile's recipes.  The baked parser is
compiled by the Makefile with its own definition and enters the link as an
input, at the place the Makefile's link gives it: after every other object.
"""
import argparse
import shlex
from pathlib import Path

TARGET = "cetta"
BAKED_SOURCE = "native/parser_baked_v1.c"
REQUIRED = ("NATIVE_CC", "CPPFLAGS", "CFLAGS", "LDFLAGS", "SRC", "PARSER_BAKED_OBJ", "BRIDGE_DEPS")


def string(word):
    """A Cesta string atom for one argument word."""
    if "\n" in word or "\r" in word:
        raise SystemExit(f"cannot write an argument containing a line break: {word!r}")
    return '"' + word.replace("\\", "\\\\").replace('"', '\\"') + '"'


def form(head, words, indent):
    """(head "w" ...), wrapped at about 100 columns."""
    lines, line = [], f"{indent}({head}"
    for word in map(string, words):
        if len(line) + 1 + len(word) > 100 and line != f"{indent}({head}":
            lines.append(line)
            line = indent + " " * (len(head) + 1) + word
        else:
            line += " " + word
    lines.append(line + ")")
    return "\n".join(lines)


def words(values, name):
    return shlex.split(values[name])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vars", required=True, help="the variables file the Makefile writes")
    parser.add_argument("--out", required=True, help="the project's object directory")
    parser.add_argument("--executable", required=True, help="the path cesta build links")
    parser.add_argument("--output", required=True, help="the project document to write")
    args = parser.parse_args()

    values = {}
    for line in Path(args.vars).read_text().splitlines():
        name, sep, value = line.partition("=")
        if sep:
            values[name] = value
    missing = [name for name in REQUIRED if name not in values]
    if missing:
        raise SystemExit(f"{args.vars}: missing {' '.join(missing)}")

    compiler = words(values, "NATIVE_CC")
    baked = words(values, "PARSER_BAKED_OBJ")
    if len(compiler) != 1 or len(baked) != 1:
        raise SystemExit("expected one native compiler and one baked parser object")
    sources, seen = [], set()
    for source in words(values, "SRC"):
        if source == BAKED_SOURCE:
            continue
        if source in seen:
            raise SystemExit(f"source listed twice: {source}")
        seen.add(source)
        sources.append(source)
    # cesta build keeps a target's objects in OUT/TARGET/obj.
    if Path(args.executable).resolve() == (Path(args.out) / TARGET).resolve():
        raise SystemExit("the executable must not be the target's object directory")

    bridge = words(values, "BRIDGE_DEPS")
    link = (words(values, "CFLAGS") + ["-Wl,--export-dynamic"] + baked + bridge
            + words(values, "LDFLAGS"))
    document = "\n".join([
        ";; The runtime executable, derived from the Makefile for one configuration.",
        ";; The Makefile rewrites this file; edit the Makefile instead.",
        "(cesta project/1",
        f"  (compiler {string(compiler[0])})",
        f"  (out {string(args.out)})",
        "  (profile runtime",
        form("flags", words(values, "CPPFLAGS") + words(values, "CFLAGS"), "    "),
        "    (link))",
        f"  (target {TARGET} (profile runtime) (kind executable)",
        f"    (output {string(args.executable)})",
        form("sources", sources, "    "),
        form("link", link, "    "),
        form("link-inputs", baked + bridge, "    ") + "))",
        "",
    ])
    output = Path(args.output)
    if not output.exists() or output.read_text() != document:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(document)
    print(f"{output}: {len(sources)} sources, {len(link)} link words")


if __name__ == "__main__":
    main()
