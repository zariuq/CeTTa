#!/usr/bin/env python3
"""Check ordering, ordinary matching and trace effects across dialect routes."""

import argparse
import os
from pathlib import Path
import re
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument("binary", type=Path)
args = parser.parse_args()
binary = str(args.binary.resolve())
root = Path(__file__).resolve().parents[2]
checks = 0


def check(language, profile, source, expected, environment=None, status=0,
          diagnostic="", fresh_scopes=False):
    global checks
    command = [binary, "--lang", language]
    if profile:
        command += ["--profile", profile]
    command += ["-e", source]
    env = dict(os.environ)
    if environment:
        env.update(environment)
    result = subprocess.run(command, env=env, capture_output=True, text=True,
                            timeout=30)
    stdout = (re.sub(r"(\$[A-Za-z_]\w*)#\d+", r"\1", result.stdout)
              if fresh_scopes else result.stdout)
    assert (result.returncode, stdout, result.stderr) == (
        status, expected, diagnostic), (command, environment, result)
    checks += 1


operators = ("@<", "@>", "@<=", "@>=")
source = (root / "tests/test_term_order_predicates.metta").read_text()
he_expected = (root / "tests/test_term_order_predicates.expected").read_text()
petta_expected = "".join(
    line.strip("[]").lower() + "\n" for line in he_expected.splitlines())
for reference in ("0", "1"):
    environment = {"CETTA_OPEN_EQUATIONS_REFERENCE": reference}
    check("he", "extended", source, he_expected, environment)
    check("petta", "extended", source, petta_expected, environment)

for language, profile in (("he", "he"), ("he", "he-compat"),
                          ("he", "he-prime"),
                          ("petta", None)):
    for operator in operators:
        call = f"({operator} a b)"
        expected = f"[{call}]\n" if language == "he" else call + "\n"
        check(language, profile, "!" + call, expected)
        definition = f"(= ({operator} $left $right) authored)\n!{call}"
        check(language, profile, definition,
              "[authored]\n" if language == "he" else "authored\n")
    check(language, profile, "!(collapse (get-type @<))",
          "[(%Undefined%)]\n" if language == "he" else "(%Undefined%)\n")

check("he", "extended", "!(get-type @<)", "[(-> $a $b Bool)]\n",
      fresh_scopes=True)
for operator in operators:
    check("mm2", None, f"!({operator} a b)", f"[({operator} a b)]\n")
for operator in operators:
    for operands in ("a", "a b c"):
        call = f"({operator} {operands})"
        check("he", "extended", "!" + call,
              f"[(Error {call} IncorrectNumberOfArguments)]\n")
        check("petta", "extended", "!" + call, "", status=2,
              diagnostic=f"error: uncaught PeTTa error: "
              f"(Error {call} IncorrectNumberOfArguments)\n")

# Both dialects compare evaluated values. Effects in an operand must occur
# before the result is observed, rather than comparing the operand's syntax.
check("he", "extended", """
!(let $state (new-state 0)
   (let $result (@< (let $write (change-state! $state 1) a) b)
      (observed $result (get-state $state))))
""", "[(observed True 1)]\n")
check("petta", "extended", """
!(bind! order-state (new-state 0))
!(let $result (@< (let $write (change-state! order-state 1) a) b)
    (observed $result (get-state order-state)))
""", "true\n(observed true 1)\n")

for reference in ("0", "1"):
    environment = {"CETTA_OPEN_EQUATIONS_REFERENCE": reference}
    check("he", "extended", """
!(let $state (new-state 0)
   (let $result (@<
      (let $write (change-state! $state (+ (get-state $state) 1)) 1)
      (let $write (change-state! $state (+ (get-state $state) 1)) 2))
      (observed $result (get-state $state))))
""", "[(observed True 2)]\n", environment)
    check("he", "extended",
          "!(collapse (@< (superpose (1 2 1)) 2))",
          "[(True False True)]\n", environment)
    check("petta", "extended",
          "!(collapse (@< (superpose (1 2 1)) 2))",
          "(true false true)\n", environment)
    check("he", "extended", "!(@< (/ 1 0) 2)",
          "[(Error (/ 1 0) DivisionByZero)]\n", environment)

case_source = (root / "tests/he_case_two_sided.metta").read_text()
case_expected = (root / "tests/he_case_two_sided.expected").read_text()
for profile in ("he", "he-compat", "extended"):
    for canonical in ("0", "1"):
        check("he", profile, case_source, case_expected,
              {"CETTA_HE_CANONICAL_EXECUTION": canonical})
check("prime", None, """
!(case ((S K) $z) ((((S K) K) matched) ($other (other $other))))
""", "[(other ((S K) $z))]\n")

occurrences = """
(edge $x b)
(edge a b)
(edge a b)
!(collapse (match &self (edge a b) found))
!(collapse (match &self (, (edge a b) (edge a b)) found))
!(collapse (match &self (edge absent c) found))
"""
for language in ("he", "petta"):
    lines = ("(found found found)", "(" + " ".join(["found"] * 9) + ")", "()")
    expected = "".join((f"[{line}]" if language == "he" else line) + "\n"
                       for line in lines)
    for fastpath in ("0", "1"):
        check(language, "extended", occurrences, expected,
              {"CETTA_JOIN_FASTPATH": fastpath})

trace_source = (root / "tests/petta/trace_effect_order.metta").read_text()
trace_expected = (root / "tests/petta/trace_effect_order.expected").read_text()
for profile in (None, "extended"):
    for machine in ("0", "1"):
        for reference in ("0", "1"):
            check("petta", profile, trace_source, trace_expected, {
                "CETTA_PETTA_SEARCH_MACHINE": machine,
                "CETTA_OPEN_EQUATIONS_REFERENCE": reference,
            })
            check("petta", profile, "!(trace! (+ 1 2))", "(trace! 3)\n", {
                "CETTA_PETTA_SEARCH_MACHINE": machine,
                "CETTA_OPEN_EQUATIONS_REFERENCE": reference,
            })
            check("petta", profile,
                  "(= (trace! $x) (authored $x))\n!(trace! a)",
                  "(authored a)\n", {
                      "CETTA_PETTA_SEARCH_MACHINE": machine,
                      "CETTA_OPEN_EQUATIONS_REFERENCE": reference,
                  })
for canonical in ("0", "1"):
    environment = {"CETTA_HE_CANONICAL_EXECUTION": canonical}
    check("he", "extended", "!(trace! before-choice (superpose (a b)))",
          "[a, b]\n", environment, diagnostic="before-choice\n")
    check("he", "extended", "!(trace! before-zero (empty))",
          "", environment, diagnostic="before-zero\n")

print(f"PASS: {checks} ordering/profile/demand/case/occurrence/trace controls")
