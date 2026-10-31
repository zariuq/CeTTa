#!/usr/bin/env python3
"""A lazy match's stored rows answer as decoded rows do.

Usage: check_oem_stored_rows.py CETTA_BINARY [--programs N] [--seed S]

The open equation machine reads the rows of a space larger than the match
trie's threshold by their ids, and unifies a pattern with a row's record
part by part; with CETTA_OEM_STORED_REFERENCE=1 it decodes each such row
and unifies the pattern with the decoded atom, as it read every row before.
Each generated program fills its space with 20 to 60 rows: symbols,
integers, strings and a float, constructor terms of mixed depth, pairs
repeating a part, duplicate rows, and rows holding variables, alone,
repeated, nested and shared across parts.  Functions whose bodies match
the space are called and collected: patterns cut from the rows' own
shapes, fixing symbols, integers, strings and nested parts, with variables
repeated within a part and across parts; a part the caller passes in,
ground, partly ground or a variable; and matches nested in matches.  Both
runs read with the native query path off, so every match is the machine's
own; each collection compares as a bag under a consistent variable renaming.
Sharing, literal strings and ordered fields stay exact.  A program the decoded reading cannot run fails.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile
from compare_answers import CollectionTrace

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SYMBOLS = ("a", "b", "c")
UNARY = ("M", "W", "C")
VARIABLES = ("$p", "$q", "$r")


def leaf(rng):
    kind = rng.random()
    if kind < 0.45:
        return rng.choice(SYMBOLS)
    if kind < 0.85:
        return str(rng.randrange(4))
    if kind < 0.97:
        return rng.choice(('"s"', '"t"'))
    return "1.5"


def term(rng, depth):
    """A ground term as a tree: a leaf string or (head, children)."""
    if depth == 0 or rng.random() < 0.3:
        return leaf(rng)
    if rng.random() < 0.6:
        return (rng.choice(UNARY), [term(rng, depth - 1)])
    return ("pair", [term(rng, depth - 1), term(rng, depth - 1)])


def show(tree):
    if isinstance(tree, str):
        return tree
    head, children = tree
    return "(" + " ".join([head] + [show(c) for c in children]) + ")"


def open_term(rng, tree, names, rate):
    """TREE with some parts turned into variables from NAMES, a name
    possibly used twice; the head of a constructor stays."""
    if rng.random() < rate:
        return rng.choice(names)
    if isinstance(tree, str):
        return tree
    head, children = tree
    return "(" + " ".join([head] + [open_term(rng, c, names, rate)
                                    for c in children]) + ")"


def rows(rng):
    trees = []
    lines = []
    for _ in range(rng.randrange(20, 61)):
        kind = rng.random()
        if kind < 0.45:
            t = term(rng, rng.randrange(1, 5))
            trees.append(("num", [t]))
            lines.append(f"(num {show(t)})")
        elif kind < 0.8:
            t1, t2 = term(rng, rng.randrange(0, 3)), term(rng, rng.randrange(0, 3))
            if rng.random() < 0.2:
                t2 = t1
            trees.append(("rel", [t1, t2]))
            lines.append(f"(rel {show(t1)} {show(t2)})")
        else:
            t1, t2 = term(rng, rng.randrange(0, 3)), term(rng, rng.randrange(0, 3))
            s = rng.choice(SYMBOLS)
            trees.append(("tri", [t1, s, t2]))
            lines.append(f"(tri {show(t1)} {s} {show(t2)})")
    lines += lines[: rng.randrange(4)]
    # rows holding variables: alone, repeated, nested, shared across parts
    opened = ["(num (M $v))", "(rel $x $x)", "(rel (pair $a b) $a)",
              "(num $z)", "(tri $p c (M $p))", "(rel (W $u) (pair $u 1))"]
    lines += rng.sample(opened, rng.randrange(0, 4))
    return trees, lines


def pattern(rng, trees):
    """A pattern cut from a stored row's shape, and its variables."""
    head, parts = rng.choice(trees)
    rate = rng.choice((0.15, 0.3, 0.5))
    names = list(VARIABLES[: rng.randrange(1, 4)])
    shown = []
    for position, part in enumerate(parts):
        if head == "tri" and position == 1:
            # the relation's middle symbol, kept most of the time
            shown.append(part if rng.random() < 0.7 else rng.choice(names))
        else:
            shown.append(open_term(rng, part, names, rate))
    text = "(" + " ".join([head] + shown) + ")"
    used = [n for n in names if n in text]
    return text, used


