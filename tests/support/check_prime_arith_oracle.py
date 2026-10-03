#!/usr/bin/env python3
"""Native arithmetic as a specified oracle, checked without being believed.

The arithmetic fixture of tests/prime/trinity runs once more with the oracle
declarations of arith.binary.oracle-declarations.metta inserted after its
definitions.  Its answers must be those of the fixture without the oracle,
apart from the firing counts, and every answer that used an oracle is
followed by its trust records (arith.binary.oracle.expected).

Then:
  * the audit replays every oracle answer through the declared equations, and
    the residue check compares sums and products modulo three primes: no
    mismatch, and the answers are unchanged;
  * a differential test runs 1,000 random closed calls of the declared
    operations with and without the oracle: the answers are identical, and
    equal to the values Python computes;
  * a build whose backend adds wrongly at large values (the planted fault) is
    caught by the audit, by the residue check and by the differential test,
    and its wrong answers carry the trust record of the operation that made
    them."""

import argparse
import math
import random
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/prime/trinity/arith.binary.metta"
WAY_A_EXPECTED = ROOT / "tests/prime/trinity/arith.binary.expected"
DECLARATIONS = ROOT / "tests/prime/trinity/arith.binary.oracle-declarations.metta"
ORACLE_EXPECTED = ROOT / "tests/prime/trinity/arith.binary.oracle.expected"
SEED = 20261003
CASES = 1000
TIMEOUT_SECONDS = 600

failures = 0


def report(ok, message):
    global failures
    print(("PASS: " if ok else "FAIL: ") + message)
    if not ok:
        failures += 1


def definitions_and_rest():
    lines = FIXTURE.read_text().rstrip("\n").split("\n")
    marker = next(i for i, line in enumerate(lines)
                  if line.startswith("!(set:define &self written"))
    return lines[:marker + 1], lines[marker + 1:]


def declarations():
    return DECLARATIONS.read_text().rstrip("\n").split("\n")


def run(binary, source, flags=()):
    with tempfile.NamedTemporaryFile(
            "w", suffix=".metta", dir=ROOT / "runtime", delete=False) as handle:
        handle.write(source)
        path = Path(handle.name)
    try:
        done = subprocess.run(
            [str(binary), "--lang", "prime", *flags, str(path)],
            capture_output=True, text=True, timeout=TIMEOUT_SECONDS)
        return done.returncode, done.stdout, done.stderr
    finally:
        path.unlink()


def answers(stdout):
    return [line for line in stdout.rstrip("\n").split("\n")
            if not line.startswith("(uses-oracle ")]


def trust_lines(stdout):
    return [line for line in stdout.split("\n") if line.startswith("(uses-oracle ")]


COST = re.compile(r"\(firings \d+\)|\((binary|binary-digits) (\d+) (\d+)( \d+)?\)")

# A trust record names two trusts apart: that the backend computes its native
# operation correctly, and that the native operation agrees with the declared
# equations.  The agreement is proved in Lean for the sum and the product of
# Trinity.Arith.Binary, and only tested, by the 9 calls checked at admission,
# for every other operation.
TRUST = re.compile(
    r"\(uses-oracle (\S+) \(trusted (gmp-mpz|uint64) [a-z-]+\) "
    r"\(spec-agreement (\(proved [^()]+\)|\(tested \(admission-calls 9\)\))\) "
    r"\(calls [1-9]\d*\) \(tests test-prime-arith-oracle\)\)$")
PROVED = {
    "padd": "(proved Trinity.Arith.Oracle.realization_meets "
            "Trinity.Arith.Oracle.conservative_padd)",
    "pmul": "(proved Trinity.Arith.Oracle.realization_meets "
            "Trinity.Arith.Oracle.conservative_pmul)",
}


def trust_record_well_formed(line):
    match = TRUST.match(line)
    if not match:
        return False
    operation, _, agreement = match.groups()
    return agreement == PROVED.get(operation, "(tested (admission-calls 9))")


def agreement_counts(records):
    proved = sum(1 for line in records if "(spec-agreement (proved " in line)
    return f"{proved} proved in Lean, {len(records) - proved} tested"


def without_costs(line):
    return COST.sub("<cost>", line)


# ---------------------------------------------------------------------------
# Numerals in the fixture's spelling


def pos(n):
    text = "one"
    for digit in bin(n)[3:]:
        text = f"(bit{digit} {text})"
    return text


def nat(n):
    return "n0" if n == 0 else f"(npos {pos(n)})"


