#!/usr/bin/env python3
"""Write a set proof C has checked as a Megalodon article, to be checked again.

The draft's `set:` theory is higher-order logic over `set` and `prop`; its
seeds are the eleven laws of the sets and their universes.  Part 7 of the
Megalodon development of the tower inside the sets states the same eleven
laws as the theorems `law_...` of Megalodon's higher-order Tarski-Grothendieck
theory.  This exporter goes from C to Megalodon; `megalodon_native_projection_v1`
goes the other way.

C is asked for the native package of a theorem.  The package exists only when
C has checked the authored proof and the regular kernel has checked the term
compiled from it.  The exporter writes the authored rule-form proof, which the
package carries, as a Megalodon theorem that cites the Part 7 laws by name.
The article is the development followed by what the exporter writes:

* each cited seed as C states it, proved by its Part 7 law, so that Megalodon
  also compares the two statements;
* a Megalodon definition for each definition of the space the statements use;
* each cited theorem of the space, written the same way, before its use;
* the theorem.

Formulas.  C writes conjunction, equivalence, existence, negation and falsity
out by `imp` and `all`.  Megalodon's preamble defines `/\\`, `<->`, `exists`,
`~` and `False` by the same formulas, so the exporter prints those forms with
Megalodon's notation and Megalodon's conversion unfolds them where a proof
instantiates them.  Equality is the one connective whose definitions differ:
C declares `eq`, Megalodon defines Leibniz equality.  No authored proof rule
builds or uses an equation, so `eq` occurs only inside statements, where it
is printed as `=`.

Proofs.  At the goal, an introduction of the quantifier is `let` and of
implication `assume`; anything else is a term given to `exact`, in which an
elimination is an application, an introduction is `fun`, a hypothesis is its
name and a cited seed is its Part 7 law.

Not written: the readable forms `pf:have` and `pf:by`, which C's native
compiler does not compile either; a definition other than one equation of a
non-recursive constant; a citation of anything but a seed or a theorem with a
native package; carriers other than `set`, `prop` and functions between them.
The article is evidence only once Megalodon has checked it.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import gslt2parse_schema_v1 as sx

DRAFT = Path(__file__).resolve().parents[1]

# C's seeds and the Part 7 theorems that state them.
SEED_LAWS = {
    "extensionality": "law_extensionality",
    "emptyLaw": "law_empty",
    "unionLaw": "law_union",
    "powerLaw": "law_power",
    "separationLaw": "law_separation",
    "replacementLaw": "law_replacement",
    "setInduction": "law_set_induction",
    "universeIn": "law_universe_in",
    "universeTransitive": "law_universe_transitive",
    "universeClosed": "law_universe_closed",
    "universeMinimal": "law_universe_minimal",
}

SET, PROP = "set", "prop"


def arrow(*types):
    result = types[-1]
    for domain in reversed(types[:-1]):
        result = ("->", domain, result)
    return result


# The constants of C's signature that name sets, and their Megalodon names.
# `imp`, `all` and `eq` are connectives and are printed as such.
SIGNATURE = {
    "Falsum": ("False", PROP),
    "In": ("In", arrow(SET, SET, PROP)),
    "Empty": ("Empty", SET),
    "Union": ("Union", arrow(SET, SET)),
    "Power": ("Power", arrow(SET, SET)),
    "Sep": ("Sep", arrow(SET, arrow(SET, PROP), SET)),
    "Repl": ("Repl", arrow(SET, arrow(SET, SET), SET)),
    "Eps_set": ("Eps_i", arrow(arrow(SET, PROP), SET)),
    "UnivOf": ("UnivOf", arrow(SET, SET)),
}
CONNECTIVES = {"imp", "all", "eq", "lam", "->", "prop", "set", "define", "="}

MEGALODON_WORDS = {
    "fun", "forall", "exists", "let", "assume", "set", "prop", "exact", "apply",
    "claim", "prove", "witness", "rewrite", "symmetry", "reflexivity", "cases",
    "Theorem", "Lemma", "Definition", "Qed", "Axiom", "Parameter", "Section",
    "End", "Variable", "Hypothesis", "Infix", "Prefix", "Postfix", "Binder",
    "Notation", "Let", "Admitted", "admit", "aby", "In", "False", "True",
}


class CannotRender(Exception):
    """A proof or statement outside what the exporter writes."""


def text(term: sx.SExpr) -> str:
    try:
        return sx.render(term)
    except sx.SchemaError:
        return repr(term)


def sym(term: sx.SExpr) -> str | None:
    return term.text if isinstance(term, sx.Symbol) else None


def is_form(term: sx.SExpr, head: str, length: int | None = None) -> bool:
    return (isinstance(term, tuple) and bool(term) and sym(term[0]) == head
            and (length is None or len(term) == length))


def parse_type(term: sx.SExpr):
    name = sym(term)
    if name in (SET, PROP):
        return name
    if is_form(term, "->") and len(term) >= 3:
        return arrow(*(parse_type(part) for part in term[1:]))
    raise CannotRender(f"the carrier {text(term)} is not set, prop or a function between them")


def type_text(t) -> str:
    if isinstance(t, str):
        return t
    domain = type_text(t[1])
    return f"{'(' + domain + ')' if isinstance(t[1], tuple) else domain}->{type_text(t[2])}"


# ---------------------------------------------------------------- formulas

class Binder:
    """A bound variable.  Identity, not its name, is what binds."""

    __slots__ = ("hint", "type")

    def __init__(self, hint: str, type_=None):
        self.hint = hint
        self.type = type_


@dataclass(frozen=True, eq=False)
class Var:
    binder: Binder


@dataclass(frozen=True, eq=False)
class Const:
    name: str


@dataclass(frozen=True, eq=False)
class App:
    head: object
    args: tuple


@dataclass(frozen=True, eq=False)
class Lam:
    binder: Binder
    body: object


@dataclass(frozen=True, eq=False)
class All:
    type: object
    family: object


@dataclass(frozen=True, eq=False)
class Imp:
    left: object
    right: object


@dataclass(frozen=True, eq=False)
class Eq:
    type: object
    left: object
    right: object


def apply(head, *args):
    if not args:
        return head
    if isinstance(head, App):
        return App(head.head, head.args + tuple(args))
    return App(head, tuple(args))


def substitute(term, binder: Binder, value):
    if isinstance(term, Var):
        return value if term.binder is binder else term
    if isinstance(term, Const):
        return term
    if isinstance(term, App):
        head = substitute(term.head, binder, value)
        return apply(head, *(substitute(arg, binder, value) for arg in term.args))
    if isinstance(term, Lam):
        return Lam(term.binder, substitute(term.body, binder, value))
    if isinstance(term, All):
        return All(term.type, substitute(term.family, binder, value))
    if isinstance(term, Imp):
        return Imp(substitute(term.left, binder, value), substitute(term.right, binder, value))
    if isinstance(term, Eq):
        return Eq(term.type, substitute(term.left, binder, value),
                  substitute(term.right, binder, value))
    raise TypeError(term)


def occurs(binder: Binder, term) -> bool:
    if isinstance(term, Var):
        return term.binder is binder
    if isinstance(term, Const):
        return False
    if isinstance(term, App):
        return occurs(binder, term.head) or any(occurs(binder, a) for a in term.args)
    if isinstance(term, Lam):
        return occurs(binder, term.body)
    if isinstance(term, All):
        return occurs(binder, term.family)
    if isinstance(term, Imp):
        return occurs(binder, term.left) or occurs(binder, term.right)
    if isinstance(term, Eq):
        return occurs(binder, term.left) or occurs(binder, term.right)
    raise TypeError(term)


def alpha_equal(a, b, pairs=()) -> bool:
    if isinstance(a, Var) and isinstance(b, Var):
        for left, right in reversed(pairs):
            if a.binder is left or b.binder is right:
                return a.binder is left and b.binder is right
        return a.binder is b.binder
    if type(a) is not type(b):
        return False
    if isinstance(a, Const):
        return a.name == b.name
    if isinstance(a, App):
        return (len(a.args) == len(b.args) and alpha_equal(a.head, b.head, pairs)
                and all(alpha_equal(x, y, pairs) for x, y in zip(a.args, b.args)))
    if isinstance(a, Lam):
        return alpha_equal(a.body, b.body, pairs + ((a.binder, b.binder),))
    if isinstance(a, All):
        return a.type == b.type and alpha_equal(a.family, b.family, pairs)
    if isinstance(a, Imp):
        return alpha_equal(a.left, b.left, pairs) and alpha_equal(a.right, b.right, pairs)
    if isinstance(a, Eq):
        return alpha_equal(a.left, b.left, pairs) and alpha_equal(a.right, b.right, pairs)
    return False


@dataclass
class Definition:
    name: str
    type: object
    params: tuple           # Binder
    equation: tuple         # (right-hand side as C wrote it, its parameters, its type)
    body: object = None     # the right-hand side, read once every name it uses is known


def instantiate(family, value):
    if isinstance(family, Lam):
        return substitute(family.body, family.binder, value)
    return apply(family, value)


def whnf(term, definitions: dict[str, Definition]):
    """Unfold definitions and beta-reduce at the head, as C's checker does."""
    while True:
        head, args = (term.head, term.args) if isinstance(term, App) else (term, ())
        if isinstance(head, Lam) and args:
            term = apply(substitute(head.body, head.binder, args[0]), *args[1:])
            continue
        if isinstance(head, Const) and head.name == "Falsum":
            p = Binder("p", PROP)
            term = apply(All(PROP, Lam(p, Var(p))), *args)
            continue
        if isinstance(head, Const) and head.name in definitions:
            definition = definitions[head.name]
            if len(args) < len(definition.params):
                return term
            body = definition.body
            for param, arg in zip(definition.params, args):
                body = substitute(body, param, arg)
            term = apply(body, *args[len(definition.params):])
            continue
        return term


