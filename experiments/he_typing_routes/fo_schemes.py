"""HE's typing judgments in the theory of first-order prenex schemes.

Types are first-order terms: atoms, with `$` variables.  A declaration `(: c T)` gives `c`
every instance of the scheme `T`; `(:< A B)` gives everything of type `A` the type `B`; a
declaration of an expression types that expression.  An arrow `(-> P1 ... Pn R)` is an
n-ary type constructor, not a curried one: an application `(f a1 ... an)` has type `σR`
when `σ` unifies the argument types with `P1 ... Pn`, and an application of another arity
has no type.  An expression whose head has a non-arrow type also has the tuple of its
elements' types.  Unification is syntactic; `%Undefined%` matches any type, and a
parameter `Atom`, or the meta-type of the argument's shape, accepts the argument as it
is.  There are no binders, no λ-terms and no conversion.

`Derivation` records how a type was obtained, which the LF and HOTG encodings replay.
A judgment has no type exactly when no derivation exists.
"""

from __future__ import annotations

import itertools
from dataclasses import dataclass, field
from typing import Iterator, Optional

from sexpr import Atom, Str, Sym, Var, read_forms, show

ARROW = Sym("->")
UNDEFINED = Sym("%Undefined%")
ATOM_TYPE = Sym("Atom")
META = {"Symbol", "Variable", "Grounded", "Expression"}

# HE's standard declarations that the corpus relies on (HE's stdlib answers to get-type).
PRELUDE = """
(: + (-> Number Number Number))
(: - (-> Number Number Number))
(: * (-> Number Number Number))
(: == (-> $t $t Bool))
(: < (-> Number Number Bool))
(: = (-> $t $t %Undefined%))
(: True Bool)
(: False Bool)
"""

_fresh = itertools.count()


def variables(atom: Atom, found: Optional[list] = None) -> list:
    """The variables of an atom, in order of first appearance."""
    found = [] if found is None else found
    if isinstance(atom, Var):
        if atom not in found:
            found.append(atom)
    elif isinstance(atom, tuple):
        for item in atom:
            variables(item, found)
    return found


def fresh_copy(atom: Atom, mapping: Optional[dict] = None) -> Atom:
    mapping = {} if mapping is None else mapping
    if isinstance(atom, Var):
        if atom not in mapping:
            mapping[atom] = Var(f"{atom.name}#{next(_fresh)}")
        return mapping[atom]
    if isinstance(atom, tuple):
        return tuple(fresh_copy(item, mapping) for item in atom)
    return atom


def walk(atom: Atom, subst: dict) -> Atom:
    while isinstance(atom, Var) and atom in subst:
        atom = subst[atom]
    return atom


def resolve(atom: Atom, subst: dict) -> Atom:
    atom = walk(atom, subst)
    if isinstance(atom, tuple):
        return tuple(resolve(item, subst) for item in atom)
    return atom


def occurs(var: Var, atom: Atom, subst: dict) -> bool:
    atom = walk(atom, subst)
    if atom == var:
        return True
    return isinstance(atom, tuple) and any(occurs(var, item, subst) for item in atom)


def unify(left: Atom, right: Atom, subst: dict) -> Optional[dict]:
    """Syntactic unification; `%Undefined%` matches anything."""
    left, right = walk(left, subst), walk(right, subst)
    if left == UNDEFINED or right == UNDEFINED:
        return subst
    if left == right:
        return subst
    if isinstance(left, Var):
        if occurs(left, right, subst):
            return None
        extended = dict(subst)
        extended[left] = right
        return extended
    if isinstance(right, Var):
        return unify(right, left, subst)
    if isinstance(left, tuple) and isinstance(right, tuple) and len(left) == len(right):
        for a, b in zip(left, right):
            subst = unify(a, b, subst)
            if subst is None:
                return None
        return subst
    return None


def is_arrow(atom: Atom) -> bool:
    return isinstance(atom, tuple) and len(atom) > 1 and atom[0] == ARROW


def meta_type(atom: Atom) -> Sym:
    if isinstance(atom, Sym):
        return Sym("Symbol")
    if isinstance(atom, Var):
        return Sym("Variable")
    if isinstance(atom, tuple):
        return Sym("Expression")
    return Sym("Grounded")


@dataclass
class Derivation:
    """How a type was obtained: `rule` is decl, subtype, literal, tuple, annotation or app."""
    rule: str
    type: Atom
    term: Atom
    detail: dict = field(default_factory=dict)
    parts: list = field(default_factory=list)


