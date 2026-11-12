"""HE's first-order typing in the HOTG set profile, with its closure law.

The encoding hosts the theory of first-order prenex schemes (`fo_schemes.py`) in HOTG.

Codes.  An HE atom is coded as a set built from one constant per symbol or literal and one
function `tpN` per length of expression; the codes are distinct and `tpN` is injective (the
unique-names laws below).  An arrow `(-> P1 ... Pn R)` is the code of that expression, so it
is the code of the n-ary function space; an application `(f a1 ... an)` is the code of the
expression, applied by the application law.

Membership.  `el x` is the set an atom stands for as an inhabitant and `ty T` the set of
inhabitants of the type `T`: `x : T` is `In (el x) (ty T)`.  Keeping the two images apart is
what makes the encoding sound for atoms typed by themselves, such as `(: Type Type)`.

The laws are HE's typing rules:
  - a declaration is a constant at every proper instance of its declared type (one law per
    declaration, so a symbol declared twice keeps both); a literal has its intrinsic type;
  - an application of an n-ary arrow to exactly n accepted arguments has the arrow's return
    type, where an argument is accepted at a parameter when it has the parameter type, has no
    type and is not an ill-typed application, or the parameter is `%Undefined%`, `Atom` or
    the meta-type of the argument's shape;
  - an expression whose head has a value type has the tuple of its elements' types, and the
    declared supertypes of that tuple;
  - subtypes extend a symbol's declared types.
Scheme variables range over proper types: HE never instantiates one with `%Undefined%` or a
meta-type.

The closure law is the completion of each fibre: an atom has a type only by one of the rules
that conclude it, acceptance holds only by one of its cases, and the codes are free (distinct
names, injective tuple codes).  It holds in the model that reads every law by HE's own typing,
built by recursion on atoms, so the extension is conservative over HOTG: every model of HOTG
expands to one of it.

Positive judgments are proved from the rules; a refusal is proved from the closure law by case
analysis over every way of typing the atom, each case closed by the unique-names laws.
"""

from __future__ import annotations

import itertools
from dataclasses import dataclass, field
from typing import Callable, Optional

import fo_schemes as F
from sexpr import Str, Sym, Var

RESERVED = {"%Undefined%": "hUndef", "Atom": "hAtom", "Symbol": "hSymbol",
            "Expression": "hExpression", "Grounded": "hGrounded", "Variable": "hVariable",
            "->": "hArrow"}
META = {"hSymbol": "IsSym", "hExpression": "IsExpr", "hGrounded": "IsGnd", "hVariable": "IsVar"}


class Unsupported(Exception):
    """A step the encoding does not prove."""


class NotRefutable(Exception):
    """A case of the closure law that the unique-names laws do not close."""


# ---------------------------------------------------------------- code terms
#
# ("leaf", name) | ("tp", n, [terms]) | ("var", name)


def leaf(name):
    return ("leaf", name)


def tp(items):
    return ("tp", len(items), list(items))


def var(name):
    return ("var", name)


def show(term) -> str:
    if term[0] == "leaf" or term[0] == "var":
        return term[1]
    return f"(tp{term[1]} " + " ".join(show(t) for t in term[2]) + ")" if term[2] else "tp0"


def forall(names, body, sort="set"):
    for name in reversed(names):
        body = f"(all {sort} (lam {name} {body}))"
    return body


def imps(premises, conclusion):
    for premise in reversed(premises):
        conclusion = f"(imp {premise} {conclusion})"
    return conclusion


def member(x, t) -> str:
    return f"(In (el {show(x)}) (ty {show(t)}))"


def eq(a, b) -> str:
    return f"(eq set {show(a)} {show(b)})"


def all_elim(proof: str, terms) -> str:
    for term in terms:
        proof = f"(pf:all-elim {proof} {term if isinstance(term, str) else show(term)})"
    return proof


def imp_elim(proof: str, premises) -> str:
    for premise in premises:
        proof = f"(pf:imp-elim {proof} {premise})"
    return proof


def occurs(name, term) -> bool:
    if term[0] == "var":
        return term[1] == name
    if term[0] == "tp":
        return any(occurs(name, t) for t in term[2])
    return False


# ---------------------------------------------------------------- the theory


