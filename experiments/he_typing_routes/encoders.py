"""Replay first-order-scheme derivations in LF and in HOTG.

Both encodings read an HE atom as data: a symbol, a number or a string is a constant, and an
expression of n atoms is a tuple of them, the same tuple whether the expression is an
application or a tuple.  HE's types are atoms, so they are data of the same kind.

LF (the intrinsic encoding of the theory in the logical framework):
  tp : type            atoms used as types;  s_k : tp, e_n : tp -> ... -> tp
  tm : tp -> type      terms of a type
  c_i : Π xs:tp. tm T  one constant per declaration (the declaration's identity is kept)
  app_n : Π p1..pn r:tp. tm (-> p1..pn r) -> tm p1 -> ... -> tm pn -> tm r
  tup_n : Π t0..tn:tp. tm t0 -> ... -> tm tn -> tm (t0 ... tn)
  lit_k : tm T         a literal at its type
  dyn : Π t:tp. tm t   an argument that nothing types, as HE accepts it
A derivation becomes a closed LF term; the kernel checks it against tm T.  A refutation is
the absence of a canonical term, which the elaborator decides by exhaustive search.

HOTG (membership): atoms are sets, built by the tuple functions tp_n; a declaration is the
axiom that the declared atom is a member of its type, for every instance of the scheme;
application and tuples are membership laws.  A derivation becomes a proof term that the
profile's kernel checks.  Membership is extrinsic: nothing here proves a non-membership.
"""

from __future__ import annotations

from typing import Optional

import fo_schemes as F
from sexpr import Str, Sym, Var, show


class Names:
    """Stable generated names for HE atoms, so that guest names never clash with builtins."""

    def __init__(self, prefix: str):
        self.prefix = prefix
        self.table: dict = {}

    def __call__(self, leaf) -> str:
        key = (type(leaf).__name__, str(leaf))
        if key not in self.table:
            self.table[key] = f"{self.prefix}{len(self.table)}"
        return self.table[key]


def merged(substs: list) -> dict:
    out: dict = {}
    for subst in substs:
        out.update(subst)
    return out


def final(atom, substs: list):
    return F.resolve(atom, merged(substs))


def unsupported(derivation: F.Derivation) -> Optional[str]:
    """The feature of a derivation that the encodings do not replay, if any."""
    if derivation is None:
        return None
    if derivation.rule == "subtype":
        return "subtype"
    if derivation.rule == "app":
        for how in derivation.detail["accepted"]:
            if how == "meta":
                return "meta-type"
        reason = unsupported(derivation.detail["function"])
        if reason:
            return reason
    for part in derivation.parts:
        reason = unsupported(part)
        if reason:
            return reason
    return None


def gradual(derivation: F.Derivation) -> bool:
    if derivation is None:
        return False
    if derivation.rule == "app" and any(h in ("untyped", "undefined")
                                        for h in derivation.detail["accepted"]):
        return True
    if derivation.rule == "app" and gradual(derivation.detail["function"]):
        return True
    return any(gradual(p) for p in derivation.parts)


# ---------------------------------------------------------------- LF