# ---------------------------------------------------------------- reading C

class Scope:
    """How a symbol of a formula resolves: an enclosing `lam`, then a binder
    the proof named with `pf:fix`, then a constant (C's open witness rule)."""

    def __init__(self, known: "Known", proof_vars=(), fixed=()):
        self.known = known
        self.proof_vars = tuple(proof_vars)  # Binder, innermost last
        self.fixed = tuple(fixed)            # (name, Binder), innermost last

    def parse(self, term: sx.SExpr, lams=()):
        name = sym(term)
        if name is not None:
            for bound, binder in reversed(lams):
                if bound == name:
                    return Var(binder)
            for bound, binder in reversed(self.fixed):
                if bound == name:
                    return Var(binder)
            if name in SIGNATURE:
                return Const(name)
            if name in self.known.definitions:
                return Const(name)
            if name in self.known.unwritable:
                raise CannotRender(f"the definition of {name} is not written: "
                                   f"{self.known.unwritable[name]}")
            if name in CONNECTIVES or name.startswith("pf:"):
                raise CannotRender(f"{name} is not a term here")
            raise CannotRender(f"{name} is neither bound nor a constant C defined")
        if not isinstance(term, tuple) or not term:
            raise CannotRender(f"{text(term)} is not a formula of the set theory")
        if is_form(term, "pf:var", 2):
            index = term[1]
            if not isinstance(index, int) or not 0 <= index < len(self.proof_vars):
                raise CannotRender(f"{text(term)} names no enclosing proof binder")
            return Var(self.proof_vars[-1 - index])
        if is_form(term, "lam", 3) and sym(term[1]) is not None:
            binder = Binder(sym(term[1]))
            return Lam(binder, self.parse(term[2], lams + ((sym(term[1]), binder),)))
        if is_form(term, "all", 3):
            domain = parse_type(term[1])
            family = self.parse(term[2], lams)
            if isinstance(family, Lam):
                family.binder.type = domain
            return All(domain, family)
        if is_form(term, "imp", 3):
            return Imp(self.parse(term[1], lams), self.parse(term[2], lams))
        if is_form(term, "eq", 4):
            return Eq(parse_type(term[1]), self.parse(term[2], lams), self.parse(term[3], lams))
        head = self.parse(term[0], lams)
        return apply(head, *(self.parse(arg, lams) for arg in term[1:]))


