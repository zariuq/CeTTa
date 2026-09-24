"""Frozen workload families for the MeTTa abstract machine goal (M0).

Every family is built as Python terms, rendered to PeTTa source, and answered by
`reference.Evaluator` (or, for folds, by a direct fold): answers never come from
an engine.  Each family has scaling sizes, a renamed holdout (every relation and
constructor renamed), a structural holdout (different term layout, same
computation class) and a near-miss (the full search runs, no answer survives).

  proof   sized Hilbert-style backward chaining (the Loowoz family): open calls,
          nested non-tail nondeterministic calls, occurs check.
  hm      Hindley–Milner inference whose polymorphic `let` re-infers the bound
          term at every use (work doubles per let level): open terms, aliases,
          unification against partially known types.
  parse   backtracking recursive-descent parsing over token lists; every rule
          alternative re-parses, remainders are open tails refined by later
          patterns.
  affine  `foldall` with a user equation affine in the accumulator
          (acc -> a(item)*acc + b(item)), plus nonlinear near-misses.
  mutation  a recursive generator with work after every subcall whose consumer
          adds an equation between two answers: pending callers must continue
          under the logical update view (calls already entered keep their
          equations, later calls see the addition).
"""
from __future__ import annotations

import copy

from reference import E, Evaluator, Program, V, render, run_deep

IMP = "→"


# ---------------------------------------------------------------- proof search
def proof_program(names: dict, structural: bool) -> Program:
    n = names
    s, x, a, b, f, fs, xs = (V(v) for v in ("s", "x", "a", "b", "f", "fs", "xs"))
    p, q, r = V("p"), V("q"), V("r")

    def imp(l, rr):
        return E(n["imp"], l, rr)

    def judg(term, ty):  # the judgement "term : ty"
        return E(n["of"], ty, term) if structural else E(n["of"], term, ty)

    def call(rel, budget, j):  # structural variant puts the budget last
        return E(rel, j, budget) if structural else E(rel, budget, j)

    prog = Program(occurs_check=True)
    ax = [imp(p, imp(q, p)),
          imp(imp(p, imp(q, r)), imp(imp(p, q), imp(p, r))),
          imp(imp(E(n["neg"], p), E(n["neg"], q)), imp(q, p))]
    for name, ty in zip(n["axioms"], ax):
        prog.eq(call(n["pos"], s, judg(name, ty)), E(n["sized"], 1, judg(name, ty)))
    prog.eq(call(n["pos"], s, judg(E(n["mp"], f, x), b)),
            E("if", E("<", 2, s),
              E("let*", (
                  (E(n["sized"], fs, judg(f, imp(a, b))), call(n["rel"], E("-", s, 2), judg(f, imp(a, b)))),
                  (E(n["sized"], xs, judg(x, a)), call(n["rel"], E("-", E("-", s, 1), fs), judg(x, a)))),
                E(n["sized"], E("+", E("+", fs, xs), 1), judg(E(n["mp"], f, x), b))),
              E("empty")))
    prog.eq(call(n["rel"], s, judg(x, a)),
            E("if", E("<", 0, s), call(n["pos"], s, judg(x, a)), E("empty")))
    return prog


PROOF_NAMES = dict(rel="obc", pos="obc-gtz", sized="MkSized", of=":", imp=IMP, neg="¬",
                   mp="mp", axioms=("ax₁", "ax₂", "ax₃"), atoms=("𝜑", "𝜓", "𝜒"))
# Renamed holdouts use names that cannot collide with a dialect builtin: a
# builtin function name used as a head constructor changes which matching path
# applies (see PROOF_BUILTIN_NOT, a separate diagnostic).
PROOF_RENAMED = dict(rel="prove-r", pos="prove-pos-r", sized="sz-r", of="has-r", imp="imp-r",
                     neg="neg-r", mp="app-r", axioms=("kk-r", "ss-r", "cc-r"),
                     atoms=("pa-r", "pb-r", "pc-r"))
PROOF_BUILTIN_NOT = dict(PROOF_RENAMED, neg="not")


def proof_goal(names: dict, kind: str):
    pa, pb, pc = names["atoms"]

    def imp(l, r):
        return E(names["imp"], l, r)
    if kind == "loowoz":
        return imp(imp(imp(pa, pb), imp(pa, pc)), imp(imp(pb, pa), imp(pb, pc)))
    if kind == "unprovable":  # not a tautology: the whole sized search fails
        return imp(imp(pa, pb), imp(pb, pa))
    raise ValueError(kind)


