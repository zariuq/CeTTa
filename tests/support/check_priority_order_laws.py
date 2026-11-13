#!/usr/bin/env python3
"""The order of a priority space is an order: laws, on atoms of every kind.

A priority space shows its order through its own operations, so the order
is read here by draining a space with space:priority:pop; a read of the space (match,
get-atoms) answers a bag and is not looked at for an order.  A space pops its
atoms in one order whatever order they were added in.  For random triples of
atoms drawn from every kind the order ranks (variables, numbers of each
representation and both zeros, strings, the empty expression, symbols,
spaces, expressions, lists stored as cons cells with an open tail or an
improper end, atoms that differ only in the names of their variables or only
in how a number is written, and atoms of more variables than an ordering key
takes), each of the six orders of adding them must pop the same three atoms
the same way.  That is antisymmetry and totality on each pair and
transitivity on each triple: an order failing one of them pops some triple
differently for two histories.

Two runs of the same program must pop alike: a space stands by the serial it
was made with, not by its address.  (A state, a capture and a foreign value
have no serial and stand by their address within a run; they are not among
the atoms here.)

On ground atoms the order is the standard order of terms, the one `msort`
sorts by: a priority space of ground atoms pops them as `msort` sorts them
(PeTTa).
"""
import itertools
import os
import random
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

GROUND = [
    "0", "1", "-1", "2", "1.0", "1.5", "-0.5", "100000000000000000000",
    "100000000000000000001", "-100000000000000000000",
    '"a"', '"b"', '"ab"', '""', "a", "b", "ab", "True", "False", "()",
    "(a)", "(a b)", "(a b c)", "(b)", "(1)", "(1 a)", "(1.0 a)", '("a" a)',
    "((a) b)", "((a b))", "(a (b c))", "(a ())", "(() a)",
]
OPEN = [
    "$x", "($x)", "($x a)", "($x $y)", "($x $x)", "(a $x)", "($y a)",
    "(($x) a)", "($x (a $y))", "($x (a $x))",
    # More variables than an ordering key takes: two that differ in their
    # last element, and one that differs from the second only in names.
    "(f %s y)" % " ".join("$a%d" % i for i in range(70)),
    "(f %s x)" % " ".join("$b%d" % i for i in range(70)),
    "(f %s x)" % " ".join("$c%d" % i for i in range(70)),
]
ZEROS = ["0.0", "-0.0"]
# PeTTa lists stored as cons cells: an open tail, improper ends, a rest
# inside an expression, a rest after an element holding a variable.  Each is
# a directive that adds it to the space NAME.
PETTA_LISTS = [
    "!(let $l (cons-atom 0 $t) (add-atom NAME $l))",
    "!(let $l (cons-atom 0 b) (add-atom NAME $l))",
    "!(let $l (cons-atom 0 1) (add-atom NAME $l))",
    "!(let $l (cons-atom (0) $t) (add-atom NAME (a $l)))",
    "!(let $l (cons-atom a (cons-atom $x $t)) (add-atom NAME $l))",
]
# Spaces made in the program's first lines, in this order.
SPACES = ["&s1", "&s2", "(k &s1)", "(k &s2)", "(&s2 a)"]
BINDS = ["!(bind! &s1 (new-space))", "!(bind! &junk (new-space))",
         "!(bind! &s2 (new-space))"]


def drain(space, count):
    """The atoms of SPACE, popped one after another, as one expression."""
    names = ["$p%d" % i for i in range(count)]
    text = "(%s)" % " ".join(names)
    for name in reversed(names):
        text = "(let %s (space:priority:pop %s) %s)" % (name, space, text)
    return text


def blank(line):
    return re.sub(r"\$[A-Za-z0-9_#-]+", "$_", line)


def run(binary, lang, lines):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write("\n".join(lines) + "\n")
        path = f.name
    try:
        p = subprocess.run([binary, "--lang", lang, "--profile", "extended", path],
                           cwd=ROOT, capture_output=True, text=True, timeout=600)
    finally:
        os.unlink(path)
    answers = {}
    for line in p.stdout.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            line = line[1:-1]
        if line.startswith("(r "):
            # HE prints a space by its address; PeTTa by its name.
            answers[int(line.split()[1])] = re.sub(r"<space 0x[0-9a-f]+>",
                                                   "<space>", line)
    return p.returncode, answers, p.stderr


def main():
    binary = os.path.abspath(sys.argv[1])
    triples = int(sys.argv[2]) if len(sys.argv) > 2 else 120
    rng = random.Random(20261111)
    checked = 0
    for lang in ("petta", "he"):
        pool = GROUND + OPEN + ZEROS + SPACES + (PETTA_LISTS if lang == "petta" else [])
        for profile in ("", " full"):
            lines, groups, tag = list(BINDS), [], 0
            for _ in range(triples):
                atoms = [rng.choice(pool) for _ in range(3)]
                tags = []
                for order in itertools.permutations(range(3)):
                    tag += 1
                    name = "&p%d" % tag
                    lines.append("!(bind! %s (new-space priority%s))" % (name, profile))
                    for i in order:
                        if atoms[i].startswith("!"):
                            lines.append(atoms[i].replace("NAME", name))
                        else:
                            lines.append("!(add-atom %s %s)" % (name, atoms[i]))
                    lines.append("!(r %d %s)" % (tag, drain(name, 3)))
                    tags.append(tag)
                groups.append((atoms, tags))
            code, first, stderr = run(binary, lang, lines)
            code2, second, _ = run(binary, lang, lines)
            if code != 0 or code2 != 0:
                print("FAIL: %s%s: exit %d: %s" % (lang, profile, code,
                      stderr.strip().splitlines()[-1:] or ""))
                return 1
            if first != second:
                print("FAIL: %s%s: two runs of one program pop differently" % (lang, profile))
                return 1
            for atoms, tags in groups:
                listings = {blank(first.get(t, "")).split(" ", 2)[2] for t in tags}
                if len(listings) != 1 or "" in listings:
                    print("FAIL: %s%s: the atoms %s pop differently by history:"
                          % (lang, profile, " ".join(atoms)))
                    for t in tags:
                        print("  ", first.get(t))
                    return 1
                checked += 1
    # Ground atoms: the standard order of terms, as msort has it.
    lines, tag, expected_pairs = [], 0, []
    for _ in range(triples):
        atoms = [rng.choice(GROUND) for _ in range(5)]
        tag += 1
        name = "&g%d" % tag
        lines.append("!(bind! %s (new-space priority))" % name)
        for atom in atoms:
            lines.append("!(add-atom %s %s)" % (name, atom))
        lines.append("!(r %d %s)" % (tag, drain(name, len(atoms))))
        tag += 1
        lines.append("!(r %d (msort (%s)))" % (tag, " ".join(atoms)))
        expected_pairs.append((tag - 1, tag, atoms))
    code, answers, stderr = run(binary, "petta", lines)
    if code != 0:
        print("FAIL: msort comparison: exit %d: %s" % (code, stderr.strip().splitlines()[-1:]))
        return 1
    for listed, sorted_tag, atoms in expected_pairs:
        a = answers.get(listed, "").split(" ", 2)[-1]
        b = answers.get(sorted_tag, "").split(" ", 2)[-1]
        if a != b:
            print("FAIL: the ground atoms %s: a priority space pops %s, msort %s"
                  % (" ".join(atoms), a, b))
            return 1
        checked += 1
    print("PASS: a priority space pops its atoms one way whatever their history, the "
          "same in two runs, and ground atoms as msort sorts them: %d groups of atoms "
          "of every kind, strict and full, PeTTa and HE" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
