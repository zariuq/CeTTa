#!/usr/bin/env python3
"""Ordered composition, native domains, and the boundary of a host child."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def documents():
    for form in ("progn", "prog1"):
        for length in (1, 2, 3, 8, 33):
            children = ["(composition-first $x)"]
            if length > 1:
                children += ["ignored"] * (length - 2) + ["(composition-last $x)"]
            for depth in (0, 1, 5):
                body = "(" + form + " " + " ".join(children) + ")"
                for _ in range(depth):
                    body = f"(progn ignored (prog1 {body} ignored))"
                for position in ("tail", "field", "binding"):
                    placed = body
                    if position == "field":
                        placed = f"(Box {body})"
                    elif position == "binding":
                        placed = f"(Box (let $v {body} (prog1 $v ignored)))"
                    source = (
                        "(= (composition-first $x) (superpose (1 1 2)))\n"
                        "(= (composition-last $x) (superpose (a b)))\n"
                        f"(= (composition-run $x) {placed})\n"
                        "!(collapse (composition-run $free))\n"
                    )
                    yield f"{form}-{length}-{depth}-{position}", source, []
    for depth in (1, 3, 8):
        body = f"(Pair $a{depth} $b{depth})"
        for level in reversed(range(depth)):
            next_level = level + 1
            body = (f"(Box (let* (($a{next_level} (+ $a{level} 1)) "
                    f"($b{next_level} (+ $a{next_level} 2))) {body}))")
        source = f"(= (composition-run $x $a0 $b0) {body})\n!(composition-run $free 1 2)\n"
        yield f"bindings-{depth}", source, []
    operations = {
        "observations": "(Flags (is-var $x) (is-expr (Pair $x a)) (is-ground $x) (is-space &self))",
        "equal-refine": "(progn (= $x a) (Result $x (is-var $x)))",
        "equal-rollback": "(let $r (= (Pair $x $x) (Pair a b)) (Result $r (is-var $x)))",
        "equal-alias": "(progn (= $x $y) (let $r (= (Pair $x $y) (Pair a b)) (Result $r (is-var $x) (is-var $y))))",
        "equal-closed-computed": "(Result (is-var $x) (= (+ 2 3) (progn ignored 5)))",
        "numeric": "(Result (is-var $x) (min 7 3) (max 7 3) (% -7 3))",
        "numeric-computed": "(Result (is-var $x) (min (+ 2 3) (progn ignored 7)) (max 5 (+ 3 4)))",
        "undefined": "(Result (is-var $x) (// (+ 2 3) (prog1 2 ignored)) (numeric-eq 1 1.0))",
        "host-child": "(progn (println! leaf) ignored (Result (is-var $x)))",
        "host-domain": "(prog1 (Result (is-var $x) (min 3.5 2.5)) ignored)",
    }
    for name, body in operations.items():
        allowed = ["println!/1"] if name == "host-child" else []
        if name == "host-domain":
            allowed = ["min/2"]
        yield name, f"(= (composition-run $x) {body})\n!(composition-run $free)\n", allowed


def invoke(binary, path, reference, stats):
    env = dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE=str(int(reference)),
               CETTA_OPEN_EQUATIONS_DEBUG="host")
    command = [str(binary), "--lang", "petta"]
    if stats:
        command.append("--emit-runtime-stats")
    command.append(str(path))
    return subprocess.run(command, env=env, capture_output=True, text=True,
                          timeout=30)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--stats", action="store_true")
    parser.add_argument("--petta-root", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    receipts = []
    with tempfile.TemporaryDirectory(prefix="tier-composition-") as temporary:
        directory = args.output or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        for name, source, allowed in documents():
            path = directory / f"{name}.metta"
            path.write_text(source)
            tier = invoke(binary, path, False, args.stats)
            machine = invoke(binary, path, True, False)
            for route, result in (("tier", tier), ("machine", machine)):
                (directory / f"{name}.{route}.out").write_text(result.stdout)
                (directory / f"{name}.{route}.err").write_text(result.stderr)
            if tier.returncode or machine.returncode or tier.stdout != machine.stdout:
                raise AssertionError(f"{name}: route discrepancy; inspect {directory}")
            hosts = [line.removeprefix("open-equations host ")
                     for line in tier.stderr.splitlines()
                     if line.startswith("open-equations host ")]
            if hosts != allowed or "open-equations decline " in tier.stderr:
                raise AssertionError(f"{name}: host boundary {hosts}, expected {allowed}")
            counters = {}
            for line in tier.stderr.splitlines():
                parts = line.split()
                if len(parts) == 3 and parts[0] == "runtime-counter":
                    counters[parts[1]] = int(parts[2])
            if args.stats:
                for counter in ("open-equation-choice", "open-equation-answer"):
                    if not counters.get(counter, 0):
                        raise AssertionError(f"{name}: tier did not take and answer a call")
            if args.petta_root:
                swi = subprocess.run(["bash", "run.sh", "--silent", str(path)],
                                     cwd=args.petta_root, capture_output=True,
                                     text=True, timeout=30)
                (directory / f"{name}.swi.out").write_text(swi.stdout)
                (directory / f"{name}.swi.err").write_text(swi.stderr)
                if swi.returncode or swi.stdout != tier.stdout:
                    raise AssertionError(f"{name}: SWI discrepancy; inspect {directory}")
            receipts.append({"name": name, "hosts": hosts, "counters": counters})
        (directory / "receipt.json").write_text(json.dumps(receipts, indent=2) + "\n")
    print(f"PASS: {len(receipts)} compositions preserve ordered answers and child/domain boundaries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
