#!/usr/bin/env python3
"""A priority space against a model of it, operation by operation.

Random programs add, pop, peek, index, truncate, remove and read a priority
space in PeTTa and in HE, strict and full, small and past the size where a
full space reads its indexes.  The model is a list kept sorted by the
standard order of terms as this file states it (numbers by value, then
strings, then symbols by their bytes, then expressions by their elements in
turn and by length), written independently of the engine.  The order shows
through the space's own operations: a pop, a peek, a get and a truncate must
be the model's, and so must a drain by pops.  A read of a priority space
answers in its order too: a collapse of a match must be the model's atoms in
order, and `once` of a read the least of them; a take under `once` takes the
least.  `get-atoms` answers the space's atoms with no order promised, so its
collapse is compared as a bag.

Reads are asked every way a priority space answers them: every atom, one
atom, the atoms beginning with one, two or a nested ground element, the
atoms a pattern open at its front selects, one of each, and takes: of one
ground atom, and of every atom a pattern meets.  Half the programs also hold atoms a variable begins, which sort
before every other expression and which a keyed read matches too; there
the space cannot search its order for a pattern and reads it row by row.
"""
import functools
import os
import random
import subprocess
import sys
import tempfile

from space_run_modes import run_both

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


# -- atoms as Python values: int, ("str", text), ("sym", text), ("var",),
# -- ("expr", tuple of atoms)

VAR = ("var",)


def show(atom):
    if isinstance(atom, int):
        return str(atom)
    if atom == VAR:
        return "$open"
    if isinstance(atom, tuple) and atom and atom[0] == "str":
        return '"' + atom[1] + '"'
    if isinstance(atom, tuple) and atom and atom[0] == "sym":
        return atom[1]
    return "(" + " ".join(show(item) for item in atom[1]) + ")"


def expr(*items):
    return ("expr", tuple(items))


def sym(text):
    return ("sym", text)


def is_open(atom):
    return (not isinstance(atom, int)) and atom[0] == "expr" and VAR in atom[1]


def klass(atom):
    if isinstance(atom, int):
        return 1
    if atom == VAR:
        return 0
    if atom[0] == "str":
        return 2
    if atom[0] == "sym":
        return 4
    return 5 if atom[1] else 3


def compare(left, right):
    a, b = klass(left), klass(right)
    if a != b:
        return -1 if a < b else 1
    if a == 1:
        return (left > right) - (left < right)
    if a in (2, 4):
        x, y = left[1].encode(), right[1].encode()
        return (x > y) - (x < y)
    if a in (0, 3):
        return 0
    for x, y in zip(left[1], right[1]):
        order = compare(x, y)
        if order:
            return order
    return (len(left[1]) > len(right[1])) - (len(left[1]) < len(right[1]))


KEY = functools.cmp_to_key(compare)


def random_atom(rng, open_rows=False):
    kind = rng.randrange(13 if open_rows else 12)
    payload = sym("p%d" % rng.randrange(6))
    if kind == 12:
        return expr(VAR, payload)
    if kind < 6:
        return expr(rng.randrange(-4, 12), payload)
    if kind == 6:
        return expr(sym(rng.choice(["alpha", "beta", "b", "exec"])), payload)
    if kind == 7:
        return expr(("str", rng.choice(["s", "t", "sa"])), payload)
    if kind == 8:
        return expr(rng.randrange(0, 4), expr(sym("edge"), payload, rng.randrange(3)))
    if kind == 9:
        return expr(rng.randrange(0, 4), payload, rng.randrange(3))
    if kind == 10:
        return expr(expr(rng.randrange(0, 3), payload), sym("tail"))
    return expr(rng.randrange(0, 3))


class Model:
    def __init__(self):
        self.atoms = []

    def add(self, atom):
        self.atoms.append(atom)
        self.atoms.sort(key=KEY)

    def listed(self, atoms=None):
        return "(" + " ".join(show(a) for a in (self.atoms if atoms is None else atoms)) + ")"


def two(atom):
    return klass(atom) == 5 and len(atom[1]) == 2