def constant_type(name: str, known: "Known"):
    if name in SIGNATURE:
        return SIGNATURE[name][1]
    if name in known.definitions:
        return known.definitions[name].type
    raise CannotRender(f"no type for {name}")


def annotate(term, expected, known: "Known") -> None:
    """Give each `lam` of a term the domain its position requires."""
    if isinstance(term, Lam):
        if not isinstance(expected, tuple):
            raise CannotRender("a function stands where a set or a proposition is expected")
        term.binder.type = expected[1]
        annotate(term.body, expected[2], known)
    elif isinstance(term, All):
        annotate(term.family, arrow(term.type, PROP), known)
    elif isinstance(term, Imp):
        annotate(term.left, PROP, known)
        annotate(term.right, PROP, known)
    elif isinstance(term, Eq):
        annotate(term.left, term.type, known)
        annotate(term.right, term.type, known)
    elif isinstance(term, App):
        if isinstance(term.head, Const):
            t = constant_type(term.head.name, known)
        elif isinstance(term.head, Var) and term.head.binder.type is not None:
            t = term.head.binder.type
        else:
            raise CannotRender("an application whose function has no known type")
        for arg in term.args:
            if not isinstance(t, tuple):
                raise CannotRender("too many arguments")
            annotate(arg, t[1], known)
            t = t[2]


# ---------------------------------------------------------------- printing

ATOM, APPL, REL, NOT, AND, IFF, IMP, BIND = 7, 6, 5, 4, 3, 2, 1, 0


def sanitize(name: str, default: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_]", "_", name or "")
    if not cleaned or not (cleaned[0].isalpha() or cleaned[0] == "_"):
        cleaned = default + cleaned
    return cleaned


def megalodon_name(name: str) -> str:
    """The Megalodon name of a definition or theorem of the space."""
    return "prime_" + re.sub(r"[^A-Za-z0-9_]", "_", name)


class Names:
    """Names in scope while printing; a bound name never shadows another."""

    def __init__(self, reserved):
        self.reserved = set(reserved) | MEGALODON_WORDS
        self.used: list[str] = []
        self.of: dict[int, list[str]] = {}

    def fresh(self, hint: str, default: str) -> str:
        base = sanitize(hint, default)
        candidate, n = base, 0
        while candidate in self.reserved or candidate in self.used:
            n += 1
            candidate = f"{base}{n}"
        return candidate

    def push(self, binder, name: str) -> None:
        self.used.append(name)
        self.of.setdefault(id(binder), []).append(name)

    def pop(self, binder) -> None:
        self.used.pop()
        self.of[id(binder)].pop()

    def name(self, binder) -> str:
        names = self.of.get(id(binder))
        if not names:
            raise CannotRender(f"the variable {binder.hint} is not in scope")
        return names[-1]


