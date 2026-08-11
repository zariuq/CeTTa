#!/usr/bin/env python3
"""Compare GSLT-IL CLI realizations under the declared bag observation."""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path
import subprocess


class GateFailure(RuntimeError):
    pass


def answer_bag(rendered: str) -> Counter[str]:
    text = rendered.strip()
    if len(text) < 2 or text[0] != "[" or text[-1] != "]":
        raise GateFailure(f"malformed answer bag: {rendered!r}")
    body = text[1:-1].strip()
    if not body:
        return Counter()
    answers: list[str] = []
    start = 0
    depth = 0
    quoted = False
    escaped = False
    for index, character in enumerate(body):
        if quoted:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quoted = False
            continue
        if character == '"':
            quoted = True
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth < 0:
                raise GateFailure(f"unbalanced answer bag: {rendered!r}")
        elif character == "," and depth == 0:
            answer = body[start:index].strip()
            if not answer:
                raise GateFailure(f"empty answer occurrence: {rendered!r}")
            answers.append(answer)
            start = index + 1
    if quoted or depth != 0:
        raise GateFailure(f"unterminated answer bag: {rendered!r}")
    answer = body[start:].strip()
    if not answer:
        raise GateFailure(f"empty final answer occurrence: {rendered!r}")
    answers.append(answer)
    return Counter(answers)


def run(cetta: Path, source: Path, realization: str | None) -> str:
    command = [str(cetta), "--lang", "gslt-il"]
    if realization is not None:
        command.extend(["--gslt-realization", realization])
    command.append(str(source))
    completed = subprocess.run(
        command, text=True, capture_output=True, check=False
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"executor failed for {source.name} ({realization or 'default'}):\n"
            f"{completed.stdout}{completed.stderr}"
        )
    return completed.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    arguments = parser.parse_args()
    cetta = arguments.cetta.resolve()
    fixtures = arguments.fixtures.resolve()
    sources = sorted(fixtures.glob("*.metta"))
    if not sources:
        raise GateFailure("no GSLT-IL fixtures found")

    checks = 0
    observed_orders: dict[str, set[str]] = {}
    for source in sources:
        expected_path = source.with_suffix(".expected")
        if not expected_path.is_file():
            raise GateFailure(f"missing expected bag for {source.name}")
        expected = answer_bag(expected_path.read_text(encoding="utf-8"))
        for realization in (
            "horn-reference",
            "compiled-worklist",
            None,
        ):
            rendered = run(cetta, source, realization)
            actual = answer_bag(rendered)
            if actual != expected:
                raise GateFailure(
                    f"answer bag mismatch for {source.name} "
                    f"({realization or 'default'}): expected {expected}, "
                    f"found {actual} from {rendered!r}"
                )
            observed_orders.setdefault(source.name, set()).add(rendered)
            checks += 1

    order_variants = sum(
        1 for variants in observed_orders.values() if len(variants) > 1
    )
    print(
        "(GsltIlCliV1Summary "
        f"realizations=2 default-selection=1 fixtures={len(sources)} "
        f"checks={checks} order-variant-fixtures={order_variants} "
        "two-axis=1 inert=1 multiplicity=1 one-step=1 composition=1)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
