#!/usr/bin/env python3
"""Differential check of scoped `=e` aliases against a reference specification.

Random programs over atomic aliases: ground rows (with duplicates), alias rows
`(=e a1 ... an)`, `(=e> a1 ... an)` and `(=e< a1 ... an)`, removals of unique
rows and of alias rows, and queries:

* `eq:match` with a ground pattern (every equal row, with multiplicity);
* `eq:match` with an open pattern, linear or with a repeated variable (the raw
  bindings, in the space's order);
* `eq:norm`, `eq:class` and `eq:same`.

The specification is independent of the C: classes are the equivalence closure
of neighbouring members; a class is printed by the members of the sink
components of the preference edges (`=e>` prefers each right neighbour, `=e<`
each left), the smallest first and then by name; `eq:class` lists the printed
member and then the others by size and name; a row equals a pattern instance
when they have one shape and equivalent symbols, a variable binding the raw
subterm at its first occurrence.  Answers come in the space's own order, the
order `get-atoms` reports just before the query.

Run: check_scoped_equational_alias_differential.py ./cetta [programs-per-dialect]
"""
import random
import subprocess
import sys

SYMS = ["A", "B", "C", "D", "E", "F"]
HEADS = ["P", "Q"]
ALIAS_FORMS = ["=e", "=e>", "=e<"]


def show(t):
    if isinstance(t, str):
        return t
    return "(" + " ".join(show(x) for x in t) + ")"


class Spec:
    def __init__(self):
        self.rows = []  # terms, in order; alias rows included

    def alias_rows(self):
        return [r for r in self.rows if isinstance(r, tuple) and r[0] in ALIAS_FORMS]

    def classes(self):
        parent = {}

        def find(x):
            parent.setdefault(x, x)
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x

        prefs = []
        for row in self.alias_rows():
            form, members = row[0], row[1:]
            for a, b in zip(members, members[1:]):
                if a == b:
                    continue
                ra, rb = find(a), find(b)
                if ra != rb:
                    parent[ra] = rb
                if form == "=e>":
                    prefs.append((a, b))
                elif form == "=e<":
                    prefs.append((b, a))
        members = {}
        for x in list(parent):
            members.setdefault(find(x), []).append(x)
        cls = {}
        for root, ms in members.items():
            if len(ms) < 2:
                continue
            reach = {m: {m} for m in ms}
            changed = True
            while changed:
                changed = False
                for a, b in prefs:
                    if a in reach:
                        for m in ms:
                            if a in reach[m] and b not in reach[m]:
                                reach[m].add(b)
                                changed = True
            sinks = [m for m in ms if all(m in reach[y] for y in reach[m])]
            rep = min(sinks)
            ordered = [rep] + sorted(m for m in ms if m != rep)
            for m in ms:
                cls[m] = ordered
        return cls

    def norm(self, s):
        c = self.classes().get(s)
        return c[0] if c else s

    def same_sym(self, a, b, cls):
        return a == b or (a in cls and b in cls and cls[a][0] == cls[b][0])

    def match(self, pattern, row, cls, env):
        if isinstance(pattern, str) and pattern.startswith("$"):
            if pattern in env:
                return self.equal(env[pattern], row, cls)
            env[pattern] = row
            return True
        if isinstance(pattern, str) or isinstance(row, str):
            return (isinstance(pattern, str) and isinstance(row, str)
                    and self.same_sym(pattern, row, cls))
        if len(pattern) != len(row):
            return False
        return all(self.match(p, r, cls, env) for p, r in zip(pattern, row))

    def equal(self, a, b, cls):
        if isinstance(a, str) or isinstance(b, str):
            return isinstance(a, str) and isinstance(b, str) and self.same_sym(a, b, cls)
        return len(a) == len(b) and all(self.equal(x, y, cls) for x, y in zip(a, b))

    def eq_match(self, pattern, template_var, order=None):
        cls = self.classes()
        out = []
        for row in (order if order is not None else self.rows):
            if isinstance(row, tuple) and row[0] in ALIAS_FORMS:
                continue
            env = {}
            if self.match(pattern, row, cls, env):
                out.append(env[template_var] if template_var else "hit")
        return out


