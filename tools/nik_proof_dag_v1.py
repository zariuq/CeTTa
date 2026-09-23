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


def replay_node_budget(article: sx.SExpr) -> int | None:
    """Count encoded DAG nodes for an explicit replay allowance, not acceptance.

    Version 2 charges both Pattern nodes and proof nodes. Count the actual list
    spines, not submitted identifiers or the expanded proof tree. Unknown or
    malformed carriers return None and remain the checker's responsibility.
    """
    if _tagged(article, "GProofDAG", 5) and article[1] == 1:
        lists = (article[2],)
    elif _tagged(article, "GProofDAG", 6) and article[1] == 2:
        lists = (article[2], article[3])
    else:
        return None
    count = 0
    for cursor in lists:
        while cursor != sx.Symbol("LNil"):
            if not _tagged(cursor, "LCons", 3):
                return None
            count += 1
            cursor = cursor[2]
    # The public interface takes a positive allowance, even for empty input.
    return max(1, count)


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
    # Retain source objects so identity memoization cannot confuse reused ids.
    # Expanded occurrence counts include every reuse; physical traversal does not.
    pattern_cache: dict[int, tuple[sx.SExpr, int, int]] = {}
    proof_cache: dict[int, tuple[sx.SExpr, int, int, int]] = {}

    def intern_key(key: sx.SExpr) -> int:
        existing = known_patterns.get(key)
        if existing is not None:
            return existing
        node_id = len(pattern_nodes)
        pattern_nodes.append((sx.Symbol("GPatternNode"), node_id, key))
        known_patterns[key] = node_id
        return node_id

    def pattern_id(pattern: sx.SExpr) -> int:
        return pattern_cache[id(pattern)][1]

    def finish_pattern(pattern: sx.SExpr) -> int:
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
                pattern_id(child)
                for child in _list_values(pattern[2], "pattern arguments")
            ]
            return intern_key(
                (sx.Symbol("GPKApply"), head,
                 _canonical_list(children, nil="LNil", cons="LCons"))
            )
        if _tagged(pattern, "PLam", 3):
            assert isinstance(pattern, tuple)
            body = pattern_id(pattern[2])
            return intern_key(
                (sx.Symbol("GPKLambda"), _binder(pattern[1]), body)
            )
        if _tagged(pattern, "PMultiLam", 4):
            assert isinstance(pattern, tuple)
            arity = _nonnegative(pattern[1], "multi-lambda arity")
            binders = _list_values(pattern[2], "multi-lambda binders")
            for binder in binders:
                _string(binder, "multi-lambda binder")
            body = pattern_id(pattern[3])
            return intern_key(
                (sx.Symbol("GPKMultiLambda"), arity, pattern[2], body)
            )
        if _tagged(pattern, "PSubst", 3):
            assert isinstance(pattern, tuple)
            body = pattern_id(pattern[1])
            replacement = pattern_id(pattern[2])
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
                pattern_id(element)
                for element in _list_values(pattern[2], "collection elements")
            ]
            return intern_key(
                (sx.Symbol("GPKCollection"), pattern[1],
                 _canonical_list(elements, nil="LNil", cons="LCons"),
                 _rest(pattern[3]))
            )
        raise ProofDAGError("rule argument or target is not a canonical Pattern")

    def pattern_children(pattern: sx.SExpr) -> list[sx.SExpr]:
        if _tagged(pattern, "PApp", 3) or _tagged(pattern, "PCollection", 4):
            return _list_values(pattern[2], "pattern children")
        if _tagged(pattern, "PLam", 3):
            return [pattern[2]]
        if _tagged(pattern, "PMultiLam", 4):
            return [pattern[3]]
        if _tagged(pattern, "PSubst", 3):
            return [pattern[1], pattern[2]]
        return []

    def intern_pattern(pattern: sx.SExpr) -> int:
        nonlocal raw_pattern_occurrences
        pending = [(pattern, False)]
        while pending:
            current, finish = pending.pop()
            if id(current) in pattern_cache:
                continue
            children = pattern_children(current)
            if not finish:
                pending.append((current, True))
                pending.extend((child, False) for child in reversed(children))
            else:
                node_id = finish_pattern(current)
                occurrences = 1 + sum(pattern_cache[id(child)][2] for child in children)
                pattern_cache[id(current)] = (current, node_id, occurrences)
        _, node_id, occurrences = pattern_cache[id(pattern)]
        raw_pattern_occurrences += occurrences
        return node_id

    target_id = intern_pattern(goal)

    # Enter arguments before children, and emit each node after its ordered
    # premises. This preserves the recursive compiler's canonical chronology
    # without making proof depth a Python call-stack limit.
    pending = [(proof, None)]
    completed: list[int] = []
    while pending:
        current, finishing = pending.pop()
        if finishing is None:
            cached = proof_cache.get(id(current))
            if cached is not None:
                _, node_id, proof_occurrences, pattern_occurrences = cached
                raw_proof_nodes += proof_occurrences
                raw_pattern_occurrences += pattern_occurrences
                completed.append(node_id)
                continue
            proof_start, pattern_start = raw_proof_nodes, raw_pattern_occurrences
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
            children = _proof_children(current[2])
            pending.append((current, (rule_id, argument_ids, len(children),
                                      proof_start, pattern_start)))
            pending.extend((child, None) for child in reversed(children))
            continue

        rule_id, argument_ids, child_count, proof_start, pattern_start = finishing
        child_ids = tuple(completed[-child_count:]) if child_count else ()
        if child_count:
            del completed[-child_count:]
        key = (rule_id, argument_ids, child_ids)
        node_id = known_proofs.get(key)
        if node_id is None:
            node_id = len(proof_nodes)
            rule_references: sx.SExpr = (
                sx.Symbol("GRuleRefs"),
                rule_id,
                _canonical_list(list(argument_ids), nil="LNil", cons="LCons"),
            )
            child_references = _canonical_list(
                [(sx.Symbol("GRNode"), child_id) for child_id in child_ids],
                nil="LNil", cons="LCons",
            )
            proof_nodes.append(
                (sx.Symbol("GDNode"), node_id, rule_references, child_references)
            )
            known_proofs[key] = node_id
        # Preserve expanded occurrence counts without revisiting an identical
        # immutable input tree. Retaining the object prevents recycled-id hits.
        proof_cache[id(current)] = (current, node_id,
            raw_proof_nodes - proof_start, raw_pattern_occurrences - pattern_start)
        completed.append(node_id)

    root_id = completed[0]
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
