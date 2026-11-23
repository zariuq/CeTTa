#!/usr/bin/env python3
"""Controls of the renaming of generated names (generated_space_names.py).

Positive: one program that makes callables and spaces, at the root and in
parallel branches, prints different names under the counter and the
unforgeable name supply, and the same text, the expected one, once each
output is renamed; renaming a renamed output changes nothing.

Negative: renaming never erases a difference.  Two runs that differ only in
whether two positions hold one callable (or one space) or two still differ
once renamed; the two kinds are numbered apart; and a symbol that only
resembles a generated name is left as it is.
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generated_space_names import rename_generated_space_names  # noqa: E402

SEED = "000102030405060708090a0b0c0d0e0f"
NAMES = """\
!(Root (|-> ($x) $x) (repr (|-> ($x) (Wrapped $x))))
!(sort-atom (collapse (hyperpose ((Made b0 (|-> ($x) (B $x))) (Made b1 (|-> ($x) (C $x)))))))
!(let $s (new-space) (let $f (|-> ($x) $x) (Both $s $f $s $f)))
"""
EXPECTED = """\
(Root lambda_1 "lambda_2")
((Made b0 lambda_3) (Made b1 lambda_4))
(Both &space(1) lambda_5 &space(1) lambda_5)
"""
SHARED = "!(let $f (|-> ($x) $x) (Pair $f $f))\n!(let $s (new-space) (Pair $s $s))\n"
DISTINCT = ("!(let* (($f (|-> ($x) $x)) ($g (|-> ($x) $x))) (Pair $f $g))\n"
            "!(let* (($s (new-space)) ($t (new-space))) (Pair $s $t))\n")
# Symbols that contain a generated spelling without being one, and the two
# kinds with equal numerals, a child's dotted numeral and a keyed block's
# among them.
UNCHANGED = ("(my_lambda_1 lambda_1x lambda_x lambda_ lambda_beef xlambda_2 lambda_3-step "
             "lambda_1. lambda_0.x lambda_0..1)\n")
KEY = "529d30b948a8330fdb6a1996a5480869"
KINDS = ("(lambda_7 &space(9) lambda_8 &space(7) lambda_7 lambda_0.1 &space(0.1) "
         "&space(%s.0.2) lambda_%s.1 &space(9) lambda_0.1)\n" % (KEY, KEY),
         "(lambda_1 &space(1) lambda_2 &space(2) lambda_1 lambda_3 &space(3) "
         "&space(4) lambda_4 &space(1) lambda_3)\n")


def run(binary, program, supply):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", dir="runtime", delete=False) as handle:
        handle.write(program)
        path = handle.name
    try:
        environment = dict(os.environ, CETTA_NAME_SUPPLY=supply, CETTA_NAME_SEED=SEED)
        done = subprocess.run([binary, "--lang", "petta", path], capture_output=True,
                              text=True, timeout=120, env=environment)
    finally:
        os.unlink(path)
    if done.returncode != 0 or done.stderr:
        sys.exit("FAIL: %s supply run (exit %d): %s" % (supply, done.returncode, done.stderr))
    return done.stdout


def fail(message, *texts):
    print("FAIL: " + message)
    for text in texts:
        print(text, end="" if text.endswith("\n") else "\n")
    sys.exit(1)


def main():
    binary = sys.argv[1]
    counter, unforgeable = run(binary, NAMES, "counter"), run(binary, NAMES, "unforgeable")
    if counter == unforgeable:
        fail("the two supplies printed the same names", counter)
    for raw in (counter, unforgeable):
        renamed = rename_generated_space_names(raw)
        if renamed != EXPECTED:
            fail("a renamed output is not the expected text", raw, renamed)
        if rename_generated_space_names(renamed) != renamed:
            fail("renaming a renamed output changed it", renamed)
    for supply in ("counter", "unforgeable"):
        shared = rename_generated_space_names(run(binary, SHARED, supply))
        distinct = rename_generated_space_names(run(binary, DISTINCT, supply))
        if shared == distinct:
            fail("renaming merged two names (%s supply)" % supply, shared, distinct)
    if rename_generated_space_names(UNCHANGED) != UNCHANGED:
        fail("a symbol that only resembles a generated name was renamed",
             rename_generated_space_names(UNCHANGED))
    if rename_generated_space_names(KINDS[0]) != KINDS[1]:
        fail("the two kinds were not numbered apart", rename_generated_space_names(KINDS[0]))
    print("PASS: generated names compare up to renaming, each kind apart; "
          "renaming merges no names and leaves look-alike symbols alone")


if __name__ == "__main__":
    main()
