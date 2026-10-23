#!/usr/bin/env python3
"""Ordered contextual case observations, including rollback and effect order."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

# Authored observations include language-specific case and Empty behavior.
CASES = {
    "empty-origin": """(: choose-value (-> Atom Atom %Undefined%))
(= (choose-value $pattern $key) (case $key (($pattern yes) ($other no))))
(= (apply-choice $f $x) ($f $x))
(= (empty-pattern) Empty)
(= (computed-choice $key) (case $key (((empty-pattern) computed) ($other no))))
!(choose-value Empty Empty)
!(apply-choice (choose-value Empty) Empty)
!(computed-choice Empty)
!(case Empty ((Empty default) ($other data)))
!(case (empty) ((Empty default) ($other data)))
""",
    'locals': """(= (f $xs) (case $xs (((Box $x $x) (Pair $x $y)) ($z (Pair miss $z)))))
!(collapse (f (Box a a)))
!(collapse (f (Box a b)))
!(collapse (let (Pair a bound) (f (Box a a)) bound))
!(collapse (let $v (f (Box a a)) (Pair $v (f (Box b b)))))
!(collapse (f $open))
""",
    'dynamic-arms': """(: via-arm (-> Atom Atom %Undefined%))
(= (via-arm $key $arm) (case $key ($arm ($other fallback))))
!(via-arm a (a hit))
!(via-arm a (b miss))
(: via-arms (-> Atom Atom %Undefined%))
(= (via-arms $key $arms) (case $key $arms))
!(via-arms b ((a wrong) (b right)))
""",
    'pattern-scope': """(: f (-> Atom Atom %Undefined%))
(= (f $key $pattern) (case $key (($pattern hit) ($other miss))))
!(f (Box a) (Box a))
!(f (Box a) (Box b))
!(collapse (f (Box $x) (Box a)))
!(f Empty Empty)
""",
    'default': """(= (pick $n) (case $n ((0 (empty)) ($other (superpose (a b a))))))
(= (f $n) (case (pick $n) ((a (Pair a $x)) (b (Pair b $y)) (Empty (Pair none $z)))))
!(collapse (f 0))
!(collapse (f 1))
!(collapse (once (f 1)))
!(collapse (let (Pair a shared) (f 1) shared))
""",
    'prepare-effects': """(= (mark $tag $value) (progn (println! $tag) $value))
(= (f $n) (case (mark key $n) (((mark first a) (mark chosen one)) ((mark second b) two))))
(= (g $n) (case (mark key $n) (((mark first a) one) ((mark second b) two) (Empty none))))
!(collapse (f a))
!(collapse (f b))
!(collapse (g a))
!(collapse (g (empty)))
""",
    'key-binding': """(= (f $x) (case (let $x a (Box a)) (((Box $x) (Pair hit $x)) ($other wrong))))
!(collapse (f $open))
!(collapse (f b))
""",
    'cons': """(= (f $xs) (case $xs (((cons $a $tail) (Pair $a $tail)) ($other no))))
!(collapse (f (cons 1 $tail)))
!(collapse (f (1 2 3)))
!(collapse (let $tail (superpose (() (2 3))) (f (cons 1 $tail))))
""",
    'opaque-argument': """(= (opaque-pick $list key-0) (Pair 0 $list))
(= (opaque-pick $list key-1) (Pair 1 $list))
(= (opaque-pick $list key-2) (Pair 2 $list))
(= (opaque-pick $list key-3) (Pair 3 $list))
(= (opaque-pick $list key-4) (Pair 4 $list))
(= (opaque-pick $list key-5) (Pair 5 $list))
(= (opaque-pick $list key-6) (Pair 6 $list))
(= (opaque-pick $list key-7) (Pair 7 $list))
(= (opaque-pick $list key-8) (Pair 8 $list))
(= (opaque-pick $list key-9) (Pair 9 $list))
(= (opaque-pick $list key-10) (Pair 10 $list))
(= (opaque-pick $list key-11) (Pair 11 $list))
(= (opaque-pick $list key-12) (Pair 12 $list))
(= (opaque-pick $list key-13) (Pair 13 $list))
(= (opaque-pick $list key-14) (Pair 14 $list))
(= (opaque-pick $list key-15) (Pair 15 $list))
!(let $xs (cons first $tail) (opaque-pick $xs key-7))
""",
    'opaque-wide': """(= (wide $x0 $x1 $x2 $x3 $x4 $x5 $x6 $x7 $x8 $x9 $x10 $x11 $x12 $x13 $x14 $x15 $x16 $x17 $x18 $x19 $x20 $x21 $x22 $x23 $x24 $x25 $x26 $x27 $x28 $x29 $x30 $x31 $x32 $x33 $x34 $x35 $x36 $x37 $x38 $x39 $x40 $x41 $x42 $x43 $x44 $x45 $x46 $x47 $x48 $x49 $x50 $x51 $x52 $x53 $x54 $x55 $x56 $x57 $x58 $x59 $x60 $x61 $x62 $x63 left) left)