def template(rng, used):
    if not used or rng.random() < 0.1:
        return "hit"
    if len(used) == 1 or rng.random() < 0.3:
        return rng.choice(used)
    return "(" + " ".join(used) + ")"


def program(rng):
    trees, lines = rows(rng)
    calls = []
    for index in range(rng.randrange(3, 8)):
        kind = rng.random()
        if kind < 0.5:
            text, used = pattern(rng, trees)
            lines.append(f"(= (q{index}) (match &self {text} {template(rng, used)}))")
            calls.append(f"(q{index})")
        elif kind < 0.75:
            # a part the caller passes in: ground, partly ground, a variable
            head, parts = rng.choice([t for t in trees if t[0] == "rel"] or trees)
            if head != "rel":
                text, used = pattern(rng, trees)
                lines.append(f"(= (q{index}) (match &self {text} {template(rng, used)}))")
                calls.append(f"(q{index})")
                continue
            lines.append(f"(= (q{index} $k) (match &self (rel $k $y) ($k $y)))")
            first = parts[0]
            calls.append(f"(q{index} {show(first)})")
            calls.append(f"(q{index} {open_term(rng, first, ['$u'], 0.4)})")
            calls.append(f"(q{index} $u)")
        else:
            # a match nested in a match, sharing a variable
            lines.append(f"(= (q{index}) (match &self (rel $x $y) "
                         f"(match &self (num {rng.choice(('$y', '(M $y)', '(pair $y $w)'))}) ($x $y))))")
            calls.append(f"(q{index})")
    for call in calls:
        lines.append(f"!(sort-atom (collapse {call}))")
    return "\n".join(lines) + "\n"


def run(binary, path, reference):
    env = dict(os.environ)
    env["CETTA_NATIVE_QUERY_REFERENCE"] = "1"
    env.pop("CETTA_OEM_STORED_REFERENCE", None)
    if reference:
        env["CETTA_OEM_STORED_REFERENCE"] = "1"
    p = subprocess.run([binary, "--profile", "extended", "--lang", "petta", path],
                       cwd=ROOT, env=env, capture_output=True, text=True,
                       timeout=120)
    return p.returncode, CollectionTrace(p.stdout, ordered_lines=range(len(p.stdout.splitlines())) if p.returncode else ())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--programs", type=int, default=200)
    parser.add_argument("--seed", type=int, default=20261030)
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    rng = random.Random(args.seed)
    with tempfile.TemporaryDirectory(prefix="cetta-oem-stored-") as work:
        for number in range(args.programs):
            source = program(rng)
            path = os.path.join(work, f"stored_{number}.metta")
            with open(path, "w") as f:
                f.write(source)
            stored = run(binary, path, False)
            decoded = run(binary, path, True)
            if decoded[0] != 0:
                print(f"FAIL: program {number} does not run reading rows decoded")
                print(source)
                print("decoded:", decoded)
                return 1
            if stored != decoded:
                print(f"FAIL: program {number} answers differently reading stored rows")
                print(source)
                print("stored:", stored)
                print("decoded:", decoded)
                return 1
    print(f"PASS: {args.programs} programs answer alike reading a lazy match's "
          f"rows stored and decoded")
    return 0


if __name__ == "__main__":
    sys.exit(main())
