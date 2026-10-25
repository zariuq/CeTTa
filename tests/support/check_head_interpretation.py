#!/usr/bin/env python3
"""Source constructor roles agree across ordinary and compiled execution."""
import argparse
import json
import os
from pathlib import Path
import subprocess


CASES = {
    "list-data": (
        "(= (walk LNil $n) $n)\n"
        "(= (walk (LCons $x $xs) $n) (walk $xs (+ $n 1)))\n"
        "!(walk (LCons a (LCons b (LCons c LNil))) 0)\n", "3\n"),
    "empty-list-head": (
        "(= (unpack (LNil)) 7)\n!(unpack (LNil))\n", "7\n"),
    "error-head": (
        "(= (unpack (Error $x $why)) $x)\n"
        "!(unpack (Error 7 held))\n", "7\n"),
    "boolean-head": (
        "(= (unpack (True $x)) $x)\n!(unpack (True 7))\n", "7\n"),
    "metatype-data": (
        "(= (unpack (Expression $x)) $x)\n!(unpack (Expression 7))\n", "7\n"),
    "defined-data-name": (
        "(= (LCons $x $xs) (Pair $x $xs))\n"
        "(= (unpack (LCons $x $xs)) $x)\n!(unpack (Pair 7 LNil))\n", "7\n"),
    "functional-head": (
        "(= (one) 1)\n(= (unpack (one)) matched)\n!(unpack 1)\n", "matched\n"),
    "quoted-head": (
        "(= (one) 1)\n(= (unpack (quote (one))) held)\n"
        "!(unpack (quote (one)))\n", ""),
    "defined-data-name-effect": (
        "(= (LCons $x $xs) (progn (println! entered) (Pair $x $xs)))\n"
        "(= (unpack (LCons $x $xs)) $x)\n!(unpack (Pair 7 LNil))\n", "entered\n7\n"),
}

PREPARED_DATA_CASES = ("list-data", "empty-list-head", "error-head", "boolean-head")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    receipts = []
    for name, (source, expected) in CASES.items():
        path = args.output / (name + ".metta")
        path.write_text(source)
        for prepared in ("0", "1"):
            for opened in ("0", "1"):
                for stress in ("0", "1"):
                    env = dict(os.environ, CETTA_PREPARED_PURE_REFERENCE=prepared,
                        CETTA_OPEN_EQUATIONS_REFERENCE=opened,
                        CETTA_OEM_COLLECT_STRESS=stress,
                        CETTA_PETTA_COLLECT_STRESS=stress,
                        CETTA_PETTA_SEARCH_MACHINE="1")
                    result = subprocess.run(
                        [str(args.binary.resolve()), "--lang", "petta", str(path.resolve())],
                        env=env, capture_output=True, text=True, timeout=30)
                    if (result.returncode, result.stdout, result.stderr) != (0, expected, ""):
                        raise AssertionError((name, prepared, opened, stress,
                            result.returncode, result.stdout, result.stderr, expected))
                    receipts.append({"case": name, "prepared_reference": prepared,
                        "open_reference": opened, "collection_stress": stress})
    # Equal answers alone do not show that a constructor pattern was admitted:
    # the reference evaluator also answers correctly when compilation declines.
    for name in PREPARED_DATA_CASES:
        for prepared in ("0", "1"):
            env = dict(os.environ, CETTA_PREPARED_PURE_REFERENCE=prepared,
                CETTA_OPEN_EQUATIONS_REFERENCE="0", CETTA_PETTA_SEARCH_MACHINE="1",
                CETTA_PREPARED_PURE_DEBUG="1")
            result = subprocess.run(
                [str(args.binary.resolve()), "--lang", "petta",
                 str((args.output / (name + ".metta")).resolve())],
                env=env, capture_output=True, text=True, timeout=30)
            admitted = "prepared-pure code: bodies lowered to register code" in result.stderr
            if (result.returncode != 0 or result.stdout != CASES[name][1] or
                    admitted != (prepared == "0") or
                    "no exact source view" in result.stderr):
                raise AssertionError((name, prepared, "constructor admission",
                    result.returncode, result.stdout, result.stderr))
            receipts.append({"case": name, "prepared_reference": prepared,
                "compiled": admitted})
    (args.output / "receipt.json").write_text(json.dumps(receipts, indent=2) + "\n")
    print(f"PASS: {len(receipts)} constructor/head observations preserve admission, data, authored calls, quotation and effects")


if __name__ == "__main__":
    main()
