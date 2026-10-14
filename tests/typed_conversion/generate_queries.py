"""Well-typed conversion queries from a recorded seed.

The seed only shuffles which universe pairs are emitted. The comparison
binary named in CURRENT-BINARY.txt fixes the nested-identity memory blowup,
so nesting depth is not capped.
"""

from __future__ import annotations

import random

SEED = 20260924
MAX_DEPTH = 8


def sort_tm(k: int) -> str:
    return f"(Sort (LevelConst {k}))"


def nest(term: str, domain: str, depth: int) -> str:
    if depth < 0:
        raise ValueError(depth)
    out = term
    for _ in range(depth):
        out = f"(App (Lam {domain} (idx 0)) {out})"
    return out


def parse(s: str, i: int = 0):
    while s[i] == " ":
        i += 1
    if s[i] != "(":
        j = i
        while j < len(s) and s[j] not in " ()":
            j += 1
        return s[i:j], j
    i += 1
    xs = []
    while True:
        while s[i] == " ":
            i += 1
        if s[i] == ")":
            return xs, i + 1
        a, i = parse(s, i)
        xs.append(a)


def emit(t) -> str:
    if isinstance(t, str):
        return t
    return "(" + " ".join(emit(x) for x in t) + ")"


def shift(t, d: int, c: int):
    if isinstance(t, str):
        return t
    head = t[0]
    if head == "idx":
        k = int(t[1])
        return ["idx", str(k if k < c else k + d)]
    if head in ("Lam", "Pi", "Sigma") and len(t) == 3:
        return [head, shift(t[1], d, c), shift(t[2], d, c + 1)]
    return [head] + [shift(x, d, c) if isinstance(x, list) else x for x in t[1:]]


def subst(t, j: int, s):
    if isinstance(t, str):
        return t
    head = t[0]
    if head == "idx":
        k = int(t[1])
        if k == j:
            return s
        if k > j:
            return ["idx", str(k - 1)]
        return t
    if head in ("Lam", "Pi", "Sigma") and len(t) == 3:
        return [head, subst(t[1], j, s), subst(t[2], j + 1, shift(s, 1, 0))]
    return [head] + [subst(x, j, s) if isinstance(x, list) else x for x in t[1:]]


def subst_idx0(term: str, replacement: str) -> str:
    body, _ = parse(term)
    rep, _ = parse(replacement)
    return emit(subst(body, 0, rep))


def q(qid, family, rules, left, right, ty, ctx="Nil", c_ctx=None, channel="diff", rules_tm="LNil", expect=None, depth=0):
    return {
        "id": qid,
        "family": family,
        "rules": rules,
        "left": left,
        "right": right,
        "ty": ty,
        "ctx": ctx,
        "c_ctx": c_ctx,
        "channel": channel,
        "rules_tm": rules_tm,
        "expect": expect,
        "depth": depth,
    }


