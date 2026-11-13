#!/usr/bin/env python3
"""Removal finds the atom its dialect's equality finds, in every kind of space.

HE compares numbers by value: 1, 1.0 and 2/2 are different atoms that are
equal, so (remove-atom S (k 1)) removes a stored (k 1.0), and a transform's
(take (k 1)) takes it.  PeTTa compares them as terms: (k 1) and (k 1.0) are
different, and neither removes the other.  A kind of space that finds equal
rows through an index of its own (a priority space's order, an exact index)
must give the same answers as the plain atom space, which reads its rows.

Each case runs on every kind of space, strict ones among them, in both
dialects, and is compared with the plain atom space of the same dialect: the answer of
the operation, and the atoms left, as bags.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

KINDS = [
    ("atom", "(new-space)"),
    ("set", "(new-space set)"),
    ("queue", "(new-space queue)"),
    ("stack", "(new-space stack)"),
    ("priority", "(new-space priority)"),
    ("atom strict", "(new-space atom strict)"),
    ("queue strict", "(new-space queue strict)"),
    ("priority strict", "(new-space priority strict)"),
]

# What is stored beside (j 2) and (k 7), and what is asked for.
STORED = ["(k 1.0)", "(k 1)", "(k 2)", "((w 1.0) c)", "((w 1) c)", "(k -0.0)",
          "(k 3 1.0)", "(k (f 1.0))"]
ASKED = ["(k 1)", "(k 1.0)", "(k 2.0)", "((w 1) c)", "((w 1.0) c)", "(k 0)",
         "(k 3 1)", "(k (f 1))", "(k 7)", "(j 2.0)"]
OPERATIONS = [
    ("remove-atom", "(remove-atom &S A)"),
    ("take", "(collapse (transform &S (take A) took))"),
    ("unique take", "(collapse (transform &S (unique (take A)) took))"),
    ("match", "(collapse (match &S A found))"),
]


def program(new, dialect):
    lines = []
    if dialect == "petta":
        lines.append("!(import! &self (library lib_transform))")
    names = []
    n = 0
    for stored in STORED:
        for asked in ASKED:
            for op, form in OPERATIONS:
                space = "&n%d" % n
                n += 1
                lines.append("!(bind! %s %s)" % (space, new))
                for atom in (stored, "(j 2)", "(k 7)"):
                    lines.append("!(add-atom %s %s)" % (space, atom))
                lines.append("!(r %d %s)" % (n, form.replace("&S", space).replace("A", asked)))
                lines.append("!(l %d (collapse (get-atoms %s)))" % (n, space))
                names.append("%s of %s over %s" % (op, asked, stored))
    return lines, names


def bag(text):
    """A printed list's elements, sorted: (a (b c) d) -> ['(b c)', 'a', 'd']."""
    text = text.strip()
    if not (text.startswith("(") and text.endswith(")")):
        return text
    items, depth, start = [], 0, None
    inner = text[1:-1]
    for i, ch in enumerate(inner):
        if ch == "(":
            if depth == 0 and start is None:
                start = i
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                items.append(inner[start:i + 1])
                start = None
        elif ch.isspace():
            if depth == 0 and start is not None:
                items.append(inner[start:i])
                start = None
        elif start is None:
            start = i
    if start is not None:
        items.append(inner[start:])
    return sorted(items)


def run(binary, dialect, profile, new):
    lines, names = program(new, dialect)
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write("\n".join(lines) + "\n")
        path = f.name
    try:
        p = subprocess.run([binary, "--lang", dialect, "--profile", profile, path],
                           cwd=ROOT, capture_output=True, text=True, timeout=600)
    finally:
        os.unlink(path)
    answers = {}
    for line in p.stdout.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            line = line[1:-1]
        if line.startswith("(r ") or line.startswith("(l "):
            tag, number, rest = line[1:-1].split(" ", 2)
            answers[(tag, int(number))] = rest if tag == "r" else bag(rest)
    return answers, names, p.stderr.strip().splitlines()[-1:]


def main():
    binary = os.path.abspath(sys.argv[1])
    checked = 0
    for dialect in ("he", "petta"):
        reference, names, error = run(binary, dialect, "extended", KINDS[0][1])
        if len(reference) != 2 * len(names):
            print("FAIL: %s: the atom space answered %d of %d lines: %s"
                  % (dialect, len(reference), 2 * len(names), error))
            return 1
        for kind, new in KINDS[1:]:
            answers, _, error = run(binary, dialect, "extended", new)
            for key in sorted(reference):
                if answers.get(key) != reference[key]:
                    print("FAIL: %s, %s space: %s: %s"
                          % (dialect, kind, names[key[1] - 1],
                             "the answer" if key[0] == "r" else "the atoms left"))
                    print("  atom space:", reference[key])
                    print("  %s space:" % kind, answers.get(key), error)
                    return 1
                checked += 1
    # The dialects' own laws, on the plain atom space: HE removes by value,
    # PeTTa by term.
    for dialect, left in (("he", ["(j 2)", "(k 7)"]),
                          ("petta", ["(j 2)", "(k 1.0)", "(k 7)"])):
        answers, names, error = run(binary, dialect, "extended", KINDS[0][1])
        at = names.index("remove-atom of (k 1) over (k 1.0)") + 1
        if answers.get(("l", at)) != left:
            print("FAIL: %s: (remove-atom S (k 1)) over a stored (k 1.0) left %s, "
                  "wanted %s %s" % (dialect, answers.get(("l", at)), left, error))
            return 1
    print("PASS: removal, takes and matches of numbers agree with the atom space "
          "in every kind of space: %d answers" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
