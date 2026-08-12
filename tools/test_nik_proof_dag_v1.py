#!/usr/bin/env python3
"""Positive and negative canaries for the exact NIK proof-DAG compiler."""

from __future__ import annotations

import gslt2parse_schema_v1 as sx
import nik_proof_dag_v1 as dag


def canonical_list(*values: sx.SExpr) -> sx.SExpr:
    result: sx.SExpr = sx.Symbol("LNil")
    for value in reversed(values):
        result = (sx.Symbol("LCons"), value, result)
    return result


def proof_list(*values: sx.SExpr) -> sx.SExpr:
    result: sx.SExpr = sx.Symbol("PrNil")
    for value in reversed(values):
        result = (sx.Symbol("PrCons"), value, result)
    return result


def proof(name: str, arguments: list[sx.SExpr], children: list[sx.SExpr]) -> sx.SExpr:
    instance: sx.SExpr = (
        sx.Symbol("GRuleInst"),
        sx.StringLiteral(name),
        canonical_list(*arguments),
    )
    return (sx.Symbol("GProof"), instance, proof_list(*children))


def pattern(head: str, *arguments: sx.SExpr) -> sx.SExpr:
    return (
        sx.Symbol("PApp"), sx.StringLiteral(head), canonical_list(*arguments)
    )


def require_error(value: sx.SExpr) -> None:
    try:
        dag.compile_article(sx.Symbol("Goal"), value)
    except dag.ProofDAGError:
        return
    raise SystemExit("malformed expanded proof unexpectedly compiled")


def main() -> int:
    goal = sx.Symbol("Goal")
    leaf = proof("shared-ax", [], [])
    shared = proof("shared-pair", [], [leaf, leaf])
    compiled = dag.compile_article(goal, shared)
    expected: sx.SExpr = (
        sx.Symbol("GProofDAG"),
        1,
        canonical_list(
            (
                sx.Symbol("GDNode"), 0,
                (sx.Symbol("GRuleInst"), sx.StringLiteral("shared-ax"),
                 sx.Symbol("LNil")),
                sx.Symbol("LNil"),
            ),
            (
                sx.Symbol("GDNode"), 1,
                (sx.Symbol("GRuleInst"), sx.StringLiteral("shared-pair"),
                 sx.Symbol("LNil")),
                canonical_list(
                    (sx.Symbol("GRNode"), 0),
                    (sx.Symbol("GRNode"), 0),
                ),
            ),
        ),
        1,
        goal,
    )
    if compiled.article != expected:
        raise SystemExit("shared proof did not compile to the canonical DAG")
    if (compiled.raw_nodes, compiled.unique_nodes, compiled.shared_occurrences) != (
        3, 2, 1,
    ):
        raise SystemExit("shared proof accounting is incorrect")
    rendered = sx.render(compiled.article)
    if sx.parse_sexprs(rendered, source="compiled DAG") != [compiled.article]:
        raise SystemExit("compiled DAG does not survive canonical S-expression round trip")

    left = proof("leaf", [sx.Symbol("Left")], [])
    right = proof("leaf", [sx.Symbol("Right")], [])
    ordered = proof(
        "root", [],
        [proof("pair", [], [left, right]), proof("pair", [], [right, left])],
    )
    ordered_compilation = dag.compile_article(goal, ordered)
    if ordered_compilation.unique_nodes != 5:
        raise SystemExit("distinct arguments or ordered premises were conflated")

    require_error(sx.Symbol("PrNil"))
    require_error(
        (
            sx.Symbol("GProof"),
            (sx.Symbol("GRuleInst"), sx.StringLiteral("bad"), sx.Symbol("LNil")),
            (sx.Symbol("PrCons"), leaf, sx.Symbol("BadTail")),
        )
    )

    leaf_pattern = pattern("K")
    pair_pattern = pattern("Pair", leaf_pattern, leaf_pattern)
    shared_goal = pattern("J", pair_pattern)
    shared_patterns = dag.compile_shared_article(
        shared_goal, proof("argument-ax", [pair_pattern], [])
    )
    if (
        shared_patterns.raw_proof_nodes,
        shared_patterns.unique_proof_nodes,
        shared_patterns.raw_pattern_occurrences,
        shared_patterns.unique_pattern_nodes,
        shared_patterns.shared_pattern_occurrences,
    ) != (1, 1, 7, 3, 4):
        raise SystemExit("shared Pattern accounting is incorrect")
    if not (
        isinstance(shared_patterns.article, tuple)
        and shared_patterns.article[:2] == (sx.Symbol("GProofDAG"), 2)
        and sx.parse_sexprs(
            sx.render(shared_patterns.article), source="shared compiled DAG"
        ) == [shared_patterns.article]
    ):
        raise SystemExit("shared Pattern article is not canonical version 2")

    differently_ordered = dag.compile_shared_article(
        pattern("J", pattern("Pair", pattern("A"), pattern("B"))),
        proof(
            "argument-ax",
            [pattern("Pair", pattern("B"), pattern("A"))],
            [],
        ),
    )
    if differently_ordered.unique_pattern_nodes != 5:
        raise SystemExit("shared Pattern DAG conflated ordered children")
    try:
        dag.compile_shared_article(sx.Symbol("NotAPattern"), leaf)
    except dag.ProofDAGError:
        pass
    else:
        raise SystemExit("shared compiler accepted a non-Pattern target")

    bound_zero: sx.SExpr = (sx.Symbol("Var"), 0)
    bound_one: sx.SExpr = (sx.Symbol("Var"), 1)
    abstraction: sx.SExpr = (
        sx.Symbol("PLam"), sx.Symbol("BNone"), bound_zero,
    )
    multi_abstraction: sx.SExpr = (
        sx.Symbol("PMultiLam"), 2, sx.Symbol("LNil"), bound_one,
    )
    substitution: sx.SExpr = (
        sx.Symbol("PSubst"), bound_zero, leaf_pattern,
    )
    collection: sx.SExpr = (
        sx.Symbol("PCollection"),
        sx.StringLiteral("Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec"),
        canonical_list(abstraction, multi_abstraction, substitution),
        sx.Symbol("RNone"),
    )
    all_constructors = dag.compile_shared_article(
        pattern("J", collection), proof("argument-ax", [collection], [])
    )
    if all_constructors.unique_pattern_nodes != 8:
        raise SystemExit("shared compiler omitted a Pattern constructor")
    if sx.parse_sexprs(
        sx.render(all_constructors.article), source="all Pattern constructors"
    ) != [all_constructors.article]:
        raise SystemExit("full shared Pattern carrier is not canonical")

    print(
        "(NikProofDAGV1Summary raw-nodes=3 unique-nodes=2 "
        "shared-occurrences=1 pattern-occurrences=7 pattern-nodes=3 "
        "constructors=7 ordered-negatives=2 malformed-negatives=3)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