def random_term(rng, depth):
    if depth == 0 or rng.random() < 0.4:
        return rng.choice(SYMS)
    head = rng.choice(HEADS)
    return (head,) + tuple(random_term(rng, depth - 1) for _ in range(rng.randint(1, 2)))


def random_row(rng, nested=False):
    head = rng.choice(HEADS)
    args = [random_term(rng, 1) for _ in range(rng.randint(1, 2))]
    if nested and all(isinstance(a, str) for a in args):
        # A compound argument keeps the row out of the flat fact relations,
        # so these programs read the expansion-merge route.
        args[0] = (rng.choice(HEADS), args[0])
    return (head,) + tuple(args)


def make_program(seed):
    rng = random.Random(seed)
    nested = seed % 2 == 0
    spec = Spec()
    lines, expected = [], []

    def add(row):
        lines.append(f"!(add-atom &self {show(row)})")
        expected.append(("ack", None))
        spec.rows.append(row)

    def remove(row):
        lines.append(f"!(remove-atom &self {show(row)})")
        expected.append(("ack", None))
        spec.rows.remove(row)

    for _ in range(rng.randint(4, 9)):
        add(random_row(rng, nested))
    for step in range(rng.randint(14, 26)):
        r = rng.random()
        if r < 0.22:
            row = random_row(rng, nested)
            add(row)
            if rng.random() < 0.3:
                add(row)  # a duplicate occurrence
        elif r < 0.40:
            k = rng.randint(2, 3)
            members = tuple(rng.choice(SYMS + HEADS) for _ in range(k))
            if len(set(members)) > 1:
                add((rng.choice(ALIAS_FORMS),) + members)
        elif r < 0.50:
            unique = [x for x in spec.rows if spec.rows.count(x) == 1]
            if unique:
                remove(rng.choice(unique))
        elif r < 0.66:
            if spec.rows and rng.random() < 0.7:
                base = rng.choice([x for x in spec.rows if x[0] not in ALIAS_FORMS] or [random_row(rng, nested)])
            else:
                base = random_row(rng, nested)
            q = tuple(rng.choice(SYMS + HEADS) if isinstance(x, str) and rng.random() < 0.3 else x
                      for x in base)
            lines.append("!(collapse (get-atoms &self))")
            expected.append(("order", None))
            if rng.random() < 0.5:
                lines.append(f"!(collapse (eq:match &self {show(q)} hit))")
            else:
                lines.append(f"!(collapse (eq:match {show(q)} hit))")
            expected.append(("match", (spec.classes(), q, None)))
        elif r < 0.80:
            head = rng.choice(HEADS)
            shape = rng.choice(["one", "two", "same"])
            if shape == "one":
                pattern = (head, "$x")
            elif shape == "two":
                pattern = (head, "$x", rng.choice(SYMS))
            else:
                pattern = (head, "$x", "$x")
            lines.append("!(collapse (get-atoms &self))")
            expected.append(("order", None))
            if rng.random() < 0.5:
                lines.append(f"!(collapse (eq:match &self {show(pattern)} $x))")
            else:
                lines.append(f"!(collapse (eq:match {show(pattern)} $x))")
            expected.append(("match", (spec.classes(), pattern, "$x")))
        elif r < 0.88:
            s = rng.choice(SYMS + HEADS)
            lines.append(f"!(eq:norm &self {s})")
            expected.append(("atom", spec.norm(s)))
        elif r < 0.94:
            s = rng.choice(SYMS)
            lines.append(f"!(collapse (eq:class &self {s}))")
            expected.append(("list", spec.classes().get(s, [s])))
        else:
            a, b = rng.choice(SYMS), rng.choice(SYMS)
            cls = spec.classes()
            lines.append(f"!(eq:same &self {a} {b})")
            expected.append(("bool", spec.same_sym(a, b, cls)))
    return "\n".join(lines) + "\n", expected


def parse_list(text):
    text = text.strip()
    if text == "()":
        return []
    assert text.startswith("(") and text.endswith(")"), text
    inner = text[1:-1]
    items, depth, cur = [], 0, ""
    for ch in inner:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == " " and depth == 0:
            if cur:
                items.append(cur)
            cur = ""
        else:
            cur += ch
    if cur:
        items.append(cur)
    return items


