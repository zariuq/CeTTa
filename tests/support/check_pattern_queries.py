#!/usr/bin/env python3
"""Directional queries retain joint permissions, rows and occurrence bags."""
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


def run(language, source, *, imported=True, profile="extended", reference=False,
        environment=None):
    env = dict(os.environ)
    env["CETTA_OPEN_EQUATIONS_REFERENCE"] = str(int(reference))
    env["CETTA_HE_SHARED_EXECUTION"] = str(int(not reference))
    env.update(environment or {})
    command = [binary, "--lang", language]
    if profile:
        command += ["--profile", profile]
    if imported:
        source = "!(import! &self pat)\n" + source
    return subprocess.run(command + ["-e", source], env=env, capture_output=True,
                          text=True, timeout=40)


def check(language, source, answers, **options):
    global checks
    result = run(language, source, **options)
    values = (["()" if language == "he" else "true"]
              if options.get("imported", True) else []) + answers
    expected = "".join(f"[{v}]\n" if language == "he" else v + "\n" for v in values)
    assert (result.returncode, result.stdout, result.stderr) == (0, expected, ""), (
        language, source, result.returncode, result.stdout, result.stderr, expected)
    checks += 1


def images(query, template):
    return f"(collapse (let (pat:hit $rows $w) {query} (pat:apply $w {template})))"


for language in ("he", "petta"):
    for reference in (False, True):
        options = dict(reference=reference)
        rows = "(edge $stored b)\n(edge a b)\n(edge a b)\n"
        check(language, rows + "!" + images("(pat:query &self match% (edge a $y))", "$y"),
              ["(b b)"], **options)
        check(language, rows + "!(collapse (match &self (edge a $y) $y))", ["(b b b)"], **options)
        for policy in ("%match", "unify"):
            check(language, rows + "!" + images(f"(pat:query &self {policy} (edge a b))", "hit"),
                  ["(hit hit hit)"], **options)
        check(language, rows + "!" + images("(pat:query &self variant (edge $q b))", "hit"),
              ["(hit)"], **options)
        check(language, "(p a) (p b) (q a) (q b)\n!" +
              images("(pat:query &self match% (, (p $x) (q $x)))", "$x"), ["(a b)"], **options)
        check(language, "(p a) (q b)\n!" +
              images("(pat:query &self match% (, (p $x) (q $x)))", "$x"), ["()"], **options)
        # A protected variable captured at an earlier leg never becomes flexible.
        for family in ("(, (p $x) (q $x))", "(, (q $x) (p $x))"):
            check(language, "(p $u) (q a)\n!" +
                  images(f"(pat:query &self match% {family})", "bad"), ["()"], **options)
        check(language, "(p $u) (q a)\n!" +
              images("(pat:query &self unify (, (p $x) (q $x)))", "$x"), ["(a)"], **options)
        # Repeated query variables constrain a global bijection across rows.
        check(language, "(p $u) (q $v)\n!" +
              images("(pat:query &self variant (, (p $x) (q $x)))", "bad"), ["()"], **options)
        check(language, "(p $u) (q $v)\n!" +
              images("(pat:query &self variant (, (p $x) (q $y)))", "hit"), ["(hit)"], **options)
        check(language, "(p a) (p a) (q a) (q a) (q a)\n!" +
              images("(pat:query &self match% (, (p $x) (q $x)))", "$x"),
              ["(a a a a a a)"], **options)
        check(language, "!" + images("(pat:query &self match% (,))", "unit"), ["(unit)"], **options)
        check(language, "(f $u $u) (f $u $v)\n!" +
              images("(pat:query &self match% (f $x $x))", "hit"), ["(hit)"], **options)
        check(language, "(f $u $u) (f $u $v)\n!" +
              images("(pat:query &self %match (f a b))", "hit"), ["(hit)"], **options)
        check(language, "(= (danger) (trace! forbidden boom))\n(row (danger))\n!" +
              images("(pat:query &self match% (row $x))", "($x $x)"),
              ["(((danger) (danger)))"], **options)
        # The selected original and instantiated rows are both usable.
        check(language, "(edge $stored b)\n!(collapse (let "
              "(pat:hit ((pat:row $id $raw $view)) $w) (pat:query &self %match (edge a b)) "
              "(pat:apply $w $raw)))", ["((edge a b))"], **options)
        check(language, "(p a) (q a)\n(= (client $ignored) (let (pat:hit $rows $w) "
              "(pat:query &self match% (, (p $x) (q $x))) (pat:apply $w (answer $x))))\n"
              "!(client first)\n!(client second)", ["(answer a)", "(answer a)"], **options)

    # The syntax is licensed by import AND profile.
    call = "(pat:query &self match% (p a))"
    check(language, "!" + call, [call], imported=False)
    # HE's imported type declaration evaluates its space operand even when
    # the unknown head remains data; PeTTa keeps the named-space symbol.
    inert = call.replace("&self", "ModuleSpace(GroundingSpace-top)") if language == "he" else call
    check(language, "!" + call, [inert], profile="he" if language == "he" else None)

    # Equal rows remain distinguishable occurrences, stable within this read.
    result = run(language, "(row a) (row a)\n!(pat:query &self match% (row a))")
    assert result.returncode == 0 and not result.stderr, result
    ids = re.findall(r"\(pat:occurrence (\d+) (\d+) (\d+)\)", result.stdout)
    assert len(ids) == 2 and ids[0][:2] == ids[1][:2] and ids[0][2] != ids[1][2], ids
    checks += 1

print(f"PASS: {checks} directional query, witness, scope and occurrence controls")
