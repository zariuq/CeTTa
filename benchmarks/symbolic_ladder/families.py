"""Generators and independent oracles for the scaled symbolic families.

The MeTTa text and the answer lists are produced from the same sizes, but the
answers are computed by this module's own recursion, not by reading a .metta
file or an .expected file.
"""

from __future__ import annotations


def hm_chain(depth: int) -> str:
    expr = "(li i)"
    for index in range(depth):
        expr = f"(app (lam x{index} (var x{index})) {expr})"
    return expr


def hm_program(depth: int) -> str:
    chain = hm_chain(depth)
    return f"""; Hindley–Milner with real logical variables. Not MeTTa's type system.
; Polymorphic let re-infers the binding at each use. An ill-typed
; application answers nothing, and the following query still answers.
; Depth {depth}.

(= (lookup ((pair $n $t) $rest) $n) $t)
(= (lookup ((pair $m $t) $rest) $n) (lookup $rest $n))
(= (lookup ((poly $n $e $env0) $rest) $n) (infer $env0 $e))
(= (lookup ((poly $m $e $env0) $rest) $n) (lookup $rest $n))

(= (infer $env (var $n)) (lookup $env $n))
(= (infer $env (li i)) int)
(= (infer $env (li b)) bool)
(= (infer $env (lam $x $body))
   (let $bt (infer ((pair $x $a) $env) $body) (fun $a $bt)))
(= (infer $env (app $f $arg))
   (let (fun $d $r) (infer $env $f)
     (let $at (infer $env $arg)
       (let $_ (uni $d $at) $r))))
(= (infer $env (letv $x $e $body))
   (infer ((poly $x $e $env) $env) $body))
(= (infer $env (both $p $q))
   (let $tp (infer $env $p)
     (let $tq (infer $env $q) (prod $tp $tq))))
(= (uni $x $x) ok)

!(infer nil (app (li i) (li b)))
!(infer nil {chain})
!(infer nil (letv id (lam x (var x))
             (both (app (var id) (li i)) (app (var id) (li b)))))
!(let (fun $a $a) (infer nil (lam z (var z))) same)
!(let (fun int bool) (infer nil (lam z (var z))) bad)
!(infer nil (li b))
"""


def hm_answers(depth: int) -> list[str]:
    del depth
    # Ill-typed application: nothing.
    # Chain of identity applications on an int literal.
    # Polymorphic let instantiates id at int and at bool.
    # Open identity type is refined by a repeated variable; the clash is empty.
    # A later ground query still answers.
    return ["int", "(prod int bool)", "same", "bool"]


def _hm_walk(term, subst):
    seen = set()
    while isinstance(term, tuple) and term and term[0] == "v" and term in subst:
        if term in seen:
            break
        seen.add(term)
        term = subst[term]
    return term


def _hm_unify(left, right, subst):
    left = _hm_walk(left, subst)
    right = _hm_walk(right, subst)
    if left == right:
        return True
    if isinstance(left, tuple) and left and left[0] == "v":
        subst[left] = right
        return True
    if isinstance(right, tuple) and right and right[0] == "v":
        subst[right] = left
        return True
    if (
        isinstance(left, tuple)
        and isinstance(right, tuple)
        and left
        and right
        and left[0] == right[0]
        and left[0] in ("fun", "prod")
        and len(left) == 3
        and len(right) == 3
    ):
        return _hm_unify(left[1], right[1], subst) and _hm_unify(
            left[2], right[2], subst
        )
    if (
        isinstance(left, tuple)
        and isinstance(right, tuple)
        and left[:1] == ("g",)
        and right[:1] == ("g",)
    ):
        return left[1] == right[1]
    return False


def _hm_fresh(state):
    state["n"] += 1
    return ("v", state["n"])


def _hm_lookup(env, name, state):
    if not env:
        return []
    item, rest = env[0], env[1:]
    found = []
    if item[0] == "pair" and item[1] == name:
        found.append((item[2], {}))
    elif item[0] == "poly" and item[1] == name:
        found.extend(_hm_infer(item[3], item[2], state, {}))
    if item[0] in ("pair", "poly"):
        found.extend(_hm_lookup(rest, name, state))
    return found