def build():
    rows = []
    # Universes. Inequalities are shuffled by SEED; the grid itself is fixed.
    ne_max = 95
    pairs = [(i, j) for i in range(ne_max) for j in range(i + 1, ne_max)]
    random.Random(SEED).shuffle(pairs)
    for i, j in pairs:
        rows.append(q(
            f"sort-ne-{i}-{j}", "universe", ["universe"],
            sort_tm(i), sort_tm(j), sort_tm(max(i, j) + 1),
        ))
    for k in range(80):
        rows.append(q(
            f"sort-eq-{k}", "universe", ["universe"],
            sort_tm(k), sort_tm(k), sort_tm(k + 1),
        ))
    for k in range(30):
        for d in range(1, 5):
            rows.append(q(
                f"beta-sort-{k}-d{d}", "beta", ["beta"],
                nest(sort_tm(k), sort_tm(k + 1), d), sort_tm(k), sort_tm(k + 1),
                depth=d,
            ))
    lam = "(Lam (Sort (LevelConst 0)) (idx 0))"
    lam_ty = "(Pi (Sort (LevelConst 0)) (Sort (LevelConst 0)))"
    for d in range(1, 5):
        rows.append(q(
            f"beta-lam-d{d}", "beta", ["beta"],
            nest(lam, lam_ty, d), lam, lam_ty, depth=d,
        ))
    for i in range(15):
        for j in range(15):
            pi = f"(Pi {sort_tm(i)} {sort_tm(j)})"
            rows.append(q(
                f"pi-eq-{i}-{j}", "universe", ["universe"],
                pi, pi, sort_tm(max(i, j) + 1),
            ))
    for i in range(20):
        pi = f"(Pi {sort_tm(i)} {sort_tm(i)})"
        rows.append(q(
            f"pi-sort-{i}", "universe", ["universe"],
            pi, sort_tm(0), sort_tm(i + 2),
        ))
    for k in range(20):
        dep = f"(Pi {sort_tm(k)} (idx 0))"
        rows.append(q(
            f"dep-eq-{k}", "dependent", ["dependent"],
            dep, dep, sort_tm(k + 1),
        ))
        for d in range(1, 4):
            rows.append(q(
                f"dep-beta-{k}-d{d}", "dependent", ["beta", "dependent"],
                nest(dep, sort_tm(k + 1), d), dep, sort_tm(k + 1),
                depth=d,
            ))
    for k in range(30):
        dom = sort_tm(k)
        pi = f"(Pi {dom} {dom})"
        rows.append(q(
            f"eta-{k}", "eta", ["eta", "eta-expansion"],
            "(idx 0)", f"(Lam {dom} (App (idx 1) (idx 0)))", pi,
            ctx=f"(Cons {pi} Nil)",
            c_ctx=f"(PrimeCtxCons {pi} PrimeCtxNil)",
        ))
    for k in range(10):
        dom = f"(Pi {sort_tm(k)} {sort_tm(k)})"
        pi = f"(Pi {dom} {sort_tm(k)})"
        rows.append(q(
            f"eta-pi-{k}", "eta", ["eta", "eta-expansion"],
            "(idx 0)", f"(Lam {dom} (App (idx 1) (idx 0)))", pi,
            ctx=f"(Cons {pi} Nil)",
            c_ctx=f"(PrimeCtxCons {pi} PrimeCtxNil)",
        ))
    for k in range(20):
        dom = sort_tm(k)
        ctx_k = f"(Cons {dom} Nil)"
        cctx = f"(PrimeCtxCons {dom} PrimeCtxNil)"
        rows.append(q(
            f"refl-{k}", "id", ["id"],
            "(Refl (idx 0))", "(Refl (idx 0))",
            f"(Id {dom} (idx 0) (idx 0))",
            ctx=ctx_k, c_ctx=cctx,
        ))
        rows.append(q(
            f"refl-beta-{k}", "id", ["id", "beta"],
            "(Refl (idx 0))",
            f"(Refl (App (Lam {dom} (idx 0)) (idx 0)))",
            f"(Id {dom} (idx 0) (idx 0))",
            ctx=ctx_k, c_ctx=cctx, depth=1,
        ))
        rows.append(q(
            f"partial-{k}", "partial", ["partial", "beta"],
            f"(App (Lam {dom} (Lam {dom} (idx 1))) (idx 0))",
            f"(Lam {dom} (idx 1))",
            f"(Pi {dom} {dom})",
            ctx=ctx_k, c_ctx=cctx, depth=1,
        ))
    for k in range(8):
        dom = sort_tm(k)
        sig = f"(Sigma {dom} {dom})"
        rows.append(q(
            f"sigma-eta-{k}", "sigma", ["sigma", "eta"],
            "(idx 0)", "(Pair (Fst (idx 0)) (Snd (idx 0)))", sig,
            ctx=f"(Cons {sig} Nil)",
            c_ctx=f"(PrimeCtxCons {sig} PrimeCtxNil)",
        ))
    # Declared rules are not visible to a bare type:eq. These stay off the
    # kernel differential and are still part of the rule table.
    rows.append(q(
        "delta-full", "delta", ["delta"],
        "(App (App (DeclConst f) (DeclConst a)) (DeclConst b))",
        "(DeclConst a)", "(DeclConst A)",
        channel="checker",
        rules_tm="(LCons (PrimeRule f 2 ((PVar 0) (PVar 1)) (PVar 0)) LNil)",
        expect="Accepted",
    ))
    rows.append(q(
        "j-refl", "j", ["j", "iota"],
        "(App (DeclConst j) (Refl (DeclConst a)))",
        "(DeclConst a)", "(DeclConst A)",
        channel="checker",
        rules_tm="(LCons (PrimeRule j 1 ((Refl (PVar 0))) (PVar 0)) LNil)",
        expect="Accepted",
    ))
    rows.append(q(
        "pair-rec", "recursor", ["recursor", "iota"],
        "(App (DeclConst elim) (Pair (DeclConst a) (DeclConst b)))",
        "(DeclConst a)", "(DeclConst A)",
        channel="checker",
        rules_tm="(LCons (PrimeRule elim 1 ((Pair (PVar 0) (PVar 1))) (PVar 0)) LNil)",
        expect="Accepted",
    ))
    return rows


