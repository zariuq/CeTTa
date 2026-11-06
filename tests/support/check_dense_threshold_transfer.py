#!/usr/bin/env python3
"""A read gives the same answer whether a head's facts are rows or a relation.

Relations take a head's facts from SPACE_DENSE_THRESHOLD facts on (or from
the first under CETTA_SPACE_DENSE=eager).  Each program below asks one
native reader shape one question, alone in its file, at one fact below the
threshold and at the threshold, under CETTA_SPACE_DENSE=off, the default
and eager; every run must answer the value computed here.
"""
import os
import subprocess
import sys
import tempfile

THRESHOLD = 4096


def programs(n):
    """(name, facts, query, expected) for n facts."""
    vals = [f"(val {i})" for i in range(1, n + 1)]
    pairs = [f"(pair {i} {2 * i})" for i in range(1, n + 1)]
    edges = [f"(edge {i} {i + 1})" for i in range(n)]
    weights = [f"(weight {i + 1} {i})" for i in range(n)]
    nested = [f"(rel (n {i}) (n {i + 1}))" for i in range(n)]
    total = n * (n + 1) // 2
    # Joins sharing two variables are no chain: they take the moments route.
    lefts = [f"(left {i} {2 * i})" for i in range(1, n + 1)]
    rights = [f"(right {i} {2 * i} {i})" for i in range(1, n + 1)]
    # A product over a join: every value 1 but the last, 2, so a missed
    # relation (product = initial 1) differs from the answer.
    factors = [f"(factor {i + 1} {2 if i == n - 1 else 1})" for i in range(n)]
    return [
        ("sum-unary", vals, "!(foldall + (match &self (val $x) $x) 0)", str(total)),
        ("max-unary", vals, "!(foldall max (match &self (val $x) $x) 0)", str(n)),
        ("count-unary", vals, "!(foldall + (match &self (val $x) 1) 0)", str(n)),
        ("sum-binary-column", pairs, "!(foldall + (match &self (pair $a $b) $b) 0)",
         str(2 * total)),
        ("sum-join", edges + weights,
         "!(foldall + (match &self (, (edge $a $b) (weight $b $w)) $w) 0)",
         str(sum(range(n)))),
        ("count-join", edges + weights,
         "!(foldall + (match &self (, (edge $a $b) (weight $b $w)) 1) 0)", str(n)),
        ("count-two-hop-nested", nested,
         "!(foldall + (match &self (, (rel $a $b) (rel $b $c)) 1) 0)", str(n - 1)),
        ("collect-two-hop-nested", nested,
         "!(size-atom (collapse (match &self (, (rel $a $b) (rel $b $c)) $c)))", str(n - 1)),
        ("sum-join-two-shared", lefts + rights,
         "!(foldall + (match &self (, (left $a $b) (right $a $b $w)) $w) 0)", str(total)),
        ("product-join", edges + factors,
         "!(foldall * (match &self (, (edge $a $b) (factor $b $w)) $w) 1)", "2"),
        ("collect-unary", vals,
         "!(size-atom (collapse (match &self (val $x) $x)))", str(n)),
    ]


def main():
    binary = sys.argv[1]
    failures = 0
    runs = 0
    with tempfile.TemporaryDirectory(prefix="dense-transfer-") as work:
        for n in (THRESHOLD - 1, THRESHOLD):
            for name, facts, query, expected in programs(n):
                path = os.path.join(work, f"{name}-{n}.metta")
                with open(path, "w") as out:
                    out.write("\n".join(facts) + "\n" + query + "\n")
                for mode in ("off", "", "eager"):
                    env = dict(os.environ)
                    env.pop("CETTA_SPACE_DENSE", None)
                    if mode:
                        env["CETTA_SPACE_DENSE"] = mode
                    got = subprocess.run([binary, "--lang", "petta", path], env=env,
                                         capture_output=True, text=True, timeout=600)
                    answer = got.stdout.strip()
                    runs += 1
                    if got.returncode != 0 or answer != expected:
                        failures += 1
                        print(f"FAIL: {name} at {n} facts, dense={mode or 'default'}: "
                              f"{answer!r}, expected {expected!r}")
    if failures:
        print(f"FAIL: {failures} of {runs} reads changed across the relation threshold")
        return 1
    print(f"PASS: {runs} native reads answer alike as rows and as relations, "
          f"at {THRESHOLD - 1} and {THRESHOLD} facts, dense off, default and eager")
    return 0


if __name__ == "__main__":
    sys.exit(main())
