#!/usr/bin/env python3
"""Differential reference: the C answer-set solver against clingo.

Random ground normal programs; for each, the set of stable models from the C
solver's enumeration (asp_cdnl_main_v1 --enumerate) must equal clingo's; the
solver's own refutations are re-checked by the C checker (--check).
usage: asp_clingo_differential_v1.py DRIVER_BINARY SEED COUNT  (with the clingo module)"""
import random
import subprocess
import sys
import tempfile

import clingo


def gen_program(rng):
    n = rng.randint(3, 14)
    m = rng.randint(2, 3 * n)
    rules = []
    for _ in range(m):
        head = rng.randrange(n)
        pos = [rng.randrange(n) for _ in range(rng.choice([0, 0, 1, 1, 2, 3]))]
        neg = [rng.randrange(n) for _ in range(rng.choice([0, 1, 1, 2]))]
        rules.append((head, pos, neg))
    return rules


def program_text(rules):
    return '\n'.join(f"{h} : {' '.join(map(str, p))} : {' '.join(map(str, q))}" for h, p, q in rules)


def clingo_models(rules):
    ctl = clingo.Control(["0", "--warn=none"])
    lines = []
    atoms = set()
    for h, p, q in rules:
        atoms.update([h, *p, *q])
        body = [f"a({x})" for x in p] + [f"not a({x})" for x in q]
        lines.append(f"a({h})" + (" :- " + ", ".join(body) if body else "") + ".")
    ctl.add("base", [], "\n".join(lines))
    ctl.ground([("base", [])])
    models = set()
    ctl.solve(on_model=lambda m: models.add(frozenset(s.arguments[0].number for s in m.symbols(atoms=True))))
    return models


def c_models(driver, programs):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n---\n".join(program_text(r) for r in programs) + "\n")
        path = f.name
    out = subprocess.run([driver, "--enumerate", path], capture_output=True, text=True, check=True).stdout
    check = subprocess.run([driver, "--check", path], capture_output=True, text=True)
    blocks = [b for b in out.split("###\n") if b.strip()]
    result = []
    for b in blocks:
        ms = set()
        for line in b.splitlines():
            if line.startswith("model"):
                ms.add(frozenset(int(x) for x in line.split()[1:]))
            elif line == "stuck":
                ms = None
                break
        result.append(ms)
    return result, check


def main():
    driver, seed, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    rng = random.Random(seed)
    programs = [gen_program(rng) for _ in range(count)]
    ours, check = c_models(driver, programs)
    agree = 0
    for i, (prog, mine) in enumerate(zip(programs, ours)):
        ref = clingo_models(prog)
        if mine == ref:
            agree += 1
        else:
            print(f"DIFF program {i}: C {sorted(map(sorted, mine or []))} clingo {sorted(map(sorted, ref))}")
            print(program_text(prog))
    total_models = sum(len(m or ()) for m in ours)
    unsat = sum(1 for m in ours if m == set())
    print(f"agree {agree}/{len(programs)}; models {total_models}; programs without a model {unsat}")
    print(check.stdout.strip().splitlines()[-1] if check.stdout.strip() else "check: no output")
    return 0 if agree == len(programs) and check.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