class Printer:
    def __init__(self, names: Names, constant_names: dict[str, str], used: set[str]):
        self.names = names
        self.constant_names = constant_names
        self.used = used

    def bound(self, binder: Binder, body_printer):
        name = self.names.fresh(binder.hint, "x")
        self.names.push(binder, name)
        try:
            return name, body_printer()
        finally:
            self.names.pop(binder)

    def at(self, term, level: int) -> str:
        printed, own = self.formula(term)
        return printed if own >= level else f"({printed})"

    def formula(self, term):
        if isinstance(term, Var):
            return self.names.name(term.binder), ATOM
        if isinstance(term, Const):
            self.used.add(term.name)
            return self.constant_names[term.name], ATOM
        if isinstance(term, Eq):
            return f"{self.at(term.left, APPL)} = {self.at(term.right, APPL)}", REL
        if isinstance(term, Imp):
            if isinstance(term.right, Const) and term.right.name == "Falsum":
                return f"~{self.at(term.left, ATOM)}", NOT
            return f"{self.at(term.left, NOT)} -> {self.at(term.right, BIND)}", IMP
        if isinstance(term, All):
            return self.quantifier(term)
        if isinstance(term, Lam):
            name, body = self.bound(term.binder, lambda: self.at(term.body, BIND))
            return f"fun {name}:{type_text(term.binder.type)} => {body}", BIND
        if isinstance(term, App):
            head = term.head
            if isinstance(head, Const) and head.name == "In" and len(term.args) == 2:
                return f"{self.at(term.args[0], APPL)} :e {self.at(term.args[1], APPL)}", REL
            if (isinstance(head, Const) and head.name in ("Sep", "Repl")
                    and len(term.args) == 2 and isinstance(term.args[1], Lam)):
                base, family = term.args
                over = self.at(base, APPL)
                name, body = self.bound(family.binder, lambda: self.at(family.body, BIND))
                if head.name == "Sep":
                    return f"{{{name} :e {over}|{body}}}", ATOM
                return f"{{{body}|{name} :e {over}}}", ATOM
            parts = [self.at(head, ATOM)] + [self.at(arg, ATOM) for arg in term.args]
            return " ".join(parts), APPL
        raise TypeError(term)

    def quantifier(self, term: All):
        family = term.family
        if term.type == PROP and isinstance(family, Lam):
            r, body = family.binder, family.body
            # falsity: every proposition
            if isinstance(body, Var) and body.binder is r:
                return "False", ATOM
            # conjunction: every r that follows from A and B
            if (isinstance(body, Imp) and isinstance(body.right, Var) and body.right.binder is r
                    and isinstance(body.left, Imp) and isinstance(body.left.right, Imp)
                    and isinstance(body.left.right.right, Var)
                    and body.left.right.right.binder is r):
                a, b = body.left.left, body.left.right.left
                if not occurs(r, a) and not occurs(r, b):
                    if (isinstance(a, Imp) and isinstance(b, Imp)
                            and alpha_equal(a.left, b.right) and alpha_equal(a.right, b.left)):
                        return f"{self.at(a.left, NOT)} <-> {self.at(a.right, NOT)}", IFF
                    return f"{self.at(a, NOT)} /\\ {self.at(b, NOT)}", AND
            # existence: every r that follows from each witness
            if (isinstance(body, Imp) and isinstance(body.right, Var) and body.right.binder is r
                    and isinstance(body.left, All) and isinstance(body.left.family, Lam)):
                inner = body.left.family
                if (isinstance(inner.body, Imp) and isinstance(inner.body.right, Var)
                        and inner.body.right.binder is r and not occurs(r, inner.body.left)):
                    name, shown = self.bound(inner.binder, lambda: self.at(inner.body.left, BIND))
                    return f"exists {name}:{type_text(body.left.type)}, {shown}", BIND
        if isinstance(family, Lam):
            name, body = self.bound(family.binder, lambda: self.at(family.body, BIND))
        else:
            witness = Binder("x", term.type)
            name, body = self.bound(witness, lambda: self.at(apply(family, Var(witness)), BIND))
        return f"forall {name}:{type_text(term.type)}, {body}", BIND


# ---------------------------------------------------------------- proof terms

@dataclass
class Doc:
    """A proof term with its layout: a leaf, an application or a `fun`."""
    kind: str
    text: str = ""
    level: int = ATOM
    parts: list = field(default_factory=list)

    def flat(self) -> str:
        if self.kind == "leaf":
            return self.text
        if self.kind == "app":
            return " ".join(part.wrapped(ATOM) for part in self.parts)
        return f"fun {self.text} => {self.parts[0].flat()}"

    def own_level(self) -> int:
        return {"leaf": self.level, "app": APPL, "fun": BIND}[self.kind]

    def wrapped(self, level: int) -> str:
        return self.flat() if self.own_level() >= level else f"({self.flat()})"

    def lines(self, indent: int, width: int, level: int = BIND) -> list[str]:
        pad = " " * indent
        if len(pad) + len(self.wrapped(level)) <= width or self.kind == "leaf":
            return [pad + self.wrapped(level)]
        opened = self.own_level() < level
        if self.kind == "app":
            head = self.parts[0].wrapped(ATOM)
            body = [pad + ("(" if opened else "") + head]
            for part in self.parts[1:]:
                body += part.lines(indent + 2, width, ATOM)
        else:
            body = [pad + ("(" if opened else "") + f"fun {self.text} =>"]
            body += self.parts[0].lines(indent + 2, width, BIND)
        if opened:
            body[-1] += ")"
        return body


def leaf(printed: str, level: int = ATOM) -> Doc:
    return Doc("leaf", printed, level)


def app_doc(function: Doc, argument: Doc) -> Doc:
    if function.kind == "app":
        return Doc("app", parts=function.parts + [argument])
    return Doc("app", parts=[function, argument])


def fun_doc(name: str, body: Doc) -> Doc:
    if body.kind == "fun":
        return Doc("fun", f"{name} {body.text}", parts=body.parts)
    return Doc("fun", name, parts=[body])


# ---------------------------------------------------------------- the space

@dataclass
class Theorem:
    name: str
    proposition: sx.SExpr
    proof: sx.SExpr
    assumptions: tuple[str, ...]


@dataclass
class Known:
    """What C answered about the names of one space."""
    theorems: dict[str, Theorem] = field(default_factory=dict)
    assumed: set[str] = field(default_factory=set)   # packages with no proof
    propositions: dict[str, sx.SExpr] = field(default_factory=dict)
    verdicts: dict[str, sx.SExpr] = field(default_factory=dict)
    definitions: dict[str, Definition] = field(default_factory=dict)
    unwritable: dict[str, str] = field(default_factory=dict)  # definitions, and why not


QUERY_MARK = "megalodon-set-proof-export-v1"


def established(result: sx.SExpr):
    if (is_form(result, "Established", 2) and is_form(result[1], "PrimeScopedValue", 2)):
        return result[1][1]
    return None


