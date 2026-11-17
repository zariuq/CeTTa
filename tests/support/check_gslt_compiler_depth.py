#!/usr/bin/env python3
"""Check structural compiler paths through the production PeTTa reader and runtime."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from native_observation import read_observation

ROOT = Path(__file__).resolve().parents[2]


def symbol(value):
    return ["symbol", value.encode().hex()]


def nested(depth, leaf):
    return "(Box " * depth + leaf + ")" * depth


def nested_value(depth, leaf):
    return [["expression", 2], symbol("Box")] * depth + leaf


def source(rules, *, width=0):
    wide = f" (operator Wide {width})" if width else ""
    return ("(gslt-presentation-v1 StructuralCompilerControls "
            "(signature (operator R 2) (operator Box 1) (operator Pair 2) (operator Leaf 0)" + wide + ") "
            "(equations) (rewrites " + " ".join(rules) + "))\n")


def rule(name, left, right):
    return f"(rule {name} (head (R {left} {right})) (body))"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--depth", type=int, default=4200)
    parser.add_argument("--factor-depth", type=int, default=96)
    parser.add_argument("--width", type=int, default=257)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--timings", action="store_true")
    args = parser.parse_args()
    if min(args.depth, args.factor_depth) < 0 or args.width < 1 or args.timeout <= 0:
        parser.error("depths must be nonnegative, and width and timeout positive")
    compiler, runtime = args.compiler.resolve(), args.runtime.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    results = []

    def check(name, program_source, query, expected, *, mode=None, factored=False):
        text, program, queries, receipt = [work / (name + suffix)
                                          for suffix in (".source.metta", ".metta", ".query.metta", ".jsonl")]
        text.write_text(program_source)
        queries.write_text(query + "\n")
        command = [str(compiler), "petta-direct", "--source", str(text)]
        if mode:
            command += ["--entry-mode", mode]
        start = time.monotonic()
        compiled = subprocess.run(command + ["--out", str(program)], cwd=ROOT,
                                  capture_output=True, timeout=args.timeout)
        (work / (name + ".compiler.err")).write_bytes(compiled.stderr)
        assert compiled.returncode == 0, (name, "compiler failed", compiled.stderr[-1500:])
        if factored:
            assert "gslt:fnix:R:10" in program.read_text(), (name, "factoring was not exercised")
        emitted = time.monotonic()
        command = [str(runtime), "--run-observation", str(receipt)]
        directory = os.environ.get("CETTA_GSLT_RECORD_DIR")
        if directory:
            command = [sys.executable, str(ROOT / "tests/support/gslt_invocation.py"),
                       "record", directory, "3", *command]
        ran = subprocess.run(command + ["--lang", "petta", str(program), str(queries)], cwd=ROOT,
                             capture_output=True, timeout=args.timeout)
        (work / (name + ".out")).write_bytes(ran.stdout)
        (work / (name + ".err")).write_bytes(ran.stderr)
        assert ran.returncode == 0, (name, "runtime failed", ran.stderr[-1500:])
        observation = read_observation(receipt)
        assert len(observation["queries"]) == 1, name
        result, = observation["queries"].values()
        assert result["terminal"]["stop"] == 0 and observation["final"]["contract_exit"] == 0, name
        assert not result["effects"] and not observation["outside_effects"], name
        actual = result["answers"]
        assert actual == expected, (name, "typed answers or occurrence order differ", len(actual), len(expected))
        row = dict(case=name, answer_occurrences=len(actual), program_bytes=program.stat().st_size,
                   compile_seconds=emitted - start, run_seconds=time.monotonic() - emitted)
        results.append(row)
        print(f"PASS: {name}: {len(actual)} exact typed answer occurrences", flush=True)
        if args.timings:
            print(json.dumps(row), flush=True)

    fact = nested(args.depth, "(Leaf)")
    check("deep-fact", source([rule("fact", "7", fact)]), "!(gslt:R $goal)",
          [[["expression", 3], symbol("R"), ["integer", "7"]]
           + nested_value(args.depth, [["expression", 1], symbol("Leaf")])])
    check("deep-input-mode", source([rule("unbox", nested(args.depth, "?x"), "?x")]),
          f"!(gslt:entry:R:10 {nested(args.depth, '7')})",
          [[["expression", 2], symbol("gslt:result:R:10"), ["integer", "7"]]], mode="R:10")
    values = [i % 7 for i in range(64)]
    facts = [rule(f"fact{i}", "17", nested(args.factor_depth, str(value)))
             for i, value in enumerate(values)]
    check("factored-duplicate-occurrences", source(facts), "!(gslt:entry:R:10 17)",
          [[["expression", 2], symbol("gslt:result:R:10")]
           + nested_value(args.factor_depth, [["integer", str(value)]]) for value in values],
          mode="R:10", factored=True)
    facts = [rule(f"fact{i}", f"(Pair {i % 4} {i % 3})",
                  nested(args.factor_depth, f"(Pair {i % 7} {i % 5})")) for i in range(64)]
    check("factored-input-and-output-slots", source(facts), "!(gslt:entry:R:10 (Pair 2 1))",
          [[["expression", 2], symbol("gslt:result:R:10")]
           + nested_value(args.factor_depth,
                          [["expression", 3], symbol("Pair"), ["integer", str(i % 7)], ["integer", str(i % 5)]])
           for i in range(64) if i % 4 == 2 and i % 3 == 1], mode="R:10", factored=True)
    # Exceed the current factoring slot allowance. Optimization eligibility
    # must not change the relation's ordered answer occurrences.
    width = 17
    facts = [rule(f"fact{i}", "17", "(Wide " + " ".join(str(i + j) for j in range(width)) + ")")
             for i in range(64)]
    check("many-varying-fields", source(facts, width=width), "!(gslt:entry:R:10 17)",
          [[["expression", 2], symbol("gslt:result:R:10"), ["expression", width + 1], symbol("Wide")]
           + [["integer", str(i + j)] for j in range(width)] for i in range(64)], mode="R:10")
    arguments = " ".join(str(i) for i in range(args.width))
    wide = "(Wide " + arguments + ")"
    check("wide-constructor", source([rule("echo", "?x", "?x")], width=args.width),
          f"!(gslt:entry:R:10 {wide})",
          [[["expression", 2], symbol("gslt:result:R:10"), ["expression", args.width + 1], symbol("Wide")]
           + [["integer", str(i)] for i in range(args.width)]], mode="R:10")
    (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print("PASS: compiled nesting, mode inputs, factoring multiplicity and constructor width")


if __name__ == "__main__":
    main()
