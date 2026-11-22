#!/usr/bin/env python3
"""Reject regressions in the compiled reader container worklist."""
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
    source = Path("src/gslt_direct_reader_v1.c").read_text()
    cases = [
        ("restored-nesting-cliff", "read_node:\n    result = CETTA_ATOM_ID_NONE;",
         "read_node:\n    if (depth == GSLT_DIRECT_PETTA_DEPTH_UNBOUNDED_V1) depth = 4096u;\n    result = CETTA_ATOM_ID_NONE;",
         "gslt_direct_petta_reader_v1_parse_term_bytes_id(plan,"),
        ("reused-previous-child", "read_node:\n    result = CETTA_ATOM_ID_NONE;",
         "read_node:", "result < 0 && id == CETTA_ATOM_ID_NONE"),
        ("extra-list-rest-children", "        if (frame->have_rest) goto failed;",
         "        (void)frame->have_rest;", "result < 0 && id == CETTA_ATOM_ID_NONE"),
    ]
    objects = [item for item in args.objects if not Path(item).name.startswith("gslt_direct_reader_v1.")]
    assert len(objects) + 1 == len(args.objects), "expected one direct reader object"
    with tempfile.TemporaryDirectory(prefix="petta-reader-depth-mutants-", dir="runtime") as directory:
        work = Path(directory)
        for name, before, after, discriminator in cases:
            assert source.count(before) == 1, name
            variant, obj, binary = work / (name + ".c"), work / (name + ".o"), work / name
            variant.write_text(source.replace(before, after))
            subprocess.run(compiler + flags + ["-c", str(variant), "-o", str(obj)], check=True)
            subprocess.run(compiler + flags + ["tests/support/test_petta_reader_depth.c", str(obj),
                           *objects, "-o", str(binary), *link, "-Wl,--wrap=cetta_malloc,--wrap=cetta_realloc"], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            if result.returncode == 0 or discriminator not in result.stderr:
                raise RuntimeError(f"{name}: missed intended assertion\n{result.stdout}\n{result.stderr}")
            print(f"PASS: {name} rejected at {discriminator}", flush=True)
    print(f"Compiled reader negative controls: {len(cases)} rejected")


if __name__ == "__main__":
    main()