def proof_case(size: int, names=PROOF_NAMES, structural=False, goal="loowoz"):
    prog = proof_program(names, structural)
    x = V("x")
    ty = proof_goal(names, goal)
    j = E(names["of"], ty, x) if structural else E(names["of"], x, ty)
    query = E(names["rel"], j, size) if structural else E(names["rel"], size, j)
    header = "!(translatePredicate (set_prolog_flag occurs_check True))\n"
    return prog, query, header


# ------------------------------------------------------------------ HM inference
def hm_program(n: dict) -> Program:
    env, name, m, t, rest, e, env0 = (V(v) for v in ("env", "n", "m", "t", "rest", "e", "env0"))
    x, body, a, bt, fn, arg, d, r, at, p, q, tp, tq = (
        V(v) for v in ("x", "body", "a", "bt", "f", "arg", "d", "r", "at", "p", "q", "tp", "tq"))
    L, I, U = n["lookup"], n["infer"], n["uni"]

    def cons(h, tl):
        return E(n["cons"], h, tl) if n["cons"] else E(h, tl)
    prog = Program()
    prog.eq(E(L, cons(E(n["pair"], name, t), rest), name), t)
    prog.eq(E(L, cons(E(n["pair"], m, t), rest), name), E(L, rest, name))
    prog.eq(E(L, cons(E(n["poly"], name, e, env0), rest), name), E(I, env0, e))
    prog.eq(E(L, cons(E(n["poly"], m, e, env0), rest), name), E(L, rest, name))
    prog.eq(E(I, env, E(n["var"], name)), E(L, env, name))
    prog.eq(E(I, env, E(n["li"], "i")), n["int"])
    prog.eq(E(I, env, E(n["li"], "b")), n["bool"])
    prog.eq(E(I, env, E(n["lam"], x, body)),
            E("let", bt, E(I, cons(E(n["pair"], x, a), env), body), E(n["fun"], a, bt)))
    prog.eq(E(I, env, E(n["app"], fn, arg)),
            E("let", E(n["fun"], d, r), E(I, env, fn),
              E("let", at, E(I, env, arg),
                E("let", V("_"), E(U, d, at), r))))
    prog.eq(E(I, env, E(n["letv"], x, e, body)),
            E(I, cons(E(n["poly"], x, e, env), env), body))
    prog.eq(E(I, env, E(n["both"], p, q)),
            E("let", tp, E(I, env, p), E("let", tq, E(I, env, q), E(n["prod"], tp, tq))))
    prog.eq(E(U, x, x), "ok")
    return prog


HM_NAMES = dict(lookup="lookup", infer="infer", uni="uni", cons=None, pair="pair", poly="poly",
                var="var", li="li", lam="lam", app="app", letv="letv", both="both", fun="fun",
                prod="prod", int="int", bool="bool", nil="nil")
HM_RENAMED = dict(lookup="find-r", infer="typeof-r", uni="same-r", cons=None, pair="bind-r",
                  poly="scheme-r", var="ref-r", li="lit-r", lam="fn-r", app="ap-r", letv="where-r",
                  both="tuple-r", fun="arrow-r", prod="times-r", int="nat-r", bool="truth-r",
                  nil="empty-env-r")
HM_STRUCTURAL = dict(HM_NAMES, cons="econs")


def hm_term(n: dict, depth: int, near_miss: bool):
    """letv f0 = λx0.x0 in letv fj = λxj. f(j-1) (f(j-1) xj) … in (fk i, fk b).
    Every use of fj re-infers its body, which uses f(j-1) twice."""
    def var(s):
        return E(n["var"], s)
    body = (E(n["both"], E(n["app"], var(f"f{depth}"), E(n["li"], "i")),
              E(n["app"], E(n["li"], "i"), E(n["li"], "b")))
            if near_miss else
            E(n["both"], E(n["app"], var(f"f{depth}"), E(n["li"], "i")),
              E(n["app"], var(f"f{depth}"), E(n["li"], "b"))))
    for j in range(depth, -1, -1):
        if j == 0:
            lam = E(n["lam"], "x0", var("x0"))
        else:
            prev = var(f"f{j-1}")
            lam = E(n["lam"], f"x{j}", E(n["app"], prev, E(n["app"], prev, var(f"x{j}"))))
        body = E(n["letv"], f"f{j}", lam, body)
    return body


def hm_case(depth: int, names=HM_NAMES, near_miss=False):
    return hm_program(names), E(names["infer"], names["nil"], hm_term(names, depth, near_miss)), ""


