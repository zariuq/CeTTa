#!/usr/bin/env python3
"""Write the frozen MAM family cases: <case>.metta, <case>.expected, manifest.tsv.

Usage: gen.py OUTDIR [--only FAMILY]

Answers come from reference.Evaluator (proof, hm, parse), a direct fold (affine)
or the graph oracles below; never from an engine.  The manifest records each
case's role (size / large / holdout-renamed / holdout-structural / near-miss),
its parameters and the oracle's engine-independent call and attempt counts.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

from families import (HM_NAMES, HM_RENAMED, HM_STRUCTURAL, MUTATION_RENAMED, PARSE_NAMES,
                      PARSE_RENAMED, PROOF_BUILTIN_NOT, PROOF_NAMES, PROOF_RENAMED, affine_case,
                      answer, hm_case, lcg, mutation_case, parse_case, proof_case, source_text)

# (case, family, role, builder, params)
EQUATION_CASES = [
    ("proof_s15", "proof", "size", lambda: proof_case(15), dict(size=15)),
    ("proof_s17", "proof", "size", lambda: proof_case(17), dict(size=17)),
    ("proof_s19", "proof", "large", lambda: proof_case(19), dict(size=19)),
    ("proof_renamed_s17", "proof", "holdout-renamed", lambda: proof_case(17, PROOF_RENAMED), dict(size=17)),
    ("proof_structural_s17", "proof", "holdout-structural", lambda: proof_case(17, structural=True), dict(size=17)),
    ("proof_unprovable_s15", "proof", "near-miss", lambda: proof_case(15, goal="unprovable"), dict(size=15)),
    ("proof_builtin_not_s17", "proof", "diagnostic-builtin-name", lambda: proof_case(17, PROOF_BUILTIN_NOT), dict(size=17)),
    ("hm_k10", "hm", "size", lambda: hm_case(10), dict(depth=10)),
    ("hm_k12", "hm", "size", lambda: hm_case(12), dict(depth=12)),
    ("hm_k14", "hm", "large", lambda: hm_case(14), dict(depth=14)),
    ("hm_renamed_k12", "hm", "holdout-renamed", lambda: hm_case(12, HM_RENAMED), dict(depth=12)),
    ("hm_structural_k12", "hm", "holdout-structural", lambda: hm_case(12, HM_STRUCTURAL), dict(depth=12)),
    ("hm_nearmiss_k12", "hm", "near-miss", lambda: hm_case(12, near_miss=True), dict(depth=12)),
    ("parse_d3w4", "parse", "size", lambda: parse_case(3, 4), dict(depth=3, width=4)),
    ("parse_d4w3", "parse", "size", lambda: parse_case(4, 3), dict(depth=4, width=3)),
    ("parse_d5w3", "parse", "large", lambda: parse_case(5, 3), dict(depth=5, width=3)),
    ("parse_renamed_d4w3", "parse", "holdout-renamed", lambda: parse_case(4, 3, PARSE_RENAMED), dict(depth=4, width=3)),
    ("parse_structural_d4w3", "parse", "holdout-structural", lambda: parse_case(4, 3, structural=True), dict(depth=4, width=3)),
    ("parse_nearmiss_d4w3", "parse", "near-miss", lambda: parse_case(4, 3, near_miss=True), dict(depth=4, width=3)),
    ("mutation_early_n6", "mutation", "size", lambda: mutation_case(6, 5, 10), dict(length=6, digits=5, trigger=10)),
    ("mutation_early_n7", "mutation", "size", lambda: mutation_case(7, 5, 10), dict(length=7, digits=5, trigger=10)),
    ("mutation_early_n8", "mutation", "large", lambda: mutation_case(8, 5, 10), dict(length=8, digits=5, trigger=10)),
    ("mutation_late_n7", "mutation", "size", lambda: mutation_case(7, 5, 70000), dict(length=7, digits=5, trigger=70000)),
    ("mutation_renamed_n7", "mutation", "holdout-renamed", lambda: mutation_case(7, 5, 10, MUTATION_RENAMED), dict(length=7, digits=5, trigger=10)),
    ("mutation_structural_n7", "mutation", "holdout-structural", lambda: mutation_case(7, 5, 10, structural=True), dict(length=7, digits=5, trigger=10)),
    ("mutation_nearmiss_n7", "mutation", "near-miss", lambda: mutation_case(7, 5, None), dict(length=7, digits=5, trigger=None)),
]

AFFINE_CASES = [
    ("affine_alt3_n100k", "size", dict(count=100_000, step="alt3")),
    ("affine_alt3_n300k", "large", dict(count=300_000, step="alt3")),
    ("affine_signmul_n300k", "large", dict(count=300_000, step="signmul")),
    ("affine_double_n5k", "size", dict(count=5_000, step="double")),
    ("affine_rolling_mod_n300k", "large", dict(count=300_000, step="rolling-mod")),
    ("affine_renamed_alt3_n300k", "holdout-renamed", dict(count=300_000, step="alt3", fact="reading", op="combine", seed=11)),
    ("affine_structural_alt3_n300k", "holdout-structural", dict(count=300_000, step="alt3", structural=True, seed=13)),
    ("affine_square_nearmiss_n300k", "near-miss", dict(count=300_000, step="square-nearmiss")),
]


def circulant(nodes, degree):
    return [(f"n{i}", f"n{(i + s) % nodes}") for i in range(nodes) for s in range(1, degree + 1)]


def random_graph(nodes, edges, seed):
    g = lcg(seed)
    seen, out = set(), []
    while len(out) < edges:
        # High bits: the generator's low bits alternate parity, which would make
        # every edge run from one parity class to the other (no 2-paths).
        a, b = (next(g) >> 16) % nodes, (next(g) >> 16) % nodes
        if a != b and (a, b) not in seen:
            seen.add((a, b))
            out.append((f"v{a}", f"v{b}"))
    return out


def path2_count(edges):
    indeg, outdeg = {}, {}
    for a, b in edges:
        outdeg[a] = outdeg.get(a, 0) + 1
        indeg[b] = indeg.get(b, 0) + 1
    return sum(indeg[y] * outdeg.get(y, 0) for y in indeg)


def path2_witnesses(edges):
    by_src = {}
    for a, b in edges:
        by_src.setdefault(a, []).append(b)
    return [f"({x} {y} {z})" for x, y in edges for z in by_src.get(y, [])]


def triangle_count(edges):
    succ = {}
    for a, b in edges:
        succ.setdefault(a, set()).add(b)
    return sum(1 for a, b in edges for c in succ.get(b, ()) if a in succ.get(c, ()))


GRAPH_CASES = [
    ("graph_path2_count_circ_n2000_d8", "size", "count", lambda: circulant(2000, 8)),
    ("graph_path2_count_circ_n2000_d32", "large", "count", lambda: circulant(2000, 32)),
    ("graph_path2_count_rand_n1500_m12000", "holdout-structural", "count", lambda: random_graph(1500, 12000, 5)),
    ("graph_path2_witness_circ_n2000_d8", "large", "witness", lambda: circulant(2000, 8)),
    ("graph_path2_witness_rand_n1500_m12000", "holdout-structural", "witness", lambda: random_graph(1500, 12000, 5)),
    ("graph_tri_count_rand_n1000_m20000", "large", "triangle", lambda: random_graph(1000, 20000, 9)),
    ("graph_tri_count_rand_n800_m12000", "holdout-structural", "triangle", lambda: random_graph(800, 12000, 17)),
]


def graph_source(edges, kind):
    facts = "\n".join(f"(edge {a} {b})" for a, b in edges)
    query = {
        "count": "!(foldall + (match &self (, (edge $x $y) (edge $y $z)) 1) 0)",
        "witness": "!(match &self (, (edge $x $y) (edge $y $z)) ($x $y $z))",
        "triangle": "!(foldall + (match &self (, (edge $a $b) (edge $b $c) (edge $c $a)) 1) 0)",
    }[kind]
    answers = {"count": lambda: [str(path2_count(edges))],
               "witness": lambda: path2_witnesses(edges),
               "triangle": lambda: [str(triangle_count(edges))]}[kind]()
    return facts + "\n" + query + "\n", answers


def main():
    out = Path(sys.argv[1])
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    out.mkdir(parents=True, exist_ok=True)
    rows = []

    def emit(case, family, role, text, answers, params, counts=None, header_lines=0):
        (out / f"{case}.metta").write_text(text)
        (out / f"{case}.expected").write_text("".join(a + "\n" for a in answers))
        rows.append(dict(case=case, family=family, role=role, params=params, answers=len(answers),
                         header_lines=header_lines, oracle=counts))
        print(case, role, len(answers), (counts or {}).get("total_calls"), flush=True)

    for case, family, role, build, params in EQUATION_CASES:
        if only and family != only:
            continue
        prog, query, header = build()
        answers, counts = answer(prog, query)
        emit(case, family, role, source_text(prog, query, header), answers, params, counts,
             header_lines=1 if header else 0)
    if not only or only == "affine":
        for case, role, params in AFFINE_CASES:
            text, query, answers = affine_case(**params)
            emit(case, "affine", role, text + "!" + query + "\n", answers, params)
    if not only or only == "graph":
        for case, role, kind, build in GRAPH_CASES:
            text, answers = graph_source(build(), kind)
            emit(case, "graph", role, text, answers, dict(kind=kind))
    manifest = out / "manifest.json"
    if only and manifest.exists():
        kept = [r for r in json.loads(manifest.read_text()) if r["family"] != only]
        rows = kept + rows
    manifest.write_text(json.dumps(rows, indent=1, ensure_ascii=False) + "\n")


if __name__ == "__main__":
    main()
