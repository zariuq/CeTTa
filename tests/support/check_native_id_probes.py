#!/usr/bin/env python3
"""A native join's legs read the same rows by ids as by their probes.

Usage: check_native_id_probes.py CETTA_BINARY [--programs N] [--seed S]

A flat leg of a native join asks its space for candidate rows by the ids
of its columns; the space answers from its exact index for a ground leg,
and from a prefix index for bound columns followed by free ones, once the
space holds more rows than a match reads one by one and has spent its cold
scans.  Each generated program fills its space with 40 to 150 rows of
binary and ternary relations over symbols, integers, strings and truth
values, with duplicate rows, rows holding variables under some heads, and
removals between the queries, which leave rows marked when a later join
reads the space.  Its six to twelve queries are joins of two to four legs
whose later legs bind a prefix or every column, with fixed atoms and
repeated variables, collected sorted and counted.  Each program runs on
the native path and with CETTA_NATIVE_QUERY_REFERENCE=1, where no query is
native. Collections compare as bags under a consistent variable renaming;
sharing, literal strings and ordered fields stay exact.  A program the reference path cannot run
fails.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile
from compare_answers import CollectionTrace

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = ("R", "S")
TERNARY = ("T", "U")


def value(rng, width):
    kind = rng.random()
    if kind < 0.55:
        return f"s{rng.randrange(width)}"
    if kind < 0.85:
        return str(rng.randrange(width))
    if kind < 0.95:
        return rng.choice(('"u"', '"v"'))
    return rng.choice(("True", "False"))


def rows(rng):
    width = rng.randrange(3, 9)
    lines = []
    facts = {head: [] for head in BINARY + TERNARY}
    for _ in range(rng.randrange(40, 151)):
        head = rng.choice(BINARY + TERNARY)
        arity = 2 if head in BINARY else 3
        row = tuple(value(rng, width) for _ in range(arity))
        facts[head].append(row)
        lines.append("(" + " ".join((head,) + row) + ")")
    lines += lines[: rng.randrange(6)]
    # rows holding variables, under some heads only, so that other heads'
    # ground legs read the exact index and theirs may not
    opened = ["(S $v $v)", "(S s0 $w)", "(U $p s1 $p)", "(U s2 $q 1)",
              "(R $x $y)"]
    lines += rng.sample(opened, rng.randrange(0, 3))
    return width, facts, lines


def column(rng, width, bound, used):
    """A leg's column: a variable bound by an earlier leg, a fresh one, or
    an atom."""
    kind = rng.random()
    if bound and kind < 0.55:
        name = rng.choice(bound)
    elif kind < 0.8:
        name = f"$v{len(used)}"
    else:
        return value(rng, width)
    used.append(name)
    return name


def leg(rng, width, bound, used):
    head = rng.choice(BINARY + TERNARY)
    arity = 2 if head in BINARY else 3
    parts = [column(rng, width, bound, used) for _ in range(arity)]
    if rng.random() < 0.15 and arity == 3:
        parts[2] = parts[0]
    return "(" + " ".join([head] + parts) + ")"


def query(rng, width):
    used = []
    first = leg(rng, width, [], used)
    legs = [first]
    for _ in range(rng.randrange(1, 4)):
        bound = sorted(set(used))
        legs.append(leg(rng, width, bound, used))
    names = sorted(set(used))
    template = ("(" + " ".join(names) + ")") if names else "hit"
    body = template
    for pattern in reversed(legs):
        body = f"(match &self {pattern} {body})"
    return body


def program(rng):
    width, facts, lines = rows(rng)
    statements = []
    for _ in range(rng.randrange(6, 13)):
        if rng.random() < 0.15:
            head = rng.choice([h for h in facts if facts[h]] or ["R"])
            if facts.get(head):
                row = rng.choice(facts[head])
                statements.append("!(remove-atom &self (" +
                                  " ".join((head,) + row) + "))")
        q = query(rng, width)
        statements.append(f"!(sort-atom (collapse {q}))")
        if rng.random() < 0.3:
            statements.append(f"!(size-atom (collapse {q}))")
    return "\n".join(lines + statements) + "\n"


def run(binary, path, reference):
    env = dict(os.environ)
    env.pop("CETTA_NATIVE_QUERY_REFERENCE", None)
    if reference:
        env["CETTA_NATIVE_QUERY_REFERENCE"] = "1"
    p = subprocess.run([binary, "--profile", "extended", "--lang", "petta", path],
                       cwd=ROOT, env=env, capture_output=True, text=True,
                       timeout=120)
    return p.returncode, CollectionTrace(p.stdout, ordered_lines=range(len(p.stdout.splitlines())) if p.returncode else ())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--programs", type=int, default=200)
    parser.add_argument("--seed", type=int, default=34)
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    with tempfile.TemporaryDirectory(prefix="native-id-probes-") as tmp:
        for index in range(args.programs):
            rng = random.Random(args.seed * 100003 + index)
            text = program(rng)
            path = os.path.join(tmp, f"p{index}.metta")
            with open(path, "w") as handle:
                handle.write(text)
            ref_code, reference = run(binary, path, True)
            if ref_code != 0:
                print(f"FAIL: program {index}: the reference path exits {ref_code}")
                print(text)
                return 1
            code, native = run(binary, path, False)
            if code != 0 or native != reference:
                print(f"FAIL: program {index}: the native path answers otherwise")
                print(text)
                ref_lines = reference.splitlines()
                nat_lines = native.splitlines()
                for i in range(max(len(ref_lines), len(nat_lines))):
                    r = ref_lines[i] if i < len(ref_lines) else "<none>"
                    n = nat_lines[i] if i < len(nat_lines) else "<none>"
                    if r != n:
                        print(f"  line {i}: reference {r[:300]}")
                        print(f"  line {i}: native    {n[:300]}")
                        break
                return 1
    print(f"PASS: {args.programs} programs answer alike on the native and "
          "reference paths")
    return 0


if __name__ == "__main__":
    sys.exit(main())
