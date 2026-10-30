#!/usr/bin/env python3
"""Write the Cesta project of the runtime executable from the Makefile's own
declaration of it.

The Makefile stays the one declaration of the runtime's sources and flags; this
derives the matching `cesta project/1` document for the active configuration.
`cesta build` compiles and links that document to the same executable as the
Makefile, and `cesta build --export-c` turns it into plain C with a Makefile.

The variables file has one NAME=VALUE line per variable, as the Makefile writes
it: NATIVE_CC, CPPFLAGS, CFLAGS, LDFLAGS, SRC, SECP256K1_SRC,
SECP256K1_CFLAGS, PARSER_BAKED_OBJ, BRIDGE_DEPS.
Words split as the shell splits the Makefile's recipes.  The baked parser is
compiled by the Makefile with its own definition and enters the link as an
input, at the place the Makefile's link gives it: after every other object.
"""
import argparse
import re
import shlex
from pathlib import Path

TARGET = "cetta"
BAKED_SOURCE = "native/parser_baked_v1.c"
REQUIRED = ("NATIVE_CC", "CPPFLAGS", "CFLAGS", "LDFLAGS", "SRC", "SECP256K1_SRC",
            "SECP256K1_CFLAGS", "PARSER_BAKED_OBJ", "BRIDGE_DEPS")


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

    if not sources:
        raise SystemExit("the runtime needs at least one source")
    special = set(words(values, "SECP256K1_SRC"))
    if special - set(sources):
        raise SystemExit("the crypto source declaration names a source outside SRC")

    source_objects = shlex.split(values.get('SOURCE_OBJECTS', ''))
    if source_objects and len(source_objects) != len(sources):
        raise SystemExit('SOURCE_OBJECTS must match the ordered non-baked runtime sources')
    object_for = dict(zip(sources, source_objects))
    compile_inputs = []
    def flags_for(source):
        flags = words(values, 'SECP256K1_CFLAGS' if source in special else 'CFLAGS')
        obj = object_for.get(source)
        if obj and values.get('CETTA_PGO'):
            stem = str(Path(obj).with_suffix(''))
            flags += ['-dumpbase', stem if '/' in stem else './' + stem]
            counts = Path(obj).with_suffix('.gcda')
            if values['CETTA_PGO'] == 'use' and counts.exists():
                compile_inputs.append(str(counts))
        if obj and values.get('CETTA_LTO') == '1':
            flags += ['-frandom-seed=' + obj]
        return flags
    source_rows = [f'    (source {string(source)} ' + form('flags', flags_for(source), '').strip() + ')'
                   for source in sources]

    # Per-source flags preserve object order directly, including units used
    # solely through an exported native symbol.
    targets, dependencies = [], []
    config = values.get('BUILD_CONFIG_HEADER', '')
    if config:
        definitions = []
        for line in Path(config).read_text().splitlines():
            match = re.fullmatch(r'#define ([A-Za-z_][A-Za-z_0-9]*) (.*)', line)
            if match:
                definitions += ['--define', f'{match[1]}={match[2]}']
        command = ['python3', 'tools/cesta_build_config.py', '--output', config,
                   '--version-file', 'VERSION', *definitions]
        targets.append('\n'.join([
            '  (target runtime_config (kind generator)',
            form('inputs', ['tools/cesta_build_config.py', 'VERSION'], '    '),
            form('outputs', [config], '    '), form('command', command, '    ') + ')']))
        dependencies.append('runtime_config')
    reader_header = values.get('OP_LANG_PREPARED_HEADER_V1', '')
    if reader_header:
        emitter = str(Path(args.out) / 'reader_emitter/reader_emitter')
        reader_sources = ['tools/emit_operational_reader_v1.c', 'src/lib_parse_native_grammar.c',
                          'src/gslt_dense_bitset_v1.c', 'native/cetta_standalone_alloc_v1.c', 'src/native_sha256.c']
        targets += ['\n'.join([
            '  (target reader_emitter', form('sources', reader_sources, '    '),
            form('flags', ['-Isrc', '-I.', '-std=c11', '-O2', '-Wall', '-Werror',
                           '-ffunction-sections', '-fdata-sections'], '    '),
            '    (link "-Wl,--gc-sections"))']), '\n'.join([
            '  (target operational_reader (kind generator)',
            form('inputs', [emitter], '    '), form('outputs', [reader_header], '    '),
            form('command', [emitter, reader_header], '    ') + ')'])]
        dependencies.append('operational_reader')
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
        form("flags", words(values, "CPPFLAGS"), "    "),
        "    (link))",
        *targets,
        f"  (target {TARGET} (profile runtime) (kind executable)",
        f"    (output {string(args.executable)})",
        *source_rows,
        form("depends", dependencies, "    "),
        form("compile-inputs", compile_inputs, "    "),
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