class LF:
    def __init__(self, space: F.Space):
        self.space = space
        self.names = Names("s")
        self.decls: dict[str, str] = {}
        self.order: list[str] = []
        self.recording = False
        self.free: list[str] = []

    def declare(self, name: str, typ) -> str:
        if name not in self.decls:
            self.decls[name] = typ
            self.order.append(name)
        return name

    # LF terms with named binders: ("con", c) ("var", x) ("app", f, a) ("pi", x, A, B)
    # ("lam", x, A, b) ("type",)
    def kernel(self, term, ctx: list) -> str:
        tag = term[0]
        if tag == "con":
            return f"(Con {term[1]})"
        if tag == "var":
            return f"(Var {ctx.index(term[1])})"
        if tag == "app":
            return f"(App {self.kernel(term[1], ctx)} {self.kernel(term[2], ctx)})"
        if tag in ("pi", "lam"):
            head = "Pi" if tag == "pi" else "Lam"
            return f"({head} {self.kernel(term[2], ctx)} {self.kernel(term[3], [term[1]] + ctx)})"
        return "(Srt type)"

    def base(self) -> None:
        self.declare("tp", ("type",))
        self.declare("tm", ("pi", "_", ("con", "tp"), ("type",)))

    def e_const(self, n: int) -> str:
        typ = ("con", "tp")
        for i in range(n):
            typ = ("pi", f"_e{i}", ("con", "tp"), typ)
        return self.declare(f"e{n}", typ)

    def tp_term(self, atom):
        if isinstance(atom, Var):
            name = "v_" + atom.name.replace("#", "_")
            if self.recording and name not in self.free:
                self.free.append(name)
            return ("var", name)
        if isinstance(atom, tuple):
            term = ("con", self.e_const(len(atom)))
            for item in atom:
                term = ("app", term, self.tp_term(item))
            return term
        name = self.names(atom)
        self.declare(name, ("con", "tp"))
        return ("con", name)

    def tm(self, atom):
        return ("app", ("con", "tm"), self.tp_term(atom))

    def closed(self, typ_body, variables: list):
        typ = typ_body
        for v in reversed(variables):
            typ = ("pi", "v_" + v.name.replace("#", "_"), ("con", "tp"), typ)
        return typ

    def quiet(self, build):
        saved, self.recording = self.recording, False
        try:
            return build()
        finally:
            self.recording = saved

    def decl_const(self, index: int, kind: str) -> str:
        return self.quiet(lambda: self._decl_const(index, kind))

    def _decl_const(self, index: int, kind: str) -> str:
        subject, typ = self.space.declarations[index]
        variables = F.variables(typ)
        return self.declare(f"{kind}{index}", self.closed(self.tm(typ), variables))

    def app_const(self, n: int) -> str:
        return self.quiet(lambda: self._app_const(n))

    def _app_const(self, n: int) -> str:
        names = [f"p{i}" for i in range(n)] + ["r"]
        arrow = (F.ARROW,) + tuple(Var(x) for x in names)
        body = self.tm(Var("r"))
        for i in reversed(range(n)):
            body = ("pi", f"_a{i}", self.tm(Var(f"p{i}")), body)
        body = ("pi", "_f", self.tm(arrow), body)
        for x in reversed(names):
            body = ("pi", "v_" + x, ("con", "tp"), body)
        return self.declare(f"app{n}", body)

    def tup_const(self, n: int) -> str:
        return self.quiet(lambda: self._tup_const(n))

    def _tup_const(self, n: int) -> str:
        names = [f"t{i}" for i in range(n + 1)]
        body = self.tm(tuple(Var(x) for x in names))
        for i in reversed(range(n + 1)):
            body = ("pi", f"_x{i}", self.tm(Var(f"t{i}")), body)
        for x in reversed(names):
            body = ("pi", "v_" + x, ("con", "tp"), body)
        return self.declare(f"tup{n}", body)

    def dyn_const(self) -> str:
        return self.quiet(lambda: self.declare(
            "dyn", ("pi", "v_t", ("con", "tp"), self.tm(Var("t")))))

    def lit_const(self, atom, typ) -> str:
        name = "lit_" + self.names(atom)
        return self.quiet(lambda: self.declare(name, self.tm(typ)))

    def term(self, derivation: F.Derivation, substs: list, expected=None):
        if derivation is None:
            # An argument that nothing types: HE accepts it at its parameter type.
            return ("app", ("con", self.dyn_const()), self.tp_term(final(expected, substs)))
        rule = derivation.rule
        if rule == "literal":
            return ("con", self.lit_const(derivation.term, derivation.type))
        if rule in ("decl", "annotation"):
            kind = "c" if rule == "decl" else "a"
            term = ("con", self.decl_const(derivation.detail["declaration"], kind))
            for v in derivation.detail["vars"]:
                term = ("app", term, self.tp_term(final(v, substs)))
            return term
        if rule == "tuple":
            parts = derivation.parts
            term = ("con", self.tup_const(len(parts) - 1))
            for part in parts:
                term = ("app", term, self.tp_term(final(part.type, substs)))
            for part in parts:
                term = ("app", term, self.term(part, substs))
            return term
        if rule == "app":
            inner = substs + [derivation.detail["subst"]]
            function = derivation.detail["function"]
            params, result = function.type[1:-1], function.type[-1]
            term = ("con", self.app_const(len(params)))
            for p in params:
                term = ("app", term, self.tp_term(final(p, inner)))
            term = ("app", term, self.tp_term(final(result, inner)))
            term = ("app", term, self.term(function, inner))
            for part, p in zip(derivation.parts, params):
                term = ("app", term, self.term(part, inner, p))
            return term
        raise ValueError(rule)

    def signature(self) -> str:
        text = "SNil"
        for name in reversed(self.order):
            text = f"(SCons (DConst {name} {self.kernel(self.decls[name], [])}) {text})"
        return text


