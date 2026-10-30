#!/usr/bin/env python3
"""A match collected whole answers the same bag on the native query path as
on the ordinary one, and leaves the same spaces behind.

Usage: check_native_query.py CETTA_BINARY [--programs N] [--seed S] [--stats]

Each generated program holds a few small relations, with duplicate rows,
loops and symbols, and collects matches over them: conjunctions of one to
four patterns sharing variables, repeating a variable, fixing a constant;
in some programs binary relations without duplicate rows, which the native
path joins by rows of bits, and conjunctions over them alone; in some, rows
holding constructor patterns, which meet ground cells one way;
templates that are data, nested data, a constant or a variable, or that add
data into, or remove it from, a set space, a bag space or the matched space
itself, alone or in let and let* sequences, some of which remove what others
add; at top level, through a function whose one equation is the match, or
inside a function's body.  Rows holding variables or floats, and patterns
over nested open terms, are among them: the native path declines those,
and they must answer the same.  Every
program runs twice, with CETTA_NATIVE_QUERY_REFERENCE=1 the second time, and
every collection is printed sorted.  Atoms holding variables print with
fresh names in an order that follows them, so the outputs are compared with
every variable named alike and every printed collection's elements sorted.
With --stats, the binary is a runtime-stats build, and the native path must
also have answered at least one collection per program on average, and
joined by rows of bits in at least one program in five, rows it builds for
the join or the pairs of a relation the space keeps as bits, so that the
agreement is not vacuous.
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SYMBOLS = ("a", "b", "c")


def value(rng, nodes):
    return str(rng.randrange(nodes)) if rng.random() < 0.8 else rng.choice(SYMBOLS)


def facts(rng, nodes, rows):
    lines = []
    for _ in range(rows):
        kind = rng.random()
        if kind < 0.55:
            lines.append(f"(edge {value(rng, nodes)} {value(rng, nodes)})")
        elif kind < 0.8:
            lines.append(f"(lab {value(rng, nodes)} {rng.choice(SYMBOLS)} {value(rng, nodes)})")
        else:
            lines.append(f"(mark {value(rng, nodes)})")
    lines += lines[: rng.randrange(4)]
    if rng.random() < 0.15:
        lines.append("(edge $open 1)")
    if rng.random() < 0.15:
        lines.append(f"(mark {rng.randrange(nodes)}.5)")
    return lines


VARIABLES = ("$x", "$y", "$z", "$w")


def binary_facts(rng, nodes):
    """Two binary relations, each row once."""
    rows = set()
    for _ in range(rng.randrange(3, 4 * nodes + 4)):
        head = "link" if rng.random() < 0.3 else "edge"
        rows.add(f"({head} {value(rng, nodes)} {value(rng, nodes)})")
    lines = sorted(rows)
    rng.shuffle(lines)
    return lines


def binary_conjunction(rng, nodes):
    used = set()
    def term():
        if used and rng.random() < 0.6:
            return rng.choice(sorted(used))
        if rng.random() < 0.15:
            return value(rng, nodes)
        fresh = rng.choice(VARIABLES)
        used.add(fresh)
        return fresh
    legs = [f"({'link' if rng.random() < 0.3 else 'edge'} {term()} {term()})"
            for _ in range(rng.randrange(2, 5))]
    text = " ".join(legs)
    bound = sorted(v for v in VARIABLES if v in text)
    return "(, " + text + ")", bound


def leg(rng, nodes, used):
    def term():
        if used and rng.random() < 0.55:
            return rng.choice(sorted(used))
        if rng.random() < 0.2:
            return value(rng, nodes)
        fresh = rng.choice(VARIABLES)
        used.add(fresh)
        return fresh
    kind = rng.random()
    if kind < 0.6:
        return f"(edge {term()} {term()})"
    if kind < 0.85:
        return f"(lab {term()} {rng.choice(SYMBOLS + ('$l',))} {term()})"
    if kind < 0.95:
        return f"(mark {term()})"
    return f"(edge (pair {term()} {term()}) {term()})"


def conjunction(rng, nodes):
    used = set()
    legs = [leg(rng, nodes, used) for _ in range(rng.randrange(1, 5))]
    text = " ".join(legs)
    bound = sorted(v for v in VARIABLES + ("$l",) if v in text)
    pattern = legs[0] if len(legs) == 1 else "(, " + text + ")"
    return pattern, bound


def data_template(rng, bound):
    pick = lambda: rng.choice(bound) if bound else "k"
    shape = rng.random()
    if shape < 0.5:
        return f"(r {pick()} {pick()})"
    if shape < 0.7:
        return f"(r (p {pick()}) {pick()})"
    if shape < 0.85:
        return "k"
    return pick()


def effect(rng, bound):
    space = rng.choice(("&s", "&b", "&self"))
    data = data_template(rng, bound)
    kind = "remove-atom" if rng.random() < 0.4 else "add-atom"
    if rng.random() < 0.3 and bound:
        # rows the match reads: removing them from the matched space changes
        # what later rows of the conjunction read (adding them could make
        # the ordinary run grow without bound)
        data = "(edge " + " ".join(rng.choice(bound) for _ in range(2)) + ")"
        if space == "&self":
            kind = "remove-atom"
    return f"({kind} {space} {data})"


def effects(rng, bound):
    shape = rng.random()
    if shape < 0.55:
        return effect(rng, bound)
    body = rng.choice(("ok", f"(done {rng.choice(bound) if bound else 'k'})"))
    if shape < 0.7:
        return f"(let $u {effect(rng, bound)} {body})"
    binders = " ".join(f"($e{i} {effect(rng, bound)})" for i in range(rng.randrange(2, 4)))
    return f"(let* ({binders}) {body})"


def program(rng):
    nodes = rng.randrange(2, 7)
    binary = rng.random() < 0.35
    lines = (binary_facts(rng, nodes) if binary
             else facts(rng, nodes, rng.randrange(3, 16)))
    lines += ["!(bind! &s (new-space set))", "!(bind! &b (new-space))"]
    if rng.random() < 0.35:
        # pairs a later query swaps: each row removes one and adds its
        # mirror, which another row may have removed
        space = rng.choice(("&s", "&b"))
        lines.append(f"!(let $h (collapse (match &self (edge $x $y) (add-atom {space} (r $x $y)))) done)")
        lines.append(f"!(let $h (collapse (match &self (edge $x $y) (let* (($a (remove-atom {space} (r $x $y))) ($b (add-atom {space} (r $y $x)))) ok))) (size-atom $h))")
    for index in range(rng.randrange(2, 6)):
        pattern, bound = (binary_conjunction(rng, nodes)
                          if binary and rng.random() < 0.8
                          else conjunction(rng, nodes))
        target = rng.random() < 0.6
        template = effects(rng, bound) if target else data_template(rng, bound)
        match = f"(match &self {pattern} {template})"
        observer = rng.random()
        if observer < 0.2 and target:
            lines.append(f"(= (rule{index}) {match})")
            lines.append(f"!(let $h (collapse (rule{index})) (size-atom $h))")
        elif observer < 0.4:
            # a collection inside a function's body
            lines.append(f"(= (query{index}) (let $h (collapse {match}) (sort-atom $h)))")
            lines.append(f"!(query{index})")
        elif observer < 0.55:
            lines.append(f"!(size-atom (collapse {match}))")
        elif observer < 0.65:
            lines.append(f"!(let $h (collapse {match}) done)")
        else:
            lines.append(f"!(let $h (collapse {match}) (sort-atom $h))")
    for index in range(rng.randrange(0, 3)):
        # effects over a list: each element its pattern meets makes them
        names = list(VARIABLES[:2])
        if rng.random() < 0.6:
            pattern, bound = conjunction(rng, nodes)
            names = bound[:2] if len(bound) >= 2 else names
            source = f"(collapse (match &self {pattern} (pair {names[0]} {names[1]})))"
        else:
            items = [f"(pair {value(rng, nodes)} {value(rng, nodes)})"
                     for _ in range(rng.randrange(1, 6))]
            items += items[: rng.randrange(3)]
            if rng.random() < 0.4:
                items.append(f"(other {value(rng, nodes)})")
            rng.shuffle(items)
            source = "(" + " ".join(items) + ")"
        meets = f"(pair {names[0]} {names[1]})"
        if rng.random() < 0.25:
            meets = f"(pair {names[0]} {value(rng, nodes)})"
        body = effects(rng, names)
        if rng.random() < 0.3:
            # a variable meets each element whole
            meets = names[0]
            body = effects(rng, names[:1])
        lines.append(f"!(let $l {source} (size-atom (collapse (let {meets} (superpose $l) {body}))))")
    # Rows read one way: constructor patterns over ground cells, with a
    # variable shared between columns, repeated, nested, or left open.  Their
    # own relations and random stream leave the programs above as they were.
    extra = random.Random(nodes * 7919 + len(lines))
    if extra.random() < 0.4:
        n = extra.randrange(nodes)
        lines += [f"(cell (p {n}))", f"(cell (p (p {n})))", "(cell (q a))",
                  f"(cell {n})"]
        lines += extra.sample(["(step (p $u) (q $u))", "(step (p (p $u)) $u)",
                               "(step (p $u) (p $u))", "(step (q $u) $w)",
                               "(step (p $u) $u)", "(step $u (p $u))"],
                              extra.randrange(1, 4))
        lines.append("!(let $h (collapse (match &self (, (cell $x) (step $x $y)) (r $x $y))) (sort-atom $h))")
        lines.append("!(size-atom (collapse (match &self (, (cell $x) (step $x $y)) t)))")
        lines.append("!(size-atom (collapse (match &self (, (cell $x) (step $x $y) (step $y $z)) t)))")
    for space in ("&s", "&b"):
        lines.append(f"!(let $h (collapse (get-atoms {space})) (sort-atom $h))")
    lines.append("!(let $h (collapse (match &self (edge $u $v) (edge $u $v))) (sort-atom $h))")
    lines.append("!(let $h (collapse (match &self (r $u $v) (r $u $v))) (sort-atom $h))")
    lines.append("!(let $h (collapse (match &self $any $any)) (size-atom $h))")
    return "\n".join(lines) + "\n"


def elements(text):
    """The top-level elements of a printed collection's inside."""
    items, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        elif c == ' ' and depth == 0:
            if i > start:
                items.append(text[start:i])
            start = i + 1
    if start < len(text):
        items.append(text[start:])
    return items