class Space:
    def __init__(self, atoms: list[Atom], binders: bool = False):
        # With `binders`, a parameter `(: $x A)` names its argument: later parameters and
        # the result see the argument itself for `$x`, the dependent reading.  Without it,
        # as in HE, `(: $x A)` is a type like any other.
        self.binders = binders
        self.declarations: list[tuple[Atom, Atom]] = []
        self.subtypes: list[tuple[Atom, Atom]] = []
        for atom in read_forms(PRELUDE):
            self._add(atom.atom)
        for atom in atoms:
            self._add(atom)

    def _add(self, atom: Atom) -> None:
        if isinstance(atom, tuple) and len(atom) == 3:
            if atom[0] == Sym(":"):
                self.declarations.append((atom[1], atom[2]))
            elif atom[0] == Sym(":<"):
                self.subtypes.append((atom[1], atom[2]))

    def declared(self, term: Atom) -> list[tuple[int, Atom, list]]:
        """Fresh instances of the declarations of `term`, with their positions and the
        fresh variables standing for the scheme's variables, in order."""
        found = []
        for index, (subject, typ) in enumerate(self.declarations):
            if subject == term and typ != UNDEFINED:
                mapping: dict = {}
                fresh = fresh_copy(typ, mapping)
                found.append((index, fresh, [mapping[v] for v in variables(typ)]))
        return found

    def supertypes(self, typ: Atom, depth: int = 64) -> list[Atom]:
        """Every type reached from `typ` through subtype declarations, `typ` first."""
        found = [typ]
        frontier = [typ]
        for _ in range(depth):
            following = []
            for current in frontier:
                for sub, sup in self.subtypes:
                    sub, sup = fresh_copy((sub, sup))
                    bindings = unify(sub, current, {})
                    if bindings is not None:
                        image = resolve(sup, bindings)
                        if image not in found:
                            found.append(image)
                            following.append(image)
            if not following:
                break
            frontier = following
        return found


@dataclass
class Typing:
    derivations: list[Derivation]
    errors: int


def literal_type(atom: Atom) -> Optional[Atom]:
    if isinstance(atom, bool):
        return Sym("Bool")
    if isinstance(atom, (int, float)):
        return Sym("Number")
    if isinstance(atom, Str):
        return Sym("String")
    return None


def types(space: Space, term: Atom) -> Typing:
    if isinstance(term, Var):
        return Typing([], 0)
    literal = literal_type(term)
    if literal is not None:
        return Typing([Derivation("literal", literal, term)], 0)
    if isinstance(term, Sym):
        found = []
        for index, typ, fresh in space.declared(term):
            for reached in space.supertypes(typ):
                rule = "decl" if reached is typ else "subtype"
                found.append(Derivation(rule, reached, term,
                                        {"declaration": index, "vars": fresh}))
        return Typing(found, 0)
    if not term:
        return Typing([], 0)
    head, arguments = term[0], term[1:]
    head_typing = types(space, head)
    argument_typings = [types(space, argument) for argument in arguments]
    found: list[Derivation] = []
    errors = 0
    values = [d for d in head_typing.derivations if not is_arrow(d.type)]
    functions = [d for d in head_typing.derivations if is_arrow(d.type)]
    if values and all(t.derivations for t in argument_typings):
        for choice in itertools.product(values, *[t.derivations for t in argument_typings]):
            tuple_type = tuple(d.type for d in choice)
            for reached in space.supertypes(tuple_type):
                found.append(Derivation("tuple", reached, term, parts=list(choice)))
    for index, typ, fresh in space.declared(term):
        for reached in space.supertypes(typ):
            rule = "annotation" if reached is typ else "subtype"
            found.append(Derivation(rule, reached, term, {"declaration": index, "vars": fresh}))
    if not head_typing.derivations and head_typing.errors:
        # An ill-typed head makes the expression ill-typed.
        errors += 1
    for function in functions:
        parameters, result = function.type[1:-1], function.type[-1]
        if len(parameters) != len(arguments):
            errors += 1
            continue
        produced = 0
        for subst, parts, accepted in _applications(parameters, arguments, argument_typings, {},
                                                    space.binders):
            found.append(Derivation("app", resolve(result, subst), term,
                                    {"function": function, "accepted": accepted,
                                     "subst": subst}, parts))
            produced += 1
        if produced == 0:
            errors += 1
    return Typing(found, errors)


def binder(parameter: Atom) -> Optional[tuple[Var, Atom]]:
    """The variable and domain of a parameter `(: $x A)`."""
    if (isinstance(parameter, tuple) and len(parameter) == 3 and parameter[0] == Sym(":")
            and isinstance(parameter[1], Var)):
        return parameter[1], parameter[2]
    return None