def integer(z):
    return "z0" if z == 0 else f"(zpos {pos(z)})" if z > 0 else f"(zneg {pos(-z)})"


def fraction(z, d):
    g = math.gcd(abs(z), d)
    z, d = z // g, d // g
    return f"(over {integer(z)} {pos(d)})"


def written(z, d):
    return f"(over {integer(z)} {pos(d)})"


def order(x, y):
    return "less" if x < y else "same" if x == y else "more"


def cases():
    rng = random.Random(SEED)
    ops = ["padd", "pmul", "pcmp", "ncmp", "nsub", "ndivmod", "pgcd",
           "qmake", "qadd", "qsub", "qmul", "qcmp"]
    out = []
    for k in range(CASES):
        op = ops[k % len(ops)]
        if op == "padd":
            x, y = rng.randint(1, 1 << 14), rng.randint(1, 1 << 14)
            out.append((op, f"(padd {pos(x)} {pos(y)})", pos(x + y)))
        elif op == "pmul":
            x, y = rng.randint(1, 1 << 8), rng.randint(1, 1 << 8)
            out.append((op, f"(pmul {pos(x)} {pos(y)})", pos(x * y)))
        elif op == "pcmp":
            x = rng.randint(1, 1 << 12)
            y = x if rng.random() < 0.2 else rng.randint(1, 1 << 12)
            out.append((op, f"(pcmp {pos(x)} {pos(y)})", order(x, y)))
        elif op == "ncmp":
            x = rng.randint(0, 1 << 12)
            y = x if rng.random() < 0.2 else rng.randint(0, 1 << 12)
            out.append((op, f"(ncmp {nat(x)} {nat(y)})", order(x, y)))
        elif op == "nsub":
            x, y = rng.randint(0, 1 << 12), rng.randint(0, 1 << 12)
            out.append((op, f"(nsub {nat(x)} {nat(y)})", nat(max(x - y, 0))))
        elif op == "ndivmod":
            x, y = rng.randint(0, 1 << 12), rng.randint(1, 1 << 6)
            out.append((op, f"(ndivmod {nat(x)} {pos(y)})",
                        f"(quot-rem {nat(x // y)} {nat(x % y)})"))
        elif op == "pgcd":
            g = rng.randint(1, 1 << 5)
            x, y = g * rng.randint(1, 1 << 5), g * rng.randint(1, 1 << 5)
            out.append((op, f"(pgcd {pos(x)} {pos(y)})", pos(math.gcd(x, y))))
        elif op == "qmake":
            z, d = rng.randint(-(1 << 9), 1 << 9), rng.randint(1, 1 << 9)
            out.append((op, f"(qmake {integer(z)} {pos(d)})", fraction(z, d)))
        else:
            z1, d1 = rng.randint(-40, 40), rng.randint(1, 40)
            z2, d2 = rng.randint(-40, 40), rng.randint(1, 40)
            a, b = written(z1, d1), written(z2, d2)
            if op == "qadd":
                value = (z1 * d2 + z2 * d1, d1 * d2)
            elif op == "qsub":
                value = (z1 * d2 - z2 * d1, d1 * d2)
            elif op == "qmul":
                value = (z1 * z2, d1 * d2)
            if op == "qcmp":
                out.append((op, f"(qcmp {a} {b})", order(z1 * d2, z2 * d1)))
            else:
                out.append((op, f"({op} {a} {b})", fraction(*value)))
    return out