def program(rng, profile, steps, grow, open_rows=False):
    """The program's lines and the answers the model gives, by tag."""
    lines = ["!(bind! &p (new-space priority%s))" % (" full" if profile == "full" else "")]
    expected = {}
    model = Model()
    tag = 0

    def ask(text, answer):
        """A question and the model's answer: a collapse of get-atoms as a
        bag, which promises no order; any other answer exactly, reads in
        the space's order."""
        nonlocal tag
        tag += 1
        lines.append("!(r %d %s)" % (tag, text))
        kind = "bag" if text.startswith("(collapse (get-atoms ") else "exact"
        expected[tag] = (kind, "(r %d %s)" % (tag, answer))

    for step in range(steps):
        roll = rng.random()
        if not model.atoms or roll < grow:
            atom = random_atom(rng, open_rows)
            model.add(atom)
            lines.append("!(add-atom &p %s)" % show(atom))
        elif roll < grow + 0.10:
            least = model.atoms.pop(0)
            # An atom with a variable prints under a name of the run's.
            if is_open(least):
                lines.append("!(space:priority:pop &p)")
            else:
                ask("(space:priority:pop &p)", show(least))
        elif roll < grow + 0.14:
            if not is_open(model.atoms[0]):
                ask("(space:priority:peek &p)", show(model.atoms[0]))
        elif roll < grow + 0.18:
            rank = rng.randrange(len(model.atoms))
            if not is_open(model.atoms[rank]):
                ask("(space:priority:get &p %d)" % rank, show(model.atoms[rank]))
        elif roll < grow + 0.22:
            if not any(is_open(a) for a in model.atoms):
                ask("(collapse (match &p $x $x))", model.listed())
        elif roll < grow + 0.25:
            if not any(is_open(a) for a in model.atoms):
                ask("(collapse (get-atoms &p))", model.listed())
        elif roll < grow + 0.31:
            # A keyed read: the atoms (K $v) for one K, in order.  An atom a
            # variable begins answers it too, with K for its variable.
            key = rng.randrange(-4, 12)
            hits = [expr(key, a[1][1]) for a in model.atoms
                    if two(a) and a[1][0] in (key, VAR)]
            if rng.random() < 0.5 or not hits:
                ask("(collapse (match &p (%d $v) (%d $v)))" % (key, key), model.listed(hits))
            elif not any(is_open(a) for a in hits):
                ask("(once (match &p (%d $v) (%d $v)))" % (key, key),
                    show(hits[0]))
        elif roll < grow + 0.35:
            # Two ground elements, then an open one.
            key = rng.randrange(0, 4)
            payload = sym("p%d" % rng.randrange(6))
            hits = [a for a in model.atoms
                    if klass(a) == 5 and len(a[1]) == 3 and a[1][0] == key
                    and a[1][1] == payload]
            ask("(collapse (match &p (%d %s $n) (%d %s $n)))"
                % (key, show(payload), key, show(payload)), model.listed(hits))
        elif roll < grow + 0.38:
            # A ground element that is an expression.
            head = expr(rng.randrange(0, 3), sym("p%d" % rng.randrange(6)))
            hits = [expr(head, a[1][1]) for a in model.atoms
                    if two(a) and a[1][0] in (head, VAR)]
            ask("(collapse (match &p (%s $t) (%s $t)))" % (show(head), show(head)),
                model.listed(hits))
        elif roll < grow + 0.41:
            # A ground element, then an expression with open elements.
            key = rng.randrange(0, 4)
            hits = [a for a in model.atoms
                    if two(a) and a[1][0] == key and klass(a[1][1]) == 5
                    and len(a[1][1][1]) == 3 and a[1][1][1][0] == sym("edge")]
            ask("(collapse (match &p (%d (edge $q $n)) (%d (edge $q $n))))" % (key, key),
                model.listed(hits))
        elif roll < grow + 0.44:
            # Open at its front, bound after: no atom with a variable held,
            # as that atom's answer would print its variable.
            payload = sym("p%d" % rng.randrange(6))
            if not any(is_open(a) for a in model.atoms):
                hits = [a for a in model.atoms if two(a) and a[1][1] == payload]
                ask("(collapse (match &p ($k %s) ($k %s)))" % (show(payload), show(payload)),
                    model.listed(hits))
        elif roll < grow + 0.48:
            # One atom of a shape: a two-element expression.
            hits = [a for a in model.atoms if two(a)]
            if hits and not any(is_open(a) for a in hits):
                ask("(once (match &p ($k $v) ($k $v)))", show(hits[0]))
        elif roll < grow + 0.53:
            # A ground read: every copy of one atom held, and every atom a
            # variable begins that it meets.
            atom = rng.choice(model.atoms)
            if not is_open(atom):
                copies = [a for a in model.atoms if compare(a, atom) == 0 or
                          (is_open(a) and two(atom) and a[1][1] == atom[1][1])]
                ask("(collapse (match &p %s found))" % show(atom),
                    "(" + " ".join("found" for _ in copies) + ")")
        elif roll < grow + 0.57:
            # Take one ground two-element expression by a transform.  What a
            # take removes where a stored atom has variables is the
            # dialect's rule for takes, not the space's order, so the
            # programs holding such atoms take nothing.
            hits = [a for a in model.atoms if two(a)]
            if hits and not open_rows:
                chosen = rng.choice(hits)
                model.atoms.remove(chosen)
                ask("(once (transform &p (take %s) (took %s)))" % (show(chosen), show(chosen)),
                    "(took %s)" % show(chosen))
        elif roll < grow + 0.61:
            # Take every atom under one key.
            key = rng.randrange(-4, 12)
            hits = [a for a in model.atoms if two(a) and a[1][0] == key]
            if not open_rows:
                for hit in hits:
                    model.atoms.remove(hit)
                ask("(collapse (transform &p (take (%d $v)) (took %d $v)))" % (key, key),
                    "(" + " ".join("(took %d %s)" % (key, show(h[1][1])) for h in hits) + ")")
        elif roll < grow + 0.64:
            keep = rng.randrange(len(model.atoms) + 1)
            lines.append("!(space:priority:truncate &p %d)" % keep)
            del model.atoms[keep:]
        else:
            ask("(space-len &p)", str(len(model.atoms)))
    for key in range(-4, 12):
        hits = [expr(key, a[1][1]) for a in model.atoms
                if two(a) and a[1][0] in (key, VAR)]
        ask("(collapse (match &p (%d $v) (%d $v)))" % (key, key), model.listed(hits))
    ask("(space-len &p)", str(len(model.atoms)))
    if not any(is_open(a) for a in model.atoms):
        ask("(collapse (match &p $x $x))", model.listed())
        # Draining sorts what is left.
        rest = list(model.atoms)
        lines.append("(= (drain) (if (== (space-len &p) 0) () "
                     "(let $least (space:priority:pop &p) (let $rest (drain) (cons-atom $least $rest)))))")
        ask("(drain)", model.listed(rest))
    return lines, expected


