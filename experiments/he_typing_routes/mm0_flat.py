"""HE's first-order typing as an MM0 theory, one theory per judgment programme.

MM0 is the substrate: the theory of first-order prenex schemes is written as an MM0 theory
and its derivations are MM0 proofs.  Atoms are terms of one sort built from a constant per
symbol or literal and a constructor `tpN` per length of expression, so an arrow is the code
of its expression and an application is checked by the application axiom of its arity.  A
declaration is an axiom whose type variables are the axiom's variables: instantiating a
prenex scheme is MM0 substitution, restricted to proper types as in HE.  Gradual acceptance,
meta-types, tuples and subtypes are the rules of `fo_schemes.py`; the facts that an atom has
no type are stated as facts, since MM0 proves derivations and not their absence.

Only positive judgments are MM0 theorems here.  A refusal would need the closure law in a
logic with negation hosted in MM0, as the HOTG route does in HOTG.
"""

from __future__ import annotations

from typing import Optional

import fo_schemes as F
from sexpr import Str, Sym, Var

RESERVED = {"%Undefined%": "hUndef", "Atom": "hAtom", "Symbol": "hSymbol",
            "Expression": "hExpression", "Grounded": "hGrounded", "Variable": "hVariable",
            "->": "hArrow"}
SHAPES = {"hSymbol": "issym", "hExpression": "isexpr", "hGrounded": "isgnd", "hVariable": "isvar"}


class Unsupported(Exception):
    pass


class Theory:
    def __init__(self, space: F.Space):
        self.space = space
        self.names: dict = {}
        self.leaves: list[str] = []
        self.arities: set[int] = set()
        self.axioms: dict[str, str] = {}
        self.theorems: list[str] = []

    def leaf_of(self, atom) -> str:
        if isinstance(atom, Sym) and atom.name in RESERVED:
            name = RESERVED[atom.name]
        else:
            key = (type(atom).__name__, str(atom))
            if key not in self.names:
                self.names[key] = f"h{len(self.names)}"
            name = self.names[key]
        if name not in self.leaves:
            self.leaves.append(name)
        return name

    def code(self, atom, bound: Optional[dict] = None) -> str:
        if isinstance(atom, Var) and bound is not None and atom in bound:
            return bound[atom]
        if isinstance(atom, tuple):
            self.arities.add(len(atom))
            if not atom:
                return "tp0"
            return f"(tp{len(atom)} " + " ".join(self.code(a, bound) for a in atom) + ")"
        return self.leaf_of(atom)

    def axiom(self, name: str, binders: list[str], hypotheses: list[str], conclusion: str) -> str:
        if name not in self.axioms:
            text = f"axiom {name}"
            if binders:
                text += " (" + " ".join(binders) + ": atom)"
            arrows = "".join(f" $ {h} $ >" for h in hypotheses)
            self.axioms[name] = f"{text}:{arrows} $ {conclusion} $;"
        return name

    # rules -------------------------------------------------------------------

    def decl(self, index: int) -> str:
        subject, typ = self.space.declarations[index]
        if F.variables(subject):
            raise Unsupported("declaration of an open atom")
        variables = F.variables(typ)
        names = [f"v{k}" for k in range(len(variables))]
        bound = dict(zip(variables, names))
        return self.axiom(f"decl{index}", names, [f"proper {n}" for n in names],
                          f"ty {self.code(subject)} {self.code(typ, bound)}")

    def sup(self, index: int, reached: int) -> str:
        subject, typ = self.space.declarations[index]
        target = self.space.supertypes(typ)[reached]
        return self.axiom(f"sup{index}_{reached}", [], [],
                          f"ty {self.code(subject)} {self.code(target)}")

    def literal(self, atom, typ) -> str:
        code = self.code(atom)
        return self.axiom(f"lit_{code}", [], [], f"ty {code} {self.code(typ)}")

    def app(self, n: int) -> str:
        self.arities.update({n + 1, n + 2})
        self.leaf_of(Sym("->"))
        ps = [f"p{i}" for i in range(n)]
        args = [f"a{i}" for i in range(n)]
        arrow = f"(tp{n + 2} hArrow " + " ".join(ps + ["r"]) + ")"
        expr = f"(tp{n + 1} f" + "".join(f" {a}" for a in args) + ")" if n else "(tp1 f)"
        return self.axiom(f"app{n}", ["f"] + ps + ["r"] + args,
                          [f"ty f {arrow}"] + [f"acc {a} {p}" for a, p in zip(args, ps)],
                          f"ty {expr} r")

    def tup(self, n: int) -> str:
        self.arities.add(n + 1)
        ts = [f"t{i}" for i in range(n + 1)]
        args = [f"a{i}" for i in range(n)]
        expr = f"(tp{n + 1} h" + "".join(f" {a}" for a in args) + ")"
        tuple_type = f"(tp{n + 1} " + " ".join(ts) + ")"
        return self.axiom(f"tup{n}", ["h"] + args + ts + ["T"],
                          [f"ty h t0", "value t0"] + [f"ty {a} {t}" for a, t in zip(args, ts[1:])]
                          + [f"tupr {tuple_type} T"], f"ty {expr} T")

    def text(self) -> str:
        lines = ["delimiter $ ( ) $;", "strict provable sort wff;", "sort atom;",
                 "term ty: atom > atom > wff;", "term acc: atom > atom > wff;",
                 "term proper: atom > wff;", "term value: atom > wff;",
                 "term tupr: atom > atom > wff;", "term untyped: atom > wff;",
                 "term noterr: atom > wff;"]
        lines += [f"term {shape}: atom > wff;" for shape in SHAPES.values()]
        for n in sorted(self.arities):
            lines.append(f"term tp{n}: " + " > ".join(["atom"] * (n + 1)) + ";" if n else
                         "term tp0: atom;")
        for leaf in self.leaves:
            lines.append(f"term {leaf}: atom;")
        lines += list(self.axioms.values())
        lines += self.theorems
        return "\n".join(lines) + "\n"