class Theory:
    """The constants and laws one judgment's proofs use, collected as they are used."""

    def __init__(self, space: F.Space):
        self.space = space
        self.names: dict = {}
        self.constants: dict[str, str] = {
            "el": "(-> set set)", "ty": "(-> set set)", "Acc": "(-> set set prop)",
            "Proper": "(-> set prop)", "Value": "(-> set prop)", "NotErr": "(-> set prop)",
            "IsArrow": "(-> set prop)", "TupR": "(-> set set prop)",
            "IsSym": "(-> set prop)", "IsExpr": "(-> set prop)", "IsGnd": "(-> set prop)",
            "IsVar": "(-> set prop)"}
        self.axioms: dict[str, str] = {}
        self.counter = itertools.count()
        self.kind: dict[str, str] = {}   # leaf code -> symbol | literal | variable | reserved

    def fresh(self, stem: str) -> str:
        return f"{stem}{next(self.counter)}"

    # codes ------------------------------------------------------------

    def leaf_of(self, atom) -> tuple:
        if isinstance(atom, Sym) and atom.name in RESERVED:
            name = RESERVED[atom.name]
            self.constants[name] = "set"
            self.kind[name] = "reserved" if atom.name != "->" else "symbol"
            return leaf(name)
        key = (type(atom).__name__, str(atom))
        if key not in self.names:
            self.names[key] = f"h{len(self.names)}"
        name = self.names[key]
        self.constants[name] = "set"
        self.kind[name] = ("symbol" if isinstance(atom, Sym) else
                           "variable" if isinstance(atom, Var) else "literal")
        return leaf(name)

    def code(self, atom, bound: Optional[dict] = None) -> tuple:
        """The code of an atom; variables in `bound` are proof variables."""
        if isinstance(atom, Var) and bound is not None and atom in bound:
            return bound[atom]
        if isinstance(atom, tuple):
            self.tp_constant(len(atom))
            return tp([self.code(item, bound) for item in atom])
        return self.leaf_of(atom)

    def tp_constant(self, n: int) -> None:
        self.constants[f"tp{n}"] = "set" if n == 0 else "(-> " + " ".join(["set"] * (n + 1)) + ")"

    def law(self, name: str, statement: str) -> str:
        self.axioms.setdefault(name, statement)
        return name

    def known(self, name: str, statement: str) -> str:
        return f"(pf:known {self.law(name, statement)})"

    # the rules ---------------------------------------------------------

    def declaration_case(self, index: int):
        """Declaration `index`: its subject's code, bound variables and type code."""
        subject, typ = self.space.declarations[index]
        if F.variables(subject):
            raise Unsupported("declaration of an open atom")
        variables = F.variables(typ)
        names = [f"v{index}_{k}" for k in range(len(variables))]
        bound = {v: var(n) for v, n in zip(variables, names)}
        return self.code(subject), names, self.code(typ, bound)

    def declaration_law(self, index: int, reached: Optional[int] = None) -> tuple[str, list]:
        """The law of declaration `index` (or of its `reached`-th supertype)."""
        subject, typ = self.space.declarations[index]
        target, variables = self.reached_type(index, reached)
        names = [f"v{index}_{k}" for k in range(len(variables))]
        bound = {v: var(n) for v, n in zip(variables, names)}
        statement = forall(names, imps([f"(Proper {n})" for n in names],
                                       member(self.code(subject), self.code(target, bound))))
        name = f"decl{index}" if reached is None else f"sup{index}_{reached}"
        return self.law(name, statement), variables

    def reached_type(self, index: int, reached: Optional[int]):
        _, typ = self.space.declarations[index]
        if reached is None:
            return typ, F.variables(typ)
        target = self.space.supertypes(typ)[reached]
        return target, F.variables(target)

    def literal_law(self, atom, typ) -> str:
        code = self.code(atom)
        return self.law(f"lit_{code[1]}", member(code, self.code(typ)))

    def app_law(self, n: int) -> str:
        self.tp_constant(n + 1)
        self.tp_constant(n + 2)
        ps = [f"p{i}" for i in range(n)]
        args = [f"a{i}" for i in range(n)]
        arrow = tp([leaf(self.leaf_of(Sym("->"))[1])] + [var(p) for p in ps] + [var("r")])
        premises = [member(var("f"), arrow)] + [f"(Acc {a} {p})" for a, p in zip(args, ps)]
        conclusion = member(tp([var("f")] + [var(a) for a in args]), var("r"))
        return self.law(f"app{n}", forall(["f"] + ps + ["r"] + args, imps(premises, conclusion)))

    def tup_law(self, n: int) -> str:
        self.tp_constant(n + 1)
        types = [f"t{i}" for i in range(n + 1)]
        args = [f"a{i}" for i in range(n)]
        premises = ([member(var("h"), var("t0")), "(Value t0)"] +
                    [member(var(a), var(t)) for a, t in zip(args, types[1:])] +
                    [f"(TupR {show(tp([var(t) for t in types]))} T)"])
        conclusion = member(tp([var("h")] + [var(a) for a in args]), var("T"))
        return self.law(f"tup{n}", forall(["h", "t0"] + args + types[1:] + ["T"],
                                          imps(premises, conclusion)))

    def subtype_pairs(self):
        """The subtype declarations as codes over their own variables."""
        pairs = []
        for j, (sub, sup) in enumerate(self.space.subtypes):
            variables = F.variables((sub, sup))
            names = [f"w{j}_{k}" for k in range(len(variables))]
            bound = {v: var(n) for v, n in zip(variables, names)}
            pairs.append((j, names, self.code(sub, bound), self.code(sup, bound)))
        return pairs

    def tupr_step_law(self, j: int) -> str:
        _, names, sub, sup = self.subtype_pairs()[j]
        return self.law(f"tupr-sub{j}", forall(["x"] + names, imps(
            [f"(TupR x {show(sub)})"], f"(TupR x {show(sup)})")))

    # acceptance --------------------------------------------------------

    def acc_cases(self):
        """The cases of acceptance, in the order of its closure law."""
        return ["typed", "untyped", "undefined", "atom", "hSymbol", "hExpression",
                "hGrounded", "hVariable"]

    def acc_law(self, case: str) -> str:
        for name in ("hUndef", "hAtom", "hSymbol", "hExpression", "hGrounded", "hVariable"):
            self.constants[name] = "set"
            self.kind[name] = "reserved"
        if case == "typed":
            return self.law("acc-in", forall(["a", "p"], imps([member(var("a"), var("p"))],
                                                              "(Acc a p)")))
        if case == "untyped":
            return self.law("acc-untyped", forall(["a", "p"], imps(
                [self.untyped_statement(var("a")), "(NotErr a)"], "(Acc a p)")))
        if case == "undefined":
            return self.law("acc-undef", forall(["a"], "(Acc a hUndef)"))
        if case == "atom":
            return self.law("acc-atom", forall(["a"], "(Acc a hAtom)"))
        shape = META[case]
        return self.law(f"acc-{case}", forall(["a"], imps([f"({shape} a)"], f"(Acc a {case})")))

    def untyped_statement(self, atom) -> str:
        return f"(all set (lam U (imp {member(atom, var('U'))} Falsum)))"

    def acc_closure(self) -> str:
        for name in ("hUndef", "hAtom", "hSymbol", "hExpression", "hGrounded", "hVariable"):
            self.constants[name] = "set"
            self.kind[name] = "reserved"
        cases = [imps([member(var("a"), var("p"))], "P"),
                 imps([self.untyped_statement(var("a")), "(NotErr a)"], "P"),
                 imps([eq(var("p"), leaf("hUndef"))], "P"),
                 imps([eq(var("p"), leaf("hAtom"))], "P")]
        for code, shape in META.items():
            cases.append(imps([eq(var("p"), leaf(code)), f"({shape} a)"], "P"))
        return self.law("acc-closure", forall(["a", "p"], imps(
            ["(Acc a p)"], forall(["P"], imps(cases, "P"), sort="prop"))))

    # the closure law of an atom ----------------------------------------

    def term_cases(self, atom):
        """The cases of the closure law of `atom`, each a (kind, data) pair."""
        cases = []
        literal = F.literal_type(atom)
        if literal is not None:
            cases.append(("literal", literal))
            return cases
        for index, (subject, typ) in enumerate(self.space.declarations):
            if subject == atom and typ != F.UNDEFINED:
                if F.variables(subject):
                    raise Unsupported("declaration of an open atom")
                reached = self.space.supertypes(typ)
                for k in range(len(reached)):
                    cases.append(("declared", (index, None if k == 0 else k)))
        if isinstance(atom, tuple) and atom:
            cases.append(("app", len(atom) - 1))
            cases.append(("tup", len(atom) - 1))
        return cases

    def closure_law(self, atom) -> tuple[str, list]:
        code = self.code(atom)
        cases = self.term_cases(atom)
        texts = []
        for kind, data in cases:
            if kind == "literal":
                texts.append(imps([eq(var("T"), self.code(data))], "P"))
            elif kind == "declared":
                index, reached = data
                target, variables = self.reached_type(index, reached)
                names = [f"c{index}_{reached or 0}_{k}" for k in range(len(variables))]
                bound = {v: var(n) for v, n in zip(variables, names)}
                texts.append(forall(names, imps([f"(Proper {n})" for n in names] +
                                                [eq(var("T"), self.code(target, bound))], "P")))
            elif kind == "app":
                n = data
                head, args = code[2][0], code[2][1:]
                ps = [f"q{i}" for i in range(n)]
                arrow = tp([self.leaf_of(Sym("->"))] + [var(p) for p in ps] + [var("T")])
                self.tp_constant(n + 2)
                premises = [member(head, arrow)] + [f"(Acc {show(a)} {p})"
                                                    for a, p in zip(args, ps)]
                texts.append(forall(ps, imps(premises, "P")))
            else:
                n = data
                head, args = code[2][0], code[2][1:]
                types = [f"s{i}" for i in range(n + 1)]
                premises = ([member(head, var("s0")), "(Value s0)"] +
                            [member(a, var(t)) for a, t in zip(args, types[1:])] +
                            [f"(TupR {show(tp([var(t) for t in types]))} T)"])
                texts.append(forall(types, imps(premises, "P")))
        statement = forall(["T"], imps([member(code, var("T"))],
                                       forall(["P"], imps(texts, "P"), sort="prop")))
        return self.law(f"closure_{show(code)}".replace(" ", "_").replace("(", "").replace(")", ""),
                        statement), cases

    # unique names --------------------------------------------------------

    def distinct_law(self, left, right) -> str:
        """`eq left right -> Falsum` for codes with different outermost forms."""
        if left[0] == "leaf" and right[0] == "leaf":
            statement = imps([eq(left, right)], "Falsum")
            return self.law(f"distinct-{left[1]}-{right[1]}", statement)
        if left[0] == "tp" and right[0] == "tp":
            n, m = left[1], right[1]
            xs = [f"x{i}" for i in range(n)]
            ys = [f"y{i}" for i in range(m)]
            self.tp_constant(n)
            self.tp_constant(m)
            statement = forall(xs + ys, imps([eq(tp([var(x) for x in xs]),
                                                 tp([var(y) for y in ys]))], "Falsum"))
            return self.law(f"distinct-tp{n}-tp{m}", statement)
        if left[0] == "leaf":
            ys = [f"y{i}" for i in range(right[1])]
            self.tp_constant(right[1])
            statement = forall(ys, imps([eq(left, tp([var(y) for y in ys]))], "Falsum"))
            return self.law(f"distinct-{left[1]}-tp{right[1]}", statement)
        xs = [f"x{i}" for i in range(left[1])]
        self.tp_constant(left[1])
        statement = forall(xs, imps([eq(tp([var(x) for x in xs]), right)], "Falsum"))
        return self.law(f"distinct-tp{left[1]}-{right[1]}", statement)

    def injective_law(self, n: int, i: int) -> str:
        xs = [f"x{k}" for k in range(n)]
        ys = [f"y{k}" for k in range(n)]
        self.tp_constant(n)
        statement = forall(xs + ys, imps([eq(tp([var(x) for x in xs]), tp([var(y) for y in ys]))],
                                         eq(var(xs[i]), var(ys[i]))))
        return self.law(f"injective-tp{n}-{i}", statement)

    def improper_law(self, code: str) -> str:
        return self.law(f"improper-{code}", forall(["x"], imps(
            ["(Proper x)", eq(var("x"), leaf(code))], "Falsum")))

    def proper(self, term, hypotheses: dict) -> str:
        """A proof that a code is proper."""
        if term[0] == "var":
            if term[1] not in hypotheses:
                raise Unsupported(f"no properness for {term[1]}")
            return hypotheses[term[1]]
        if term[0] == "tp":
            n = term[1]
            xs = [f"x{k}" for k in range(n)]
            self.tp_constant(n)
            name = self.law(f"proper-tp{n}", forall(xs, f"(Proper {show(tp([var(x) for x in xs]))})"))
            return all_elim(f"(pf:known {name})", term[2])
        if self.kind.get(term[1]) == "reserved":
            raise Unsupported(f"reserved instance {term[1]}")
        return f"(pf:known {self.law(f'proper-{term[1]}', f'(Proper {term[1]})')})"

    # the theory text ------------------------------------------------------

    def text(self, space_name: str) -> list[str]:
        lines = [f"!(bind! {space_name} (let $s (new-space) (let $_ (add-atom $s "
                 f"(set:profile megalodon-hotg)) $s)))",
                 f"!(add-atom {space_name} (set:rule eq-subst))"]
        constants = " ".join(f"(: {n} {t})" for n, t in self.constants.items())
        lines.append(f"!(pf:theory {space_name} ({constants}))")
        lines.append(f"!(let $u (pf:theorem {space_name} eq-refl (all set (lam x (eq set x x))) "
                     "(pf:fix x (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known extensionality) x) x) "
                     "(pf:fix z (pf:fix p1 (pf:assume k2 (pf:imp-elim (pf:imp-elim k2 (pf:assume m m)) "
                     "(pf:assume m m)))))))) ())")
        lines.append(f"!(let $u (pf:theorem {space_name} eq-sym (all set (lam x (all set (lam y "
                     "(imp (eq set x y) (eq set y x)))))) (pf:fix x (pf:fix y (pf:assume h "
                     "(pf:eq-subst (lam z (eq set z x)) h (pf:all-elim (pf:known eq-refl) x)))))) ())")
        for name, statement in self.axioms.items():
            lines.append(f"!(let $u (pf:axiom {space_name} {name} {statement}) ())")
        return lines