(= (wide $x0 $x1 $x2 $x3 $x4 $x5 $x6 $x7 $x8 $x9 $x10 $x11 $x12 $x13 $x14 $x15 $x16 $x17 $x18 $x19 $x20 $x21 $x22 $x23 $x24 $x25 $x26 $x27 $x28 $x29 $x30 $x31 $x32 $x33 $x34 $x35 $x36 $x37 $x38 $x39 $x40 $x41 $x42 $x43 $x44 $x45 $x46 $x47 $x48 $x49 $x50 $x51 $x52 $x53 $x54 $x55 $x56 $x57 $x58 $x59 $x60 $x61 $x62 $x63 right) right)
!(wide (cons a $tail) value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value value right)
""",
}
EXPECTED = {
    "empty-origin": {"petta": "yes\nyes\ncomputed\ndata\ndefault\n"},'locals': {'petta': '((Pair a $V0))\n'
                     '((Pair miss (Box a b)))\n'
                     '(bound)\n'
                     '((Pair (Pair a $V0) (Pair b $V1)))\n'
                     '((Pair $V0 $V1))\n',
            'he': '[((Pair a $V0))]\n'
                  '[((Pair miss (Box a b)))]\n'
                  '[(bound)]\n'
                  '[((Pair (Pair a $V0) (Pair b $V1)))]\n'
                  '[((Pair $V0 $V1))]\n'},
 'dynamic-arms': {'petta': 'hit\nfallback\nright\n', 'he': '[hit]\n[fallback]\n[right]\n'},
 'pattern-scope': {'petta': 'hit\nmiss\n(hit)\nhit\n', 'he': '[hit]\n[miss]\n[(hit)]\n[hit]\n'},
 'default': {'petta': '((Pair none $V0))\n'
                      '((Pair a $V0) (Pair b $V1) (Pair a $V2))\n'
                      '((Pair a $V0))\n'
                      '(shared shared)\n',
             'he': '[((Pair none $V0))]\n'
                   '[((Pair a $V0) (Pair b $V1) (Pair a $V2))]\n'
                   '[((once (Pair a $V0)) (once (Pair b $V1)) (once (Pair a $V0)))]\n'
                   '[(shared shared)]\n'},
 'prepare-effects': {'petta': 'key\n'
                              'first\n'
                              'second\n'
                              'chosen\n'
                              'key\n'
                              'first\n'
                              'second\n'
                              'first\n'
                              'second\n'
                              'key\n'
                              '(one)\n'
                              '(two)\n'
                              '(one)\n'
                              '()\n',
                     'he': 'key\nkey\nkey\n[()]\n[()]\n[()]\n[()]\n'},
 'key-binding': {'petta': '((Pair hit a))\n()\n', 'he': '[((Pair hit a))]\n[()]\n'},
 'cons': {'petta': '((Pair 1 $V0))\n((Pair 1 (2 3)))\n((Pair 1 ()) (Pair 1 (2 3)))\n',
          'he': '[((Pair 1 $V0))]\n[((Pair 1 (2 3)))]\n[((Pair 1 ()) (Pair 1 (2 3)))]\n'},
 'opaque-argument': {'petta': '(Pair 7 (cons first $V0))\n'},
 'opaque-wide': {'petta': 'right\n'}}

CASES["if-sharing"] = """(= (branch $flag $x)
    (if $flag (Pair $x $local $local) (Pair no $x $local)))
