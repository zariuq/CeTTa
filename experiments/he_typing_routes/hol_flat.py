"""HE's typing judgments in the flat reading, replayed in the hosted HOL Light checker.

The flat reading hosts the theory of first-order prenex schemes in HOL:

  - an HE base type is a nullary type operator, a type variable a HOL type variable;
  - an arrow `(-> P1 ... Pn R)` is the (n+1)-ary type operator `he-arrow:n`, which is opaque:
    it is not HOL's function type, so arrows of different arities are different types;
  - a compound non-arrow type is the product of its elements, as in the committed contract;
  - every declaration is its own constant, `he:d<i>` for the i-th declaration, at its
    declared scheme (the declaration keeps its occurrence identity); a literal is a
    constant of its type;
  - an application `(f a1 ... an)` is `he-apply:n f a1 ... an`, where the polymorphic
    constant `he-apply:n : he-arrow:n A1 ... An B -> A1 -> ... -> An -> B` applies an
    n-ary arrow to exactly n arguments;
  - a tuple is the pairing of its elements, ending in `one`.

A judgment's types are those of its first-order derivations that the checker reads at the
expected type.  A refusal is earned when every way of typing the judgment fails at a step
that the checker refuses: the application constant of another arity, or an argument whose
type differs from its parameter under the most general unifier of the earlier parameters.
Derivations through `%Undefined%`, a meta-type parameter or a subtype declaration are not
translated, so they leave the judgment open.
"""

from __future__ import annotations

from typing import Optional

import fo_schemes as F
from encoders import Names, final
from sexpr import Var


class Untranslatable(Exception):
    """A derivation step that the flat reading does not translate."""


def hol_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def hol_list(items: list[str]) -> str:
    text = "HOL:Nil"
    for item in reversed(items):
        text = f"(HOL:Cons {item} {text})"
    return text


