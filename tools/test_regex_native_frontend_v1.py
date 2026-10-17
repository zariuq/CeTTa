#!/usr/bin/env python3
"""Check exact general-parser indexing and admitted LR observations."""

import argparse
import json
from pathlib import Path
import subprocess

from test_regex_selection_v1 import portable_patterns, source

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--reference-probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--generated", type=int, default=64)
    parser.add_argument("--seed", type=int, default=20261016)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    patterns = ["", "a", "λ", "a|ab|a", "(a?)*", "[a--b]", "[a&&b]", "[[]",
                "(a)(b)\\12", "(a)(b)\\12\\12\\12\\12", "(?<x>a)\\k<x>", "(?=a)a", "(?<=([ab]+)([bc]+))$",
                "a{2,3}?", "a{١۲,٣}", "(?x:a # note\n b)", "(", "[", "a{", "a\\"]
    patterns += [source(pattern)[0] for pattern in portable_patterns(args.seed, args.generated)]
    checks = 0
    admitted = 0
    traced = 0
    fields = ("accepted", "values", "descriptors", "gss", "nodes", "choices", "forest_sha256", "value_sha256")
    for index, pattern in enumerate(patterns):
        path = args.output / f"pattern-{index:04d}.txt"
        path.write_text(pattern, encoding="utf-8")
        records = {}
        for mode in ("gll", "glr"):
            suffix = [] if mode == "gll" else ["glr"]
            for name, binary in (("reference", args.reference_probe), ("current", args.probe)):
                completed = subprocess.run([str(binary.resolve()), str(path), *suffix], cwd=ROOT,
                    capture_output=True, text=True, timeout=60, check=True)
                records[mode, name] = json.loads(completed.stdout)
            for field in fields:
                assert records[mode, "reference"][field] == records[mode, "current"][field], (
                    index, pattern, mode, field, records)
                checks += 1
        completed = subprocess.run([str(args.probe.resolve()), str(path), "lr"], cwd=ROOT,
            capture_output=True, text=True, timeout=60, check=True)
        corridor = json.loads(completed.stdout)
        if corridor.get("corridor") != "needs-general":
            for field in ("accepted", "values", "value_sha256"):
                assert corridor[field] == records["gll", "current"][field], (index, pattern, field, corridor, records)
                checks += 1
            admitted += 1
        # The value-only trace route is admitted exactly where the forest
        # corridor is, and replays the same values.
        completed = subprocess.run([str(args.probe.resolve()), str(path), "trace"], cwd=ROOT,
            capture_output=True, text=True, timeout=60, check=True)
        trace = json.loads(completed.stdout)
        assert (trace.get("corridor") == "needs-general") == (corridor.get("corridor") == "needs-general"), (
            index, pattern, trace, corridor)
        checks += 1
        if trace.get("corridor") != "needs-general":
            for field in ("accepted", "values", "value_sha256"):
                assert trace[field] == records["gll", "current"][field], (index, pattern, field, trace, records)
                checks += 1
            traced += 1
    print(f"(RegexNativeFrontendV1Summary {checks} 0 {len(patterns)} {admitted} {traced})")


if __name__ == "__main__":
    main()
