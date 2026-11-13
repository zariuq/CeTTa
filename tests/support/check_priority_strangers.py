#!/usr/bin/env python3
"""Reads, takes and removals of a space whatever its rows hold.

A PeTTa list is stored in several forms: a plain expression, cons cells with
an open tail or an improper end, a list value, a list pattern with a rest.
A partial application is a value that is no list.  A priority space's order
reads each form as the value it spells, so a search places every row however
it is stored.  The general indexes of an atom space (the trie, the path and
head indexes, the native query engine) read a row stored as cells as standing
for anything at its place, and leave it to the matcher.  A row with more
variables than an ordering key names, or with a variable deeper than one
reads, is ordered without a key.

The store holds plain atoms, atoms with variables, keys that stand after a
variable, and odd rows in counts with no limit: none, one, 17, 100.  They
stand under the heads the patterns ask for and away from them; a row is
itself an open list; a rest stands where an inner list ends and where the
list holding it ends at the same node.  Odd rows are added and removed
between reads.  Every directive is run on a strict priority space, a full
one, an atom space with its general indexes and a strict atom space, each as
it ships and with a collection at every opportunity.  The reference is a
strict atom space read with the native query engine off: every row read by
the matcher.  Every answer is a bag: each must equal the reference's,
variables named by first occurrence, since a run names them.
"""
import os
import random
import re
import subprocess
import sys
import tempfile

from space_run_modes import run_both

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

ODD_COUNTS = (0, 1, 17, 100)


def petta_odd(rng):
    key = rng.randrange(4)
    return rng.choice([
        # An open tail under a key the patterns ask for.
        "!(let $l (cons-atom %d $t) (add-atom &p (zz $l)))" % key,
        # An open list, then a key after it.
        "!(let $l (cons-atom %d (cons-atom 2 $t)) (add-atom &p (k $l 5)))" % key,
        # A row that is itself an open list, and one whose rest stands
        # where the outer list ends, after an inner list.
        "!(let $l (cons-atom zz (cons-atom %d $t)) (add-atom &p $l))" % key,
        "!(let $l (cons-atom job (cons-atom (n (%d)) $t)) (add-atom &p $l))" % key,
        # A rest where an inner list ends, and one where its own list
        # ends at the same node after it.
        "!(let $l (cons-atom %d $t) (add-atom &p (job (n $l))))" % key,
        "!(let $l (cons-atom (%d) $t) (add-atom &p (job (n $l))))" % key,
        # Improper ends: an atom, a number.
        "!(let $l (cons-atom %d b) (add-atom &p (zz $l)))" % key,
        "!(let $l (cons-atom %d 7) (add-atom &p (zz $l)))" % key,
        # A partial application holding a variable.
        "!(let $f (+ %d) (add-atom &p (job $f $x)))" % key,
    ])


def wide(head):
    return "!(add-atom &p (%s %s))" % (head, " ".join("$v%d" % i for i in range(70)))


def deep(head):
    term = "$x"
    for _ in range(600):
        term = "(d %s)" % term
    return "!(add-atom &p (%s %s))" % (head, term)


def keyless(rng):
    return rng.choice([wide("job"), wide("zz"), deep("job"), deep("zz")])


def program(rng, lang, kind, size, odd):
    new = "(new-space)" if kind == "atom" else "(new-space %s)" % kind
    lines = ["!(import! &self (library lib_transform))", "!(bind! &p %s)" % new]
    store = []
    for i in range(size):
        roll = rng.random()
        if roll < 0.4:
            store.append("(job %d)" % rng.randrange(size))
        elif roll < 0.55:
            store.append("(a %d)" % i)
        elif roll < 0.7:
            store.append("(k %s %d)" % (rng.choice("abc"), rng.randrange(size)))
        elif roll < 0.8:
            store.append("(zz (%d %d))" % (rng.randrange(4), rng.randrange(4)))
        elif roll < 0.9:
            store.append("(job $x %d)" % rng.randrange(8))
        else:
            store.append("(k $y %d)" % rng.randrange(8))
    for atom in store:
        lines.append("!(add-atom &p %s)" % atom)

    def odd_row():
        return petta_odd(rng) if lang == "petta" and rng.random() < 0.85 else keyless(rng)

    for _ in range(odd):
        lines.append(odd_row())
    key = rng.randrange(4)
    reads = ["(job $j)", "(job $a $b)", "(k $y 5)", "(k a $n)", "(zz $z)",
             "(job %d)" % rng.randrange(size), "(k $y %d)" % rng.randrange(8),
             "($h 3)", "(a %d)" % rng.randrange(size),
             "(zz (%d $w))" % key, "(zz (%d 7 8))" % key, "(zz (%d))" % key,
             "(zz %d $u)" % key, "(k (%d 2 $q) 5)" % key, "(k (%d 2) 5)" % key,
             "(job (n (%d)))" % key, "(job (n (%d $u)))" % key, "(job (n $m))",
             "(job (n ((%d))))" % key, "(job (n ((%d) $u)))" % key]
    tag = 0
    for step in range(20):
        tag += 1
        roll = rng.random()
        pattern = rng.choice(reads)
        if roll < 0.35:
            lines.append("!(r %d (collapse (match &p %s %s)))" % (tag, pattern, pattern))
        elif roll < 0.45:
            lines.append("!(r %d (size-atom (collapse (match &p %s seen))))" % (tag, pattern))
        elif roll < 0.55 and lang == "petta":
            # Patterns that are lists stored otherwise: open, improper.
            if rng.random() < 0.5:
                lines.append("!(r %d (let $q (cons-atom %d $s) (collapse (match &p (zz $q) $s))))"
                             % (tag, key))
            else:
                lines.append("!(r %d (let $q (cons-atom %d b) (collapse (match &p (zz $q) $q))))"
                             % (tag, key))
        elif roll < 0.65:
            ground = rng.choice([a for a in store if "$" not in a])
            lines.append("!(r %d (collapse (transform &p (take %s) took)))" % (tag, ground))
        elif roll < 0.75:
            lines.append("!(r %d (remove-atom &p %s))" % (tag, rng.choice(store + [pattern])))
        elif roll < 0.85 and odd:
            # Churn: an odd row joins between reads.
            lines.append(odd_row())
            tag -= 1
        else:
            lines.append("!(r %d (size-atom (collapse (get-atoms &p))))" % tag)
    tag += 1
    lines.append("!(r %d (collapse (get-atoms &p)))" % tag)
    return "\n".join(lines) + "\n"


