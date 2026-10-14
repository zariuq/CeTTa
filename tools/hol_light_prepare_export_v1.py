"""Group paired type-definition results for registered HOL proof replay.

This is a data adapter. It neither proves exported claims nor admits axioms.
The receiver checks the actual source-rule requests and both defining laws.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Iterable, Iterator


def group_type_definitions(nodes: Iterable[dict]) -> Iterator[dict]:
    source = iter(nodes)
    for node in source:
        if node["rule"] != "TYPE_DEFINITION":
            yield node
            continue
        mate = next(source, None)
        if mate is None or mate["rule"] != "TYPE_DEFINITION":
            raise ValueError("type definition is missing its paired source result")
        left, right = node["arguments"], mate["arguments"]
        if len(left) != 4 or len(right) != 4 or left[0] != right[0]:
            raise ValueError("type-definition results cite different existence proofs")
        abs_type, rep_type = left[3], right[3]
        if (abs_type[:2] != ["op", "fun"] or rep_type[:2] != ["op", "fun"]
                or len(abs_type[2]) != 2 or len(rep_type[2]) != 2
                or abs_type[2] != rep_type[2][::-1]):
            raise ValueError("type-definition result types are not complementary")
        abstract_type = abs_type[2][1]
        if len(abstract_type) != 3 or abstract_type[0] != "op":
            raise ValueError("type-definition result has no new type operator")
        yield {
            "id": node["id"], "mate": mate["id"], "rule": "TYPE_DEFINITION_PAIR",
            "arguments": [left[0], abstract_type[1], left[2], right[2]],
            "theorems": [node["theorem"], mate["theorem"]],
        }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nodes", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.nodes.open() as source, args.output.open("w") as target:
        count = 0
        for event in group_type_definitions(json.loads(line) for line in source):
            target.write(json.dumps(event, separators=(",", ":")) + "\n")
            count += 1
    print(json.dumps({"prepared_events": count, "proofs_checked": False}))


if __name__ == "__main__":
    main()