class HOLFlat:
    def __init__(self, space: F.Space):
        self.space = space
        self.type_names = Names("he:t")
        self.literal_names = Names("he:l")
        self.operators: dict[str, int] = {}
        self.constants: dict[str, str] = {}
        self.products = False

    # ------------------------------------------------------------ types

    def operator(self, name: str, arity: int) -> str:
        self.operators.setdefault(name, arity)
        return name

    @staticmethod
    def tyapp(name: str, arguments: list[str]) -> str:
        return f"(Tyapp {hol_string(name)} {hol_list(arguments)})"

    def fun(self, domain: str, codomain: str) -> str:
        return self.tyapp("fun", [domain, codomain])

    def unit(self) -> str:
        self.products = True
        return self.tyapp("1", [])

    def prod(self, left: str, right: str) -> str:
        self.products = True
        return self.tyapp("prod", [left, right])

    def arrow(self, components: list[str]) -> str:
        n = len(components) - 1
        return self.tyapp(self.operator(f"he-arrow:{n}", n + 1), components)

    def ty(self, atom) -> str:
        if isinstance(atom, Var):
            return f'(Tyvar {hol_string("v_" + atom.name.replace("#", "_"))})'
        if atom == F.UNDEFINED:
            raise Untranslatable("gradual")
        if isinstance(atom, tuple):
            if F.is_arrow(atom):
                return self.arrow([self.ty(item) for item in atom[1:]])
            value = self.unit()
            for item in reversed(atom):
                value = self.prod(self.ty(item), value)
            return value
        return self.tyapp(self.operator(self.type_names(atom), 0), [])

    # ------------------------------------------------------------ terms

    def constant(self, name: str, scheme: str) -> str:
        self.constants.setdefault(name, scheme)
        return name

    @staticmethod
    def const(name: str, typ: str) -> str:
        return f"(Const {hol_string(name)} {typ})"

    @staticmethod
    def comb(function: str, argument: str) -> str:
        return f"(Comb {function} {argument})"

    def declaration(self, index: int) -> str:
        _, typ = self.space.declarations[index]
        return self.constant(f"he:d{index}", self.ty(typ))

    def apply_constant(self, n: int) -> str:
        names = [f"A{i}" for i in range(n)] + ["B"]
        variables = [f"(Tyvar {hol_string(x)})" for x in names]
        body = variables[-1]
        for variable in reversed(variables[:-1]):
            body = self.fun(variable, body)
        return self.constant(f"he-apply:{n}", self.fun(self.arrow(variables), body))

    def apply_at(self, parameters: list[str], result: str) -> str:
        body = result
        for parameter in reversed(parameters):
            body = self.fun(parameter, body)
        name = self.apply_constant(len(parameters))
        return self.const(name, self.fun(self.arrow(parameters + [result]), body))

    def tuple(self, terms: list[str], types: list[str]) -> str:
        value = self.const("one", self.unit())
        value_type = self.unit()
        for term, typ in reversed(list(zip(terms, types))):
            pair_type = self.prod(typ, value_type)
            pairing = self.const(",", self.fun(typ, self.fun(value_type, pair_type)))
            value = self.comb(self.comb(pairing, term), value)
            value_type = pair_type
        return value

    def term(self, derivation: Optional[F.Derivation], substs: list) -> str:
        if derivation is None:
            raise Untranslatable("gradual")
        rule = derivation.rule
        if rule == "literal":
            name = self.constant(self.literal_names(derivation.term), self.ty(derivation.type))
            return self.const(name, self.ty(derivation.type))
        if rule in ("decl", "annotation"):
            name = self.declaration(derivation.detail["declaration"])
            return self.const(name, self.ty(final(derivation.type, substs)))
        if rule == "tuple":
            terms = [self.term(part, substs) for part in derivation.parts]
            types = [self.ty(final(part.type, substs)) for part in derivation.parts]
            return self.tuple(terms, types)
        if rule == "app":
            accepted = derivation.detail["accepted"]
            if any(how != "typed" for how in accepted):
                raise Untranslatable("meta-type" if "meta" in accepted else "gradual")
            inner = substs + [derivation.detail["subst"]]
            function = derivation.detail["function"]
            parameters, result = function.type[1:-1], function.type[-1]
            term = self.apply_at([self.ty(final(p, inner)) for p in parameters],
                                 self.ty(final(result, inner)))
            term = self.comb(term, self.term(function, inner))
            for part in derivation.parts:
                term = self.comb(term, self.term(part, inner))
            return term
        raise Untranslatable(rule)

    # ------------------------------------------------------------ attempts

    def attempt(self, failure) -> tuple[list[str], str]:
        """One failed way of typing an application: the terms the checker must read, and
        the term one application step longer that it must refuse.  The step is refused
        only for the failure: the instance of `he-apply` agrees with the head on every
        component it can, and the earlier arguments are read at the most general unifier
        of their parameters."""
        kind = failure[0]
        if kind == "arity":
            _, function, count = failure
            parameters, result = function.type[1:-1], function.type[-1]
            components = [self.ty(p) for p in parameters[:count]]
            components += [f"(Tyvar {hol_string(f'A{i}')})" for i in range(len(components), count)]
            apply = self.apply_at(components, self.ty(result))
            head = self.term(function, [])
            return [apply, head], self.comb(apply, head)
        _, function, chosen, subst, index = failure
        parameters, result = function.type[1:-1], function.type[-1]
        term = self.apply_at([self.ty(final(p, [subst])) for p in parameters],
                             self.ty(final(result, [subst])))
        term = self.comb(term, self.term(function, [subst]))
        for part in chosen[:-1]:
            term = self.comb(term, self.term(part, [subst]))
        failing = self.term(chosen[-1], [subst])
        return [term, failing], self.comb(term, failing)

    # ------------------------------------------------------------ signature

    def commands(self) -> str:
        steps = [f"(HOL:Step {hol_string('type:' + name)} "
                 f"(DECLARE_TYPE {hol_string(name)} {arity}))"
                 for name, arity in self.operators.items()]
        if self.products:
            a, b = '(Tyvar "A")', '(Tyvar "B")'
            pair = self.tyapp("prod", [a, b])
            steps.append('(HOL:Step "type:prod" (DECLARE_TYPE "prod" 2))')
            steps.append('(HOL:Step "type:1" (DECLARE_TYPE "1" 0))')
            for name, typ in ((",", self.fun(a, self.fun(b, pair))), ("FST", self.fun(pair, a)),
                              ("SND", self.fun(pair, b)), ("one", self.tyapp("1", []))):
                steps.append(f"(HOL:Step {hol_string('constant:' + name)} "
                             f"(DECLARE_CONSTANT {hol_string(name)} {typ}))")
        steps += [f"(HOL:Step {hol_string('constant:' + name)} "
                  f"(DECLARE_CONSTANT {hol_string(name)} {typ}))"
                  for name, typ in self.constants.items()]
        return hol_list(steps)


# ---------------------------------------------------------------- failures