def lf_judgment(space: F.Space, derivation: F.Derivation):
    """The signature, closed term and closed type checking one derivation."""
    lf = LF(space)
    lf.base()
    substs = [derivation.detail.get("subst", {})] if derivation.rule == "app" else []
    lf.recording = True
    term = lf.term(derivation, [])
    typ = lf.tm(final(derivation.type, substs))
    lf.recording = False
    # Close over the type variables the judgment leaves free.
    for name in reversed(lf.free):
        term = ("lam", name, ("con", "tp"), term)
        typ = ("pi", name, ("con", "tp"), typ)
    return lf.signature(), lf.kernel(term, []), lf.kernel(typ, [])


# ---------------------------------------------------------------- HOTG


class HOTG:
    def __init__(self, space: F.Space):
        self.space = space
        self.names = Names("h")
        self.constants: dict[str, str] = {}
        self.axioms: dict[str, str] = {}
        self.recording = False
        self.free: list[str] = []

    def const(self, name: str, typ: str) -> str:
        self.constants.setdefault(name, typ)
        return name

    def tp_fn(self, n: int) -> str:
        return self.const(f"tp{n}", "(-> " + " ".join(["set"] * (n + 1)) + ")")

    def set_term(self, atom) -> str:
        if isinstance(atom, Var):
            name = "x" + atom.name.replace("#", "_")
            if self.recording and name not in self.free:
                self.free.append(name)
            return name
        if isinstance(atom, tuple):
            if not atom:
                return self.const("tp0", "set")
            return f"({self.tp_fn(len(atom))} " + " ".join(self.set_term(a) for a in atom) + ")"
        return self.const(self.names(atom), "set")

    def member(self, element, typ) -> str:
        return f"(In {self.set_term(element)} {self.set_term(typ)})"

    @staticmethod
    def forall(names: list, body: str) -> str:
        for x in reversed(names):
            body = f"(all set (lam {x} {body}))"
        return body

    def axiom(self, name: str, statement: str) -> str:
        self.axioms.setdefault(name, statement)
        return name

    def quietly(self, build):
        saved, self.recording = self.recording, False
        try:
            return build()
        finally:
            self.recording = saved

    def decl_axiom(self, index: int, kind: str) -> str:
        subject, typ = self.space.declarations[index]
        variables = F.variables(typ)
        names = ["x" + v.name.replace("#", "_") for v in variables]
        return self.quietly(lambda: self.axiom(
            f"{kind}{index}", self.forall(names, self.member(subject, typ))))

    def app_law(self, n: int) -> str:
        return self.quietly(lambda: self._app_law(n))

    def _app_law(self, n: int) -> str:
        params = [Var(f"p{i}") for i in range(n)]
        args = [Var(f"a{i}") for i in range(n)]
        f, r = Var("f"), Var("r")
        statement = self.member((f,) + tuple(args), r)
        for a, p in reversed(list(zip(args, params))):
            statement = f"(imp {self.member(a, p)} {statement})"
        statement = f"(imp {self.member(f, (F.ARROW,) + tuple(params) + (r,))} {statement})"
        names = ["xf"] + [f"xp{i}" for i in range(n)] + ["xr"] + [f"xa{i}" for i in range(n)]
        return self.axiom(f"app{n}", self.forall(names, statement))

    def tup_law(self, n: int) -> str:
        return self.quietly(lambda: self._tup_law(n))

    def _tup_law(self, n: int) -> str:
        elems = [Var(f"y{i}") for i in range(n + 1)]
        types = [Var(f"t{i}") for i in range(n + 1)]
        statement = self.member(tuple(elems), tuple(types))
        for e, t in reversed(list(zip(elems, types))):
            statement = f"(imp {self.member(e, t)} {statement})"
        names = [f"xy{i}" for i in range(n + 1)] + [f"xt{i}" for i in range(n + 1)]
        return self.axiom(f"tup{n}", self.forall(names, statement))

    def lit_axiom(self, atom, typ) -> str:
        return self.quietly(lambda: self.axiom("lit_" + self.names(atom), self.member(atom, typ)))

    @staticmethod
    def instantiate(proof: str, terms: list) -> str:
        for t in terms:
            proof = f"(pf:all-elim {proof} {t})"
        return proof

    def proof(self, derivation: F.Derivation, substs: list) -> Optional[str]:
        rule = derivation.rule
        if rule == "literal":
            return f"(pf:known {self.lit_axiom(derivation.term, derivation.type)})"
        if rule in ("decl", "annotation"):
            kind = "d" if rule == "decl" else "n"
            name = self.decl_axiom(derivation.detail["declaration"], kind)
            terms = [self.set_term(final(v, substs)) for v in derivation.detail["vars"]]
            return self.instantiate(f"(pf:known {name})", terms)
        if rule == "tuple":
            parts = derivation.parts
            law = self.tup_law(len(parts) - 1)
            terms = [self.set_term(p.term) for p in parts]
            terms += [self.set_term(final(p.type, substs)) for p in parts]
            proof = self.instantiate(f"(pf:known {law})", terms)
            for part in parts:
                proof = f"(pf:imp-elim {proof} {self.proof(part, substs)})"
            return proof
        if rule == "app":
            inner = substs + [derivation.detail["subst"]]
            function = derivation.detail["function"]
            params, result = function.type[1:-1], function.type[-1]
            arguments = derivation.term[1:]
            law = self.app_law(len(params))
            terms = [self.set_term(derivation.term[0])]
            terms += [self.set_term(final(p, inner)) for p in params]
            terms += [self.set_term(final(result, inner))]
            terms += [self.set_term(a) for a in arguments]
            proof = self.instantiate(f"(pf:known {law})", terms)
            proof = f"(pf:imp-elim {proof} {self.proof(function, inner)})"
            for part in derivation.parts:
                proof = f"(pf:imp-elim {proof} {self.proof(part, inner)})"
            return proof
        raise ValueError(rule)


