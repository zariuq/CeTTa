#!/usr/bin/env python3
"""Check that structural traversal controls reject lost events and false completion."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cc", "cppflags", "cflags", "ldflags", "baseline"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--objects", nargs="+", required=True)
    args = parser.parse_args()
    subprocess.run([str(Path(args.baseline).resolve())], check=True, timeout=20)
    cc = shlex.split(args.cc)
    flags = shlex.split(args.cppflags) + shlex.split(args.cflags) + ["-Iinclude"]
    flags = [flag for flag in flags if not flag.startswith("-O")] + ["-O0"]
    link = shlex.split(args.ldflags) + ["-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc"]
    implementation = Path("src/atom_walk.inc").read_text()
    atom = Path("src/atom.c").read_text()
    include = '#include "atom_walk.inc"'
    assert atom.count(include) == 1
    cases = [
        ("lost-duplicates", "walk->unique = unique;", "walk->unique = true; (void)unique;", "counts.enters"),
        ("reversed-children", "CettaExprIndex child = frame->next;",
         "CettaExprIndex child = node->expr.len - 1u - frame->next;", "item.node->ground.ival == expected_leaf"),
        ("frame-capacity-is-end", "if (!atom_walk_reserve(walk)) return ATOM_WALK_RESOURCE;",
         "if (!atom_walk_reserve(walk)) return ATOM_WALK_DONE;", "trace_until_stop(walk, &actual)"),
        ("set-capacity-is-end", "(void)atom_visit_set_add(&walk->active, node, &fault);\n            if (fault) return ATOM_WALK_RESOURCE;",
         "(void)atom_visit_set_add(&walk->active, node, &fault);\n            if (fault) return ATOM_WALK_DONE;", "step == ATOM_WALK_ENTER"),
        ("stale-active-path", "atom_walk_erase(&walk->active, node);",
         "if (!walk->len) atom_walk_erase(&walk->active, node);", "step == ATOM_WALK_ENTER"),
    ]
    objects = [item for item in args.objects if not Path(item).name.startswith("atom.")]
    assert len(objects) + 1 == len(args.objects), "expected one actual atom object"
    with tempfile.TemporaryDirectory(prefix="atom-walk-mutants-", dir="runtime") as directory:
        work = Path(directory)
        for name, before, after, discriminator in cases:
            assert implementation.count(before) == 1, name
            changed = atom.replace(include, implementation.replace(before, after))
            source, obj, binary = work / (name + ".c"), work / (name + ".o"), work / name
            source.write_text(changed)
            subprocess.run(cc + flags + ["-c", str(source), "-o", str(obj)], check=True)
            subprocess.run(cc + flags + ["tests/support/test_atom_walk.c", str(obj),
                           *objects, "-o", str(binary), *link], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            if result.returncode == 0 or discriminator not in result.stderr:
                raise RuntimeError(f"{name}: missed intended assertion\n{result.stdout}\n{result.stderr}")
            print(f"PASS: {name} rejected at {discriminator}", flush=True)
    print(f"Structural traversal negative controls: {len(cases)} rejected")


if __name__ == "__main__":
    main()