def canonical(output):
    lines = []
    for line in re.sub(r"\$[A-Za-z0-9_#]+", "$_", output).splitlines():
        if line.startswith("(") and line.endswith(")"):
            line = "(" + " ".join(sorted(elements(line[1:-1]))) + ")"
        lines.append(line)
    return "\n".join(lines)


def run(binary, path, reference, stats):
    env = dict(os.environ)
    env.pop("CETTA_NATIVE_QUERY_REFERENCE", None)
    if reference:
        env["CETTA_NATIVE_QUERY_REFERENCE"] = "1"
    command = [binary, "--profile", "extended", "--lang", "petta"]
    if stats:
        command.append("--emit-runtime-stats")
    p = subprocess.run(command + [path], cwd=ROOT, env=env,
                       capture_output=True, text=True, timeout=120)
    admissions = dense = 0
    for line in p.stderr.splitlines():
        fields = line.split()
        if fields[:2] == ["runtime-counter", "native-query-admission"]:
            admissions = int(fields[2])
        if fields[:2] in (["runtime-counter", "dense-relation-admission"],
                          ["runtime-counter", "native-query-bits-leg"]):
            dense += int(fields[2])
    return (p.returncode, canonical(p.stdout)), admissions, dense


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--programs", type=int, default=300)
    parser.add_argument("--seed", type=int, default=20261027)
    parser.add_argument("--stats", action="store_true")
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    rng = random.Random(args.seed)
    admitted = dense_programs = 0
    with tempfile.TemporaryDirectory(prefix="cetta-native-query-") as work:
        for number in range(args.programs):
            source = program(rng)
            path = os.path.join(work, f"query_{number}.metta")
            with open(path, "w") as f:
                f.write(source)
            native, admissions, dense = run(binary, path, False, args.stats)
            reference, declined, _ = run(binary, path, True, args.stats)
            if native != reference or declined:
                print(f"FAIL: program {number} answers differently on the native path")
                print(source)
                print("native:", native)
                print("reference:", reference)
                return 1
            admitted += admissions
            dense_programs += dense > 0
    if args.stats and admitted < args.programs:
        print(f"FAIL: the native path answered {admitted} collections in "
              f"{args.programs} programs")
        return 1
    if args.stats and dense_programs * 5 < args.programs:
        print(f"FAIL: rows of bits joined in {dense_programs} of "
              f"{args.programs} programs")
        return 1
    print(f"PASS: {args.programs} programs answer and leave the same spaces "
          f"on the native query path as on the ordinary one"
          + (f"; it answered {admitted} collections, by rows of bits in "
             f"{dense_programs} programs" if args.stats else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