def hotg_judgment(space: F.Space, derivation: F.Derivation):
    """The theory, proof and membership statement for one derivation."""
    hotg = HOTG(space)
    substs = [derivation.detail.get("subst", {})] if derivation.rule == "app" else []
    hotg.recording = True
    proof = hotg.proof(derivation, [])
    typ = final(derivation.type, substs)
    statement = hotg.member(derivation.term, typ)
    hotg.recording = False
    for x in reversed(hotg.free):
        proof = f"(pf:fix {x} {proof})"
    statement = HOTG.forall(hotg.free, statement)
    return hotg, proof, statement


# ---------------------------------------------------------------- dependent readings
#
# For the dependent reading of telescopes the encodings index typing by the atom typed:
# LF's `has : tp -> tp -> type` relates an atom to its type, and a declaration of `c` with
# a telescope `(-> (: $x1 A1) ... R)` becomes an introduction rule for applications of `c`
# whose later domains and result mention the arguments themselves:
#   ap_i : Π implicit:tp. Π a1..an:tp. has a1 A1[a/x] -> ... -> has (c a1..an) R[a/x]
# In HOTG the same rule is a membership law.


def telescope(typ):
    """Binder variables, domains and result of a declared arrow (binders may be absent)."""
    names, domains = [], []
    for parameter in typ[1:-1]:
        named = F.binder(parameter)
        if named is None:
            names.append(None)
            domains.append(parameter)
        else:
            names.append(named[0])
            domains.append(named[1])
    return names, domains, typ[-1]