# ---------------------------------------------------------------- positive proofs


@dataclass
class Prover:
    theory: Theory
    hypotheses: dict = field(default_factory=dict)  # free variable -> its properness proof
    free: dict = field(default_factory=dict)        # fo variable -> proof variable code
    filler: Optional[tuple] = None                  # the code for unresolved variables, if any

    def term_code(self, atom, substs) -> tuple:
        """The code of a resolved type; unresolved variables become free proof variables."""
        resolved = F.resolve(atom, merged(substs))
        return self.code_with_free(resolved)

    def code_with_free(self, atom) -> tuple:
        if isinstance(atom, Var) and self.filler is not None:
            return self.filler
        if isinstance(atom, Var):
            if atom not in self.free:
                name = self.theory.fresh("z")
                self.free[atom] = var(name)
                self.hypotheses[name] = f"pz_{name}"
            return self.free[atom]
        if isinstance(atom, tuple):
            self.theory.tp_constant(len(atom))
            return tp([self.code_with_free(item) for item in atom])
        return self.theory.leaf_of(atom)

    def proof(self, d: Optional[F.Derivation], substs: list) -> str:
        """A proof that the derivation's atom has its (resolved) type."""
        th = self.theory
        if d is None:
            raise Unsupported("gradual argument without derivation")
        if d.rule == "literal":
            return f"(pf:known {th.literal_law(d.term, d.type)})"
        if d.rule in ("decl", "annotation"):
            index = d.detail["declaration"]
            name, variables = th.declaration_law(index)
            instances = [self.term_code(v, substs) for v in d.detail["vars"]]
            proof = all_elim(f"(pf:known {name})", instances)
            return imp_elim(proof, [th.proper(i, self.hypotheses) for i in instances])
        if d.rule == "subtype":
            index = d.detail["declaration"]
            _, typ = th.space.declarations[index]
            reached = th.space.supertypes(typ)
            if F.variables(typ) or d.type not in reached:
                raise Unsupported("subtype of an open declaration")
            name, _ = th.declaration_law(index, reached.index(d.type))
            return f"(pf:known {name})"
        if d.rule == "tuple":
            parts = d.parts
            n = len(parts) - 1
            law = th.tup_law(n)
            head, args = parts[0], parts[1:]
            types = [self.term_code(p.type, substs) for p in parts]
            target = self.term_code(d.type, substs)
            terms = [th.code(p.term) for p in parts]
            proof = all_elim(f"(pf:known {law})", [terms[0], types[0]] + terms[1:] + types[1:] + [target])
            premises = [self.proof(head, substs), self.value(types[0])]
            premises += [self.proof(a, substs) for a in args]
            source_fo = tuple(F.resolve(p.type, merged(substs)) for p in parts)
            premises.append(self.tupr(tp(types), source_fo, F.resolve(d.type, merged(substs))))
            return imp_elim(proof, premises)
        if d.rule == "app":
            inner = substs + [d.detail["subst"]]
            function = d.detail["function"]
            parameters, result = function.type[1:-1], function.type[-1]
            n = len(parameters)
            law = th.app_law(n)
            head = th.code(d.term[0])
            args = [th.code(a) for a in d.term[1:]]
            ps = [self.term_code(p, inner) for p in parameters]
            r = self.term_code(result, inner)
            proof = all_elim(f"(pf:known {law})", [head] + ps + [r] + args)
            premises = [self.proof(function, inner)]
            for how, part, p, a, atom in zip(d.detail["accepted"], d.parts, ps, args, d.term[1:]):
                premises.append(self.accept(how, part, p, a, atom, inner))
            return imp_elim(proof, premises)
        raise Unsupported(d.rule)

    def accept(self, how, part, p, a, atom, substs) -> str:
        th = self.theory
        if how == "typed":
            law = th.acc_law("typed")
            arg_type = self.term_code(part.type, substs)
            if arg_type != p:
                raise Unsupported("argument accepted through %Undefined% inside a type")
            return imp_elim(all_elim(f"(pf:known {law})", [a, p]), [self.proof(part, substs)])
        if how == "untyped":
            law = th.acc_law("untyped")
            return imp_elim(all_elim(f"(pf:known {law})", [a, p]),
                            [untyped_proof(th, atom), self.not_err(atom)])
        if how == "undefined":
            return all_elim(f"(pf:known {th.acc_law('undefined')})", [a])
        if how == "meta":
            code = show(p)
            if code == "hAtom":
                return all_elim(f"(pf:known {th.acc_law('atom')})", [a])
            if code in META:
                shape = META[code]
                fact = th.law(f"{shape}-{show(a)}".replace(" ", "_").replace("(", "").replace(")", ""),
                              f"({shape} {show(a)})")
                return imp_elim(all_elim(f"(pf:known {th.acc_law(code)})", [a]), [f"(pf:known {fact})"])
        raise Unsupported(f"acceptance {how}")

    def not_err(self, atom) -> str:
        th = self.theory
        code = th.code(atom)
        if code[0] == "leaf":
            return f"(pf:known {th.law(f'noterr-{code[1]}', f'(NotErr {code[1]})')})"
        raise Unsupported("untyped expression argument")

    def value(self, typ) -> str:
        th = self.theory
        if typ[0] == "leaf":
            return f"(pf:known {th.law(f'value-{typ[1]}', f'(Value {typ[1]})')})"
        if typ[0] == "tp" and typ[2] and typ[2][0] == leaf("hArrow") and typ[1] >= 2:
            raise Unsupported("tuple of an arrow head")
        if typ[0] == "tp":
            n = typ[1]
            xs = [f"x{k}" for k in range(n)]
            name = th.law(f"value-tp{n}", forall(xs, imps(
                [imps([eq(var("x0"), leaf("hArrow"))], "Falsum")],
                f"(Value {show(tp([var(x) for x in xs]))})")))
            th.constants["hArrow"] = "set"
            first = typ[2][0]
            if first[0] == "var":
                raise Unsupported("tuple type with a variable head")
            distinct = th.distinct_law(first, leaf("hArrow"))
            not_arrow = self.distinct_proof(first, leaf("hArrow"), distinct)
            return imp_elim(all_elim(f"(pf:known {name})", typ[2]), [not_arrow])
        raise Unsupported("value of a variable type")

    def distinct_proof(self, left, right, law) -> str:
        """`eq left right -> Falsum` from the distinctness law."""
        if left[0] == "leaf" and right[0] == "leaf":
            return f"(pf:known {law})"
        terms = (left[2] if left[0] == "tp" else []) + (right[2] if right[0] == "tp" else [])
        return all_elim(f"(pf:known {law})", terms)

    def tupr(self, source, source_fo, target_fo) -> str:
        """`TupR source target`: the tuple type, or a supertype reached by subtype steps."""
        th = self.theory
        law = th.law("tupr-refl", forall(["x"], "(TupR x x)"))
        proof = all_elim(f"(pf:known {law})", [source])
        if F.canonical(source_fo) == F.canonical(target_fo):
            return proof
        # Breadth-first search over subtype steps, as HE closes a tuple's types.
        frontier = [(source_fo, [])]
        seen = [F.canonical(source_fo)]
        for _ in range(64):
            following = []
            for current, steps in frontier:
                for j, (sub, sup) in enumerate(th.space.subtypes):
                    mapping: dict = {}
                    fsub, fsup = F.fresh_copy((sub, sup), mapping)
                    bindings = F.unify(fsub, current, {})
                    if bindings is None:
                        continue
                    image = F.resolve(fsup, bindings)
                    instance = [F.resolve(mapping[v], bindings) for v in F.variables((sub, sup))]
                    path = steps + [(j, instance)]
                    if F.canonical(image) == F.canonical(target_fo):
                        for step, values in path:
                            step_law = th.tupr_step_law(step)
                            codes = [self.code_with_free(v) for v in values]
                            proof = imp_elim(all_elim(f"(pf:known {step_law})", [source] + codes),
                                             [proof])
                        return proof
                    if F.canonical(image) not in seen:
                        seen.append(F.canonical(image))
                        following.append((image, path))
            if not following:
                break
            frontier = following
        raise Unsupported("tuple supertype not reached")