def _applications(parameters, arguments, typings, subst,
                  binders: bool = False) -> Iterator[tuple[dict, list, list]]:
    if not parameters:
        yield subst, [], []
        return
    parameter, argument, typing = parameters[0], arguments[0], typings[0]
    rest = (parameters[1:], arguments[1:], typings[1:])
    named = binder(parameter) if binders else None
    if named is not None:
        variable, domain = named
        bound = unify(variable, argument, subst)
        if bound is None:
            return
        for tail_subst, parts, accepted in _applications(
                [domain] + list(rest[0]), [argument] + list(rest[1]),
                [typing] + list(rest[2]), bound, binders):
            yield tail_subst, parts, accepted
        return
    parameter_now = resolve(parameter, subst)
    meta = parameter_now == ATOM_TYPE or parameter_now == meta_type(argument)
    if not typing.derivations and typing.errors and not meta:
        # An ill-typed argument makes the application ill-typed.
        return
    if (not typing.derivations or parameter_now == UNDEFINED or meta):
        how = ("untyped" if not typing.derivations else "undefined" if parameter_now == UNDEFINED
               else "meta")
        for tail_subst, parts, accepted in _applications(*rest, subst, binders):
            yield tail_subst, [None] + parts, [how] + accepted
        return
    for derivation in typing.derivations:
        extended = unify(derivation.type, parameter, subst)
        if extended is None:
            continue
        for tail_subst, parts, accepted in _applications(*rest, extended, binders):
            yield tail_subst, [derivation] + parts, ["typed"] + accepted


def evaluate_ground(atom: Atom) -> Atom:
    """Reduce ground arithmetic, as HE's interpreter reduces a returned type."""
    if isinstance(atom, tuple):
        atom = tuple(evaluate_ground(item) for item in atom)
        if (len(atom) == 3 and isinstance(atom[0], Sym) and atom[0].name in "+-*"
                and all(isinstance(x, int) for x in atom[1:])):
            a, b = atom[1], atom[2]
            return {"+": a + b, "-": a - b, "*": a * b}[atom[0].name]
    return atom


def get_type(space: Space, term: Atom) -> tuple[list[Atom], list[Derivation]]:
    """HE's `get-type`: the types of every derivation, or `%Undefined%` when there is no
    derivation and no failed application."""
    typing = types(space, term)
    if not typing.derivations:
        return ([UNDEFINED] if typing.errors == 0 else []), []
    return [evaluate_ground(d.type) for d in typing.derivations], typing.derivations


def canonical(atom: Atom, mapping: Optional[dict] = None) -> Atom:
    """Rename variables in order of appearance, for comparison up to renaming."""
    mapping = {} if mapping is None else mapping
    if isinstance(atom, Var):
        if atom not in mapping:
            mapping[atom] = Var(f"v{len(mapping)}")
        return mapping[atom]
    if isinstance(atom, tuple):
        return tuple(canonical(item, mapping) for item in atom)
    return atom


def answer_text(type_list: list[Atom]) -> str:
    return show(tuple(canonical(t) for t in type_list))


def features(space: Space, term: Atom) -> list[str]:
    """The rules and kinds of types a judgment's derivations use: its fragment."""
    typing = types(space, term)
    found: set[str] = set()

    def visit(d: Optional[Derivation]) -> None:
        if d is None:
            return
        if d.rule in ("decl", "annotation", "subtype"):
            if d.rule != "decl":
                found.add(d.rule)
            if d.detail.get("vars"):
                found.add("scheme")
            subject = d.term
            if sum(1 for s, _ in space.declarations if s == subject) > 1:
                found.add("overload")
        if d.rule == "tuple":
            found.add("tuple")
        if d.rule == "literal":
            found.add("literal")
        if d.rule == "app":
            for how in d.detail["accepted"]:
                if how in ("untyped", "undefined"):
                    found.add("gradual")
                if how == "meta":
                    found.add("meta-type")
            function = d.detail["function"]
            if is_arrow(function.type[-1]):
                found.add("arrow-return")
            if isinstance(d.term, tuple) and isinstance(d.term[0], tuple):
                found.add("expression-head")
            for part in d.parts:
                if part is not None and is_arrow(resolve(part.type, d.detail["subst"])):
                    found.add("arrow-argument")
            visit(function)
        for part in d.parts:
            visit(part)

    for d in typing.derivations:
        visit(d)
    if not typing.derivations:
        found.add("refuted" if typing.errors else "unknown")
    elif len(typing.derivations) > 1:
        found.add("several-types")
    if typing.derivations and any(evaluate_ground(d.type) != d.type for d in typing.derivations):
        found.add("answer-evaluated")
    if not found - {"literal"}:
        found.add("static")
    return sorted(found)