# ----------------------------------------------------------------------- parsing
def parse_program(n: dict) -> Program:
    ts, a, b, r1, r2, e, nn, rest = (V(v) for v in ("ts", "a", "b", "r1", "r2", "e", "n", "rest"))
    X, T, F, R = n["expr"], n["term"], n["factor"], n["res"]
    prog = Program()
    prog.eq(E(X, ts), E("let", E(R, a, E(n["plus"], r1)), E(T, ts),
                        E("let", E(R, b, r2), E(X, r1), E(R, E(n["add"], a, b), r2))))
    prog.eq(E(X, ts), E(T, ts))
    prog.eq(E(T, ts), E("let", E(R, a, E(n["times"], r1)), E(F, ts),
                        E("let", E(R, b, r2), E(T, r1), E(R, E(n["mul"], a, b), r2))))
    prog.eq(E(T, ts), E(F, ts))
    prog.eq(E(F, E(n["num"], nn, rest)), E(R, E(n["lit"], nn), rest))
    prog.eq(E(F, E(n["lp"], rest)), E("let", E(R, e, E(n["rp"], r2)), E(X, rest), E(R, E(n["par"], e), r2)))
    return prog


PARSE_NAMES = dict(expr="pexpr", term="pterm", factor="pfactor", res="res", plus="tplus",
                   times="ttimes", num="tnum", lp="tlp", rp="trp", add="add", mul="mul",
                   lit="lit", par="par", end="tend")
PARSE_RENAMED = dict(expr="sum-of-r", term="product-of-r", factor="atom-of-r", res="parsed-r",
                     plus="op-add-r", times="op-mul-r", num="digit-r", lp="open-r", rp="close-r",
                     add="plus-node-r", mul="times-node-r", lit="leaf-r", par="group-r", end="eof-r")


def parse_tokens(depth: int, width: int, near_miss: bool):
    """A nested expression: depth levels of parentheses, each level a sum of
    `width` products of the next level.  Returns a flat token list (Python)."""
    counter = [0]

    def num():
        counter[0] += 1
        return [("num", counter[0] % 10)]

    def level(d):
        if d == 0:
            return num()
        out = []
        for i in range(width):
            if i:
                out.append(("plus",) if i % 2 else ("times",))
            out += [("lp",)] + level(d - 1) + [("rp",)]
        return out
    toks = level(depth)
    if near_miss:
        toks = toks + [("plus",)]
    return toks


def encode_tokens(n: dict, toks, structural: bool):
    tail = n["end"]
    for tok in reversed(toks):
        if structural:  # flat cons cells instead of tokens that carry their tail
            head = E(n["num"], tok[1]) if tok[0] == "num" else n[tok[0]]
            tail = E("tcons", head, tail)
        elif tok[0] == "num":
            tail = E(n["num"], tok[1], tail)
        else:
            tail = E(n[tok[0]], tail)
    return tail


def parse_program_structural(n: dict) -> Program:
    ts, a, b, r1, r2, e, nn, rest = (V(v) for v in ("ts", "a", "b", "r1", "r2", "e", "n", "rest"))
    X, T, F, R = n["expr"], n["term"], n["factor"], n["res"]
    prog = Program()
    prog.eq(E(X, ts), E("let", E(R, a, E("tcons", n["plus"], r1)), E(T, ts),
                        E("let", E(R, b, r2), E(X, r1), E(R, E(n["add"], a, b), r2))))
    prog.eq(E(X, ts), E(T, ts))
    prog.eq(E(T, ts), E("let", E(R, a, E("tcons", n["times"], r1)), E(F, ts),
                        E("let", E(R, b, r2), E(T, r1), E(R, E(n["mul"], a, b), r2))))
    prog.eq(E(T, ts), E(F, ts))
    prog.eq(E(F, E("tcons", E(n["num"], nn), rest)), E(R, E(n["lit"], nn), rest))
    prog.eq(E(F, E("tcons", n["lp"], rest)),
            E("let", E(R, e, E("tcons", n["rp"], r2)), E(X, rest), E(R, E(n["par"], e), r2)))
    return prog


def parse_case(depth: int, width: int, names=PARSE_NAMES, structural=False, near_miss=False):
    prog = parse_program_structural(names) if structural else parse_program(names)
    toks = encode_tokens(names, parse_tokens(depth, width, near_miss), structural)
    ast = V("ast")
    query = E("let", E(names["res"], ast, names["end"]), E(names["expr"], toks), ast)
    return prog, query, ""