def merged(substs: list) -> dict:
    out: dict = {}
    for subst in substs:
        out.update(subst)
    return out


def untyped_proof(th: Theory, atom) -> str:
    """`forall U. In (el atom) (ty U) -> Falsum` for an atom without types."""
    law, cases = th.closure_law(atom)
    if cases:
        refuter = Refuter(th)
        return refuter.refute(atom)
    return (f"(pf:fix U (pf:assume H (pf:all-elim (pf:imp-elim (pf:all-elim (pf:known {law}) U) H)"
            f" Falsum)))")


# ---------------------------------------------------------------- refutations


class Refuter:
    """Proofs of `forall T. In (el e) (ty T) -> Falsum` from the closure law."""

    def __init__(self, theory: Theory, depth: int = 0):
        self.th = theory
        self.depth = depth

    def refute(self, atom) -> str:
        th = self.th
        name_t = th.fresh("T")
        hyp = th.fresh("H")
        body = self.cases(atom, var(name_t), hyp, {}, {}, self.consistent)
        return f"(pf:fix {name_t} (pf:assume {hyp} {body}))"

    @staticmethod
    def consistent(bindings, proper) -> str:
        raise NotRefutable("a case of the closure law is consistent")

    # unification of codes under equality hypotheses ---------------------

    def resolve(self, term, bindings):
        while term[0] == "var" and term[1] in bindings:
            term = bindings[term[1]][0]
        return term

    def deep(self, term, bindings):
        term = self.resolve(term, bindings)
        if term[0] == "tp":
            return tp([self.deep(t, bindings) for t in term[2]])
        return term

    def unify(self, left, right, proof: str, bindings, proper, k) -> str:
        """`proof : eq left right`; Falsum by the unique-names laws, or `k` with bindings."""
        th = self.th
        # Rewrite bound variables on either side, transporting the equation.
        while left[0] == "var" and left[1] in bindings:
            value, eq_proof = bindings[left[1]]
            proof = (f"(pf:eq-subst (lam m {eq(var('m'), right)}) {eq_proof} {proof})")
            left = value
        while right[0] == "var" and right[1] in bindings:
            value, eq_proof = bindings[right[1]]
            proof = (f"(pf:eq-subst (lam m {eq(left, var('m'))}) {eq_proof} {proof})")
            right = value
        if left == right:
            return k(bindings, proper)
        if left[0] == "var" or right[0] == "var":
            if left[0] != "var":
                proof = imp_elim(all_elim("(pf:known eq-sym)", [left, right]), [proof])
                left, right = right, left
            name = left[1]
            if occurs(name, self.deep(right, bindings)):
                raise Unsupported("occurs check")
            if right[0] == "leaf" and th.kind.get(right[1]) == "reserved" and name in proper:
                law = th.improper_law(right[1])
                return imp_elim(all_elim(f"(pf:known {law})", [var(name)]), [proper[name], proof])
            extended = dict(bindings)
            extended[name] = (right, proof)
            return k(extended, proper)
        if left[0] == "tp" and right[0] == "tp" and left[1] == right[1]:
            n = left[1]
            steps = []
            for i in range(n):
                law = th.injective_law(n, i)
                steps.append((left[2][i], right[2][i],
                              imp_elim(all_elim(f"(pf:known {law})", left[2] + right[2]), [proof])))

            def chain(i, bindings_i, proper_i):
                if i == len(steps):
                    return k(bindings_i, proper_i)
                a, b, step = steps[i]
                return self.unify(a, b, step, bindings_i, proper_i,
                                  lambda b2, p2: chain(i + 1, b2, p2))
            return chain(0, bindings, proper)
        law = th.distinct_law(left, right)
        terms = (left[2] if left[0] == "tp" else []) + (right[2] if right[0] == "tp" else [])
        return imp_elim(all_elim(f"(pf:known {law})", terms), [proof])

    def transport(self, statement_of, term, proof: str, bindings) -> tuple[tuple, str]:
        """From `proof : statement_of(term)`, a proof of statement_of(resolved term)."""
        while term[0] == "var" and term[1] in bindings:
            value, eq_proof = bindings[term[1]]
            proof = f"(pf:eq-subst (lam m {statement_of(var('m'))}) {eq_proof} {proof})"
            term = value
        return term, proof

    # the closure law of an atom ------------------------------------------

    def cases(self, atom, target, hyp: str, bindings, proper, k) -> str:
        """`hyp : In (el atom) (ty target)`; Falsum in every case of the closure law."""
        th = self.th
        law, cases = th.closure_law(atom)
        code = th.code(atom)
        proofs = []
        for kind, data in cases:
            if kind == "literal":
                e = th.fresh("E")
                proofs.append(f"(pf:assume {e} " +
                              self.unify(target, th.code(data), e, bindings, proper, k) + ")")
            elif kind == "declared":
                index, reached = data
                typ, variables = th.reached_type(index, reached)
                names = [th.fresh("v") for _ in variables]
                bound = {v: var(n) for v, n in zip(variables, names)}
                pnames = [th.fresh("pv") for _ in variables]
                e = th.fresh("E")
                proper2 = dict(proper)
                proper2.update({n: p for n, p in zip(names, pnames)})
                inner = self.unify(target, th.code(typ, bound), e, bindings, proper2, k)
                text = f"(pf:assume {e} {inner})"
                for p in reversed(pnames):
                    text = f"(pf:assume {p} {text})"
                for n in reversed(names):
                    text = f"(pf:fix {n} {text})"
                proofs.append(text)
            elif kind == "app":
                proofs.append(self.app_case(code, data, target, bindings, proper, k))
            else:
                proofs.append(self.tup_case(code, data, target, bindings, proper, k))
        opened = f"(pf:all-elim (pf:imp-elim (pf:all-elim (pf:known {law}) {show(target)}) {hyp}) Falsum)"
        return imp_elim(opened, proofs)

    def app_case(self, code, n, target, bindings, proper, k) -> str:
        th = self.th
        head, args = code[2][0], code[2][1:]
        ps = [th.fresh("p") for _ in range(n)]
        hh = th.fresh("Hh")
        accs = [th.fresh("A") for _ in range(n)]
        arrow = tp([leaf("hArrow")] + [var(p) for p in ps] + [target])

        def after_head(b1, pr1):
            def chain(i, b2, pr2):
                if i == n:
                    return k(b2, pr2)
                return self.acceptance(args[i], var(ps[i]), accs[i], b2, pr2,
                                       lambda b3, pr3: chain(i + 1, b3, pr3))
            return chain(0, b1, pr1)

        inner = self.cases(self.th.atom_of_code[show(head)], arrow, hh, bindings, proper, after_head)
        text = inner
        for a in reversed(accs):
            text = f"(pf:assume {a} {text})"
        text = f"(pf:assume {hh} {text})"
        for p in reversed(ps):
            text = f"(pf:fix {p} {text})"
        return text

    def tup_case(self, code, n, target, bindings, proper, k) -> str:
        th = self.th
        head, args = code[2][0], code[2][1:]
        ts = [th.fresh("t") for _ in range(n + 1)]
        hh = th.fresh("Hh")
        hv = th.fresh("Hv")
        has = [th.fresh("Ha") for _ in range(n)]
        htr = th.fresh("Htr")

        def after_head(b1, pr1):
            t0, value_proof = self.transport_all(lambda m: f"(Value {show(m)})", var(ts[0]), hv, b1)
            if t0[0] == "tp" and t0[1] >= 2 and t0[2][0] == leaf("hArrow"):
                xs = [f"x{i}" for i in range(1, t0[1])]
                law = th.law(f"value-arrow{t0[1]}", forall(xs, imps(
                    [f"(Value {show(tp([leaf('hArrow')] + [var(x) for x in xs]))})"], "Falsum")))
                return imp_elim(all_elim(f"(pf:known {law})", t0[2][1:]), [value_proof])

            def chain(i, b2, pr2):
                if i == n:
                    return self.tupr_cases(tp([var(t) for t in ts]), target, htr, b2, pr2, k)
                return self.cases(self.th.atom_of_code[show(args[i])], var(ts[i + 1]), has[i],
                                  b2, pr2, lambda b3, pr3: chain(i + 1, b3, pr3))
            return chain(0, b1, pr1)

        inner = self.cases(self.th.atom_of_code[show(head)], var(ts[0]), hh, bindings, proper,
                           after_head)
        text = f"(pf:assume {htr} {inner})"
        for h in reversed(has):
            text = f"(pf:assume {h} {text})"
        text = f"(pf:assume {hh} (pf:assume {hv} {text}))"
        for t in reversed(ts):
            text = f"(pf:fix {t} {text})"
        return text

    def transport_all(self, statement_of, term, proof, bindings):
        """From `proof : statement_of(term)`, a proof about the code with every bound variable
        replaced by its value."""
        while True:
            names = [n for n in bindings if occurs(n, term)]
            if not names:
                return term, proof
            name = names[0]
            value, eq_proof = bindings[name]
            before = term
            proof = (f"(pf:eq-subst (lam m {statement_of(substitute(before, name, var('m')))}) "
                     f"{eq_proof} {proof})")
            term = substitute(before, name, value)

    def tupr_cases(self, source, target, hyp, bindings, proper, k) -> str:
        th = self.th
        pairs = th.subtype_pairs()
        if self.depth > len(pairs):
            raise Unsupported("subtype chain")
        law = th.law("tupr-closure", tupr_closure_statement(th))
        e = th.fresh("E")
        cases = [f"(pf:assume {e} " + self.unify(source, target, e, bindings, proper, k) + ")"]
        for j, names, sub, sup in pairs:
            fresh = [th.fresh("w") for _ in names]
            renaming = {n: var(f) for n, f in zip(names, fresh)}
            sub_f = rename(sub, renaming)
            sup_f = rename(sup, renaming)
            hstep = th.fresh("Hs")
            estep = th.fresh("Es")
            deeper = Refuter(th, self.depth + 1)
            inner = deeper.unify(target, sup_f, estep, bindings, proper,
                                 lambda b2, p2: deeper.tupr_cases(source, sub_f, hstep, b2, p2, k))
            text = f"(pf:assume {hstep} (pf:assume {estep} {inner}))"
            for f in reversed(fresh):
                text = f"(pf:fix {f} {text})"
            cases.append(text)
        opened = (f"(pf:all-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known {law}) "
                  f"{show(source)}) {show(target)}) {hyp}) Falsum)")
        return imp_elim(opened, cases)

    # acceptance -------------------------------------------------------------

    def acceptance(self, arg, parameter, hyp, bindings, proper, k) -> str:
        """`hyp : Acc arg parameter`; Falsum in every case of acceptance."""
        th = self.th
        law = th.acc_closure()
        atom = th.atom_of_code[show(arg)]
        proofs = []
        # Typed at the parameter.
        h = th.fresh("Hin")
        proofs.append(f"(pf:assume {h} " +
                      self.cases(atom, parameter, h, bindings, proper, k) + ")")
        # Untyped and not an ill-typed application.
        hu, hn = th.fresh("Hu"), th.fresh("Hn")
        proofs.append(f"(pf:assume {hu} (pf:assume {hn} " +
                      self.untyped_case(atom, hu, hn, bindings, proper, k) + "))")
        # %Undefined%, Atom and the meta-types.
        for code in ("hUndef", "hAtom"):
            e = th.fresh("E")
            proofs.append(f"(pf:assume {e} " +
                          self.unify(parameter, leaf(code), e, bindings, proper, k) + ")")
        for code, shape in META.items():
            e, hs = th.fresh("E"), th.fresh("Hs")
            proofs.append(f"(pf:assume {e} (pf:assume {hs} " +
                          self.unify(parameter, leaf(code), e, bindings, proper,
                                     lambda b2, p2, shape=shape, hs=hs:
                                     self.shape_case(atom, shape, hs, b2, p2, k)) + "))")
        opened = (f"(pf:all-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known {law}) "
                  f"{show(arg)}) {show(parameter)}) {hyp}) Falsum)")
        return imp_elim(opened, proofs)

    def untyped_case(self, atom, hu, hn, bindings, proper, k) -> str:
        th = self.th
        typing = F.types(th.space, atom)
        if typing.derivations:
            d = typing.derivations[0]
            prover = Prover(th, filler=self.filler())
            substs = [d.detail["subst"]] if d.rule == "app" else []
            positive = prover.proof(d, substs)
            typ = prover.term_code(d.type, substs)
            return imp_elim(f"(pf:all-elim {hu} {show(typ)})", [positive])
        if typing.errors:
            # An ill-typed application: its head has an arrow type, so acceptance by being
            # untyped would make it typed.
            if not (isinstance(atom, tuple) and atom):
                raise Unsupported("ill-typed atom")
            head = atom[0]
            head_typing = F.types(th.space, head)
            arrows = [d for d in head_typing.derivations if F.is_arrow(d.type)]
            if not arrows:
                raise Unsupported("ill-typed head")
            d = arrows[0]
            prover = Prover(th, filler=self.filler())
            substs = [d.detail["subst"]] if d.rule == "app" else []
            positive = prover.proof(d, substs)
            typ = prover.term_code(d.type, substs)
            n = len(atom) - 1
            code = th.code(atom)
            law = th.law(f"err{n}", err_statement(th, n))
            isarrow = th.law(f"isarrow{typ[1]}", forall(
                [f"x{i}" for i in range(1, typ[1])],
                f"(IsArrow {show(tp([leaf('hArrow')] + [var(f'x{i}') for i in range(1, typ[1])]))})"))
            arrow_proof = all_elim(f"(pf:known {isarrow})", typ[2][1:])
            return imp_elim(all_elim(f"(pf:known {law})", code[2] + [typ]),
                            [positive, arrow_proof, hn, hu])
        return k(bindings, proper)

    def filler(self) -> tuple:
        """A proper code for the variables of a witness typing."""
        return self.th.leaf_of(Sym("he-witness-type"))

    def shape_case(self, atom, shape, hs, bindings, proper, k) -> str:
        th = self.th
        actual = {"IsSym": isinstance(atom, Sym), "IsExpr": isinstance(atom, tuple),
                  "IsVar": isinstance(atom, Var)}.get(shape)
        if shape == "IsGnd":
            actual = not isinstance(atom, (Sym, tuple, Var))
        if actual:
            return k(bindings, proper)
        code = th.code(atom)
        if code[0] == "tp":
            n = code[1]
            xs = [f"x{i}" for i in range(n)]
            law = th.law(f"not-{shape}-tp{n}", forall(xs, imps(
                [f"({shape} {show(tp([var(x) for x in xs]))})"], "Falsum")))
            return imp_elim(all_elim(f"(pf:known {law})", code[2]), [hs])
        law = th.law(f"not-{shape}-{code[1]}", imps([f"({shape} {code[1]})"], "Falsum"))
        return imp_elim(f"(pf:known {law})", [hs])


