#!/usr/bin/env python3
"""Reads of a priority space by any pattern, against a model by unification.

A priority space answers a pattern from its order: the rows that begin with
the pattern's ground front, at whatever depth that front ends, and the rows
that hold a variable where the front has a term.  This check knows nothing
of that.  Its model is a list of atoms sorted by the standard order of terms
and a unifier, written apart from the engine: a pattern's answers are its
instances by the stored atoms it unifies with, a bag.

Random programs fill a space with atoms of every shape (numbers, symbols,
expressions nested three deep, a variable alone, variables at every node,
one variable twice) and ask patterns made from stored atoms by opening
some of their subterms, and patterns made at random: all the answers, as a
bag, and one alone (`once`), which must be one of them.  Pops and removals in between change what the
order holds.  A removal is each dialect's own: PeTTa's `remove-atom` removes
every stored atom its argument unifies with; HE's removes one stored atom
equal to it, or, for an atom with variables, the stored atom that is the
same but for their names where exactly one is.  The whole space is listed
now and then, as a bag.  PeTTa and HE, strict and full, below and above the
size at which a full space reads its indexes, as shipped and collecting at
every opportunity.  Variables print under names of the run's, so each is
compared as `$_`.
"""
import functools
import os
import random
import re
import subprocess
import sys
import tempfile

from space_run_modes import run_both

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SYMBOLS = ("a", "job", "w", "zz")


def sym(text):
    return ("sym", text)


def var(n):
    return ("var", n)


def pat(n):
    return ("pat", n)


def is_var(t):
    return isinstance(t, tuple) and t[0] in ("var", "pat")


def is_expr(t):
    return isinstance(t, tuple) and t[0] == "expr"


def show(t):
    if isinstance(t, int):
        return str(t)
    if t[0] == "sym":
        return t[1]
    if t[0] == "var":
        return "$r%d" % t[1]
    if t[0] == "pat":
        return "$q%d" % t[1]
    return "(" + " ".join(show(x) for x in t[1]) + ")"


def klass(t):
    if is_var(t):
        return 0
    if isinstance(t, int):
        return 1
    if t[0] == "sym":
        return 4
    return 5 if t[1] else 3


def compare(left, right):
    a, b = klass(left), klass(right)
    if a != b:
        return -1 if a < b else 1
    if a == 1:
        return (left > right) - (left < right)
    if a == 4:
        x, y = left[1].encode(), right[1].encode()
        return (x > y) - (x < y)
    if a == 0:
        return (left[1] > right[1]) - (left[1] < right[1])
    if a == 3:
        return 0
    for x, y in zip(left[1], right[1]):
        order = compare(x, y)
        if order:
            return order
    return (len(left[1]) > len(right[1])) - (len(left[1]) < len(right[1]))


KEY = functools.cmp_to_key(compare)


def canonical(t):
    """The atom with its variables numbered by first occurrence."""
    names = {}

    def walk(x):
        if is_var(x):
            return var(names.setdefault(x, len(names)))
        if is_expr(x):
            return ("expr", tuple(walk(y) for y in x[1]))
        return x
    return walk(t)


def random_term(rng, depth, variables):
    roll = rng.random()
    if depth == 0 or roll < 0.40:
        leaf = rng.random()
        if variables and leaf < 0.18:
            return var(rng.randrange(2))
        if leaf < 0.60:
            return rng.randrange(0, 4)
        return sym(rng.choice(SYMBOLS))
    return ("expr", tuple(random_term(rng, depth - 1, variables)
                          for _ in range(rng.randrange(1, 4))))


def random_atom(rng, variables):
    roll = rng.random()
    if variables and roll < 0.03:
        return var(0)
    if roll < 0.08:
        return random_term(rng, 0, False)
    # Mostly keyed atoms: a key that is a number, a symbol or an expression.
    if roll < 0.55:
        key = rng.choice((rng.randrange(0, 4), sym(rng.choice(SYMBOLS)),
                          ("expr", (sym("w"), rng.randrange(0, 3)))))
        if variables and rng.random() < 0.12:
            key = rng.choice((var(0), ("expr", (sym("w"), var(0))),
                              ("expr", (var(0), rng.randrange(0, 3)))))
        return canonical(("expr", (key, random_term(rng, 2, variables))))
    return canonical(random_term(rng, 3, variables))


def opened(rng, t, counter, chance):
    """T with some subterms replaced by new pattern variables."""
    if rng.random() < chance:
        counter[0] += 1
        return pat(counter[0])
    if is_expr(t):
        return ("expr", tuple(opened(rng, x, counter, chance) for x in t[1]))
    if is_var(t):
        counter[0] += 1
        return pat(counter[0])
    return t


def walk(t, bound):
    while is_var(t) and t in bound:
        t = bound[t]
    return t


def unify(a, b, bound):
    a, b = walk(a, bound), walk(b, bound)
    if a == b:
        return True
    if is_var(a):
        bound[a] = b
        return True
    if is_var(b):
        bound[b] = a
        return True
    if is_expr(a) and is_expr(b) and len(a[1]) == len(b[1]):
        return all(unify(x, y, bound) for x, y in zip(a[1], b[1]))
    return False