def run_once(binary, lang, lines, mode_env):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write("\n".join(lines) + "\n")
        path = f.name
    try:
        env = dict(os.environ, **mode_env)
        p = subprocess.run([binary, "--lang", lang, "--profile", "extended", path],
                           cwd=ROOT, env=env, capture_output=True, text=True, timeout=600)
    finally:
        os.unlink(path)
    answers = {}
    for line in p.stdout.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            line = line[1:-1]
        if line.startswith("(r "):
            answers[int(line.split()[1])] = line
    return p.returncode, answers, p.stderr


def elements(text):
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


def as_bag(line):
    """An answer line (r TAG (x ...)) as the bag of its collapse."""
    fields = elements(line)
    if len(fields) != 3 or not fields[2].startswith("("):
        return tuple(fields)
    return tuple(sorted(elements(fields[2])))


def agrees(expectation, line):
    kind, value = expectation
    if line is None:
        return False
    if kind == "bag":
        return as_bag(line) == as_bag(value)
    return line == value


def run(binary, lang, lines):
    return run_both(lambda mode_env: run_once(binary, lang, lines, mode_env),
                    same=lambda a, b: as_bag(a) == as_bag(b))


def main():
    binary = os.path.abspath(sys.argv[1])
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    checked = 0
    for seed in range(rounds):
        # Small spaces, spaces past the row-reading threshold, and spaces
        # that shrink through many removals.
        for steps, grow in ((40, 0.45), (260, 0.62), (400, 0.40)):
            for profile, open_rows in (("strict", False), ("full", False),
                                       ("strict", True), ("full", True)):
                rng = random.Random(seed * 7919 + steps + (1 if open_rows else 0))
                lines, expected = program(rng, profile, steps, grow, open_rows)
                for lang in ("petta", "he"):
                    code, answers, stderr = run(binary, lang, lines)
                    for tag in sorted(expected):
                        if not agrees(expected[tag], answers.get(tag)):
                            print("FAIL: seed %d, %d steps, %s%s, %s, answer %d" %
                                  (seed, steps, profile,
                                   " with open atoms" if open_rows else "", lang, tag))
                            print("  asked   :", [l for l in lines if l.startswith("!(r %d " % tag)][0])
                            print("  model   :", expected[tag][1])
                            print("  engine  :", answers.get(tag))
                            if stderr.strip():
                                print("  stderr  :", stderr.strip().splitlines()[-1])
                            return 1
                    if code != 0:
                        print("FAIL: seed %d, %d steps, %s, %s: exit %d: %s" %
                              (seed, steps, profile, lang, code,
                               stderr.strip().splitlines()[-1:] or ""))
                        return 1
                    checked += len(expected)
    print("PASS: a priority space answers as its model does, its own operations in the "
          "model's order and its reads in it too: %d answers, strict and full, "
          "ground and holding atoms with variables, PeTTa and HE, as shipped and "
          "collecting at every opportunity" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