def substitute(term, name, value):
    if term[0] == "var":
        return value if term[1] == name else term
    if term[0] == "tp":
        return tp([substitute(t, name, value) for t in term[2]])
    return term


def rename(term, renaming):
    if term[0] == "var":
        return renaming.get(term[1], term)
    if term[0] == "tp":
        return tp([rename(t, renaming) for t in term[2]])
    return term


def tupr_closure_statement(th: Theory) -> str:
    cases = [imps([eq(var("x"), var("y"))], "P")]
    for j, names, sub, sup in th.subtype_pairs():
        cases.append(forall(names, imps([f"(TupR x {show(sub)})", eq(var("y"), sup)], "P")))
    return forall(["x", "y"], imps(["(TupR x y)"], forall(["P"], imps(cases, "P"), sort="prop")))


def err_statement(th: Theory, n: int) -> str:
    """If the head of `(h a1 ... an)` has an arrow type and the expression is not an ill-typed
    application, then it has a type."""
    args = [f"a{i}" for i in range(n)]
    expr = tp([var("h")] + [var(a) for a in args])
    th.tp_constant(n + 1)
    return forall(["h"] + args + ["X"], imps(
        [member(var("h"), var("X")), "(IsArrow X)", f"(NotErr {show(expr)})",
         th.untyped_statement(expr)], "Falsum"))