def parse_term(text):
    tokens = text.replace("(", " ( ").replace(")", " ) ").split()
    pos = 0

    def walk():
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        if tok != "(":
            return tok
        items = []
        while tokens[pos] != ")":
            items.append(walk())
        pos += 1
        return tuple(items)

    return walk()


def check_output(dialect, out_lines, expected, program):
    if len(out_lines) != len(expected):
        raise AssertionError(
            f"{dialect}: {len(out_lines)} result lines for {len(expected)} commands\n{program}\n"
            + "\n".join(out_lines))
    order = None
    for i, (line, (kind, value)) in enumerate(zip(out_lines, expected)):
        got = line.strip()
        if dialect == "he":
            assert got.startswith("[") and got.endswith("]"), got
            got = got[1:-1]
        if kind == "ack":
            continue
        if kind == "order":
            order = list(parse_term(got))
            continue
        if kind == "match":
            cls, pattern, var = value
            spec = Spec()
            spec.classes = lambda cls=cls: cls
            value = spec.eq_match(pattern, var, order)
            kind = "list"
        if kind == "list":
            ok = parse_list(got) == [show(v) for v in value]
        elif kind == "atom":
            ok = got == show(value)
        else:
            ok = got.lower() == ("true" if value else "false")
        if not ok:
            raise AssertionError(
                f"{dialect}: command {i + 1} gave {got!r}, the specification "
                f"{value!r}\nprogram:\n{program}")


MODULE_CASE = {
    "eqmod.metta": "(=e Cold Warm)\n(Probe Cold)\n",
    "main.metta": ("!(import! &self eqmod)\n(=e> Warm Hot)\n(Probe Hot)\n"
                   "!(eq:same &self Cold Warm)\n!(eq:same &self Cold Hot)\n"
                   "!(eq:norm &self Warm)\n"
                   "!(collapse (eq:match &self (Probe Warm) hit))\n"
                   "!(collapse (eq:class &self Hot))\n"),
}
# An imported module's aliases and rows join the importer's: one class
# {Cold, Warm, Hot}, printed Cold (the sinks are Cold and Hot).
MODULE_EXPECTED = ["true", "true", "Cold", "(hit hit)", "(Cold Hot Warm)"]


def check_module_case(cetta):
    import os
    import tempfile
    cetta = os.path.abspath(cetta)
    with tempfile.TemporaryDirectory(prefix="eq-alias-module-") as d:
        for name, text in MODULE_CASE.items():
            with open(os.path.join(d, name), "w") as f:
                f.write(text)
        for dialect in ("petta", "he"):
            proc = subprocess.run([cetta, "--lang", dialect, "--profile", "extended", "main.metta"],
                                  cwd=d, capture_output=True, text=True, timeout=60)
            lines = [l.strip() for l in proc.stdout.splitlines() if l.strip()][1:]
            if dialect == "he":
                lines = [l[1:-1] for l in lines]
            if [l.lower() if l in ("True", "False") else l for l in lines] != MODULE_EXPECTED:
                raise AssertionError(f"{dialect} module case: {lines} != {MODULE_EXPECTED}\n{proc.stderr}")


def main():
    cetta = sys.argv[1] if len(sys.argv) > 1 else "./cetta"
    check_module_case(cetta)
    programs = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    queries = 0
    for dialect in ("petta", "he"):
        for seed in range(1, programs + 1):
            program, expected = make_program(seed)
            proc = subprocess.run(
                [cetta, "--lang", dialect, "--profile", "extended", "-e", program],
                capture_output=True, text=True, timeout=60)
            if proc.returncode != 0:
                raise AssertionError(f"{dialect} seed {seed} exited {proc.returncode}\n"
                                     f"{proc.stderr}\n{program}")
            lines = [l for l in proc.stdout.splitlines() if l.strip()]
            check_output(dialect, lines, expected, program)
            queries += sum(1 for kind, _ in expected if kind not in ("ack", "order"))
    print(f"PASS: scoped equational alias differential, {programs} programs x 2 dialects, "
          f"{queries} queries equal to the specification, and the imported-module case")


if __name__ == "__main__":
    main()