def _bound_application(declared_type, arity: int):
    """The parameters named by the application rule of a declaration: the implicit
    variables and one atom per argument, with the domains and result rewritten."""
    names, domains, result = telescope(declared_type)
    if len(domains) != arity:
        return None
    arguments = [Var(f"arg{j}") for j in range(arity)]
    rename = {}
    for name, argument in zip(names, arguments):
        if name is not None:
            rename[name] = argument
    domains = [F.resolve(d, rename) for d in domains]
    result = F.resolve(result, rename)
    implicit = [v for v in F.variables(declared_type) if v not in rename]
    return implicit, arguments, domains, result


class DependentLF(LF):
    def base(self) -> None:
        self.declare("tp", ("type",))
        self.declare("has", ("pi", "_a", ("con", "tp"), ("pi", "_t", ("con", "tp"), ("type",))))

    def has(self, atom, typ):
        return ("app", ("app", ("con", "has"), self.tp_term(atom)), self.tp_term(typ))

    def _decl_const(self, index: int, kind: str) -> str:
        subject, typ = self.space.declarations[index]
        return self.declare(f"{kind}{index}", self.closed(self.has(subject, typ), F.variables(typ)))

    def rule_const(self, index: int, arity: int) -> Optional[str]:
        subject, typ = self.space.declarations[index]
        bound = _bound_application(typ, arity)
        if bound is None:
            return None
        implicit, arguments, domains, result = bound

        def build():
            body = self.has((subject,) + tuple(arguments), result)
            for j in reversed(range(arity)):
                body = ("pi", f"_h{j}", self.has(arguments[j], domains[j]), body)
            for v in reversed(implicit + arguments):
                body = ("pi", "v_" + v.name.replace("#", "_"), ("con", "tp"), body)
            return self.declare(f"ap{index}", body)

        return self.quiet(build)

    def _tup_const(self, n: int) -> str:
        elems = [Var(f"y{i}") for i in range(n + 1)]
        types = [Var(f"t{i}") for i in range(n + 1)]
        body = self.has(tuple(elems), tuple(types))
        for e, t in reversed(list(zip(elems, types))):
            body = ("pi", "_x" + e.name, self.has(e, t), body)
        for v in reversed(elems + types):
            body = ("pi", "v_" + v.name, ("con", "tp"), body)
        return self.declare(f"tup{n}", body)

    def dyn_const(self) -> str:
        return self.quiet(lambda: self.declare(
            "dyn", ("pi", "v_a", ("con", "tp"), ("pi", "v_t", ("con", "tp"),
                                                     self.has(Var("a"), Var("t"))))))

    def lit_const(self, atom, typ) -> str:
        name = "lit_" + self.names(atom)
        return self.quiet(lambda: self.declare(name, self.has(atom, typ)))

    def term(self, derivation, substs: list, expected=None, atom=None):
        if derivation is None:
            return ("app", ("app", ("con", self.dyn_const()), self.tp_term(atom)),
                    self.tp_term(final(expected, substs)))
        rule = derivation.rule
        if rule == "literal":
            return ("con", self.lit_const(derivation.term, derivation.type))
        if rule in ("decl", "annotation"):
            kind = "c" if rule == "decl" else "a"
            term = ("con", self.decl_const(derivation.detail["declaration"], kind))
            for v in derivation.detail["vars"]:
                term = ("app", term, self.tp_term(final(v, substs)))
            return term
        if rule == "tuple":
            parts = derivation.parts
            term = ("con", self.tup_const(len(parts) - 1))
            for part in parts:
                term = ("app", term, self.tp_term(part.term))
            for part in parts:
                term = ("app", term, self.tp_term(final(part.type, substs)))
            for part in parts:
                term = ("app", term, self.term(part, substs))
            return term
        if rule == "app":
            inner = substs + [derivation.detail["subst"]]
            function = derivation.detail["function"]
            arguments = derivation.term[1:]
            if function.rule != "decl":
                raise ValueError("application of a non-constant head")
            index = function.detail["declaration"]
            name = self.rule_const(index, len(arguments))
            subject, declared = self.space.declarations[index]
            implicit, _, domains, _ = _bound_application(declared, len(arguments))
            fresh = dict(zip(F.variables(declared), function.detail["vars"]))
            term = ("con", name)
            for v in implicit:
                term = ("app", term, self.tp_term(final(fresh[v], inner)))
            for a in arguments:
                term = ("app", term, self.tp_term(a))
            for part, a, domain in zip(derivation.parts, arguments, function.type[1:-1]):
                named = F.binder(domain) if self.space.binders else None
                expected_type = named[1] if named else domain
                term = ("app", term, self.term(part, inner, expected_type, a))
            return term
        raise ValueError(rule)


