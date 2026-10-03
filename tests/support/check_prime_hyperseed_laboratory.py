#!/usr/bin/env python3
"""Hyperseed's d-calculus concepts as Prime programs, and chapter 5's
laboratory recomputed from them, with and without native arithmetic.

tests/prime/trinity/hyperseed.laboratory.metta is run after the definitions
of arith.binary.metta (binary numbers, fractions and decimal notation, up to
`written`), which this script places before it, as
check_prime_arith_oracle.py places them before the oracle declarations:

  * on the declared equations alone, its answers must be
    hyperseed.laboratory.expected;
  * with the declarations of arith.binary.oracle-declarations.metta in
    between, its answers must be hyperseed.laboratory.oracle.expected.

Then:
  * the run with the oracle gives the same bytes twice (and, with --repeat,
    the run on the equations alone too);
  * the arithmetic definitions answer as in arith.binary.expected, and the
    oracle declarations are admitted;
  * both runs give the same answers apart from the firing counts, and every
    answer the oracle computed is followed by a well-formed trust record;
  * every value labelled with a Laboratory theorem agrees with it, both ways;
  * the firings of each value and the time of each run are reported.

With --oracle-only, only the run with the oracle is made and checked: its
answers, its trust records and its Laboratory values.  The run on the
equations alone takes about a minute and a half, so `make test-prime` runs
this mode and the slower list `make test-prime-all` runs both."""

import argparse
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from check_prime_arith_oracle import trust_lines, trust_record_well_formed, agreement_counts

ROOT = Path(__file__).resolve().parents[2]
TRINITY = ROOT / "tests/prime/trinity"
ARITH = TRINITY / "arith.binary.metta"
ARITH_EXPECTED = TRINITY / "arith.binary.expected"
DECLARATIONS = TRINITY / "arith.binary.oracle-declarations.metta"
BODY = TRINITY / "hyperseed.laboratory.metta"
EXPECTED = TRINITY / "hyperseed.laboratory.expected"
ORACLE_EXPECTED = TRINITY / "hyperseed.laboratory.oracle.expected"
TIMEOUT_SECONDS = 900

FIRINGS = re.compile(r"\(firings (\d+)\)")
LABEL = re.compile(r'"(Laboratory\.[A-Za-z0-9_]+)"(?: (\([^()]*\)))?')

failures = 0


def report(ok, message):
    global failures
    print(("PASS: " if ok else "FAIL: ") + message)
    if not ok:
        failures += 1


def lines(path):
    return path.read_text().rstrip("\n").split("\n")


def arithmetic():
    source = lines(ARITH)
    marker = next(i for i, line in enumerate(source)
                  if line.startswith("!(set:define &self written"))
    return source[:marker + 1]


def run(binary, source):
    with tempfile.NamedTemporaryFile(
            "w", suffix=".metta", dir=ROOT / "runtime", delete=False) as handle:
        handle.write(source)
        path = Path(handle.name)
    try:
        start = time.monotonic()
        done = subprocess.run([str(binary), "--lang", "prime", str(path)],
                              capture_output=True, text=True, timeout=TIMEOUT_SECONDS)
        return done.returncode, done.stdout, done.stderr, time.monotonic() - start
    finally:
        path.unlink()


def split(stdout, skip):
    """The output of the arithmetic definitions, and that of what follows
    them: the oracle declarations (the first `skip` answers) and the body."""
    out = stdout.rstrip("\n").split("\n")
    marker = out.index("[written]") + 1
    rest = out[marker:]
    answers = [i for i, line in enumerate(rest) if line.startswith("[")]
    cut = answers[skip] if skip < len(answers) else len(rest)
    return out[:marker], rest[:cut], rest[cut:]


def answers(body):
    return [line for line in body if not line.startswith("(uses-oracle ")]


def without_firings(line):
    return FIRINGS.sub("(firings <n>)", line)


