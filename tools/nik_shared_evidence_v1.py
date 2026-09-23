"""Bounded sharing of immutable NIK evidence constructors.

The result is the existing expanded S-expression, with the same ordered
arguments and premises. Sharing is an allocation policy, not an acceptance
cache: the ordinary proof-DAG compiler and independent checker still run.
Tuple identities are used only for live immutable children retained by the
cached parent. Eviction removes the key and cannot change an existing result.
"""

from collections import OrderedDict
from collections.abc import Sequence

import gslt2parse_schema_v1 as sx


class EvidenceBuilder:
    """Construct canonical evidence with a bounded, non-authoritative cache."""

    def __init__(self, capacity: int = 65536):
        if capacity < 0:
            raise ValueError("evidence cache capacity must be nonnegative")
        self.capacity = capacity
        self.nodes: OrderedDict[tuple, tuple[sx.SExpr, ...]] = OrderedDict()
        self.hits = self.misses = 0
        self.list_nil, self.proof_nil = sx.Symbol("LNil"), sx.Symbol("PrNil")

    @staticmethod
    def _field_key(value: sx.SExpr):
        if isinstance(value, tuple):
            return (tuple, id(value))
        if type(value) not in {sx.Symbol, sx.Variable, sx.StringLiteral, int}:
            raise TypeError("evidence fields must be immutable S-expressions")
        return (type(value), value)

    def node(self, tag: str, *fields: sx.SExpr) -> tuple[sx.SExpr, ...]:
        key = (tag, tuple(self._field_key(field) for field in fields))
        found = self.nodes.get(key)
        if found is not None:
            self.hits += 1
            self.nodes.move_to_end(key)
            return found
        self.misses += 1
        result = (sx.Symbol(tag), *fields)
        if self.capacity:
            self.nodes[key] = result
            if len(self.nodes) > self.capacity:
                self.nodes.popitem(last=False)
        return result

    def list_term(self, *values: sx.SExpr) -> sx.SExpr:
        result = self.list_nil
        for value in reversed(values):
            result = self.node("LCons", value, result)
        return result

    def proof_list(self, *values: sx.SExpr) -> sx.SExpr:
        result = self.proof_nil
        for value in reversed(values):
            result = self.node("PrCons", value, result)
        return result

    def app(self, head: str, *arguments: sx.SExpr) -> sx.SExpr:
        return self.node("PApp", sx.StringLiteral(head), self.list_term(*arguments))

    def rule(self, name: str, *arguments: sx.SExpr) -> sx.SExpr:
        return self.node("GRuleInst", sx.StringLiteral(name), self.list_term(*arguments))

    def proof_node(self, name: str, arguments: Sequence[sx.SExpr],
                   children: Sequence[sx.SExpr]) -> sx.SExpr:
        return self.node("GProof", self.rule(name, *arguments), self.proof_list(*children))

    def nat(self, value: int) -> sx.SExpr:
        if value < 0:
            raise ValueError("cannot encode a negative natural number")
        result = self.app("MNZero")
        for _ in range(value):
            result = self.app("MNSucc", result)
        return result


builder = EvidenceBuilder()
app = builder.app
list_term = builder.list_term
proof_list = builder.proof_list
rule = builder.rule
proof_node = builder.proof_node
nat = builder.nat
