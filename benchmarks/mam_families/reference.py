"""Reference evaluator for the equation fragment used by the frozen MAM families.

This is an independent implementation of PeTTa's observable semantics for a small
fragment, written against Python term structures (never by parsing a .metta file
or reading an engine's output):

* a relation is a symbol with equations; calling it tries every equation in
  authored order, renaming its variables apart and unifying the head arguments
  with the call's argument values (optionally with an occurs check);
* arguments of a relation call, and function-headed subterms of data, are
  evaluated eagerly, left to right, before the enclosing term is built or called;
* `let` / `let*` unify each answer of the bound expression with the pattern and
  continue; `if` evaluates its condition and one branch; `(empty)` has no answer;
* integer arithmetic `+ - *`, floor `%`, comparisons `< > <= >=`, and `==`
  (integer equality, or structural equality of ground terms);
* `(add-atom &self (= lhs rhs))` adds a ground equation and answers `true`; a call
  uses the equations present when it was entered (the logical update view), so
  calls already running keep their snapshot and later calls see the addition;
* answers of a top-level query are produced in depth-first order, duplicates kept.

The evaluator is written with generators: each yielded value is one answer, and a
suspended generator chain is the pending caller computation.  It counts calls and equation-head attempts per relation:
attempts[r] = calls[r] * |equations(r)|, the engine-independent attempt measure.
"""
from __future__ import annotations

import sys
from dataclasses import dataclass, field

sys.setrecursionlimit(1_000_000)


class Var:
    """A source variable (in equations and queries): identified by name."""

    __slots__ = ("name",)

    def __init__(self, name: str):
        self.name = name

    def __repr__(self):
        return "$" + self.name


class Cell:
    """A runtime logical variable."""

    __slots__ = ("ref",)

    def __init__(self):
        self.ref = None


def V(name: str) -> Var:
    return Var(name)


def E(*items):
    return tuple(items)


def run_deep(fn, *args):
    """Run fn on a thread with a large stack: continuation chains are deep."""
    import threading
    threading.stack_size(1 << 30)
    box = {}

    def target():
        try:
            box["value"] = fn(*args)
        except BaseException as exc:  # re-raised in the caller
            box["error"] = exc
    th = threading.Thread(target=target)
    th.start()
    th.join()
    if "error" in box:
        raise box["error"]
    return box["value"]


def render(t) -> str:
    if isinstance(t, tuple):
        return "(" + " ".join(render(a) for a in t) + ")"
    if isinstance(t, Var):
        return "$" + t.name
    if isinstance(t, bool):
        return "True" if t else "False"
    return str(t)


ARITH = {"+", "-", "*", "%", "<", ">", "<=", ">=", "=="}


@dataclass
class Program:
    equations: dict = field(default_factory=dict)  # name -> list[(args tuple, rhs)]
    occurs_check: bool = False

    def eq(self, lhs: tuple, rhs):
        self.equations.setdefault(lhs[0], []).append((lhs[1:], rhs))
        return self

    def text(self) -> str:
        lines = []
        for name, eqs in self.equations.items():
            for args, rhs in eqs:
                lines.append(f"(= {render((name,) + args)}\n   {render(rhs)})")
        return "\n".join(lines) + "\n"


