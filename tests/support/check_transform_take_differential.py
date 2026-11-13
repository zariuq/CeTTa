#!/usr/bin/env python3
"""A transform's takes against a model of them, in PeTTa.

A firing takes the stored atoms its takes met: for each, copies of it,
stored atoms that are the same atom or, for an atom with variables, the same
but for their names.  It is valid when the store holds as many copies as it
takes, and one more of an atom it also reads; it removes those copies, the
first by position, and nothing else.  An atom a taken atom only unifies with
is neither counted nor removed, and a pattern that binds a stored atom's
variable takes that stored atom, not the instance it made of it.

Random programs fill a space with duplicates and with atoms a variable
begins, then run transforms that take: one take, of a pattern or of a
ground atom, every match firing; and a take inside `unique` and inside
`collect`, which take what a plain take takes: the one stored atom the one
match met, and every stored atom the matches met.  The model is a list of
atoms and the rule above, written independently of the engine.  After every
transform the answers and the whole store must be the model's, in each kind
of space, below and above the size at which a space reads its indexes; and,
with 4,500 distinct facts of one head, past the size at which an atom space
keeps them as a relation.  No space promises an order of answers (a queue
and a priority space show theirs through their own operations only), so the
transforms are those whose outcome no order of firing changes, and answers,
stores and a collected value are compared as bags.  Variables print under
names of the run's, so each prints here as `$_`.
"""
import os
import random
import re
import subprocess
import sys
import tempfile

from space_run_modes import run_both

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_priority_space_differential import (  # noqa: E402
    KEY, VAR, expr, is_open, show, sym)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# name, constructor, whether the model keeps it sorted (as a priority space
# pops), and an unused flag kept for the programs' seeds
SPACES = (
    ("atom", "(new-space)", False, False),
    ("atom strict", "(new-space atom strict)", False, False),
    ("queue", "(new-space queue)", False, False),
    ("priority", "(new-space priority)", True, False),
    ("priority full", "(new-space priority full)", True, False),
)

KEYS = (0, 1, 2, sym("a"))
PAYLOADS = tuple(sym("p%d" % i) for i in range(3))


def two(atom):
    return (not isinstance(atom, int)) and atom[0] == "expr" and len(atom[1]) == 2


class Store:
    """The atoms of a space in the order it reads them."""

    def __init__(self, ordered):
        self.ordered = ordered
        self.atoms = []

    def add(self, atom):
        self.atoms.append(atom)
        if self.ordered:
            self.atoms.sort(key=KEY)

    def copies(self, atom):
        return sum(1 for a in self.atoms if a == atom)

    def take(self, atom):
        self.atoms.remove(atom)  # the first by position


def keyed(store, key):
    """The stored atoms (KEY $v) meets, in order: (atom, v)."""
    return [(a, a[1][1]) for a in store.atoms
            if two(a) and a[1][0] in (key, VAR)]


def fire(store, takes, reads):
    """Fire one candidate if the store holds what it reads and takes; whether
    it did.  A branch reads and takes the atoms its proposal met, which
    earlier firings may have removed since."""
    for atom in set(takes) | set(reads):
        need = takes.count(atom) + (1 if atom in reads else 0)
        if store.copies(atom) < need:
            return False
    for atom in takes:
        store.take(atom)
    return True


def run_model(store, form, key, ground, first_only):
    """The answers of one transform, firing against the model's store."""
    if form == "one":
        candidates = [([inst], [], "(took %s)" % show(v)) for inst, v in keyed(store, key)]
    elif form == "any":
        candidates = [([a], [], "(took %s)" % show(a[1][1]))
                      for a in store.atoms if two(a)]
    elif form == "ground":
        candidates = [([a], [], "took") for a in store.atoms
                      if two(a) and a[1][1] == ground[1][1]
                      and a[1][0] in (ground[1][0], VAR)]
    elif form == "two":
        first = keyed(store, key)
        candidates = [([a, b], [], "(took %s %s)" % (show(v), show(w)))
                      for a, v in first for b, w in first]
    elif form == "unique":
        # Exactly one match, and the firing takes the stored atom it met.
        first = keyed(store, key)
        candidates = ([([first[0][0]], [], "(took %s)" % show(first[0][1]))]
                      if len(first) == 1 else [])
    elif form in ("collect", "collect-count"):
        # One firing over every match: its value is the pattern's instances
        # in order, and it takes the stored atoms they met.
        first = keyed(store, key)
        value = "(" + " ".join(show(expr(key, v)) for _, v in first) + ")"
        candidates = [([a for a, _ in first], [],
                       "(got %s)" % value if form == "collect"
                       else "(got %d)" % len(first))]
    else:  # a read and a take of one atom
        first = keyed(store, key)
        candidates = [([b], [a], "(took %s)" % show(v))
                      for a, v in first for b, w in first if w == v]
    answers = []
    for takes, reads, answer in candidates:
        if fire(store, takes, reads):
            answers.append(answer)
            if first_only:
                break
    return answers


