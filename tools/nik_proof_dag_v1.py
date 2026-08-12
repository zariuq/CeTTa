#!/usr/bin/env python3
"""Compile an expanded NIK proof tree to the exact chronological DAG carrier.

This compiler is not part of the trust boundary.  It preserves each rule
instance and the order of its premises, shares only structurally identical
closed subproofs, and emits an article that the admitted native checker
validates independently.
"""

from __future__ import annotations

from dataclasses import dataclass

import gslt2parse_schema_v1 as sx


class ProofDAGError(ValueError):
    """The input is not a canonical expanded GProof article."""


@dataclass(frozen=True, slots=True)
class DAGCompilation:
    article: sx.SExpr
    raw_nodes: int
    unique_nodes: int

    @property
    def shared_occurrences(self) -> int:
        return self.raw_nodes - self.unique_nodes


@dataclass(frozen=True, slots=True)
class SharedDAGCompilation:
    article: sx.SExpr
    raw_proof_nodes: int
    unique_proof_nodes: int
    raw_pattern_occurrences: int
    unique_pattern_nodes: int

    @property
    def shared_proof_occurrences(self) -> int:
        return self.raw_proof_nodes - self.unique_proof_nodes

    @property
    def shared_pattern_occurrences(self) -> int:
        return self.raw_pattern_occurrences - self.unique_pattern_nodes


def _tagged(term: sx.SExpr, tag: str, arity: int) -> bool:
    return (
        isinstance(term, tuple)
        and len(term) == arity
        and term[0] == sx.Symbol(tag)
    )


def _canonical_list(
    values: list[sx.SExpr], *, nil: str, cons: str,
) -> sx.SExpr:
    result: sx.SExpr = sx.Symbol(nil)
    for value in reversed(values):
        result = (sx.Symbol(cons), value, result)
    return result


def _proof_children(term: sx.SExpr) -> list[sx.SExpr]:
    children: list[sx.SExpr] = []
    cursor = term
    while cursor != sx.Symbol("PrNil"):
        if not _tagged(cursor, "PrCons", 3):
            raise ProofDAGError(
                "proof children are not a canonical PrNil/PrCons list"
            )
        assert isinstance(cursor, tuple)
        children.append(cursor[1])
        cursor = cursor[2]
    return children


def _list_values(term: sx.SExpr, context: str) -> list[sx.SExpr]:
    values: list[sx.SExpr] = []
    cursor = term
    while cursor != sx.Symbol("LNil"):
        if not _tagged(cursor, "LCons", 3):
            raise ProofDAGError(
                f"{context} is not a canonical LNil/LCons list"
            )
        assert isinstance(cursor, tuple)
        values.append(cursor[1])
        cursor = cursor[2]
    return values


def _nonnegative(value: sx.SExpr, context: str) -> int:
    if not isinstance(value, int) or value < 0:
        raise ProofDAGError(f"{context} is not a nonnegative integer")
    return value


def _string(value: sx.SExpr, context: str) -> sx.StringLiteral:
    if not isinstance(value, sx.StringLiteral):
        raise ProofDAGError(f"{context} is not a string")
    return value


def _binder(value: sx.SExpr) -> sx.SExpr:
    if value == sx.Symbol("BNone"):
        return value
    if _tagged(value, "BSome", 2):
        assert isinstance(value, tuple)
        _string(value[1], "pattern binder")
        return value
    raise ProofDAGError("pattern binder is not BNone or BSome")


def _rest(value: sx.SExpr) -> sx.SExpr:
    if value == sx.Symbol("RNone"):
        return value
    if _tagged(value, "RSome", 2):
        assert isinstance(value, tuple)
        _string(value[1], "collection rest")
        return value
    raise ProofDAGError("collection rest is not RNone or RSome")


def compile_article(goal: sx.SExpr, proof: sx.SExpr) -> DAGCompilation:
    """Hash-cons a closed expanded proof into a version-1 GProofDAG article."""

    nodes: list[sx.SExpr] = []
    interned: dict[tuple[sx.SExpr, tuple[int, ...]], int] = {}
    raw_nodes = 0

    def visit(current: sx.SExpr) -> int:
        nonlocal raw_nodes
        raw_nodes += 1
        if not _tagged(current, "GProof", 3):
            raise ProofDAGError(
                "proof node is not (GProof rule-instance children)"
            )
        assert isinstance(current, tuple)
        rule_instance = current[1]
        if not _tagged(rule_instance, "GRuleInst", 3):
            raise ProofDAGError(
                "proof node rule is not (GRuleInst rule-id arguments)"
            )
        child_ids = tuple(
            visit(child) for child in _proof_children(current[2])
        )
        key = (rule_instance, child_ids)
        try:
            existing = interned.get(key)
        except TypeError as error:
            raise ProofDAGError("proof article contains a noncanonical value") from error
        if existing is not None:
            return existing

        node_id = len(nodes)
        references = _canonical_list(
            [
                (sx.Symbol("GRNode"), child_id)
                for child_id in child_ids
            ],
            nil="LNil",
            cons="LCons",
        )
        nodes.append(
            (sx.Symbol("GDNode"), node_id, rule_instance, references)
        )
        interned[key] = node_id
        return node_id

    root_id = visit(proof)
    encoded_nodes = _canonical_list(nodes, nil="LNil", cons="LCons")
    article: sx.SExpr = (
        sx.Symbol("GProofDAG"),
        1,
        encoded_nodes,
        root_id,
        goal,
    )
    return DAGCompilation(article, raw_nodes, len(nodes))


