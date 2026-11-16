#!/usr/bin/env python3
"""Reject a restored language nesting cliff and a broken unbounded allowance."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("cc", "cppflags", "cflags", "ldflags", "baseline"):
        parser.add_argument("--" + option, required=True)
    parser.add_argument("--objects", nargs="+", required=True)
    args = parser.parse_args()
    subprocess.run([str(Path(args.baseline).resolve())], check=True, timeout=20)
    compiler = shlex.split(args.cc)
    flags = shlex.split(args.cppflags) + shlex.split(args.cflags) + ["-Iinclude"]
    flags = [flag for flag in flags if not flag.startswith("-O")] + ["-O0"]
    link = shlex.split(args.ldflags)
    source = Path("src/parser.c").read_text()
    cases = [
        ("restored-nesting-cliff", "#define CETTA_PARSE_DEPTH_LIMIT (-1)",
         "#define CETTA_PARSE_DEPTH_LIMIT 4096", "parser_text_well_formed(source)"),
        ("negative-allowance-is-exhaustion", "read_node:\n    if (depth == 0)",
         "read_node:\n    if (depth <= 0)", "parse_metta_text(source, &arena, &atoms) == 1"),
    ]
    objects = [item for item in args.objects if not Path(item).name.startswith("parser.")]
    assert len(objects) + 1 == len(args.objects), "expected one parser object"
    with tempfile.TemporaryDirectory(prefix="parser-depth-mutants-", dir="runtime") as directory:
        work = Path(directory)
        for name, before, after, discriminator in cases:
            assert source.count(before) == 1, name
            variant, obj, binary = work / (name + ".c"), work / (name + ".o"), work / name
            variant.write_text(source.replace(before, after))
            subprocess.run(compiler + flags + ["-c", str(variant), "-o", str(obj)], check=True)
            subprocess.run(compiler + flags + ["tests/support/test_parser_depth.c", str(obj),
                           *objects, "-o", str(binary), *link], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            if result.returncode == 0 or discriminator not in result.stderr:
                raise RuntimeError(f"{name}: missed intended assertion\n{result.stdout}\n{result.stderr}")
            print(f"PASS: {name} rejected at {discriminator}", flush=True)
    print(f"Parser depth negative controls: {len(cases)} rejected")


if __name__ == "__main__":
    main()