def lf_dependent_judgment(space: F.Space, derivation: F.Derivation):
    lf = DependentLF(space)
    lf.base()
    substs = [derivation.detail.get("subst", {})] if derivation.rule == "app" else []
    lf.recording = True
    term = lf.term(derivation, [])
    typ = lf.has(derivation.term, final(derivation.type, substs))
    lf.recording = False
    for name in reversed(lf.free):
        term = ("lam", name, ("con", "tp"), term)
        typ = ("pi", name, ("con", "tp"), typ)
    return lf.signature(), lf.kernel(term, []), lf.kernel(typ, [])


class DependentHOTG(HOTG):
    def rule_axiom(self, index: int, arity: int) -> str:
        subject, typ = self.space.declarations[index]
        implicit, arguments, domains, result = _bound_application(typ, arity)

        def build():
            statement = self.member((subject,) + tuple(arguments), result)
            for a, d in reversed(list(zip(arguments, domains))):
                statement = f"(imp {self.member(a, d)} {statement})"
            names = ["x" + v.name.replace("#", "_") for v in implicit + arguments]
            return self.axiom(f"ap{index}", self.forall(names, statement))

        return self.quietly(build)

    def proof(self, derivation, substs):
        if derivation is not None and derivation.rule == "app":
            inner = substs + [derivation.detail["subst"]]
            function = derivation.detail["function"]
            arguments = derivation.term[1:]
            if function.rule != "decl":
                raise ValueError("application of a non-constant head")
            index = function.detail["declaration"]
            law = self.rule_axiom(index, len(arguments))
            subject, declared = self.space.declarations[index]
            implicit, _, _, _ = _bound_application(declared, len(arguments))
            fresh = dict(zip(F.variables(declared), function.detail["vars"]))
            terms = [self.set_term(final(fresh[v], inner)) for v in implicit]
            terms += [self.set_term(a) for a in arguments]
            proof = self.instantiate(f"(pf:known {law})", terms)
            for part in derivation.parts:
                if part is None:
                    return None
                sub = self.proof(part, inner)
                if sub is None:
                    return None
                proof = f"(pf:imp-elim {proof} {sub})"
            return proof
        return super().proof(derivation, substs)


def hotg_dependent_judgment(space: F.Space, derivation: F.Derivation):
    hotg = DependentHOTG(space)
    substs = [derivation.detail.get("subst", {})] if derivation.rule == "app" else []
    hotg.recording = True
    proof = hotg.proof(derivation, [])
    typ = final(derivation.type, substs)
    statement = hotg.member(derivation.term, typ)
    hotg.recording = False
    if proof is None:
        return hotg, None, statement
    for x in reversed(hotg.free):
        proof = f"(pf:fix {x} {proof})"
    statement = HOTG.forall(hotg.free, statement)
    return hotg, proof, statement