def compile_shared_article(
    goal: sx.SExpr, proof: sx.SExpr,
) -> SharedDAGCompilation:
    """Share Pattern subterms and proof nodes in a version-2 GProofDAG."""

    pattern_nodes: list[sx.SExpr] = []
    known_patterns: dict[sx.SExpr, int] = {}
    proof_nodes: list[sx.SExpr] = []
    known_proofs: dict[tuple[sx.StringLiteral, tuple[int, ...], tuple[int, ...]], int] = {}
    raw_pattern_occurrences = 0
    raw_proof_nodes = 0

    def intern_key(key: sx.SExpr) -> int:
        existing = known_patterns.get(key)
        if existing is not None:
            return existing
        node_id = len(pattern_nodes)
        pattern_nodes.append((sx.Symbol("GPatternNode"), node_id, key))
        known_patterns[key] = node_id
        return node_id

    def intern_pattern(pattern: sx.SExpr) -> int:
        nonlocal raw_pattern_occurrences
        raw_pattern_occurrences += 1
        if _tagged(pattern, "Var", 2):
            assert isinstance(pattern, tuple)
            return intern_key(
                (sx.Symbol("GPKBVar"), _nonnegative(pattern[1], "variable index"))
            )
        if _tagged(pattern, "FVar", 2):
            assert isinstance(pattern, tuple)
            return intern_key(
                (sx.Symbol("GPKFVar"), _string(pattern[1], "free variable"))
            )
        if _tagged(pattern, "PApp", 3):
            assert isinstance(pattern, tuple)
            head = _string(pattern[1], "pattern application head")
            children = [
                intern_pattern(child)
                for child in _list_values(pattern[2], "pattern arguments")
            ]
            return intern_key(
                (sx.Symbol("GPKApply"), head,
                 _canonical_list(children, nil="LNil", cons="LCons"))
            )
        if _tagged(pattern, "PLam", 3):
            assert isinstance(pattern, tuple)
            body = intern_pattern(pattern[2])
            return intern_key(
                (sx.Symbol("GPKLambda"), _binder(pattern[1]), body)
            )
        if _tagged(pattern, "PMultiLam", 4):
            assert isinstance(pattern, tuple)
            arity = _nonnegative(pattern[1], "multi-lambda arity")
            binders = _list_values(pattern[2], "multi-lambda binders")
            for binder in binders:
                _string(binder, "multi-lambda binder")
            body = intern_pattern(pattern[3])
            return intern_key(
                (sx.Symbol("GPKMultiLambda"), arity, pattern[2], body)
            )
        if _tagged(pattern, "PSubst", 3):
            assert isinstance(pattern, tuple)
            body = intern_pattern(pattern[1])
            replacement = intern_pattern(pattern[2])
            return intern_key(
                (sx.Symbol("GPKSubst"), body, replacement)
            )
        if _tagged(pattern, "PCollection", 4):
            assert isinstance(pattern, tuple)
            if pattern[1] not in {
                sx.StringLiteral(
                    "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec"
                ),
                sx.StringLiteral(
                    "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashBag"
                ),
                sx.StringLiteral(
                    "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashSet"
                ),
            }:
                raise ProofDAGError("unknown collection type")
            elements = [
                intern_pattern(element)
                for element in _list_values(pattern[2], "collection elements")
            ]
            return intern_key(
                (sx.Symbol("GPKCollection"), pattern[1],
                 _canonical_list(elements, nil="LNil", cons="LCons"),
                 _rest(pattern[3]))
            )
        raise ProofDAGError("rule argument or target is not a canonical Pattern")

    target_id = intern_pattern(goal)

    def visit(current: sx.SExpr) -> int:
        nonlocal raw_proof_nodes
        raw_proof_nodes += 1
        if not _tagged(current, "GProof", 3):
            raise ProofDAGError(
                "proof node is not (GProof rule-instance children)"
            )
        assert isinstance(current, tuple)
        rule_instance = current[1]
        if not _tagged(rule_instance, "GRuleInst", 3):
            raise ProofDAGError(
                "proof node rule is not (GRuleInst rule-id arguments)"
            )
        assert isinstance(rule_instance, tuple)
        rule_id = _string(rule_instance[1], "rule identifier")
        argument_ids = tuple(
            intern_pattern(argument)
            for argument in _list_values(rule_instance[2], "rule arguments")
        )
        child_ids = tuple(
            visit(child) for child in _proof_children(current[2])
        )
        key = (rule_id, argument_ids, child_ids)
        existing = known_proofs.get(key)
        if existing is not None:
            return existing
        node_id = len(proof_nodes)
        rule_references: sx.SExpr = (
            sx.Symbol("GRuleRefs"),
            rule_id,
            _canonical_list(
                list(argument_ids), nil="LNil", cons="LCons"
            ),
        )
        child_references = _canonical_list(
            [(sx.Symbol("GRNode"), child_id) for child_id in child_ids],
            nil="LNil",
            cons="LCons",
        )
        proof_nodes.append(
            (sx.Symbol("GDNode"), node_id, rule_references, child_references)
        )
        known_proofs[key] = node_id
        return node_id

    root_id = visit(proof)
    article: sx.SExpr = (
        sx.Symbol("GProofDAG"),
        2,
        _canonical_list(pattern_nodes, nil="LNil", cons="LCons"),
        _canonical_list(proof_nodes, nil="LNil", cons="LCons"),
        root_id,
        target_id,
    )
    return SharedDAGCompilation(
        article=article,
        raw_proof_nodes=raw_proof_nodes,
        unique_proof_nodes=len(proof_nodes),
        raw_pattern_occurrences=raw_pattern_occurrences,
        unique_pattern_nodes=len(pattern_nodes),
    )
