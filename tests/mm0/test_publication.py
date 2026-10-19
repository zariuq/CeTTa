#!/usr/bin/env python3
"""Publication is one transaction (review finding 2).

Runs a CeTTa binary built with -DMM0_ADAPTER_FAULTS (see build.mk,
native-mm0-fault-host) and fails each fallible step of publishing a theorem
or a theory in turn (CETTA_MM0_FAULT).  Each failure must be reported with
NOMEM, never the step budget's LIMIT, and must leave the
theory's declarations and the live handle counts exactly as they were.
Without a fault the same calls publish.

    test_publication.py CETTA-FAULT-BINARY
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PROGRAM = """!(import! &self mm0kernel)
!(let* (($th (mm0:theory))
        ($_ (mm0:declare $th (sort wff (strict provable))))
        ($s0 (mm0:stats $th)) ($l0 (mm0:live))
        ($proof (mm0:prove $th (theorem t ((var P wff)) (P) P) () ((Mm0Row 1 (hyp 0) ())) 1))
        ($s1 (mm0:stats $th)) ($l1 (mm0:live))
        ($axiom (mm0:declare $th (axiom ax ((var Q wff)) () Q)))
        ($s2 (mm0:stats $th)) ($l2 (mm0:live)))
   (result $s0 $l0 $proof $s1 $l1 $axiom $s2 $l2))
"""

THEORY = """!(import! &self mm0kernel)
!(let* (($l0 (mm0:live)) ($th (mm0:theory)) ($l1 (mm0:live))) (result $l0 $th $l1))
"""

THEOREM_FAULTS = {
    "theorem-record": "NOMEM",
    "theorem-capsule": "NOMEM",
    "theorem-owned-atom": "NOMEM",
}
THEORY_FAULTS = {
    "theory-record": "NOMEM",
    "theory-capsule": "NOMEM",
    "theory-owned-atom": "NOMEM",
}


def run(binary, program, fault):
    env = dict(os.environ)
    env.pop("CETTA_MM0_FAULT", None)
    if fault:
        env["CETTA_MM0_FAULT"] = fault
    out = subprocess.run([binary, "--lang", "he", "--quiet", "-e", program], cwd=ROOT, env=env,
                         capture_output=True, text=True, timeout=120)
    if out.returncode != 0:
        raise AssertionError(f"{fault}: exit {out.returncode}: {out.stderr}")
    line = out.stdout.strip().splitlines()[-1]
    return line


def fields(line):
    """Split (result A B ...) at top level."""
    text = line.strip().lstrip("[").rstrip("]")
    assert text.startswith("(result ") and text.endswith(")"), text
    body = text[len("(result "):-1]
    parts, depth, cur, quoted = [], 0, "", False
    for ch in body:
        if ch == '"' and (not cur or cur[-1] != "\\"):
            quoted = not quoted
        if not quoted and ch == "(":
            depth += 1
        if not quoted and ch == ")":
            depth -= 1
        if not quoted and ch == " " and depth == 0:
            if cur:
                parts.append(cur)
            cur = ""
            continue
        cur += ch
    if cur:
        parts.append(cur)
    return parts


def main():
    binary = sys.argv[1]
    checks = failures = 0

    def check(cond, what, detail=""):
        nonlocal checks, failures
        checks += 1
        if not cond:
            failures += 1
            print(f"FAIL {what}: {detail}")

    s0, l0, proof, s1, l1, axiom, s2, l2 = fields(run(binary, PROGRAM, None))
    check(proof.startswith('(Sealed mm0)'), "without a fault the theorem publishes", proof)
    check(axiom.startswith('(Sealed mm0)'), "without a fault the axiom publishes", axiom)
    check(s1.split()[3] == str(int(s0.split()[3]) + 1), "one more theorem in the theory", (s0, s1))
    for fault, status in THEOREM_FAULTS.items():
        s0, l0, proof, s1, l1, axiom, s2, l2 = fields(run(binary, PROGRAM, fault))
        check(proof.startswith(f'(mm0:error "{status}"'), f"{fault}: the proof reports {status}", proof)
        check(axiom.startswith(f'(mm0:error "{status}"'), f"{fault}: the axiom reports {status}", axiom)
        check(s1 == s0 and s2 == s0, f"{fault}: the theory is unchanged", (s0, s1, s2))
        check(l1 == l0 and l2 == l0, f"{fault}: no handle survives the failure", (l0, l1, l2))
    for fault, status in THEORY_FAULTS.items():
        l0, th, l1 = fields(run(binary, THEORY, fault))
        check(th.startswith(f'(mm0:error "{status}"'), f"{fault}: a new theory reports {status}", th)
        check(l1 == l0, f"{fault}: no theory survives the failure", (l0, l1))
    print(f"Mm0PublicationSummary {checks} {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
