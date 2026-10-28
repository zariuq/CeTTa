#!/usr/bin/env python3
"""Deterministic overloaded calls release their choices before recursion.

The fixture checks the real evaluator's choices and answers, including genuine
same-arity alternatives. All I/O is local polling without pending requests.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def run(binary, path, source, *, reference=False, prepared_reference=False):
    path.write_text(source)
    env = dict(os.environ, CETTA_PETTA_MACHINE_STATS="1")
    env.pop("CETTA_OPEN_EQUATIONS_REFERENCE", None)
    env["CETTA_PREPARED_PURE_REFERENCE"] = "1" if prepared_reference else "0"
    if reference:
        env["CETTA_OPEN_EQUATIONS_REFERENCE"] = "1"
    result = subprocess.run([binary, "--lang", "petta", str(path)],
                            env=env, text=True, capture_output=True, timeout=30)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    lines = [line for line in result.stderr.splitlines()
             if line.startswith("PETTA_MACHINE_STATS ")]
    if not lines:
        return result.stdout.strip().splitlines(), None
    stats = dict(re.findall(r"(\w+)=(\d+)", lines[-1]))
    return result.stdout.strip().splitlines(), int(stats["max_choice_depth"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    cases = {
        "wait-case": ("", "(case (io:wait 0) (($r ok)))"),
        "wait-catch": ("", "(catch (io:wait 0))"),
        "poll-case": ("", "(case (io:poll) (($r ok)))"),
        "poll-catch": ("", "(catch (io:poll))"),
        "typed-overloads": (
            "(: choose (-> Number Number))\n"
            "(: choose (-> Number Number Number))\n"
            "(= (choose $x) $x)\n(= (choose $x $y) $x)\n",
            "(case (choose 1) (($r ok)))"),
        "untyped-overloads": (
            "(= (choose $x) $x)\n(= (choose $x $y) $x)\n",
            "(case (choose 1) (($r ok)))"),
    }
    checks = 0
    with tempfile.TemporaryDirectory(prefix="overloaded-choice-") as directory:
        path = Path(directory) / "fixture.metta"
        for reference in (False, True):
            for name, (definitions, body) in cases.items():
                depths = []
                for count in (32, 256):
                    source = "!(import! &self (library io))\n" + definitions + (
                        "(= (repeat-test $n) (if (== $n 0) done "
                        "(let $w " + body + " (repeat-test (- $n 1)))))\n"
                        "!(repeat-test " + str(count) + ")\n")
                    output, depth = run(binary, path, source, reference=reference)
                    assert output[-1:] == ["done"], (name, output)
                    if depth is None:
                        # A fully prepared pure producer need not enter the
                        # search machine. Check its answers above, then measure
                        # actual machine choices on the ordinary route; absent
                        # instrumentation is never interpreted as depth zero.
                        ordinary, depth = run(binary, path, source,
                            reference=reference, prepared_reference=True)
                        assert ordinary == output, (name, output, ordinary)
                    assert depth is not None, (name, "missing machine statistics")
                    depths.append(depth)
                    checks += 1
                assert depths[1] <= depths[0] + 2, (name, reference, depths)
                assert max(depths) <= 8, (name, reference, depths)
                checks += 2

        # Arity pruning must retain every same-arity answer, in order, including
        # duplicates. It must not execute the body of an impossible arity.
        source = """(= (pick $x) first)
(= (pick $x $y) (println! IMPOSSIBLE-ARITY))
(= (pick $x) first)
(= (pick $x) second)
!(collapse (pick 1))
!(collapse (pick $open))
"""
        for reference in (False, True):
            output, _ = run(binary, path, source, reference=reference)
            assert output == ["(first first second)", "(first first second)"], output
            checks += 1
    print(f"overloaded choice retention: {checks} checks passed")


if __name__ == "__main__":
    main()