!(let (Pair a b b) (branch True a) success)
!(collapse (let (Pair a b c) (branch True a) impossible))
!(collapse (branch False a))
"""
EXPECTED["if-sharing"] = {"petta": "success\n()\n((Pair no a $V0))\n"}

CASES["if-late-local"] = (
    "(= (wide-if $flag) (if $flag (Unused "
    + " ".join(f"$unused{i}" for i in range(96))
    + ") (Last $late $late)))\n"
    "!(let (Last a a) (wide-if False) same)\n"
    "!(collapse (let (Last a b) (wide-if False) impossible))\n"
    "!(collapse (let (Last $x $x) (wide-if False) (Pair $x $x)))\n"
    "!(let (Last Empty Empty) (wide-if False) empty-data)\n"
)
EXPECTED["if-late-local"] = {"petta": "same\n()\n((Pair $V0 $V0))\nempty-data\n"}


def normalized(text):
    lines = []
    for line in text.splitlines():
        identities = {}
        # Preserve sharing within each result; printed frame IDs may differ.
        lines.append(re.sub(r"\$[A-Za-z_][A-Za-z_0-9]*(?:#[0-9]+)?",
                            lambda match: identities.setdefault(
                                match[0], f"$V{len(identities)}"), line))
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True, type=Path)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    count = 0
    with tempfile.TemporaryDirectory(prefix="cetta-contextual-case-") as directory:
        for name, source in CASES.items():
            path = Path(directory) / (name + ".metta")
            path.write_text(source)
            for lang in EXPECTED[name]:
                for ordinary in (False, True):
                    for reference, stress in ((False, False), (True, False), (False, True)):
                        env = dict(os.environ,
                                   CETTA_OPEN_EQUATIONS_REFERENCE=str(int(ordinary)),
                                   CETTA_PREPARED_PURE_REFERENCE=str(int(ordinary)),
                                   CETTA_PETTA_EQUATION_BODY_ACTIVATION_REFERENCE=str(int(reference)),
                                   CETTA_PETTA_COLLECT_STRESS=str(int(stress)))
                        result = subprocess.run([str(binary), "--lang", lang, str(path)],
                                                env=env, text=True, capture_output=True,
                                                timeout=30)
                        assert result.returncode == 0 and not result.stderr, (
                            name, lang, ordinary, reference, stress, result)
                        assert normalized(result.stdout) == EXPECTED[name][lang], (
                            name, lang, ordinary, reference, stress,
                            result.stdout, EXPECTED[name][lang])
                        count += 1
        # Representation changes preserve the finite-fuel observation too.
        # The unconditional closed-IF prototype incorrectly completed at 21.
        path = Path(directory) / "if-fuel.metta"
        path.write_text(CASES["if-sharing"])
        for ordinary in (False, True):
            for reference in (False, True):
                for fuel in (21, 144):
                    env = dict(os.environ,
                               CETTA_OPEN_EQUATIONS_REFERENCE=str(int(ordinary)),
                               CETTA_PREPARED_PURE_REFERENCE=str(int(ordinary)),
                               CETTA_PETTA_EQUATION_BODY_ACTIVATION_REFERENCE=str(int(reference)))
                    result = subprocess.run(
                        [str(binary), "--lang", "petta", "--fuel", str(fuel), str(path)],
                        env=env, text=True, capture_output=True, timeout=30)
                    if fuel == 21:
                        assert result.returncode == 1 and not result.stdout
                        assert result.stderr == "error: observation incomplete: fuel-exhausted\n"
                    else:
                        assert result.returncode == 0 and not result.stderr
                        assert normalized(result.stdout) == EXPECTED["if-sharing"]["petta"]
                    count += 1
    # One argument is opaque, but a different argument still selects a head.
    with tempfile.TemporaryDirectory(prefix="cetta-partial-selector-") as directory:
        path = Path(directory) / "selector.metta"
        path.write_text(CASES["opaque-argument"])
        for mode, expected_candidates in (("default", 1), ("linear", 16)):
            env = dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE="1",
                       CETTA_PREPARED_PURE_REFERENCE="1", CETTA_PETTA_MACHINE_STATS="1",
                       CETTA_PETTA_MATCH_DECISION=mode)
            result = subprocess.run([str(binary), "--lang", "petta", str(path)],
                                    env=env, text=True, capture_output=True, timeout=30)
            assert result.returncode == 0 and result.stdout == EXPECTED["opaque-argument"]["petta"]
            records = [dict(field.split("=", 1) for field in line.split()[1:])
                       for line in result.stderr.splitlines()
                       if line.startswith("PETTA_MACHINE_STATS ")]
            assert len(records) == 1 and int(records[0]["equation_candidates"]) == expected_candidates
    print(f"PASS: {count} ordered contextual observations and two selector coverage receipts")


if __name__ == "__main__":
    main()