def resolve(t, bound):
    t = walk(t, bound)
    if is_expr(t):
        return ("expr", tuple(resolve(x, bound) for x in t[1]))
    return t


def blank(text):
    return re.sub(r"\$[A-Za-z0-9_#-]+", "$_", text)


def fresh_names(t):
    """T with its variables under names no stored atom uses."""
    if is_var(t):
        return pat(100 + t[1])
    if is_expr(t):
        return ("expr", tuple(fresh_names(x) for x in t[1]))
    return t


def has_vars(t):
    return is_var(t) or (is_expr(t) and any(has_vars(x) for x in t[1]))


def program(rng, profile, steps, grow, variables, lang):
    lines = ["!(bind! &p (new-space priority%s))" % (" full" if profile == "full" else "")]
    expected = {}
    atoms = []
    tag = 0

    def ask(text, answer):
        """A question and the model's answer: a collapse as a bag, any
        other answer exactly."""
        nonlocal tag
        tag += 1
        lines.append("!(r %d %s)" % (tag, text))
        kind = "bag" if text.startswith("(collapse ") else "exact"
        expected[tag] = (kind, blank("(r %d %s)" % (tag, answer)))

    def ask_one_of(text, answers):
        nonlocal tag
        tag += 1
        lines.append("!(r %d %s)" % (tag, text))
        expected[tag] = ("one-of", [blank("(r %d %s)" % (tag, a)) for a in answers])

    for _ in range(steps):
        roll = rng.random()
        if not atoms or roll < grow:
            atom = random_atom(rng, variables)
            atoms.append(atom)
            atoms.sort(key=KEY)
            lines.append("!(add-atom &p %s)" % show(atom))
            continue
        if roll < grow + 0.06:
            atoms.pop(0)
            lines.append("!(space:priority:pop &p)")
            continue
        if roll < grow + 0.12:
            # A removal: of a stored atom as written, of one the same but
            # for its variables' names, or of a pattern.
            counter = [0]
            chosen = rng.choice(atoms)
            kind = rng.random()
            target = (fresh_names(chosen) if kind < 0.6
                      else opened(rng, chosen, counter, 0.2))
            if is_var(target):
                continue
            lines.append("!(remove-atom &p %s)" % show(target))
            if lang == "petta":
                atoms[:] = [a for a in atoms if not unify(target, a, {})]
            elif not has_vars(target):
                if target in atoms:
                    atoms.remove(target)
            else:
                alike = [i for i, a in enumerate(atoms)
                         if canonical(a) == canonical(target)]
                if len(alike) == 1:
                    del atoms[alike[0]]
            continue
        if roll < grow + 0.15:
            ask("(collapse (get-atoms &p))",
                "(" + " ".join(show(a) for a in atoms) + ")")
            continue
        counter = [0]
        if rng.random() < 0.7:
            pattern = opened(rng, rng.choice(atoms), counter,
                             rng.choice((0.0, 0.15, 0.3)))
        else:
            pattern = opened(rng, random_atom(rng, False), counter, 0.2)
        if is_var(pattern):
            continue
        answers = []
        for atom in atoms:
            bound = {}
            if unify(pattern, atom, bound):
                answers.append(show(resolve(pattern, bound)))
        text = show(pattern)
        if rng.random() < 0.3 and answers:
            ask_one_of("(once (match &p %s %s))" % (text, text), answers)
        else:
            ask("(collapse (match &p %s %s))" % (text, text),
                "(" + " ".join(answers) + ")")
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
            answers[int(line.split()[1])] = blank(line)
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
    if kind == "one-of":
        return line in value
    return line == value


def run(binary, lang, lines):
    return run_both(lambda mode_env: run_once(binary, lang, lines, mode_env),
                    same=lambda a, b: as_bag(a) == as_bag(b))


def main():
    binary = os.path.abspath(sys.argv[1])
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    checked = 0
    for seed in range(rounds):
        for steps, grow in ((60, 0.5), (420, 0.62)):
            for profile in ("strict", "full"):
                for variables in (False, True):
                    for lang in ("petta", "he"):
                        rng = random.Random(seed * 6151 + steps +
                                            (3 if variables else 0))
                        lines, expected = program(rng, profile, steps, grow,
                                                  variables, lang)
                        code, answers, stderr = run(binary, lang, lines)
                        for tag in sorted(expected):
                            if not agrees(expected[tag], answers.get(tag)):
                                print("FAIL: seed %d, %d steps, %s%s, %s, answer %d" %
                                      (seed, steps, profile,
                                       " with variables" if variables else "", lang, tag))
                                print("  asked   :", [l for l in lines
                                                      if l.startswith("!(r %d " % tag)][0])
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
    print("PASS: a priority space answers every pattern as unification over its "
          "atoms does, as bags: %d answers, strict and full, ground and with "
          "variables at every node, PeTTa and HE, as shipped and collecting at "
          "every opportunity" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
