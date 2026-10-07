#!/usr/bin/env python3
"""Exercise directional declarations through public evaluation boundaries."""

import argparse
import os
from pathlib import Path
import re
import subprocess


def alpha(text):
    names = {}
    return re.sub(r"\$[\w-]+(?:#\d+)?", lambda m: names.setdefault(m[0], f"$v{len(names)}"), text)


parser = argparse.ArgumentParser()
parser.add_argument("binary", type=Path)
args = parser.parse_args()
binary = str(args.binary.resolve())
checks = 0


def check(language, source, expected, *, reference=False, profile="extended"):
    global checks
    command = [binary, "--lang", language]
    if profile:
        command += ["--profile", profile]
    environment = dict(os.environ)
    environment["CETTA_OPEN_EQUATIONS_REFERENCE"] = "1" if reference else "0"
    environment["CETTA_HE_SHARED_EXECUTION"] = "0" if reference else "1"
    completed = subprocess.run(command + ["-e", source], env=environment,
                               capture_output=True, text=True, timeout=30)
    wanted = "".join(f"[{line}]\n" if language == "he" else line + "\n" for line in expected)
    observed = completed.returncode, alpha(completed.stdout), completed.stderr
    assert observed == (0, alpha(wanted), ""), (language, reference, profile, source, observed, wanted)
    checks += 1


for reference in (False, True):
    for language in ("he", "petta"):
        # A rigid caller is not refined merely to make a concrete rule fit.
        check(language, "(=% (sk ((S K) K)) I)\n!(collapse (sk ((S K) $x)))\n!(sk ((S K) K))",
              ["((sk ((S K) $x)))" if language == "he" else "()", "I"], reference=reference)
        check(language, "(=% (f a) fixed)\n(=% (f $x) (value $x))\n"
              "!(collapse (f $q))\n!(collapse (f a))\n!(collapse (f b))",
              ["((value $q))", "(fixed (value a))", "((value b))"], reference=reference)
        # Policies coexist; equal rule occurrences do not collapse.
        check(language, "(=% (f a) fixed)\n(= (f b) ordinary)\n(=% (f a) fixed)\n"
              "!(collapse (f $q))\n!(collapse (f a))\n!(collapse (f b))",
              ["(ordinary)", "(fixed fixed)", "(ordinary)"], reference=reference)
        # Repeated pattern variables check identity in the protected subject.
        check(language, "(=% (same $x $x) yes)\n!(collapse (same $u $v))\n!(same $u $u)",
              ["((same $u $v))" if language == "he" else "()", "yes"], reference=reference)
        # Head protection ends before an explicitly binding body executes.
        check(language, "(=% (bind-body $x) (unify $x a (bound $x) no))\n!(bind-body $q)",
              ["(bound a)"], reference=reference)
        # A rejected candidate must not execute the body's observable effect.
        check(language, "(=% (only a) (trace! forbidden bad))\n!(collapse (only $q))",
              ["((only $q))" if language == "he" else "()"], reference=reference)
        # Rule-local variables belong to distinct activations.
        check(language, "(=% (fresh $x) (out $x $private))\n!(pair (fresh a) (fresh b))",
              ["(pair (out a $p) (out b $q))"], reference=reference)
        # Rule bodies call other directional rules through the same engine.
        check(language, "(=% (g $x) (answer $x))\n(=% (f $x) (g $x))\n!(f a)\n!(f b)",
              ["(answer a)", "(answer b)"], reference=reference)
        # A mirror marker and malformed declarations remain ordinary data.
        check(language, "(%= (reserved a) wrong)\n(=% (malformed a) wrong extra)\n"
              "!(reserved a)\n!(malformed a)", ["(reserved a)", "(malformed a)"], reference=reference)
        check(language, "(=% (f a) one)\n!(f a)", ["(f a)"],
              profile="he" if language == "he" else None, reference=reference)
        effect = "(())" if language == "he" else "(true)"
        # Revisions invalidate the previously compiled ordinary caller.
        check(language, "(= (f $x) (old $x))\n"
              "(= (driver $x) (collapse (f $x)))\n!(driver a)\n"
              "!(collapse (add-atom &self (=% (f a) new)))\n"
              "!(driver a)\n!(driver $q)\n"
              "!(collapse (remove-atom &self (=% (f a) new)))\n!(driver a)",
              ["((old a))", effect, "((old a) new)", "((old $q))", effect,
               "((old a))"], reference=reference)
        check(language, "!(f a)\n"
              "!(collapse (add-atom &self (=% (f $x) (new $x))))\n!(f a)\n"
              "!(collapse (remove-atom &self (=% (f $x) (new $x))))\n!(f a)",
              ["(f a)", effect, "(new a)", effect, "(f a)"], reference=reference)
        # A body mutation does not rewrite the alternatives already captured.
        check(language, "(=% (f $x) (let $z (remove-atom &self (=% (f a) later)) first))\n"
              "(=% (f a) later)\n!(collapse (f a))\n!(collapse (f a))",
              ["(first later)", "(first)"], reference=reference)
        check(language, "(=% (f $x) (answer $x))\n"
              "(= (apply $fn $x) ($fn $x))\n!(apply f a)",
              ["(answer a)"], reference=reference)
        fixture = Path(__file__).resolve().parents[1] / "fixtures" / "directional_rules"
        check(language, f'!(import! &self {fixture})\n'
              "!(collapse (imported-client a))\n!(collapse (imported-client $q))",
              ["()" if language == "he" else "true", "(selected (imported a))",
               "((imported $q))"], reference=reference)

    # HE admits equations whose operator is itself a term. PeTTa's function
    # application convention is different, so this is an HE-specific client.
    check("he", "(=% ((S K) K) I)\n!((S K) K)\n!((S K) $q)",
          ["I", "((S K) $q)"], reference=reference)
    for marker in ("=", "=%"):
        check("he", f"({marker} ((K $x) $y) $x)\n!((K a) b)\n!((K $x) b)",
              ["a", "$x"], reference=reference)
    check("he", "(=% (I $x) $x)\n(=% ((K $x) $y) $x)\n"
          "(=% (((S $x) $y) $z) (($x $z) ($y $z)))\n!(((S K) K) a)",
          ["a"], reference=reference)

print(f"PASS: {checks} native rule, policy, occurrence, scope, effect and profile controls")