def program(rng, space, ordered, in_order, size, open_rows):
    lines = ["!(import! &self (library lib_transform))", "!(bind! &s %s)" % space]
    expected = {}
    store = Store(ordered)
    tag = 0

    def ask(text, answer):
        nonlocal tag
        tag += 1
        lines.append("!(r %d %s)" % (tag, text))
        expected[tag] = "(r %d %s)" % (tag, answer)

    def listing():
        ask("(collapse (get-atoms &s))",
            "(" + " ".join(show(a) for a in store.atoms) + ")")

    for step in range(size):
        roll = rng.random()
        if roll < 0.62 or not store.atoms:
            head = rng.choice(KEYS)
            if open_rows and rng.random() < 0.12:
                head = VAR
            atom = expr(head, rng.choice(PAYLOADS))
            if rng.random() < 0.08:
                atom = expr(rng.choice(KEYS), rng.choice(PAYLOADS), 7)
            store.add(atom)
            lines.append("!(add-atom &s %s)" % show(atom))
            continue
        # Forms whose outcome no order of firing changes: two takes, or a
        # read beside a take, fire as the firings before them left the
        # store, and `once` fires whichever match comes first.
        form = rng.choice(("one", "one", "any", "ground", "unique", "collect",
                           "collect-count"))
        key = rng.choice(KEYS)
        ground = expr(key, rng.choice(PAYLOADS))
        first_only = False
        source = {
            "one": "(take (%s $v))" % show(key),
            "any": "(take ($k $v))",
            "ground": "(take %s)" % show(ground),
            "two": "(, (take (%s $v)) (take (%s $w)))" % (show(key), show(key)),
            "read": "(, (read (%s $v)) (take (%s $v)))" % (show(key), show(key)),
            "unique": "(unique (take (%s $v)))" % show(key),
            "collect": "(collect (take (%s $v)) $all)" % show(key),
            "collect-count": "(, (collect (take (%s $v)) $all) "
                             "(let $n (size-atom $all)))" % show(key),
        }[form]
        template = {"one": "(took $v)", "any": "(took $v)", "ground": "took",
                    "two": "(took $v $w)", "read": "(took $v)",
                    "unique": "(took $v)", "collect": "(got $all)",
                    "collect-count": "(got $n)"}[form]
        call = "(transform &s %s %s)" % (source, template)
        answers = run_model(store, form, key, ground, first_only)
        ask("(collapse (once %s))" % call if first_only else "(collapse %s)" % call,
            "(" + " ".join(answers) + ")")
        listing()
    listing()
    return lines, expected


def relation_program(rng, space, ordered, in_order, open_rows):
    """Takes from more facts of one head than the size (4,096) at which an
    atom space keeps them as a relation: distinct facts, a few of them twice,
    and, with OPEN_ROWS, atoms a variable begins beside them."""
    lines = ["!(import! &self (library lib_transform))", "!(bind! &s %s)" % space]
    expected = {}
    store = Store(ordered)
    tag = 0
    head = sym("a")

    def ask(text, answer):
        nonlocal tag
        tag += 1
        lines.append("!(r %d %s)" % (tag, text))
        expected[tag] = "(r %d %s)" % (tag, answer)

    facts = [expr(head, i) for i in range(4500)]
    facts += [expr(head, rng.randrange(4500)) for _ in range(30)]
    if open_rows:
        facts += [expr(VAR, rng.randrange(4500)) for _ in range(5)]
    rng.shuffle(facts)
    for atom in facts:
        store.atoms.append(atom)
        lines.append("!(add-atom &s %s)" % show(atom))
    if ordered:
        store.atoms.sort(key=KEY)
    for _ in range(40):
        form = rng.choice(("ground", "ground", "unique-ground"))
        ground = expr(head, rng.randrange(4600))
        if form == "one":
            call = "(transform &s (take (a $v)) (took $v))"
            answers = run_model(store, "one", head, ground, True)
            ask("(collapse (once %s))" % call, "(" + " ".join(answers) + ")")
        elif form == "ground":
            call = "(transform &s (take %s) took)" % show(ground)
            answers = run_model(store, "ground", head, ground, False)
            ask("(collapse %s)" % call, "(" + " ".join(answers) + ")")
        else:
            # Exactly one stored atom the ground atom meets.
            met = [a for a in store.atoms
                   if two(a) and a[1][1] == ground[1][1] and a[1][0] in (head, VAR)]
            if len(met) == 1:
                store.take(met[0])
            ask("(collapse (transform &s (unique (take %s)) took))" % show(ground),
                "(took)" if len(met) == 1 else "()")
        ask("(size-atom (collapse (get-atoms &s)))", str(len(store.atoms)))
    return lines, expected


