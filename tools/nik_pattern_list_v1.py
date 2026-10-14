"""Canonical Pattern lists with shared immutable suffixes.

Sharing changes allocation, not the ordered list or its encoded constructors.
Rows are immutable source values; suffix identifiers are local to an encoder.
They never serve as proof identities or acceptance evidence.
"""

from collections.abc import Callable, Hashable, Iterable, Sequence
from typing import Generic, TypeVar

import gslt2parse_schema_v1 as sx
from nik_shared_evidence_v1 import app


Row = TypeVar("Row", bound=Hashable)


def application(head: str, arguments: Sequence[sx.SExpr] = ()) -> sx.SExpr:
    return app(head, *arguments)


class SharedPatternListEncoder(Generic[Row]):
    """Encode ordered rows, preserving duplicates and sharing equal suffixes."""

    def __init__(self, nil: str,
                 cell: Callable[[Row], tuple[str, Sequence[sx.SExpr]]]):
        self.empty = application(nil)
        self.cell = cell
        self.nodes: dict[tuple[Row, int], tuple[int, sx.SExpr]] = {}

    def encode(self, rows: Iterable[Row]) -> sx.SExpr:
        tail_id, tail = 0, self.empty
        for row in reversed(tuple(rows)):
            key = (row, tail_id)
            found = self.nodes.get(key)
            if found is None:
                head, fields = self.cell(row)
                found = (len(self.nodes) + 1, application(head, (*fields, tail)))
                self.nodes[key] = found
            tail_id, tail = found
        return tail