def run_queries(cetta: Path, source: Path, queries: list[str]) -> list[sx.SExpr]:
    """Run the source, then the queries; return each query's one answer."""
    program = (source.read_text(encoding="utf-8") + f"\n!(quote {QUERY_MARK})\n"
               + "\n".join(queries) + "\n")
    with tempfile.TemporaryDirectory(prefix="megalodon-set-export-") as directory:
        path = Path(directory) / source.name
        path.write_text(program, encoding="utf-8")
        run = subprocess.run([str(cetta.resolve()), "--lang", "prime", str(path)],
                             text=True, capture_output=True, check=False)
    if run.returncode != 0:
        raise CannotRender(f"C did not run {source.name}: {run.stderr.strip()[:400]}")
    lines = run.stdout.splitlines()
    marks = [i for i, line in enumerate(lines) if line == f"[(quote {QUERY_MARK})]"]
    if len(marks) != 1 or len(lines) - marks[0] - 1 != len(queries):
        raise CannotRender("C's answers to the export queries could not be read")
    answers = []
    for line in lines[marks[0] + 1:]:
        if not (line.startswith("[") and line.endswith("]")):
            raise CannotRender(f"unexpected answer {line[:200]}")
        forms = sx.parse_sexprs(line[1:-1], source="cetta")
        if len(forms) != 1:
            raise CannotRender(f"expected one answer, read {len(forms)}")
        answers.append(forms[0])
    return answers


def ask(cetta: Path, source: Path, names: list[str]) -> list[tuple[sx.SExpr, sx.SExpr]]:
    """One run of the space: for each name, its native package and its
    proposition, as `try` reports them."""
    queries = []
    for name in names:
        queries.append(f"!(try (set:native-proof {name}))")
        queries.append(f"!(try (set:known-proposition {name}))")
    answers = run_queries(cetta, source, queries)
    return [(answers[2 * i], answers[2 * i + 1]) for i in range(len(names))]


def symbols(term: sx.SExpr, found: set[str]) -> set[str]:
    if isinstance(term, sx.Symbol):
        name = term.text
        if not (name.startswith("pf:") or name.startswith("$") or name in CONNECTIVES
                or name in SIGNATURE):
            found.add(name)
    elif isinstance(term, tuple):
        for part in term:
            symbols(part, found)
    return found


def read_definition(name: str, proposition: sx.SExpr, known: Known) -> Definition:
    if not (is_form(proposition, "define", 3) and is_form(proposition[2], "=", 3)):
        raise CannotRender(f"{name} is defined by more than one equation")
    declared = parse_type(proposition[1])
    lhs, rhs = proposition[2][1], proposition[2][2]
    if sym(lhs) == name:
        params = ()
    elif isinstance(lhs, tuple) and lhs and sym(lhs[0]) == name:
        params = lhs[1:]
    else:
        raise CannotRender(f"the equation of {name} does not define {name}")
    names = [sym(p) for p in params]
    if any(n is None or not n.startswith("$") for n in names) or len(set(names)) != len(names):
        raise CannotRender(f"{name} is defined by cases, not by one equation over variables")
    if name in symbols(rhs, set()):
        raise CannotRender(f"{name} is recursive")
    t, binders = declared, []
    for param in names:
        if not isinstance(t, tuple):
            raise CannotRender(f"{name} has more parameters than its type")
        binders.append(Binder(param.split("#")[0].lstrip("$"), t[1]))
        t = t[2]
    return Definition(name, declared, tuple(binders), (rhs, tuple(zip(names, binders)), t))


def gather(cetta: Path, source: Path, names: list[str]) -> Known:
    """Ask C, run by run, about the seeds, the named theorems and every name
    they reach.  C answers about the space as the source leaves it."""
    known = Known()
    asked: set[str] = set()
    pending = [*names, *SEED_LAWS]
    raw_definitions: dict[str, Definition] = {}
    while pending:
        batch = sorted(set(pending) - asked)
        pending = []
        if not batch:
            break
        asked.update(batch)
        for name, (package, proposition) in zip(batch, ask(cetta, source, batch)):
            known.verdicts[name] = package
            stated = established(proposition)
            if stated is not None:
                known.propositions[name] = stated
                if is_form(stated, "define"):
                    try:
                        raw_definitions[name] = read_definition(name, stated, known)
                        pending += symbols(stated[2][2], set())
                    except CannotRender as reason:
                        known.unwritable[name] = str(reason)
            value = established(package)
            if is_form(value, "SetNativeProofV1", 10) and value[3] == ():
                # A seed or an axiom: its package is its own assumption.
                known.assumed.add(name)
            elif is_form(value, "SetNativeProofV1", 10) and sym(value[1]) == name:
                rows = value[8] if isinstance(value[8], tuple) else ()
                known.theorems[name] = Theorem(
                    name, value[2], value[3],
                    tuple(sym(row[0]) for row in rows if isinstance(row, tuple) and row))
                if stated is not None and stated != value[2]:
                    raise CannotRender(f"the package of {name} states another proposition")
                pending += symbols(value[2], set()) | symbols(value[3], set())
    # A definition is read once every name it mentions is known; one that
    # cannot be written makes those that use it unwritable too.
    while True:
        known.definitions = dict(raw_definitions)
        failed = {}
        for name, definition in raw_definitions.items():
            rhs, lams, result_type = definition.equation
            try:
                body = Scope(known).parse(rhs, lams)
                annotate(body, result_type, known)
                definition.body = body
            except CannotRender as reason:
                failed[name] = str(reason)
        if not failed:
            return known
        known.unwritable.update(failed)
        for name in failed:
            del raw_definitions[name]