def laboratory(body):
    """The Laboratory values: (theorem, label, agreed, firings)."""
    values = []
    for line in answers(body):
        found = LABEL.search(line)
        firings = FIRINGS.search(line)
        if found and firings:
            values.append((found.group(1), found.group(2) or "", " True " in line,
                           int(firings.group(1))))
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--repeat", action="store_true",
                        help="also run the program on the equations alone twice")
    parser.add_argument("--oracle-only", action="store_true",
                        help="run and check the program with the oracle only")
    args = parser.parse_args()
    binary = Path(args.binary).resolve()

    head = arithmetic()
    declarations = lines(DECLARATIONS)
    declared = sum(1 for line in declarations if line.startswith("!"))
    body = lines(BODY)
    plain = "\n".join(head + body) + "\n"
    oracle = "\n".join(head + declarations + body) + "\n"

    # 1. With the oracle, twice.
    status, out_on, err_on, time_on = run(binary, oracle)
    status2, out_on2, _, time_on2 = run(binary, oracle)
    report(status == 0 and status2 == 0 and out_on == out_on2,
           f"with the oracle the program gives the same bytes on two runs "
           f"({time_on:.1f} s and {time_on2:.1f} s)")
    head_on, declared_on, body_on = split(out_on, declared)
    report(len(declared_on) == declared and
           all(line.startswith("[") and "Refuted" not in line and "False" not in line
               for line in declared_on),
           f"{declared} oracle declarations admitted")
    report("\n".join(body_on) + "\n" == ORACLE_EXPECTED.read_text(),
           "with the oracle the answers are hyperseed.laboratory.oracle.expected")
    records = trust_lines("\n".join(body_on))
    report(len(records) > 0 and all(trust_record_well_formed(line) for line in records),
           f"{len(records)} trust records, each naming the backend's trust and the agreement "
           f"with the equations apart: {agreement_counts(records)}")

    if args.oracle_only:
        way_a = lines(ARITH_EXPECTED)
        report(head_on == way_a[:way_a.index("[written]") + 1],
               "the arithmetic definitions answer as in arith.binary.expected")
        lab_on = laboratory(body_on)
        report(len(lab_on) > 0 and all(agreed for _, _, agreed, _ in lab_on),
               f"chapter 5: the {len(lab_on)} Laboratory values agree with their Lean "
               "theorems with the oracle")
        print(f"{'Lean theorem':38} {'value':44} {'oracle':>8}")
        for name, label, _, n_on in lab_on:
            print(f"{name:38} {label[:44]:44} {n_on:>8}")
        print(f"{'total':83} {sum(n for _, _, _, n in lab_on):>8}")
        print(f"time: {time_on:.1f} s with the oracle (the arithmetic definitions included)")
        print(f"Prime Hyperseed laboratory with the oracle: "
              f"{'passed' if failures == 0 else f'{failures} failed'}")
        return 0 if failures == 0 else 1

    # 2. On the equations alone.
    status, out_off, err_off, time_off = run(binary, plain)
    head_off, _, body_off = split(out_off, 0)
    way_a = lines(ARITH_EXPECTED)
    report(status == 0 and head_off == way_a[:way_a.index("[written]") + 1],
           "the arithmetic definitions answer as in arith.binary.expected")
    report("\n".join(body_off) + "\n" == EXPECTED.read_text(),
           f"on the equations alone the answers are hyperseed.laboratory.expected ({time_off:.1f} s)")
    report(not trust_lines("\n".join(body_off)),
           "on the equations alone no answer carries a trust record")
    if args.repeat:
        status, out_off2, _, time_off2 = run(binary, plain)
        report(status == 0 and out_off2 == out_off,
               f"on the equations alone the program gives the same bytes on two runs "
               f"({time_off2:.1f} s)")

    # 3. The same answers both ways.
    on, off = answers(body_on), answers(body_off)
    differing = sum(1 for x, y in zip(on, off) if x != y)
    report(len(on) == len(off) and
           all(without_firings(x) == without_firings(y) for x, y in zip(on, off)),
           f"{len(off)} answers identical with and without the oracle apart from "
           f"{differing} firing counts")

    # 4. The laboratory, both ways.
    lab_on, lab_off = laboratory(body_on), laboratory(body_off)
    report(len(lab_on) == len(lab_off) > 0 and
           all(agreed for _, _, agreed, _ in lab_on + lab_off),
           f"chapter 5: the {len(lab_off)} Laboratory values agree with their Lean theorems "
           "with and without the oracle")
    print(f"{'Lean theorem':38} {'value':44} {'equations':>10} {'oracle':>8}")
    for (name, label, _, n_off), (_, _, _, n_on) in zip(lab_off, lab_on):
        print(f"{name:38} {label[:44]:44} {n_off:>10} {n_on:>8}")
    total_off = sum(n for _, _, _, n in lab_off)
    total_on = sum(n for _, _, _, n in lab_on)
    print(f"{'total':83} {total_off:>10} {total_on:>8}")
    print(f"time: {time_off:.1f} s on the equations alone, {time_on:.1f} s with the oracle "
          "(the arithmetic definitions included)")

    print(f"Prime Hyperseed laboratory: {'passed' if failures == 0 else f'{failures} failed'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
