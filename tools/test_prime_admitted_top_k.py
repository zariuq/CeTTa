#!/usr/bin/env python3
"""Compare the admitted natural-score selector with stable sorting and filtering."""

import argparse
from pathlib import Path
import random
import subprocess


ROOT = Path(__file__).resolve().parent.parent


def natural(value):
    return "(S " * value + "Z" + ")" * value


def rows_term(rows):
    result = "RNil"
    for index, score, payload in reversed(rows):
        result = f"(RCons (row {natural(index)} {natural(score)} {payload}) {result})"
    return result


def cases():
    # These distinguish tie direction, strict eligibility, zero budgets and
    # occurrence preservation independently of randomized coverage.
    yield 3, 2, [(0, 2, "pa"), (1, 2, "pb"), (2, 2, "pc")]
    yield 2, 3, [(0, 1, "pa"), (2, 5, "pd"), (1, 2, "pb")]
    yield 3, 0, [(0, 2, "pa"), (1, 2, "pb")]
    yield 2, 3, [(1, 2, "pb"), (1, 2, "pb"), (0, 1, "pa")]
    rng = random.Random(20261026)
    for _ in range(64):
        rows = [(rng.randrange(6), rng.randrange(5), rng.choice(("pa", "pb", "pc", "pd")))
                for _ in range(rng.randrange(7))]
        yield rng.randrange(7), rng.randrange(7), rows


def observe(binary, declarations, queries, timeout):
    result = subprocess.run(
        [str(binary), "--lang", "prime", "-e", declarations + "\n" + queries],
        cwd=ROOT, capture_output=True, text=True, timeout=timeout,
    )
    if result.returncode or result.stderr:
        raise RuntimeError(f"native execution failed ({result.returncode}): {result.stderr}")
    lines = result.stdout.splitlines()
    boundary = lines.index("[topk]") + 1
    return lines[:boundary], lines[boundary:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    binary = args.binary.resolve()
    source = (ROOT / "lib/prime/natural_top_k.metta").read_text()
    declarations = ("!(bind! &math (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))\n" +
                    source.split("!(hol:define &self take-bound", 1)[0].replace("&self", "&math"))
    checks = list(cases())
    queries = "\n".join(
        f"!(eval (topk {natural(before)} {natural(budget)} {rows_term(rows)}) (admitted &math))"
        for before, budget, rows in checks
    )
    expected = [
        "[" + rows_term(sorted((row for row in rows if row[0] < before),
                              key=lambda row: -row[1])[:budget]) + "]"
        for before, budget, rows in checks
    ]
    admissions, actual = observe(binary, declarations, queries, args.timeout)
    names = ("Nat B Payload Row Rows geq leq lt pick score index len insert take bounded eligible topk").split()
    assert admissions == ["[()]"] + [f"[{name}]" for name in names], admissions
    assert len(actual) == len(expected), (len(actual), len(expected))
    for index, (got, wanted) in enumerate(zip(actual, expected)):
        assert got == wanted, (index, checks[index], got, wanted)

    mutations = {
        "reversed-ties": ("(geq (score $r) (score $f))", "(lt (score $f) (score $r))"),
        "future-row": ("(lt (index $r) $b)", "(leq (index $r) $b)"),
        "unbounded": ("(take $k (insert $r (bounded $k $rs)))",
                      "(insert $r (bounded $k $rs))"),
    }
    for label, (old, new) in mutations.items():
        assert declarations.count(old) == 1, label
        mutant_admissions, mutant = observe(
            binary, declarations.replace(old, new), queries, args.timeout)
        assert mutant_admissions == admissions, (label, mutant_admissions)
        assert len(mutant) == len(expected), (label, len(mutant))
        assert mutant != expected, f"reference failed to distinguish {label}"
    print(f"Admitted top-k: {len(checks)} reference cases; {len(mutations)} wrong algorithms distinguished")


if __name__ == "__main__":
    main()