class Prover:
    def __init__(self, theory: Theory):
        self.th = theory
        self.free: dict = {}

    def term_code(self, atom, substs) -> str:
        resolved = F.resolve(atom, merged(substs))
        return self.code_with_free(resolved)

    def code_with_free(self, atom) -> str:
        if isinstance(atom, Var):
            if atom not in self.free:
                self.free[atom] = f"z{len(self.free)}"
            return self.free[atom]
        if isinstance(atom, tuple):
            self.th.arities.add(len(atom))
            if not atom:
                return "tp0"
            return f"(tp{len(atom)} " + " ".join(self.code_with_free(a) for a in atom) + ")"
        return self.th.leaf_of(atom)

    def proper(self, code: str) -> str:
        th = self.th
        if code.startswith("z"):
            return f"pz_{code}"
        if code.startswith("("):
            n = int(code[3:].split(" ", 1)[0].rstrip(")"))
            xs = [f"x{k}" for k in range(n)]
            th.arities.add(n)
            return th.axiom(f"proper_tp{n}", xs, [], f"proper (tp{n} " + " ".join(xs) + ")")
        if code in RESERVED.values() and code != "hArrow":
            raise Unsupported("reserved instance")
        return th.axiom(f"proper_{code}", [], [], f"proper {code}")

    def proof(self, d: Optional[F.Derivation], substs: list) -> str:
        th = self.th
        if d is None:
            raise Unsupported("gradual argument without derivation")
        if d.rule == "literal":
            return th.literal(d.term, d.type)
        if d.rule in ("decl", "annotation"):
            name = th.decl(d.detail["declaration"])
            instances = [self.term_code(v, substs) for v in d.detail["vars"]]
            hyps = [self.proper(i) for i in instances]
            return f"({name} " + " ".join(hyps) + ")" if hyps else name
        if d.rule == "subtype":
            index = d.detail["declaration"]
            _, typ = th.space.declarations[index]
            reached = th.space.supertypes(typ)
            if F.variables(typ) or d.type not in reached:
                raise Unsupported("subtype of an open declaration")
            return th.sup(index, reached.index(d.type))
        if d.rule == "tuple":
            parts = d.parts
            law = th.tup(len(parts) - 1)
            types = [self.term_code(p.type, substs) for p in parts]
            if F.is_arrow(F.resolve(parts[0].type, merged(substs))):
                raise Unsupported("tuple of an arrow head")
            value = self.value(types[0])
            target = F.resolve(d.type, merged(substs))
            source = tuple(F.resolve(p.type, merged(substs)) for p in parts)
            tupr = self.tupr(source, target)
            args = [self.proof(p, substs) for p in parts[1:]]
            return f"({law} {self.proof(parts[0], substs)} {value} " + " ".join(args + [tupr]) + ")"
        if d.rule == "app":
            inner = substs + [d.detail["subst"]]
            function = d.detail["function"]
            n = len(function.type) - 2
            law = th.app(n)
            accs = []
            for how, part, parameter, atom in zip(d.detail["accepted"], d.parts,
                                                  function.type[1:-1], d.term[1:]):
                accs.append(self.accept(how, part, self.term_code(parameter, inner), atom, inner))
            return f"({law} {self.proof(function, inner)}" + "".join(f" {a}" for a in accs) + ")"
        raise Unsupported(d.rule)

    def accept(self, how, part, parameter, atom, substs) -> str:
        th = self.th
        if how == "typed":
            if self.term_code(part.type, substs) != parameter:
                raise Unsupported("argument accepted through %Undefined% inside a type")
            th.axiom("acc_in", ["a", "p"], ["ty a p"], "acc a p")
            return f"(acc_in {self.proof(part, substs)})"
        if how == "untyped":
            code = th.code(atom)
            th.axiom("acc_untyped", ["a", "p"], ["untyped a", "noterr a"], "acc a p")
            untyped = th.axiom(f"untyped_{safe(code)}", [], [], f"untyped {code}")
            noterr = th.axiom(f"noterr_{safe(code)}", [], [], f"noterr {code}")
            return f"(acc_untyped {untyped} {noterr})"
        if how == "undefined":
            return th.axiom("acc_undef", ["a"], [], "acc a hUndef")
        if how == "meta":
            if parameter == "hAtom":
                th.leaf_of(Sym("Atom"))
                return th.axiom("acc_atom", ["a"], [], "acc a hAtom")
            shape = SHAPES[parameter]
            code = th.code(atom)
            law = th.axiom(f"acc_{shape}", ["a"], [f"{shape} a"], f"acc a {parameter}")
            fact = th.axiom(f"{shape}_{safe(code)}", [], [], f"{shape} {code}")
            return f"({law} {fact})"
        raise Unsupported(how)

    def value(self, code: str) -> str:
        return self.th.axiom(f"value_{safe(code)}", [], [], f"value {code}")

    def tupr(self, source, target) -> str:
        """`tupr source target`: the tuple type, or a supertype reached by subtype steps."""
        th = self.th
        proof = th.axiom("tupr_refl", ["x"], [], "tupr x x")
        if F.canonical(source) == F.canonical(target):
            return proof
        frontier = [(source, [])]
        seen = [F.canonical(source)]
        for _ in range(64):
            following = []
            for current, steps in frontier:
                for j, (sub, sup) in enumerate(th.space.subtypes):
                    fsub, fsup = F.fresh_copy((sub, sup))
                    bindings = F.unify(fsub, current, {})
                    if bindings is None:
                        continue
                    image = F.resolve(fsup, bindings)
                    path = steps + [j]
                    if F.canonical(image) == F.canonical(target):
                        for step in path:
                            sub_j, sup_j = th.space.subtypes[step]
                            names = [f"w{k}" for k in range(len(F.variables((sub_j, sup_j))))]
                            bound = dict(zip(F.variables((sub_j, sup_j)), names))
                            law = th.axiom(f"tupr_sub{step}", ["x"] + names,
                                           [f"tupr x {th.code(sub_j, bound)}"],
                                           f"tupr x {th.code(sup_j, bound)}")
                            proof = f"({law} {proof})"
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


