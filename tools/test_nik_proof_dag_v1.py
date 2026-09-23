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
    if dag.replay_node_budget(compiled.article) != 2:
        raise SystemExit("version-one replay allowance did not count proof nodes")
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
    if dag.replay_node_budget(shared_patterns.article) != 4:
        raise SystemExit("version-two replay allowance omitted Pattern nodes")
    changed_root = (*shared_patterns.article[:4], 10**12, shared_patterns.article[5])
    if dag.replay_node_budget(changed_root) != 4:
        raise SystemExit("untrusted identifiers controlled the replay allowance")
    if dag.replay_node_budget((sx.Symbol("GProofDAG"), 1, sx.Symbol("BadTail"), 0, goal)) is not None:
        raise SystemExit("malformed node spine acquired a replay allowance")
    if dag.replay_node_budget(leaf) is not None:
        raise SystemExit("raw proof was mistaken for a chronological DAG")
    large_nodes = sx.Symbol("LNil")
    for _ in range(1_000_001):
        large_nodes = (sx.Symbol("LCons"), sx.Symbol("Node"), large_nodes)
    if dag.replay_node_budget((sx.Symbol("GProofDAG"), 1, large_nodes, 0, goal)) != 1_000_001:
        raise SystemExit("replay allowance silently retained the default node ceiling")
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

    deep = pattern("Leaf")
    for _ in range(4096):
        deep = pattern("Next", deep)
    deep_compilation = dag.compile_shared_article(deep, proof("deep", [deep, deep], []))
    if (deep_compilation.unique_pattern_nodes,
        deep_compilation.raw_pattern_occurrences) != (4097, 3 * 4097):
        raise SystemExit("deep sharing lost a node or an expanded occurrence")
    deep_text = sx.render(deep_compilation.article)
    deep_parsed = sx.parse_sexprs(deep_text, source="deep shared DAG")
    if len(deep_parsed) != 1 or sx.render(deep_parsed[0]) != deep_text:
        raise SystemExit("deep shared article changed in the serialization round trip")
    if deep_compilation.article[-1] != 4096:
        raise SystemExit("deep shared article lost its actual target")
    deep_proof = proof("leaf", [leaf_pattern], [])
    for _ in range(4096):
        deep_proof = proof("step", [], [deep_proof])
    deep_proofs = dag.compile_shared_article(leaf_pattern, deep_proof)
    if (deep_proofs.raw_proof_nodes, deep_proofs.unique_proof_nodes,
        deep_proofs.article[-2]) != (4097, 4097, 4096):
        raise SystemExit("deep proof chronology or node accounting changed")
    shared_ordered = dag.compile_shared_article(leaf_pattern,
        proof("root", [], [proof("pair", [], [leaf, deep_proof]),
                           proof("pair", [], [deep_proof, leaf])]))
    if shared_ordered.unique_proof_nodes != 4101:
        raise SystemExit("deep traversal conflated reversed proof premises")
    repeated = proof("leaf", [leaf_pattern], [])
    for _ in range(30):
        repeated = proof("pair", [leaf_pattern], [repeated, repeated])
    repeated_dag = dag.compile_shared_article(leaf_pattern, repeated)
    if (repeated_dag.unique_proof_nodes, repeated_dag.raw_proof_nodes,
        repeated_dag.raw_pattern_occurrences) != (31, 2 ** 31 - 1, 2 ** 31):
        raise SystemExit("shared subproof traversal lost expanded occurrences")
    # Shared versus separately allocated input has identical canonical output.
    unshared = proof("pair", [leaf_pattern],
        [proof("leaf", [leaf_pattern], []), proof("leaf", [leaf_pattern], [])])
    one_leaf = proof("leaf", [leaf_pattern], [])
    shared = proof("pair", [leaf_pattern], [one_leaf, one_leaf])
    if dag.compile_shared_article(leaf_pattern, unshared) != dag.compile_shared_article(leaf_pattern, shared):
        raise SystemExit("physical sharing changed the encoded article or its counts")
    try:
        dag.compile_shared_article(pattern("Next", sx.Symbol("Malformed")), leaf)
    except dag.ProofDAGError:
        pass
    else:
        raise SystemExit("iterative compiler accepted a malformed nested Pattern")

    print(
        "(NikProofDAGV1Summary raw-nodes=3 unique-nodes=2 "
        "shared-occurrences=1 pattern-occurrences=7 pattern-nodes=3 "
        "constructors=7 ordered-negatives=3 malformed-negatives=4 deep=4096 proof-depth=4096 shared-proof-depth=30 replay-budget-controls=6)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