# ---------------------------------------------------------------- judgments


def index_atoms(th: Theory, atom) -> None:
    """Record the atom of every code that occurs in `atom`, for the refuter."""
    code = th.code(atom)
    th.atom_of_code[show(code)] = atom
    if isinstance(atom, tuple):
        for item in atom:
            index_atoms(th, item)


def judgment_checks(space: F.Space, expression):
    """("typed", [(k, statement, proof, mutant, type)]) for each derivation,
    ("refuted", statement, proof), or ("open", reason)."""
    typing = F.types(space, expression)
    if typing.derivations:
        checks = []
        for k, d in enumerate(typing.derivations):
            th = Theory(space)
            th.atom_of_code = {}
            index_atoms(th, expression)
            prover = Prover(th)
            substs = [d.detail["subst"]] if d.rule == "app" else []
            try:
                proof = prover.proof(d, substs)
                typ = prover.term_code(d.type, substs)
            except (Unsupported, NotRefutable, KeyError) as why:
                checks.append((k, None, str(why), None, F.evaluate_ground(d.type), None))
                continue
            statement = member(th.code(expression), typ)
            mutant = member(th.code(expression), th.leaf_of(Sym("he-mutant-type")))
            free = list(prover.free.values())
            names = [v[1] for v in free]
            premises = [f"(Proper {n})" for n in names]
            statement = forall(names, imps(premises, statement))
            mutant = forall(names, imps(premises, mutant))
            for n in reversed(names):
                proof = f"(pf:fix {n} (pf:assume {prover.hypotheses[n]} {proof}))"
            checks.append((k, th, statement, proof, F.evaluate_ground(d.type), mutant))
        return "typed", checks
    if not typing.errors:
        return "unknown", []
    th = Theory(space)
    th.atom_of_code = {}
    index_atoms(th, expression)
    try:
        proof = Refuter(th).refute(expression)
    except (Unsupported, NotRefutable, KeyError) as why:
        return "open", [str(why)]
    statement = forall(["T"], imps([member(th.code(expression), var("T"))], "Falsum"))
    return "refuted", [(th, statement, proof)]