class Evaluator:
    def __init__(self, program: Program):
        self.p = program
        self.undo: list = []
        self.calls: dict = {}
        self.attempts: dict = {}

    # -- terms -----------------------------------------------------------
    @staticmethod
    def deref(t):
        while type(t) is Cell and t.ref is not None:
            t = t.ref
        return t

    def occurs(self, v, t) -> bool:
        t = self.deref(t)
        if t is v:
            return True
        if type(t) is tuple:
            return any(self.occurs(v, a) for a in t)
        return False

    def unify(self, a, b) -> bool:
        a, b = self.deref(a), self.deref(b)
        if a is b:
            return True
        if type(a) is Cell:
            if self.p.occurs_check and self.occurs(a, b):
                return False
            a.ref = b
            self.undo.append(a)
            return True
        if type(b) is Cell:
            if self.p.occurs_check and self.occurs(b, a):
                return False
            b.ref = a
            self.undo.append(b)
            return True
        if type(a) is tuple and type(b) is tuple:
            if len(a) != len(b):
                return False
            return all(self.unify(x, y) for x, y in zip(a, b))
        if type(a) is bool or type(b) is bool:
            return type(a) is type(b) and a == b
        return a == b

    def reset(self, mark: int):
        undo = self.undo
        while len(undo) > mark:
            undo.pop().ref = None

    def resolve(self, t):
        t = self.deref(t)
        if type(t) is tuple:
            return tuple(self.resolve(a) for a in t)
        if type(t) is Cell:
            raise ValueError("answer contains an unbound variable")
        return t

    @staticmethod
    def instantiate(t, env: dict):
        if type(t) is Var:
            c = env.get(t.name)
            if c is None:
                c = env[t.name] = Cell()
            return c
        if type(t) is tuple:
            return tuple(Evaluator.instantiate(a, env) for a in t)
        return t

    # -- evaluation (generators: one yielded value per answer, in order) ---
    def eval_list(self, items, i=0):
        if i == len(items):
            yield ()
            return
        for v in self.eval(items[i]):
            for rest in self.eval_list(items, i + 1):
                yield (v,) + rest

    def eval(self, t):
        # A variable holds a value; values are never re-evaluated.
        if type(t) is Cell or type(t) is not tuple or not t:
            yield t
            return
        head = t[0]
        if head == "if":
            for c in self.eval(t[1]):
                c = self.deref(c)
                if c is True:
                    yield from self.eval(t[2])
                elif c is False:
                    yield from self.eval(t[3])
                else:
                    raise ValueError(f"non-Boolean condition {c!r}")
            return
        if head == "empty" and len(t) == 1:
            return
        if head == "let":
            pat, bound, body = t[1], t[2], t[3]
            for v in self.eval(bound):
                mark = len(self.undo)
                if self.unify(pat, v):
                    yield from self.eval(body)
                self.reset(mark)
            return
        if head == "let*":
            bindings = t[1]
            if not bindings:
                yield from self.eval(t[2])
                return
            (pat, bound), rest = bindings[0], bindings[1:]
            yield from self.eval(("let", pat, bound, ("let*", rest, t[2])))
            return
        if head == "add-atom" and len(t) == 3 and t[1] == "&self":
            atom = self.resolve(t[2])
            if type(atom) is not tuple or len(atom) != 3 or atom[0] != "=" or \
                    type(atom[1]) is not tuple or not atom[1]:
                raise ValueError(f"add-atom expects a ground equation, got {atom!r}")
            self.p.eq(atom[1], atom[2])
            yield True
            return
        if head in ARITH and len(t) == 3:
            for vals in self.eval_list(t[1:]):
                x, y = (self.deref(v) for v in vals)
                if head == "==" and (type(x) is not int or type(y) is not int):
                    # Structural equality of ground terms.
                    yield self.resolve(x) == self.resolve(y)
                    continue
                if type(x) is not int or type(y) is not int:
                    raise ValueError(f"non-integer operand in {head}")
                if head == "+": yield x + y
                elif head == "-": yield x - y
                elif head == "*": yield x * y
                elif head == "%":
                    if y == 0:
                        raise ZeroDivisionError
                    yield x % y
                elif head == "<": yield x < y
                elif head == ">": yield x > y
                elif head == "<=": yield x <= y
                elif head == ">=": yield x >= y
                else: yield x == y
            return
        if type(head) is str and head in self.p.equations:
            for args in self.eval_list(t[1:]):
                yield from self.call(head, args)
            return
        # Data: evaluate children (function-headed subterms), rebuild.
        yield from self.eval_list(t)

    def call(self, name, args):
        eqs = list(self.p.equations[name])  # the equations present at entry
        self.calls[name] = self.calls.get(name, 0) + 1
        self.attempts[name] = self.attempts.get(name, 0) + len(eqs)
        for params, rhs in eqs:
            if len(params) != len(args):
                continue
            env: dict = {}
            mark = len(self.undo)
            if all(self.unify(self.instantiate(q, env), a) for q, a in zip(params, args)):
                yield from self.eval(self.instantiate(rhs, env))
            self.reset(mark)

    def run(self, query) -> list[str]:
        return [render(self.resolve(v)) for v in self.eval(self.instantiate(query, {}))]

    def counts(self) -> dict:
        return dict(calls=dict(self.calls), attempts=dict(self.attempts),
                    total_calls=sum(self.calls.values()),
                    total_attempts=sum(self.attempts.values()))