def _hm_infer(env, expr, state, subst):
    kind = expr[0]
    if kind == "var":
        return [
            (_hm_walk(term, inner), dict(subst))
            for term, inner in _hm_lookup(env, expr[1], state)
        ]
    if kind == "li":
        ground = {"i": "int", "b": "bool"}[expr[1]]
        return [(("g", ground), subst)]
    if kind == "lam":
        domain = _hm_fresh(state)
        body_env = (("pair", expr[1], domain),) + tuple(env)
        outs = []
        for body, body_subst in _hm_infer(body_env, expr[2], state, dict(subst)):
            outs.append((("fun", domain, body), body_subst))
        return outs
    if kind == "app":
        outs = []
        for fun, fun_subst in _hm_infer(env, expr[1], state, dict(subst)):
            fun = _hm_walk(fun, fun_subst)
            if isinstance(fun, tuple) and fun and fun[0] == "v":
                domain = _hm_fresh(state)
                result = _hm_fresh(state)
                fun_subst = dict(fun_subst)
                fun_subst[fun] = ("fun", domain, result)
                fun = ("fun", domain, result)
            if not (isinstance(fun, tuple) and fun and fun[0] == "fun"):
                continue
            domain, result = fun[1], fun[2]
            for arg, arg_subst in _hm_infer(env, expr[2], state, dict(fun_subst)):
                branch = dict(arg_subst)
                if _hm_unify(domain, arg, branch):
                    outs.append((_hm_walk(result, branch), branch))
        return outs
    if kind == "letv":
        body_env = (("poly", expr[1], expr[2], env),) + tuple(env)
        return _hm_infer(body_env, expr[3], state, dict(subst))
    if kind == "both":
        outs = []
        for left, left_subst in _hm_infer(env, expr[1], state, dict(subst)):
            for right, right_subst in _hm_infer(env, expr[2], state, dict(left_subst)):
                outs.append((("prod", left, right), right_subst))
        return outs
    raise ValueError(kind)


def _hm_show(term, subst):
    term = _hm_walk(term, subst)
    if isinstance(term, tuple) and term and term[0] == "g":
        return term[1]
    if isinstance(term, tuple) and term and term[0] == "fun":
        return f"(fun {_hm_show(term[1], subst)} {_hm_show(term[2], subst)})"
    if isinstance(term, tuple) and term and term[0] == "prod":
        return f"(prod {_hm_show(term[1], subst)} {_hm_show(term[2], subst)})"
    if isinstance(term, tuple) and term and term[0] == "v":
        return f"$V{term[1]}"
    return str(term)


def hm_infer_types(expr) -> list[str]:
    state = {"n": 0}
    return [_hm_show(term, subst) for term, subst in _hm_infer((), expr, state, {})]


def hm_holdout_program() -> str:
    return """; Hindley–Milner holdout. Same rules as the depth ladder, different terms.
; Bool is instantiated before int. An open identity is refined through an
; inner identity. A domain/range clash answers nothing, and the next query
; still answers.

(= (lookup ((pair $n $t) $rest) $n) $t)
(= (lookup ((pair $m $t) $rest) $n) (lookup $rest $n))
(= (lookup ((poly $n $e $env0) $rest) $n) (infer $env0 $e))
(= (lookup ((poly $m $e $env0) $rest) $n) (lookup $rest $n))

(= (infer $env (var $n)) (lookup $env $n))
(= (infer $env (li i)) int)
(= (infer $env (li b)) bool)
(= (infer $env (lam $x $body))
   (let $bt (infer ((pair $x $a) $env) $body) (fun $a $bt)))
(= (infer $env (app $f $arg))
   (let (fun $d $r) (infer $env $f)
     (let $at (infer $env $arg)
       (let $_ (uni $d $at) $r))))
(= (infer $env (letv $x $e $body))
   (infer ((poly $x $e $env) $env) $body))
(= (infer $env (both $p $q))
   (let $tp (infer $env $p)
     (let $tq (infer $env $q) (prod $tp $tq))))
(= (uni $x $x) ok)

!(infer nil (app (li b) (li i)))
!(infer nil (app (lam f (app (var f) (li b))) (lam y (var y))))
!(infer nil (letv id (lam x (var x))
             (both (app (var id) (li b)) (app (var id) (li i)))))
!(let (fun $a $a) (infer nil (lam z (app (lam w (var w)) (var z)))) twin)
!(let (fun int bool) (infer nil (lam z (var z))) clash)
!(infer nil (both (li b) (li b)))
"""


def hm_holdout_answers() -> list[str]:
    # (app (li b) (li i)) is empty: bool is not a function.
    apply_bool = ("app", ("lam", "f", ("app", ("var", "f"), ("li", "b"))), ("lam", "y", ("var", "y")))
    poly = (
        "letv",
        "id",
        ("lam", "x", ("var", "x")),
        ("both", ("app", ("var", "id"), ("li", "b")), ("app", ("var", "id"), ("li", "i"))),
    )
    both_bool = ("both", ("li", "b"), ("li", "b"))
    return [
        hm_infer_types(apply_bool)[0],
        hm_infer_types(poly)[0],
        "twin",
        hm_infer_types(both_bool)[0],
    ]


