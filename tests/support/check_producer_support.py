#!/usr/bin/env python3
"""Compare native producer support with an independent exported worklist model."""
import argparse
from collections import Counter, defaultdict, deque
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def closure(case):
    readers = defaultdict(list)
    for source, target in case["dependencies"]:
        readers[target].append(source)
    support = set(map(tuple, case["seeds"]))
    pending = deque(support)
    while pending:
        target, item = pending.popleft()
        for source in readers[target]:
            answer = source, item
            if answer not in support:
                support.add(answer)
                pending.append(answer)
    return sorted(support)


def program(case, seed=True):
    forms = ["!(import! &self (library lib_tabling))", "!(tabled (node $key))"]
    # An explicitly empty clause keeps a producer with no seeds or callees
    # distinct from unknown syntax, which MeTTa must retain as data.
    forms.extend(f"(= (node {node}) (empty))" for node in range(case["nodes"]))
    forms.extend(f"(= (node {source}) (node {target}))"
                 for source, target in case["dependencies"])
    if seed:
        forms.extend(f"(= (node {node}) (seed {item}))" for node, item in case["seeds"])
    forms.extend(f"!(let $value (node {node}) (support {node} $value))"
                 for node in range(case["nodes"]))
    return "\n".join(forms) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--stats", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    reference = Path(__file__).with_name("producer_support_reference.json")
    cases = json.loads(reference.read_text())
    out = args.output_directory
    out.mkdir(parents=True, exist_ok=True)
    binary = args.binary.resolve()
    records = []
    for case in cases:
        assert closure(case) == sorted(map(tuple, case["support"])), case["name"]
        expected = Counter(f"(support {node} (seed {item}))"
                           for node, item in case["support"])
        path = out / (case["name"] + ".metta")
        path.write_text(program(case))
        for profile in ("default", "extended"):
            for route in (0, 1):
                def execute(source):
                    command = [str(binary), "--lang", "petta"]
                    if profile != "default":
                        command.extend(("--profile", profile))
                    if args.stats:
                        command.append("--emit-runtime-stats")
                    environment = dict(os.environ,
                        CETTA_OPEN_EQUATIONS_REFERENCE=str(route),
                        CETTA_PETTA_MACHINE_STATS="1" if args.stats else "0")
                    run = subprocess.run(command + [str(source.resolve())], cwd=root,
                        env=environment,
                        capture_output=True, text=True, timeout=30)
                    assert run.returncode == 0, (case["name"], run.stderr[-1000:])
                    lines = run.stdout.splitlines()
                    assert lines[:2] == ["true", "true"], run.stdout
                    return Counter(lines[2:]), run.stderr

                actual, diagnostics = execute(path)
                assert actual == expected, (case["name"], profile, route, actual, expected)
                reused = sum(map(int, re.findall(
                    r"\btable_generator_rounds_reused=(\d+)", diagnostics)))
                if args.stats and "cycle" in case["name"]:
                    assert reused > 0, (case["name"], diagnostics[-2000:])
                if case["seeds"]:
                    mutant = out / (case["name"] + "-without-seed.metta")
                    mutant.write_text(program(case, seed=False))
                    changed, _ = execute(mutant)
                    assert changed != expected, case["name"]
                records.append(dict(case=case["name"], profile=profile, route=route,
                    answers=sum(actual.values()), reused_rounds=reused,
                    source_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    (out / "results.json").write_text(json.dumps(dict(
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        reference_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
        checks=records), indent=2) + "\n")
    print(f"PASS: {len(records)} native producer/model replays preserve cyclic and diamond support;"
          " omitted-seed defects rejected")


if __name__ == "__main__":
    main()
