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

    typed = {
        "raw": ("(: typed-f (-> Atom Atom))\n(= (typed-f $x) (Box $x))\n",
                "(typed-f (+ 2 3))"),
        "translated": ("(: typed-f (-> %Undefined% %Undefined%))\n"
                       "(= (typed-f $x) (+ $x 1))\n",
                       "(typed-f (progn ignored (+ 2 3)))"),
        "guarded": ("(: typed-f (-> Number Number))\n(= (typed-f $x) (+ $x 1))\n",
                    "(typed-f (+ 2 3))"),
        "overloads": ("(: typed-f (-> Number Atom))\n(: typed-f (-> Atom Atom))\n"
                      "(= (typed-f $x) (Seen $x))\n",
                      "(collapse (typed-f (+ 2 3)))"),
        "shared-reject": ("(: typed-f (-> $t $t %Undefined%))\n"
                          "(= (typed-f $x $y) (Pair $x $y))\n",
                          "(collapse (typed-f 6 symbol))"),
        "result-reject": ("(: typed-f (-> Number Number))\n"
                          "(= (typed-f $x) symbol)\n",
                          "(collapse (typed-f 6))"),
        "held-family": ("(: typed-f (-> Number Atom))\n"
                        "(: typed-f (-> Number %Undefined%))\n"
                        "(= (typed-f $x) (+ $x 1))\n",
                        "(collapse (typed-f 6))"),
        "bound-check": ("(: 6 Foo)\n(: typed-f (-> Foo Foo))\n"
                        "(= (typed-f $x) $x)\n", "(typed-f 6)"),
        "late-atom": ("(: marker Atom)\n(: typed-f (-> $t $t Atom))\n"
                      "(= (typed-f $x $y) (Pair $x $y))\n",
                      "(collapse (typed-f marker (+ 2 3)))"),
    }
    for name, (prefix, body) in typed.items():
        for position in ("tail", "field", "binding"):
            if position == "field":
                placed = f"(Box {body})"
            elif position == "binding":
                placed = f"(let $v {body} (Box $v))"
            else:
                placed = body
            # A result-bearing outer box also witnesses the empty-call cases.
            source = prefix + f"(= (composition-run $free) (Box {placed}))\n!(composition-run $free)\n"
            yield f"typed-{name}-{position}", source, []
    parameters = [f"$v{i}" for i in range(70)]
    domains = ["Atom" if i in (0, 64, 69) else "Number" for i in range(70)]
    arguments = ["(+ 2 3)" if d == "Atom" else "6" for d in domains]
    source = ("(: typed-f (-> " + " ".join(domains) + " Atom))\n"
              "(= (typed-f " + " ".join(parameters) + ") (Row " + " ".join(parameters) + "))\n"
              "(= (composition-run $free) (typed-f " + " ".join(arguments) + "))\n"
              "!(composition-run $free)\n")
    yield "typed-arity-70", source, []

    # A shared engine registry spelling can still name an ordinary equation.
    for name in ("fold", "chain"):
        for arity in (0, 1, 2, 7, 70):
            parameters = [f"$v{i}" for i in range(arity)]
            domains = ["Atom" if i == 0 else "Number" for i in range(arity)]
            arguments = ["(+ 2 3)" if d == "Atom" else "6" for d in domains]
            prefix = (f"(: {name} (-> " + " ".join(domains + ["%Undefined%"]) + "))\n"
                      f"(= ({name} " + " ".join(parameters) + ") (Row " + " ".join(parameters) + "))\n")
            for position in ("tail", "field", "binding"):
                body = f"({name} " + " ".join(arguments) + ")"
                if position == "field":
                    body = f"(Box {body})"
                elif position == "binding":
                    body = f"(let $v {body} (Box $v))"
                source = prefix + f"(= (composition-run $free) {body})\n!(composition-run $free)\n"
                profile = "extended" if name == "chain" and arity == 1 else None
                yield f"typed-registry-{name}-{arity}-{position}", source, [], profile

    source = ("(: min (-> Atom Atom Atom))\n(= (min $x $y) authored)\n"
              "(= (composition-run $free) (Row (is-var $free) (min (+ 2 3) 7)))\n!(composition-run $free)\n")
    yield "extended-min", source, ["min/2"], "extended"
    source = ("(: chain (-> Number Number Number %Undefined%))\n"
              "(= (chain $x $y $z) authored)\n"
              "(= (composition-run $free) (Row (is-var $free) (chain (+ 2 3) $v (Row $v))))\n"
              "!(composition-run $free)\n")
    yield "owned-chain", source, ["chain/3"]
    source = ("(: fold-step (-> Number Number Number))\n"
              "(= (fold-step $item $acc) rejected)\n"
              "(= (composition-run $free) (Row (is-var $free) (collapse (foldl fold-step (1) 0))))\n"
              "!(composition-run $free)\n")
    yield "fold-adapter-result-guard", source, ["foldl/3"]


def invoke(binary, path, reference, stats, profile=None):
    env = dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE=str(int(reference)),
               CETTA_OPEN_EQUATIONS_DEBUG="host")
    command = [str(binary), "--lang", "petta"]
    if profile:
        command += ["--profile", profile]
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
        for document in documents():
            name, source, allowed = document[:3]
            profile = document[3] if len(document) > 3 else None
            path = directory / f"{name}.metta"
            path.write_text(source)
            tier = invoke(binary, path, False, args.stats, profile)
            machine = invoke(binary, path, True, False, profile)
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
                if not counters.get("open-equation-choice", 0):
                    raise AssertionError(f"{name}: tier did not take a call")
                if not (counters.get("open-equation-answer", 0) or
                        counters.get("open-equation-host-transfer", 0)):
                    raise AssertionError(f"{name}: tier neither answered nor transferred its continuation")
                if name.startswith("typed-") and not counters.get("open-equation-typed-call", 0):
                    raise AssertionError(f"{name}: typed call did not enter the tier")
            if args.petta_root and profile is None:
                swi = subprocess.run(["bash", "run.sh", "--silent", str(path)],
                                     cwd=args.petta_root, capture_output=True,
                                     text=True, timeout=30)
                (directory / f"{name}.swi.out").write_text(swi.stdout)
                (directory / f"{name}.swi.err").write_text(swi.stderr)
                if swi.returncode or swi.stdout != tier.stdout:
                    raise AssertionError(f"{name}: SWI discrepancy; inspect {directory}")
            receipts.append({"name": name, "profile": profile or "default",
                             "hosts": hosts, "counters": counters})
        (directory / "receipt.json").write_text(json.dumps(receipts, indent=2) + "\n")
    print(f"PASS: {len(receipts)} compositions preserve ordered answers and child/domain boundaries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
