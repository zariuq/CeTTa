#!/usr/bin/env python3
"""Directional matching preserves syntax, permissions and saved identities."""

import argparse
import os
from pathlib import Path
import re
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument("binary", type=Path)
args = parser.parse_args()
binary = str(args.binary.resolve())
checks = 0


def variable_names(text):
    lines = []
    for line in text.splitlines(keepends=True):
        names = {}
        lines.append(re.sub(r'"(?:\\.|[^"\\])*"|\$[\w-]+(?:#\d+)?',
            lambda m: names.setdefault(m[0], f'$v{len(names)}')
                if m[0].startswith('$') else m[0], line))
    return ''.join(lines)


def check(language, source, answers, *, profile="extended", imported=True,
          reference=False, fault=None, renamed=False):
    global checks
    command = [binary, "--lang", language]
    if profile:
        command += ["--profile", profile]
    if imported:
        source = "!(import! &self pat)\n" + source
    command += ["-e", source]
    environment = dict(os.environ)
    environment["CETTA_OPEN_EQUATIONS_REFERENCE"] = "1" if reference else "0"
    result = subprocess.run(command, env=environment, capture_output=True,
                            text=True, timeout=30)
    if language == "petta" and fault:
        expected = (2, "", f"error: uncaught PeTTa error: {fault}\n")
    else:
        values = (["()" if language == "he" else "true"] if imported else []) + answers
        stdout = "".join(f"[{value}]\n" if language == "he" else value + "\n"
                         for value in values)
        expected = (0, stdout, "")
    observed = (result.returncode, result.stdout, result.stderr)
    if renamed:
        expected = expected[0], variable_names(expected[1]), variable_names(expected[2])
        observed = observed[0], variable_names(observed[1]), variable_names(observed[2])
    assert observed == expected, (language, profile, reference, source, observed, expected)
    checks += 1


cases = [
    # Several constraints, one binding environment.
    ("!(pat:apply (pat:solve match% (((p $x) (p a)) ((q $x) (q a)))) (answer $x))",
     ["(answer a)"]),
    ("!(collapse (pat:apply (pat:solve match% (((p $x) (p a)) ((q $x) (q b)))) (answer $x)))",
     ["()"]),
    # A captured subject variable stays protected in the later constraint.
    ("!(collapse (pat:solve match% (((p $x) (p $u)) ((q $x) (q a)))))", ["()"]),
    ("!(collapse (pat:solve match% (((q $x) (q a)) ((p $x) (p $u)))))", ["()"]),
    ("!(collapse (pat:solve match% ((($x $x) ($u $v)))))", ["()"]),
    ("!(pat:apply (pat:solve %match (((p a) (p $x)) ((q a) (q $x)))) (answer $x))",
     ["(answer a)"]),
    ("!(collapse (pat:solve %match (((p $u) (p $x)) ((q a) (q $x)))))", ["()"]),
    # Intentional cross-input aliases must not be standardized apart.
    ("!(collapse (pat:solve match% ((($x $y) ($y a)))))", ["()"]),
    ("!(pat:apply (pat:solve unify ((($x $y) ($y a)))) ($x $y))", ["(a a)"]),
    ("!(pat:apply (pat:solve match% ()) retained)", ["retained"]),
    # Both repeated occurrences and injectivity constrain a variant witness.
    ("!(collapse (pat:solve variant ((($x $x) ($y $z)))))", ["()"]),
    ("!(collapse (pat:solve variant ((($x $y) ($z $z)))))", ["()"]),
    ("!(collapse (pat:solve variant ((($x) ($u)) (($x) ($v)))))", ["()"]),
    # Syntax inspection and application are not evaluation.
    ("(= (danger) (trace! forbidden boom))\n"
     "!(pat:apply (pat:solve match% ((($x) ((danger))))) ($x $x))",
     ["((danger) (danger))"]),
    ("!(pat:apply (pat:solve match% ()) (trace! forbidden data))",
     ["(trace! forbidden data)"]),
    ("!(pat:apply (pat:solve match% ()) (Error literal reason))",
     ["(Error literal reason)"]),
    # The same identity must survive between host calls in one activation.
    ("(= (pattern-client $subject) (let $w (pat:solve match% (((p $local) $subject))) "
     "(pat:apply $w (answer $local))))\n!(pattern-client (p a))\n!(pattern-client (p b))",
     ["(answer a)", "(answer b)"]),
    ("(= (pattern-client $solve $apply $subject) (let $w ($solve match% (((p $local) $subject))) "
     "($apply $w (answer $local))))\n!(pattern-client pat:solve pat:apply (p a))",
     ["(answer a)"]),
    ("!(let $w (pat:solve match% ((($x) (a)))) (pat:apply $w ($x $x)))", ["(a a)"]),
]

for reference in (False, True):
    for language in ("he", "petta"):
        for source, answers in cases:
            check(language, source, answers, reference=reference)

for language in ("he", "petta"):
    for call, reason in (
        ("(pat:solve unknown ())", "UnknownMatchingPolicy"),
        ("(pat:solve match% (malformed))", "ExpectedConstraintPairs"),
        ("(pat:apply wrong data)", "ExpectedMatchingWitness"),
        ("(pat:solve match% () extra)", "IncorrectNumberOfArguments"),
    ):
        fault = f"(Error {call} {reason})"
        if language == "petta" and reason == "IncorrectNumberOfArguments":
            # PeTTa's registered arity dispatch rejects over-application
            # before entering the native operation.
            fault = "(Error (domain_error (function_input_arities pat:solve (2)) 3) none)"
        check(language, "!" + call, [fault], fault=fault)
    for call in (
        "(pat:solve match% ((a a)))",
        "(pat:match% kb (p $x) (answer $x))",
        "(pat:%match kb (p a) $stored (answer $stored))",
        "(pat:is-instance (f a) (f $x))",
        "(pat:is-variant (f $x $y) (f $y $x))",
    ):
        check(language, "!" + call, [call], imported=False, renamed=True)
        # Import alone does not change a base dialect's unknown operation.
        check(language, "!" + call, [call], profile="he" if language == "he" else None,
              renamed=True)

assert variable_names('(p $x $x)') != variable_names('(p $x $y)')
assert variable_names('(text "$x")') != variable_names('(text "$y")')

# Before importing the module, these names remain ordinary authored functions.
# Calls evaluate their arguments as in the pinned base implementation.
authored = '''(= (score a) 5)
(= (score b) 3)
(= (pat:is-instance $a $b) (authored $a $b))
(= (pat:is-variant $a $b) (authored $a $b))
(= (pat:match% $space $p $body) (authored $p $body))
(= (pat:%match $space $p $row $body) (authored $p $row $body))
!(pat:is-instance (score a) (score b))
!(pat:is-variant (score a) (score b))
!(pat:match% kb (score a) (score b))
!(pat:%match kb (score a) (score b) (score a))'''
for reference in (False, True):
    for language, profile in (('he', 'he-compat'), ('he', 'he'), ('he', 'extended'),
                              ('petta', None), ('petta', 'extended')):
        check(language, authored,
              ['(authored 5 3)'] * 3 + ['(authored 5 3 5)'],
              imported=False, profile=profile, reference=reference)

print(f"PASS: {checks} matching API, demand, failure, profile and scope controls")