# ---------------------------------------------------------------- rendering

@dataclass
class Context:
    proof_vars: tuple = ()      # Binder
    fixed: tuple = ()           # (C name, Binder)
    hypotheses: tuple = ()      # (C name or None, Megalodon name, formula)


@dataclass
class Block:
    name: str
    lines: list[str]


class Writer:
    """Writes theorems of one space.  `cite` maps a seed to the Part 7 law a
    proof cites (the negative controls change it)."""

    def __init__(self, known: Known, cite: dict[str, str] | None = None, width: int = 100):
        self.known = known
        self.cite = dict(SEED_LAWS if cite is None else cite)
        self.width = width
        self.constant_names = {c: m for c, (m, _) in SIGNATURE.items()}
        self.constant_names.update({d: megalodon_name(d) for d in known.definitions})
        self.reserved = (set(self.constant_names.values()) | set(SEED_LAWS.values())
                         | set(self.cite.values())
                         | {megalodon_name(t) for t in known.theorems}
                         | {"prime_seed_" + s for s in SEED_LAWS})
        self.blocks: list[Block] = []
        self.written: set[str] = set()
        self.cited_seeds: list[str] = []
        self.seed_propositions: dict[str, object] = {}
        self.used_constants: set[str] = set()

    # -- statements

    def statement(self, source: sx.SExpr):
        proposition = Scope(self.known).parse(source)
        annotate(proposition, PROP, self.known)
        return proposition

    def seed(self, name: str):
        if name not in self.seed_propositions:
            if name not in self.known.propositions:
                raise CannotRender(f"C did not state the seed {name}")
            self.seed_propositions[name] = self.statement(self.known.propositions[name])
        return self.seed_propositions[name]

    def printer(self, names: Names) -> Printer:
        return Printer(names, self.constant_names, self.used_constants)

    def show(self, formula, names: Names) -> str:
        try:
            return self.printer(names).formula(formula)[0]
        except CannotRender:
            return "a formula"

    # -- proofs

    @staticmethod
    def introduction(proof):
        if is_form(proof, "pf:all-intro", 2):
            return "all", None, proof[1]
        if is_form(proof, "pf:fix", 3) and sym(proof[1]) is not None:
            return "all", sym(proof[1]), proof[2]
        if is_form(proof, "pf:imp-intro", 2):
            return "imp", None, proof[1]
        if is_form(proof, "pf:assume", 3) and sym(proof[1]) is not None:
            return "imp", sym(proof[1]), proof[2]
        return None

    @staticmethod
    def refuse_readable(proof) -> None:
        if isinstance(proof, tuple) and proof and sym(proof[0]) in ("pf:have", "pf:by"):
            raise CannotRender(
                f"{sym(proof[0])} is a readable step, not a rule of the logic; neither C's "
                "native compiler nor this exporter translates it")

    def hypothesis_name(self, names: Names) -> str:
        n = 0
        while f"H{n}" in names.used or f"H{n}" in names.reserved:
            n += 1
        return f"H{n}"

    def introduce(self, kind: str, c_name: str | None, goal, ctx: Context, names: Names):
        """One introduction at the goal: the name it binds, its scope key, the
        context and goal after it, and the assumed formula of an implication."""
        shape = whnf(goal, self.known.definitions)
        if kind == "all":
            if not isinstance(shape, All):
                raise CannotRender(f"an introduction of the quantifier at {self.show(goal, names)}")
            hint = c_name or (shape.family.binder.hint if isinstance(shape.family, Lam) else "x")
            binder = Binder(hint, shape.type)
            name = names.fresh(hint, "x")
            names.push(binder, name)
            fixed = ctx.fixed + (((c_name, binder),) if c_name else ())
            return (name, binder, Context(ctx.proof_vars + (binder,), fixed, ctx.hypotheses),
                    instantiate(shape.family, Var(binder)), None)
        if not isinstance(shape, Imp):
            raise CannotRender(f"an introduction of implication at {self.show(goal, names)}")
        key = Binder(c_name or "H")
        name = names.fresh(c_name, "H") if c_name else self.hypothesis_name(names)
        names.push(key, name)
        hypotheses = ctx.hypotheses + ((c_name, name, shape.left),)
        return name, key, Context(ctx.proof_vars, ctx.fixed, hypotheses), shape.right, shape.left

    def check(self, proof, goal, ctx: Context, names: Names) -> Doc:
        intro = self.introduction(proof)
        if intro is None:
            return self.synth(proof, ctx, names)[0]
        kind, c_name, body = intro
        name, key, inner, inner_goal, _ = self.introduce(kind, c_name, goal, ctx, names)
        try:
            return fun_doc(name, self.check(body, inner_goal, inner, names))
        finally:
            names.pop(key)

    def synth(self, proof, ctx: Context, names: Names):
        self.refuse_readable(proof)
        if is_form(proof, "pf:hyp", 2):
            index = proof[1]
            if not isinstance(index, int) or not 0 <= index < len(ctx.hypotheses):
                raise CannotRender(f"{text(proof)} names no hypothesis")
            _, name, formula = ctx.hypotheses[-1 - index]
            return leaf(name), formula
        if sym(proof) is not None:
            for c_name, name, formula in reversed(ctx.hypotheses):
                if c_name == sym(proof):
                    return leaf(name), formula
            raise CannotRender(f"no hypothesis is named {sym(proof)}")
        if is_form(proof, "pf:known", 2) and sym(proof[1]) is not None:
            return self.citation(sym(proof[1]))
        scope = Scope(self.known, ctx.proof_vars, ctx.fixed)
        if is_form(proof, "pf:typed", 3):
            stated = scope.parse(proof[1])
            annotate(stated, PROP, self.known)
            name = self.hypothesis_name(names)
            shown = self.printer(names).at(stated, ATOM)
            ascription = leaf(f"fun {name}:{shown} => {name}", BIND)
            return app_doc(ascription, self.check(proof[2], stated, ctx, names)), stated
        if is_form(proof, "pf:imp-elim", 3):
            function, formula = self.synth(proof[1], ctx, names)
            shape = whnf(formula, self.known.definitions)
            if not isinstance(shape, Imp):
                raise CannotRender(f"implication eliminated at {self.show(formula, names)}")
            argument = self.check(proof[2], shape.left, ctx, names)
            return app_doc(function, argument), shape.right
        if is_form(proof, "pf:all-elim", 3):
            function, formula = self.synth(proof[1], ctx, names)
            shape = whnf(formula, self.known.definitions)
            if not isinstance(shape, All):
                raise CannotRender(f"quantifier eliminated at {self.show(formula, names)}")
            witness = scope.parse(proof[2])
            annotate(witness, shape.type, self.known)
            printed, level = self.printer(names).formula(witness)
            return app_doc(function, leaf(printed, level)), instantiate(shape.family, witness)
        raise CannotRender(f"{text(proof)[:120]} is not a rule of the logic")

    def citation(self, name: str):
        if name in SEED_LAWS:
            if name not in self.cited_seeds:
                self.cited_seeds.append(name)
            return leaf(self.cite[name]), self.seed(name)
        if name in self.known.theorems:
            self.theorem(name)
            return leaf(megalodon_name(name)), self.statement(self.known.theorems[name].proposition)
        if name in self.known.assumed:
            raise CannotRender(
                f"{name} is cited, and is an assumption of the space, not one of the eleven laws")
        if name in self.known.propositions:
            raise CannotRender(
                f"{name} is cited, and is neither one of the eleven laws nor a theorem with a "
                f"native package ({text(self.known.verdicts.get(name, ()))[:160]})")
        raise CannotRender(f"{name} is cited and C does not know it")

    # -- blocks

    def theorem(self, name: str, statement: sx.SExpr | None = None) -> Block:
        """Write a theorem of the space, after what it cites."""
        if name in self.written and statement is None:
            return next(b for b in self.blocks if b.name == name)
        if name in self.known.assumed:
            raise CannotRender(f"{name} is an assumption of the space; it has no proof to export")
        if name not in self.known.theorems:
            verdict = self.known.verdicts.get(name)
            if (is_form(verdict, "Undetermined", 2)
                    and is_form(verdict[1], "set:native-unsupported-proof-form", 2)):
                self.refuse_readable(verdict[1][1])
            raise CannotRender(
                f"C has no native package of {name}: "
                f"{text(verdict)[:300] if verdict is not None else 'not asked'}")
        record = self.known.theorems[name]
        self.written.add(name)
        proposition = self.statement(statement if statement is not None else record.proposition)
        names = Names(self.reserved)
        lines = [f"(** {name}, checked by C. **)",
                 f"Theorem {megalodon_name(name)} : {self.printer(names).formula(proposition)[0]}."]
        lines += self.tactics(record.proof, proposition, names)
        lines.append("Qed.")
        block = Block(name, lines)
        self.blocks.append(block)
        return block

    def tactics(self, proof, goal, names: Names) -> list[str]:
        """Introductions at the goal as `let` and `assume`, then `exact`."""
        lines, ctx = [], Context()
        while (intro := self.introduction(proof)) is not None:
            kind, c_name, proof = intro
            name, _, ctx, goal, assumed = self.introduce(kind, c_name, goal, ctx, names)
            lines.append(f"let {name}." if assumed is None else
                         f"assume {name}: {self.printer(names).formula(assumed)[0]}.")
        term = self.synth(proof, ctx, names)[0]
        body = term.lines(0, self.width - len("exact ."), BIND)
        body[0] = "exact " + body[0]
        body[-1] += "."
        return lines + body

    def seed_blocks(self, seeds) -> list[Block]:
        blocks = []
        for seed in seeds:
            shown = self.printer(Names(self.reserved)).formula(self.seed(seed))[0]
            blocks.append(Block("seed " + seed, [
                f"(** The seed {seed}, as C states it, is {SEED_LAWS[seed]}. **)",
                f"Theorem prime_seed_{seed} : {shown}.",
                f"exact {SEED_LAWS[seed]}.",
                "Qed."]))
        return blocks

    def definition_blocks(self) -> list[Block]:
        """The definitions the written formulas use, each after those it uses."""
        order: list[str] = []

        def visit(name: str) -> None:
            if name in order or name not in self.known.definitions:
                return
            for used in sorted(constants(self.known.definitions[name].body)):
                if used != name:
                    visit(used)
            order.append(name)

        for name in sorted(self.used_constants):
            visit(name)
        blocks = []
        for name in order:
            definition = self.known.definitions[name]
            names = Names(self.reserved)
            params = []
            for binder in definition.params:
                param = names.fresh(binder.hint, "x")
                names.push(binder, param)
                params.append(f"({param}:{type_text(binder.type)})")
            body = self.printer(names).formula(definition.body)[0]
            value = f"fun {' '.join(params)} => {body}" if params else body
            blocks.append(Block("definition " + name, [
                f"(** {name}, as C defines it. **)",
                f"Definition {megalodon_name(name)} : {type_text(definition.type)} := {value}."]))
        return blocks