def blank(line):
    return re.sub(r"\$[A-Za-z0-9_#-]+", "$_", line)


def as_bag(line):
    """An answer line with the atoms of its list sorted."""
    match = re.match(r"^\(r (\d+) \((.*)\)\)$", line)
    if not match:
        return line
    items, depth, start = [], 0, None
    text = match.group(2)
    for i, ch in enumerate(text):
        if ch == "(":
            if depth == 0:
                start = i
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                items.append(text[start:i + 1])
                start = None
        elif depth == 0 and not ch.isspace() and start is None:
            start = i
        elif depth == 0 and ch.isspace() and start is not None:
            items.append(text[start:i])
            start = None
    if start is not None and depth == 0:
        items.append(text[start:])
    # A collected value is a bag too: (got (x ...)).
    items = [collected(item) for item in items]
    return "(r %s (%s))" % (match.group(1), " ".join(sorted(items)))


def collected(item):
    """(got (x ...)) with the elements of its value sorted."""
    match = re.match(r"^\(got \((.*)\)\)$", item)
    if not match:
        return item
    inner = as_bag("(r 0 (%s))" % match.group(1))
    return "(got %s)" % inner[len("(r 0 "):-1]


def run_once(binary, lines, mode_env):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write("\n".join(lines) + "\n")
        path = f.name
    try:
        env = dict(os.environ, **mode_env)
        p = subprocess.run([binary, "--lang", "petta", "--profile", "extended", path],
                           cwd=ROOT, env=env, capture_output=True, text=True, timeout=600)
    finally:
        os.unlink(path)
    answers = {}
    for line in p.stdout.splitlines():
        line = line.strip()
        if line.startswith("(r "):
            answers[int(line.split()[1])] = blank(line)
    return p.returncode, answers, p.stderr


def run(binary, lines):
    return run_both(lambda mode_env: run_once(binary, lines, mode_env),
                    same=lambda a, b: as_bag(a) == as_bag(b))


def main():
    binary = os.path.abspath(sys.argv[1])
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    checked = 0
    for seed in range(rounds):
        for size in (14, 60, 220):
            for name, space, ordered, in_order in SPACES:
                for open_rows in (False, True):
                    rng = random.Random(seed * 104729 + size + (7 if open_rows else 0))
                    lines, expected = program(rng, space, ordered, in_order, size,
                                              open_rows)
                    code, answers, stderr = run(binary, lines)
                    same = as_bag
                    for tag in sorted(expected):
                        if same(answers.get(tag) or "") != same(blank(expected[tag])):
                            print("FAIL: seed %d, %d steps, %s space%s, answer %d" %
                                  (seed, size, name,
                                   " with open atoms" if open_rows else "", tag))
                            print("  asked   :", [l for l in lines
                                                  if l.startswith("!(r %d " % tag)][0])
                            print("  model   :", blank(expected[tag]))
                            print("  engine  :", answers.get(tag))
                            if stderr.strip():
                                print("  stderr  :", stderr.strip().splitlines()[-1])
                            return 1
                    if code != 0:
                        print("FAIL: seed %d, %d steps, %s space: exit %d: %s" %
                              (seed, size, name, code,
                               stderr.strip().splitlines()[-1:] or ""))
                        return 1
                    checked += len(expected)
    # Past the size at which an atom space keeps one head's facts as a relation.
    for name, space, ordered, in_order in SPACES:
        for open_rows in (False, True):
            rng = random.Random(4096 + (7 if open_rows else 0))
            lines, expected = relation_program(rng, space, ordered, in_order, open_rows)
            code, answers, stderr = run(binary, lines)
            for tag in sorted(expected):
                if as_bag(answers.get(tag) or "") != as_bag(blank(expected[tag])):
                    print("FAIL: 4,500 facts of one head, %s space%s, answer %d" %
                          (name, " with open atoms" if open_rows else "", tag))
                    print("  asked   :", [l for l in lines
                                          if l.startswith("!(r %d " % tag)][0][:200])
                    print("  model   :", blank(expected[tag])[:200])
                    print("  engine  :", (answers.get(tag) or "")[:200])
                    if stderr.strip():
                        print("  stderr  :", stderr.strip().splitlines()[-1])
                    return 1
            if code != 0:
                print("FAIL: 4,500 facts of one head, %s space: exit %d: %s" %
                      (name, code, stderr.strip().splitlines()[-1:] or ""))
                return 1
            checked += len(expected)
    print("PASS: a transform's takes remove the copies its firings take and nothing "
          "else: %d answers and stores, %d kinds of space, ground and holding atoms "
          "with variables" % (checked, len(SPACES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