def safe(code: str) -> str:
    return code.replace("(", "").replace(")", "").replace(" ", "_")


def programme_theory(judgments) -> tuple[Theory, list]:
    """One MM0 theory for the judgments of a programme: [(ident, k, theorem name, type)]."""
    theory = None
    planned = []
    for ident, space, expression, typing in judgments:
        if theory is None:
            theory = Theory(space)
        theory.space = space
        for k, d in enumerate(typing.derivations):
            prover = Prover(theory)
            substs = [d.detail["subst"]] if d.rule == "app" else []
            try:
                proof = prover.proof(d, substs)
                typ = prover.term_code(d.type, substs)
            except Unsupported as why:
                planned.append((ident, k, None, F.evaluate_ground(d.type), str(why)))
                continue
            name = f"j_{ident.replace('.', '_')}_{k}"
            binders = " ".join(f"({z}: atom)" for z in prover.free.values())
            hyps = " ".join(f"(pz_{z}: $ proper {z} $)" for z in prover.free.values())
            head = f"pub theorem {name}"
            if binders:
                head += f" {binders}"
            if hyps:
                head += f" {hyps}"
            theory.theorems.append(f"{head}: $ ty {theory.code(expression)} {typ} $ = '{proof};")
            planned.append((ident, k, name, F.evaluate_ground(d.type), None))
    return theory, planned