def _tokens(count: int) -> tuple[str, ...]:
    return tuple("d" for _ in range(count))


def _fmt_tokens(tokens: tuple[str, ...]) -> str:
    text = "end"
    for tok in reversed(tokens):
        text = f"({tok} {text})"
    return text


def _parses(tokens: tuple[str, ...]) -> list:
    found = []
    if tokens and tokens[0] == "d":
        for inner in _parses(tokens[1:]):
            found.append(("more", inner))
    found.append(("stop", tokens))
    return found


def _fmt_parse(value) -> str:
    tag = value[0]
    if tag == "more":
        return f"(more {_fmt_parse(value[1])})"
    return f"(stop {_fmt_tokens(value[1])})"


def parse_program(count: int) -> str:
    full = _fmt_parse(_parses(_tokens(count))[0])
    sample = _fmt_tokens(_tokens(count))
    return f"""; Ambiguous remainder parser. Each leading d may be consumed or left
; in the remainder. Fully consumed input is one answer; shorter prefixes
; keep an open tail. Count {count}.

(= (num (d $rest))
   (let $r (num $rest) (more $r)))
(= (num $rest) (stop $rest))

!(num {sample})
!(let {full} (num {sample}) bound)
!(let (stop end) (num (d (d (d mismatch)))) miss)
!(num end)
"""


def parse_answers(count: int) -> list[str]:
    tokens = _tokens(count)
    lines = [_fmt_parse(value) for value in _parses(tokens)]
    lines.append("bound")
    # The mismatch query consumes nothing that is exactly (stop end):
    # its parses keep a d or the word mismatch, so that query is empty.
    lines.append("(stop end)")
    return lines


def reach_program(nodes: int, copies: int) -> str:
    facts = ["(gen 1 x)"]
    if nodes >= 2:
        for _ in range(copies):
            facts.append("(edge 1 2)")
        for src in range(2, nodes):
            facts.append(f"(edge {src} {src + 1})")
    facts.append(f"(kill {nodes} x)")
    queries = [f"!(live {node} $v)" for node in range(1, nodes + 1)]
    queries.append(f"!(live {nodes} y)")
    queries.append("!(match &self (kill $k $v) (killed $k $v))")
    body = "\n".join(facts)
    asks = "\n".join(queries)
    return f"""; Recursive reaching definitions as an ordered bag. Parallel edges
; from 1 to 2 keep duplicate derivations. The kill is observed and is
; not subtracted. Nodes {nodes}, copies {copies}.

{body}

(= (live $n $v) (match &self (gen $n $v) (hit $n $v)))
(= (live $n $v)
   (match &self (edge $s $n)
     (let (hit $s $v) (live $s $v) (hit $n $v))))

{asks}
"""


def reach_answers(nodes: int, copies: int) -> list[str]:
    lines = ["(hit 1 x)"]
    if nodes >= 2:
        for node in range(2, nodes + 1):
            lines.extend(f"(hit {node} x)" for _ in range(copies))
    # Unknown variable: nothing.
    lines.append(f"(killed {nodes} x)")
    return lines


def shape_edges(nodes: int, degree: int) -> list[tuple[str, str]]:
    return [
        (f"n{i}", f"n{(i + step) % nodes}")
        for i in range(nodes)
        for step in range(1, degree + 1)
    ]


def _shape_facts(edges: list[tuple[str, str]]) -> str:
    return "\n".join(f"(edge {src} {dst})" for src, dst in edges)


def star_count(edges: list[tuple[str, str]]) -> int:
    total = 0
    for center, _dst in edges:
        total += sum(1 for other, _ in edges if other == center)
    return total


def tri_count(edges: list[tuple[str, str]]) -> int:
    total = 0
    for src, mid in edges:
        for mid2, dst in edges:
            if mid2 != mid:
                continue
            for dst2, back in edges:
                if dst2 == dst and back == src:
                    total += 1
    return total


def dia_count(edges: list[tuple[str, str]]) -> int:
    total = 0
    for src, left in edges:
        for src2, right in edges:
            if src2 != src:
                continue
            for left2, sink in edges:
                if left2 != left:
                    continue
                for right2, sink2 in edges:
                    if right2 == right and sink2 == sink:
                        total += 1
    return total