def run_once(binary, lang, text, mode_env):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        env = dict(os.environ, **mode_env)
        p = subprocess.run([binary, "--lang", lang, "--profile", "extended", path],
                           cwd=ROOT, env=env, capture_output=True, text=True, timeout=600)
    finally:
        os.unlink(path)
    lines = {}
    for line in p.stdout.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            line = line[1:-1]
        if line.startswith("(r "):
            lines[int(line.split()[1])] = line
    return p.returncode, lines, p.stderr


def parts(text):
    """The top-level elements of the expression TEXT."""
    inner = text.strip()[1:-1]
    out, depth, cur, quoted = [], 0, "", False
    for ch in inner:
        if ch == '"':
            quoted = not quoted
        if not quoted and ch == "(":
            depth += 1
        if not quoted and ch == ")":
            depth -= 1
        if not quoted and depth == 0 and ch.isspace():
            if cur:
                out.append(cur)
            cur = ""
            continue
        cur += ch
    if cur:
        out.append(cur)
    return out


def renamed(text):
    names = {}
    return re.sub(r"\$[^\s()]+",
                  lambda m: names.setdefault(m.group(0), "$_%d" % len(names)), text)


def bag(line):
    """A directive's answer as a bag: the elements of its collapse, each
    with its variables named by first occurrence."""
    fields = parts(line)
    if len(fields) < 3 or not fields[2].startswith("("):
        return tuple(fields[2:])
    return tuple(sorted(renamed(e) for e in parts(fields[2])))


def run(binary, lang, text, extra=None):
    return run_both(lambda mode_env: run_once(binary, lang, text,
                                              dict(mode_env, **(extra or {}))),
                    same=lambda a, b: bag(a) == bag(b))


KINDS = ("priority", "priority full", "atom", "atom strict")
REFERENCE = ("atom strict", {"CETTA_NATIVE_QUERY_REFERENCE": "1"})


def main():
    binary = os.path.abspath(sys.argv[1])
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    checked = 0
    for lang in ("petta", "he"):
        for seed in range(rounds):
            for size in (24, 400):
                for odd in ODD_COUNTS:
                    outputs = {}
                    for kind, extra in [(k, None) for k in KINDS] + [REFERENCE]:
                        rng = random.Random((seed * 7919 + size) * 131 + odd)
                        text = program(rng, lang, kind, size, odd)
                        code, lines, stderr = run(binary, lang, text, extra)
                        if code != 0:
                            print("FAIL: %s seed %d size %d odd %d, %s space: exit %d: %s" %
                                  (lang, seed, size, odd, kind, code,
                                   stderr.strip().splitlines()[-1:] or ""))
                            for tag in sorted(lines):
                                if "|| with" in lines[tag]:
                                    print("  ", lines[tag][:400])
                                    break
                            return 1
                        outputs[(kind, bool(extra))] = lines
                    atom = outputs[(REFERENCE[0], True)]
                    for kind in KINDS:
                        got = outputs[(kind, False)]
                        for tag in sorted(atom):
                            if tag not in got or bag(got[tag]) != bag(atom[tag]):
                                print("FAIL: %s seed %d size %d odd %d, answer %d: "
                                      "%s space and the reference differ" %
                                      (lang, seed, size, odd, tag, kind))
                                print("  %s:" % kind, (got.get(tag) or "")[:400])
                                print("  atom    :", atom[tag][:400])
                                return 1
                            checked += 1
    print("PASS: priority and atom spaces holding rows of every stored form, in counts "
          "up to 100, answer reads, takes and removals as a space read row by row does, "
          "strict and full, as shipped and collecting at every opportunity: %d answers, "
          "PeTTa and HE" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