def differential(binary, with_oracle_binary=None):
    """Answers of the cases without the oracle (by `binary`) and with it (by
    `with_oracle_binary`, `binary` by default)."""
    head, _ = definitions_and_rest()
    calls = cases()
    body = [f"!{call}" for _, call, _ in calls]
    off_status, off_out, off_err = run(binary, "\n".join(head + body) + "\n")
    on_status, on_out, on_err = run(with_oracle_binary or binary,
                                    "\n".join(head + declarations() + body) + "\n")
    off = answers(off_out)[-len(calls):]
    on = answers(on_out)[-len(calls):]
    return calls, (off_status, off, off_err), (on_status, on, on_err, on_out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--fault-binary")
    args = parser.parse_args()
    binary = Path(args.binary).resolve()

    head, rest = definitions_and_rest()
    with_oracle = "\n".join(head + declarations() + rest) + "\n"
    declared = sum(1 for line in declarations() if line.startswith("!"))

    # 1. The fixture with the oracle declared.
    status, stdout, stderr = run(binary, with_oracle)
    report(status == 0 and stdout == ORACLE_EXPECTED.read_text(),
           "the arithmetic fixture with the oracle matches arith.binary.oracle.expected")
    on = answers(stdout)
    way_a = WAY_A_EXPECTED.read_text().rstrip("\n").split("\n")
    marker = way_a.index("[written]") + 1
    declared_answers = on[marker:marker + declared]
    on_answers = on[:marker] + on[marker + declared:]
    report(all(line.startswith("[") and line.endswith("]") and "False" not in line
               for line in declared_answers),
           f"{declared} declarations admitted")
    same = len(on_answers) == len(way_a) and all(
        without_costs(x) == without_costs(y) for x, y in zip(on_answers, way_a))
    changed = sum(1 for x, y in zip(on_answers, way_a) if x != y)
    report(same, f"{len(way_a)} answers identical to way (A) apart from "
                 f"{changed} cost counts")
    records = trust_lines(stdout)
    report(len(records) > 0 and all(trust_record_well_formed(line) for line in records),
           f"{len(records)} trust records, each naming the backend's trust and the "
           f"agreement with the equations apart: {agreement_counts(records)}")
    chapter5 = [line for line in on_answers if "Laboratory." in line]
    expected5 = [line for line in way_a if "Laboratory." in line]
    report(len(chapter5) == len(expected5) > 0 and
           all("True" in line for line in chapter5),
           f"chapter 5: the {len(chapter5)} Laboratory values agree")

    # 2. The audit and the residue check.
    status, audited, stderr = run(binary, with_oracle, ("--oracle-audit", "--oracle-residues"))
    summary = re.search(
        r"\(oracle-audit \(answers (\d+)\) \(agreed (\d+)\) \(undecided (\d+)\) "
        r"\(mismatches (\d+)\)\)", stderr)
    residues = re.search(r"\(oracle-residues \(checked (\d+)\) \(mismatches (\d+)\)\)", stderr)
    report(status == 0 and audited == stdout and summary is not None and
           summary.group(1) == summary.group(2) and int(summary.group(1)) > 0 and
           summary.group(3) == "0" and summary.group(4) == "0" and
           residues is not None and int(residues.group(1)) > 0 and residues.group(2) == "0",
           "audit: every oracle answer replayed through the equations agrees "
           f"({summary.group(1) if summary else '?'} answers), residues agree "
           f"({residues.group(1) if residues else '?'} sums and products)")

    # 3. The differential test.
    calls, (off_status, off, _), (on_status, on_diff, _, on_out) = differential(binary)
    expected = [f"[{value}]" for _, _, value in calls]
    agree_on = sum(1 for x, y in zip(on_diff, off) if x == y)
    agree_python = sum(1 for x, y in zip(off, expected) if x == y)
    used = Counter(line.split()[1] for line in trust_lines(on_out))
    report(off_status == 0 and on_status == 0 and len(off) == CASES and
           agree_on == CASES and agree_python == CASES and
           sum(used.values()) >= CASES,
           f"differential: {agree_on}/{CASES} oracle answers identical to the declared "
           f"equations, {agree_python}/{CASES} equal to Python, over "
           f"{len(set(op for op, _, _ in calls))} operations")

    # 4. The planted fault.
    if args.fault_binary:
        fault = Path(args.fault_binary).resolve()
        status, out, err = run(fault, with_oracle, ("--oracle-audit",))
        report(status == 3 and "(oracle-audit-mismatch " in out,
               "planted fault: the audit stops the run at the first wrong answer")
        status, out, err = run(fault, with_oracle, ("--oracle-residues",))
        report(status == 3 and "(oracle-residue-mismatch " in out,
               "planted fault: the residue check stops the run at the first wrong sum")
        status, out, err = run(fault, with_oracle)
        lines = out.rstrip("\n").split("\n")
        wrong = [i for i, line in enumerate(lines)
                 if "Laboratory." in line and "False" in line]
        carried = all(i + 1 < len(lines) and lines[i + 1].startswith("(uses-oracle ")
                      for i in wrong)
        report(status == 0 and len(wrong) > 0 and carried,
               f"planted fault: {len(wrong)} chapter 5 verdicts turn False, each with its "
               "trust records")
        calls, (_, off, _), (_, on_fault, _, _) = differential(binary, fault)
        mismatches = sum(1 for x, y in zip(on_fault, off) if x != y)
        report(mismatches > 0,
               f"planted fault: the differential test finds {mismatches} wrong answers")

    print(f"Prime arithmetic oracle: {'passed' if failures == 0 else f'{failures} failed'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