def constants(term, found=None) -> set[str]:
    found = set() if found is None else found
    if isinstance(term, Const):
        found.add(term.name)
    elif isinstance(term, App):
        constants(term.head, found)
        for arg in term.args:
            constants(arg, found)
    elif isinstance(term, Lam):
        constants(term.body, found)
    elif isinstance(term, All):
        constants(term.family, found)
    elif isinstance(term, Imp):
        constants(term.left, found)
        constants(term.right, found)
    elif isinstance(term, Eq):
        constants(term.left, found)
        constants(term.right, found)
    return found


# ---------------------------------------------------------------- articles

@dataclass
class Article:
    text: str
    theorem: str | None
    first_line: int        # the exported part: its lines in the article
    theorem_lines: tuple[int, int]
    cited_seeds: tuple[str, ...]
    definitions: tuple[str, ...]
    theorems: tuple[str, ...]


def source_label(source: Path) -> str:
    try:
        return str(source.resolve().relative_to(DRAFT))
    except ValueError:
        return source.name


def write_article(known: Known, theorem: str | None, development: str, source_label_text: str,
                  cite: dict[str, str] | None = None,
                  statement: sx.SExpr | None = None, seeds=None) -> Article:
    """The development followed by the export of `theorem` (or, with `seeds`,
    of those seeds alone).  `cite` and `statement` alter the theorem: they
    exist for the negative controls."""
    writer = Writer(known, cite)
    if seeds is None:
        main = writer.theorem(theorem, statement)
        blocks = (writer.seed_blocks(writer.cited_seeds) + writer.definition_blocks()
                  + writer.blocks)
        # The laws the article cites are the laws C's package assumes.
        package = known.theorems[theorem].assumptions
        if statement is None and cite is None and set(package) != set(writer.cited_seeds):
            raise CannotRender(
                f"the package of {theorem} assumes {sorted(package)}, the proof cites "
                f"{sorted(writer.cited_seeds)}")
    else:
        main = None
        blocks = writer.seed_blocks(seeds)
    if main is not None:
        header = [
            "",
            f"(** Exported from C's set theory: the theorem {theorem} of {source_label_text}.",
            "    Each seed C cites is stated as C states it and proved by its Part 7 law;",
            "    the proofs cite the Part 7 laws themselves. **)",
        ]
    else:
        header = [
            "",
            "(** Exported from C's set theory: its seeds, each stated as C states it and",
            "    proved by its Part 7 law. **)",
        ]
    prefix = development.rstrip("\n") + "\n"
    first_line = prefix.count("\n") + 1
    lines = list(header)
    theorem_lines = (0, 0)
    for block in blocks:
        lines.append("")
        start = first_line + len(lines)
        lines += block.lines
        if main is not None and block is main:
            theorem_lines = (start, first_line + len(lines) - 1)
    return Article(prefix + "\n".join(lines) + "\n", theorem, first_line, theorem_lines,
                   tuple(writer.cited_seeds if main else seeds),
                   tuple(b.name.split(" ", 1)[1] for b in blocks
                         if b.name.startswith("definition ")),
                   tuple(b.name for b in writer.blocks))