def failures(space: F.Space, term) -> Optional[list]:
    """Every way of typing `term` fails: the failed steps, one per way, or None when some
    way fails other than at an application step (or `term` is typed or unknown)."""
    typing = F.types(space, term)
    if typing.derivations or not typing.errors:
        return None
    if not isinstance(term, tuple) or not term:
        return None
    head, arguments = term[0], term[1:]
    head_typing = F.types(space, head)
    if not head_typing.derivations:
        return failures(space, head)
    argument_typings = [F.types(space, argument) for argument in arguments]
    found: list = []
    if any(not F.is_arrow(d.type) for d in head_typing.derivations):
        # A tuple fails only at an element with no type.
        missing = next(((a, t) for a, t in zip(arguments, argument_typings)
                        if not t.derivations), None)
        if missing is None or not missing[1].errors:
            return None
        inner = failures(space, missing[0])
        if inner is None:
            return None
        found += inner
    for function in head_typing.derivations:
        if not F.is_arrow(function.type):
            continue
        parameters = function.type[1:-1]
        if len(parameters) != len(arguments):
            found.append(("arity", function, len(arguments)))
            continue
        inner = _mismatches(space, function, list(parameters), list(arguments),
                            argument_typings, {}, [])
        if inner is None:
            return None
        found += inner
    return found


def _mismatches(space, function, parameters, arguments, typings, subst, chosen):
    if not parameters:
        return None
    parameter, argument, typing = parameters[0], arguments[0], typings[0]
    now = F.resolve(parameter, subst)
    meta = now == F.ATOM_TYPE or now == F.meta_type(argument)
    if not typing.derivations and typing.errors and not meta:
        return failures(space, argument)
    if not typing.derivations or now == F.UNDEFINED or meta:
        return None
    found: list = []
    for derivation in typing.derivations:
        extended = F.unify(derivation.type, parameter, subst)
        if extended is None:
            found.append(("mismatch", function, chosen + [derivation], subst, len(chosen)))
            continue
        inner = _mismatches(space, function, parameters[1:], arguments[1:], typings[1:],
                            extended, chosen + [derivation])
        if inner is None:
            return None
        found += inner
    return found


# ---------------------------------------------------------------- the checking programme

HEADER = """(scope:profile lexical-fresh per-call reference)
!(import! &self {lib}/data.metta)
!(import! &self {lib}/syntax.metta)
!(import! &self {lib}/kernel.metta)
!(import! &self {lib}/definitions.metta)

; Accepted when the checker reads the term at the type; Refused when it does not read it.
(= (flat-check $signature $term $type)
   (case (hol-light:run HOL:Nil $signature)
     (((Some $state)
        (case (hol-light:read $state $term)
          (((Some $value)
             (if (== (hol-light:term-type $value) (Some $type)) Accepted WrongType))
           ($refused Refused))))
      ($refused NoSignature))))
(= (flat-read $signature $term)
   (case (hol-light:run HOL:Nil $signature)
     (((Some $state)
        (case (hol-light:read $state $term)
          (((Some $value) Read)
           ($refused Refused))))
      ($refused NoSignature))))
"""


def judgment_checks(space: F.Space, expression):
    """The checks of one judgment: ("type", tag-suffix, line-builder, type) for each
    derivation, or ("refute", ...) for each failed way of typing it.  Returns the plan and
    the checker lines."""
    typing = F.types(space, expression)
    checks, lines = [], []
    if typing.derivations:
        for k, derivation in enumerate(typing.derivations):
            flat = HOLFlat(space)
            try:
                term = flat.term(derivation, [])
                substs = ([derivation.detail["subst"]] if derivation.rule == "app" else [])
                typ = flat.ty(final(derivation.type, substs))
            except Untranslatable as why:
                checks.append(("open", k, str(why), F.evaluate_ground(derivation.type)))
                continue
            # The mutation control: the same term at a type it does not have.
            mutant = flat.tyapp(flat.operator("he:mutant", 0), [])
            commands = flat.commands()
            lines.append((k, f"(Checked (flat-check {commands} {term} {typ}) "
                             f"(flat-check {commands} {term} {mutant}))"))
            checks.append(("type", k, None, F.evaluate_ground(derivation.type)))
        return "typed", checks, lines
    found = failures(space, expression)
    if not found:
        return ("unknown" if not typing.errors else "open"), checks, lines
    for k, failure in enumerate(found):
        flat = HOLFlat(space)
        try:
            prefixes, term = flat.attempt(failure)
        except Untranslatable as why:
            checks.append(("open", k, str(why), None))
            continue
        commands = flat.commands()
        reads = " ".join(f"(flat-read {commands} {prefix})" for prefix in prefixes)
        lines.append((k, f"(Attempt ({reads}) (flat-read {commands} {term}))"))
        checks.append(("refute", k, failure[0], None))
    return "refuted", checks, lines