# ------------------------------------------------------------------ affine folds
def lcg(seed: int):
    x = seed
    while True:
        x = (1103515245 * x + 12345) % (1 << 31)
        yield x


def affine_items(count: int, kind: str, seed: int):
    g = lcg(seed)
    if kind == "sign":
        return [1 if next(g) >> 16 & 1 else -1 for _ in range(count)]
    return [(next(g) >> 16) % 11 - 5 for _ in range(count)]


AFFINE_STEPS = {
    # name: (rhs builder over (item, acc), python step, item kind)
    "alt3": (lambda i, a: E("+", E("*", -1, a), E("*", 3, i)), lambda i, a: -a + 3 * i, "small"),
    "signmul": (lambda i, a: E("+", E("*", i, a), 1), lambda i, a: i * a + 1, "sign"),
    "double": (lambda i, a: E("+", E("*", 2, a), i), lambda i, a: 2 * a + i, "small"),
    "square-nearmiss": (lambda i, a: E("%", E("+", E("*", a, a), i), 1000003),
                        lambda i, a: (a * a + i) % 1000003, "small"),
    "rolling-mod": (lambda i, a: E("%", E("+", E("*", 31, a), i), 1000003),
                    lambda i, a: (31 * a + i) % 1000003, "small"),
}


def affine_case(count: int, step: str, fact="obs", op="upd", seed=7, init=0, structural=False):
    rhs_fn, py_step, kind = AFFINE_STEPS[step]
    items = affine_items(count, kind, seed)
    i, a, k = V("item"), V("acc"), V("k")
    facts = ("\n".join(f"({fact} {j} {v})" for j, v in enumerate(items)) if not structural else
             "\n".join(f"({fact} (at {j}) (val {v}))" for j, v in enumerate(items)))
    program = f"(= ({op} $item $acc) {render(rhs_fn(i, a))})\n"
    pattern = f"({fact} $k $v)" if not structural else f"({fact} (at $k) (val $v))"
    query = f"(foldall {op} (match &self {pattern} $v) {init})"
    acc = init
    for item in items:
        acc = py_step(item, acc)
    return facts + "\n" + program, query, [str(acc)]


# ------------------------------------------------------- mutation between answers
MUTATION_NAMES = dict(digit="digit", num="num", cons="dcons", nil="dnil")
MUTATION_RENAMED = dict(digit="glyph", num="numeral", cons="gcons", nil="gnil")


def mutation_program(n: dict, digits: int, structural: bool) -> Program:
    k, d, r = V("k"), V("d"), V("r")
    prog = Program()
    for value in range(digits):
        prog.eq(E(n["digit"]), value)
    base, built = ((n["nil"], E(n["cons"], d, r)) if structural else
                   (0, E("+", E("*", digits, r), d)))
    # Two guarded equations: every call has two applicable equations, so a
    # closed call is a choice point the incremental answer producer can own.
    prog.eq(E(n["num"], k), E("if", E("==", k, 0), base, E("empty")))
    prog.eq(E(n["num"], k),
            E("if", E(">", k, 0),
              E("let", d, E(n["digit"]),
                E("let", r, E(n["num"], E("-", k, 1)), built)),
              E("empty")))
    return prog


def mutation_case(length: int, digits: int, trigger_index: int | None,
                  names=MUTATION_NAMES, structural=False):
    """The consumer adds digit `digits` when it meets the answer the unmutated
    enumeration produces at `trigger_index`; None never triggers (near-miss)."""
    prog = mutation_program(names, digits, structural)
    v, u = V("v"), V("_")
    if trigger_index is None:
        trigger = E(names["cons"], -1, names["nil"]) if structural else -1
    else:
        plain = Evaluator(copy.deepcopy(prog))
        values = run_deep(lambda: [plain.resolve(x) for _, x in
                                   zip(range(trigger_index + 1),
                                       plain.eval(E(names["num"], length)))])
        trigger = values[trigger_index]
    added = E("add-atom", "&self", E("=", E(names["digit"]), digits))
    query = E("let", v, E(names["num"], length),
              E("if", E("==", v, trigger), E("let", u, added, v), v))
    return prog, query, ""


# -------------------------------------------------------------------- execution
def answer(prog: Program, query):
    # The query may add equations; the source keeps the authored program.
    ev = Evaluator(copy.deepcopy(prog))
    out = run_deep(ev.run, query)
    return out, ev.counts()


def source_text(prog: Program, query, header: str) -> str:
    return header + prog.text() + "!" + render(query) + "\n"