def check_article(megalodon: Path, preamble: Path, article: Path) -> subprocess.CompletedProcess:
    return subprocess.run([str(megalodon.resolve()), "-I", str(preamble), str(article)],
                          text=True, capture_output=True, check=False)


def failure_line(run: subprocess.CompletedProcess) -> int | None:
    found = re.search(r"line (\d+)", run.stdout + run.stderr)
    return int(found.group(1)) if found else None


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True,
                        help="the .metta file whose space holds the theorem")
    what = parser.add_mutually_exclusive_group(required=True)
    what.add_argument("--theorem", help="the theorem of the space to export")
    what.add_argument("--seeds", action="store_true",
                      help="export the eleven seeds alone, each proved by its Part 7 law")
    parser.add_argument("--development", type=Path, required=True,
                        help="the Megalodon development whose Part 7 states the eleven laws")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--megalodon", type=Path)
    parser.add_argument("--preamble", type=Path)
    args = parser.parse_args(argv)
    label = args.theorem or "seeds"
    try:
        known = gather(args.cetta, args.source, [args.theorem] if args.theorem else [])
        article = write_article(known, args.theorem,
                                args.development.read_text(encoding="utf-8"),
                                source_label(args.source),
                                seeds=list(SEED_LAWS) if args.seeds else None)
    except CannotRender as reason:
        print(f"(MegalodonSetProofExportV1 {label} cannot-render {json.dumps(str(reason))})")
        return 2
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(article.text, encoding="utf-8")
    verdict = "unchecked"
    if args.megalodon and args.preamble:
        run = check_article(args.megalodon, args.preamble, args.out)
        verdict = "accepted" if run.returncode == 0 else "rejected"
        if run.returncode != 0:
            sys.stdout.write(run.stdout + run.stderr)
    print(f"(MegalodonSetProofExportV1 {label} cites ({' '.join(article.cited_seeds)}) "
          f"definitions ({' '.join(article.definitions)}) theorems ({' '.join(article.theorems)}) "
          f"lines {article.theorem_lines[0]}-{article.theorem_lines[1]} megalodon {verdict})")
    return 0 if verdict != "rejected" else 1


if __name__ == "__main__":
    raise SystemExit(main())