def closed_pi_inhabitants() -> list[str]:
    """Twenty closed elements of (Pi (Sort 0) (Sort 0)), nesting ≤ 16."""
    dom = "(Sort (LevelConst 0))"
    pi = "(Pi (Sort (LevelConst 0)) (Sort (LevelConst 0)))"
    terms = [f"(Lam {dom} {nest('(idx 0)', dom, d)})" for d in range(16)]
    terms += [nest(f"(Lam {dom} (idx 0))", pi, d) for d in range(1, 5)]
    if len(terms) != 20:
        raise ValueError(len(terms))
    return terms


def stability_equations() -> list[dict]:
    pi = "(Pi (Sort (LevelConst 0)) (Sort (LevelConst 0)))"
    inhabitants = closed_pi_inhabitants()
    specs = [
        {
            "id": "eta-pi-sort0",
            "left": "(idx 0)",
            "right": "(Lam (Sort (LevelConst 0)) (App (idx 1) (idx 0)))",
            "ty": pi,
            "ctx": f"(Cons {pi} Nil)",
            "c_ctx": f"(PrimeCtxCons {pi} PrimeCtxNil)",
        },
        {
            "id": "beta-pi-sort0",
            "left": "(idx 0)",
            "right": f"(App (Lam {pi} (idx 0)) (idx 0))",
            "ty": pi,
            "ctx": f"(Cons {pi} Nil)",
            "c_ctx": f"(PrimeCtxCons {pi} PrimeCtxNil)",
        },
    ]
    out = []
    for spec in specs:
        subs = []
        for n, inhab in enumerate(inhabitants):
            subs.append({
                "n": n,
                "left": subst_idx0(spec["left"], inhab),
                "right": subst_idx0(spec["right"], inhab),
            })
        out.append({**spec, "subs": subs, "note": "20 closed inhabitants"})
    out.append({
        "id": "phase1-sort0-variable",
        "subs": [],
        "note": "context variable has type Sort 0; (Pi (Sort 0) (Sort 0)) has type Sort 1, so this context has no closed inhabitant",
    })
    return out


def _check():
    rows = build()
    diff = [r for r in rows if r["channel"] == "diff"]
    if len(diff) < 5000:
        raise SystemExit(f"diff queries {len(diff)}")
    lifted = subst_idx0(
        "(Lam (Sort (LevelConst 0)) (App (idx 1) (idx 0)))",
        "(Lam (Sort (LevelConst 0)) (idx 0))",
    )
    if lifted != "(Lam (Sort (LevelConst 0)) (App (Lam (Sort (LevelConst 0)) (idx 0)) (idx 0)))":
        raise SystemExit(lifted)
    if len(closed_pi_inhabitants()) != 20:
        raise SystemExit("inhabitants")


if __name__ == "__main__":
    _check()
    rows = build()
    diff = [r for r in rows if r["channel"] == "diff"]
    print(f"seed {SEED}")
    print(f"queries {len(rows)}")
    print(f"diff {len(diff)}")
    print(f"max-depth {max(r['depth'] for r in rows)}")
