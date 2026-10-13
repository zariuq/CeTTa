#!/usr/bin/env python3
"""Require semantic assertions to detect three mutations of actual atom.c."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[3]
MUTANTS = {
    "copy-id": ('''return atom_symbol_data_span_hashed(
            owner, source->spelling->bytes, source->spelling->len,
            source->spelling->hash);''', 'return atom_symbol_id(owner, source->symbol_number);'),
    "eternal-miss": ('''return symbol_lookup_span_hashed(g_symbols,
        (const uint8_t *)atom->spelling->bytes, atom->spelling->len,
        atom->spelling->hash);''', 'return SYMBOL_ID_NONE;'),
    "mixed-hash": ('uint64_t sh = atom_symbol_spelling_hash(a);',
                   'uint64_t sh = atom_symbol_is_spelled(a) ? 0u : atom_symbol_spelling_hash(a);'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cc", "cppflags", "cflags", "ldflags"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--output", type=Path, default=Path("runtime/gc-experiments/engine-mutants"))
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    original = (ROOT / "src/atom.c").read_text()
    rows = []
    for name in ("positive", *MUTANTS):
        source = out / (name + ".c")
        text = original
        if name != "positive":
            old, new = MUTANTS[name]
            if text.count(old) != 1:
                raise AssertionError("mutation site changed: " + name)
            text = text.replace(old, new)
        source.write_text(text)
        binary = out / name
        command = (shlex.split(args.cc) + shlex.split(args.cppflags) + shlex.split(args.cflags)
                   + ["-o", str(binary), "tests/test_managed_symbol_values.c", "src/symbol.c",
                      str(source), "src/binding/frame_identity.c"] + shlex.split(args.ldflags))
        build = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
        (out / (name + ".build.log")).write_text(build.stdout + build.stderr)
        if build.returncode:
            raise RuntimeError("compiler failure does not count as detection: " + name)
        run = subprocess.run([str(binary)], cwd=ROOT, env=os.environ.copy(),
                             capture_output=True, text=True, timeout=30)
        (out / (name + ".run.log")).write_text(run.stdout + run.stderr)
        if name == "positive":
            passed = run.returncode == 0 and "passed" in run.stdout
        else:
            passed = (run.returncode != 0 and "Assertion" in run.stderr
                      and "test_managed_symbol_values.c" in run.stderr)
        rows.append({"case": name, "passed": passed, "exit": run.returncode})
        if not passed:
            raise AssertionError(f"{name}: positive control or semantic mutant check failed")
        print("PASS:", name, flush=True)
    (out / "results.json").write_text(json.dumps(rows, indent=2) + "\n")


if __name__ == "__main__":
    main()
