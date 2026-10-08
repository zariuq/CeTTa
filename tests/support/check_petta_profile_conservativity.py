#!/usr/bin/env python3
"""Exercise native fallback ownership and shared PeTTa execution routes."""

import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
args = parser.parse_args()
binary = str(args.binary.resolve())
root = Path(__file__).resolve().parents[2]
checks = 0


def check(source, expected, profile=None, machine="1", reference="0",
          status=0, diagnostic=""):
    global checks
    command = [binary, "--lang", "petta"]
    if profile:
        command += ["--profile", profile]
    result = subprocess.run(command + ["-e", source], cwd=root,
        env={**os.environ, "CETTA_PETTA_SEARCH_MACHINE": machine,
             "CETTA_OPEN_EQUATIONS_REFERENCE": reference},
        text=True, capture_output=True, timeout=60)
    assert (result.returncode, result.stdout, result.stderr) == (status, expected, diagnostic), (
        command, machine, reference, source, result)
    checks += 1


owned = [
    ("(= (unify $a $b $then $else) mine)\n"
     "!(collapse (unify a a yes no))", "(mine)\n"),
    ("!(import! &self (library lib_he))\n"
     "!(collapse (unify a a yes no))", "true\n(yes)\n"),
    ("!(import! &self (library lib_he))\n"
     "(= (if-equal $a $b $then $else) mine)\n"
     "!(collapse (if-equal a a yes no))", "true\n(yes mine)\n"),
    ("(= (unify $a $b $then $else) first)\n"
     "(= (unify $a $b $then $else) second)\n"
     "!(collapse (unify a a yes no))", "(first second)\n"),
    ("(= (ordinary-owned $x) (unify $x $x yes no))\n"
     "(= (unify $a $b $then $else) mine)\n"
     "!(collapse (ordinary-owned a))", "(mine)\n"),
    ("!(bind! &profile-space (new-space))\n"
     "!(add-atom &profile-space (edge a b))\n"
     "!(match &profile-space (edge a $x) $x)", "true\nb\n"),
    ("!(bind! &profile-state (new-state (new-space)))\n"
     "!(add-atom (get-state &profile-state) (edge a b))\n"
     "!(match (get-state &profile-state) (edge a $x) $x)", "true\ntrue\nb\n"),
    ("(edge a b)\n(edge a b)\n(edge b c)\n"
     "!(collapse (match &self (, (edge $x $y) (edge $y $z)) ($x $z)))",
     "((a c) (a c))\n"),
]
for profile in (None, "extended"):
    for machine in ("0", "1"):
        for reference in ("0", "1"):
            for source, expected in owned:
                check(source, expected, profile, machine, reference)

# An authored callable keeps its arity contract, including overapplication.
for profile in (None, "extended"):
    check("(= (unify $x) (Unary $x))\n!(unify a)", "(Unary a)\n", profile)
    check("(= (unify $x) (Unary $x))\n!(unify a a yes no)", "", profile,
          status=2, diagnostic="error: uncaught PeTTa error: "
          "(Error (domain_error (function_input_arities unify (1)) 4) none)\n")
check("!(collapse (unify a b yes no))", "(no)\n", "extended")
check("!(unify a a yes no)", "(unify a a yes no)\n")

# A value containing an operation name stays data. Only executable source
# occurrences acquire the operation's argument demands.
check("(= (hold $x) (data $x))\n!(hold (quote (data a b)))", "((data a b))\n", "extended")
for profile in (None, "extended"):
    check("(= (last-value $xs) (car-atom (reverse $xs)))\n"
          "!(last-value (range 1 30000))", "30000\n", profile)
check((root / "tests/petta/typecheck_v2_profile_isolation.metta").read_text(),
      (root / "tests/petta/typecheck_v2_profile_isolation.extended.expected").read_text(),
      "extended")

# Typed lists enter the same equation engine as tuples, while retaining
# their constructor, empty tail, aliases and occurrence multiplicity.
# Rest patterns still use the established host matching implementation.
typed_lists = [
    ("(= (identity $xs) $xs)\n!(identity [])\n!(identity [a b])",
     "[]\n[a b]\n"),
    ("(= (split (cons $h $t)) ($h $t))\n"
     "!(split [])\n!(split [a b])\n!(split [a])\n!(split (a b))",
     "(a [b])\n(a [])\n(a (b))\n"),
    ("(= (only-empty []) typed)\n(= (only-empty ()) tuple)\n"
     "!(only-empty [])\n!(only-empty ())\n!(collapse (only-empty $q))",
     "typed\ntuple\n(typed tuple)\n"),
    ("(= (only-pair [$x $x]) (Pair $x $x))\n"
     "!(only-pair [a a])\n!(only-pair [a b])\n!(only-pair (a a))",
     "(Pair a a)\n"),
    ("(= (choose $xs) (superpose $xs))\n"
     "!(collapse (choose []))\n!(collapse (choose [a b a]))",
     "()\n(a b a)\n"),
    ("(= (same $x) (Pair $x $x))\n!(same [a b])",
     "(Pair [a b] [a b])\n"),
    ("(edge [a b])\n(edge [a b])\n"
     "!(collapse (match &self (edge [a b]) matched))\n"
     "!(collapse (match &self (edge (a b)) wrong))",
     "(matched matched)\n()\n"),
    ("(= (rest [$h | $t]) ($h $t))\n!(rest [a b])\n!(rest [])",
     "(a [b])\n"),
    ("(= (local-rest $xs) (let [$h | $t] $xs ($h $t)))\n"
     "!(local-rest [a b])\n!(local-rest [])", "(a [b])\n"),
    ("(= (prepend $xs) (cons a $xs))\n"
     "!(prepend [b c])\n!(prepend [])\n!(prepend (b c))",
     "[a b c]\n[a]\n(a b c)\n"),
    ("(= (sum-list $xs) (foldl + $xs 0))\n"
     "!(sum-list [])\n!(sum-list [1 2 3])\n!(sum-list (1 2 3))",
     "0\n6\n6\n"),
    ("(= (traced-choice $xs) (trace! before (superpose $xs)))\n"
     "!(collapse (traced-choice [a a b]))\n!(collapse (traced-choice []))",
     "before\nbefore\n(a a b)\n()\n"),
]
for machine in ("0", "1"):
    for reference in ("0", "1"):
        for source, expected in typed_lists:
            check(source, expected, "extended", machine, reference)

print(f"PASS: {checks} PeTTa profile ownership, occurrence, demand and route controls")