def shape_count_program(nodes: int, degree: int, kind: str) -> str:
    edges = shape_edges(nodes, degree)
    query = {
        "star": "!(foldall + (match &self (, (edge $c $a) (edge $c $b)) 1) 0)",
        "tri": "!(foldall + (match &self (, (edge $a $b) (edge $b $c) (edge $c $a)) 1) 0)",
        "dia": "!(foldall + (match &self (, (edge $s $a) (edge $s $b) (edge $a $t) (edge $b $t)) 1) 0)",
    }[kind]
    return (
        f"; Circulant {kind} count. Node i links to the next {degree} nodes.\n"
        f"; Nodes {nodes}.\n\n{_shape_facts(edges)}\n\n{query}\n"
    )


def shape_count_answers(nodes: int, degree: int, kind: str) -> list[str]:
    edges = shape_edges(nodes, degree)
    count = {"star": star_count, "tri": tri_count, "dia": dia_count}[kind](edges)
    return [str(count)]


def hyper_holdout_program() -> str:
    return """; Structured hyperedges. The label is data, not a join key.
; A node that is the target of two hedges is reported twice.
; A hedge into a missing node answers nothing.

(hedge a (lbl p) b)
(hedge a (lbl q) c)
(hedge d (lbl r) b)
(hedge e (lbl p) missing)
(node b)
(node c)

!(match &self (, (hedge $x $lab $y) (node $y)) (h $x $lab $y))
!(match &self (hedge e (lbl p) missing) (absent e))
"""


def hyper_holdout_answers() -> list[str]:
    hedges = [("a", "(lbl p)", "b"), ("a", "(lbl q)", "c"), ("d", "(lbl r)", "b"), ("e", "(lbl p)", "missing")]
    nodes = ["b", "c"]
    return [f"(h {src} {label} {dst})" for src, label, dst in hedges if dst in nodes] + ["(absent e)"]


CASES = [
    ("hm_n8", "hm", lambda: hm_program(8), lambda: hm_answers(8)),
    ("hm_n16", "hm", lambda: hm_program(16), lambda: hm_answers(16)),
    ("hm_n32", "hm", lambda: hm_program(32), lambda: hm_answers(32)),
    ("hm_holdout12", "hm", hm_holdout_program, hm_holdout_answers),
    ("parse_n8", "parse", lambda: parse_program(8), lambda: parse_answers(8)),
    ("parse_n16", "parse", lambda: parse_program(16), lambda: parse_answers(16)),
    ("parse_n32", "parse", lambda: parse_program(32), lambda: parse_answers(32)),
    ("parse_holdout7", "parse", lambda: parse_program(7), lambda: parse_answers(7)),
    ("reach_n8", "reach", lambda: reach_program(8, 2), lambda: reach_answers(8, 2)),
    ("reach_n16", "reach", lambda: reach_program(16, 2), lambda: reach_answers(16, 2)),
    ("reach_n32", "reach", lambda: reach_program(32, 2), lambda: reach_answers(32, 2)),
    ("reach_holdout24", "reach", lambda: reach_program(24, 3), lambda: reach_answers(24, 3)),
    ("star_n6", "graph", lambda: shape_count_program(6, 2, "star"), lambda: shape_count_answers(6, 2, "star")),
    ("star_n8", "graph", lambda: shape_count_program(8, 2, "star"), lambda: shape_count_answers(8, 2, "star")),
    ("star_holdout7", "graph", lambda: shape_count_program(7, 2, "star"), lambda: shape_count_answers(7, 2, "star")),
    ("tri_n6", "graph", lambda: shape_count_program(6, 2, "tri"), lambda: shape_count_answers(6, 2, "tri")),
    ("tri_holdout7", "graph", lambda: shape_count_program(7, 2, "tri"), lambda: shape_count_answers(7, 2, "tri")),
    ("dia_n5", "graph", lambda: shape_count_program(5, 2, "dia"), lambda: shape_count_answers(5, 2, "dia")),
    ("dia_holdout4", "graph", lambda: shape_count_program(4, 2, "dia"), lambda: shape_count_answers(4, 2, "dia")),
    ("hyper_holdout", "graph", hyper_holdout_program, hyper_holdout_answers),
]


def write_cases(directory: str) -> None:
    from pathlib import Path

    root = Path(directory)
    for stem, _family, program, answers in CASES:
        (root / f"{stem}.metta").write_text(program())
        (root / f"{stem}.expected").write_text("\n".join(answers()) + "\n")


if __name__ == "__main__":
    from pathlib import Path

    write_cases(str(Path(__file__).resolve().parent))
