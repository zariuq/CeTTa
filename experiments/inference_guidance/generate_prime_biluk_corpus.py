#!/usr/bin/env python3
"""Generate Prime constructor types and authored Horn data for one set.mm goal."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import sys
from pathlib import Path
from typing import Any


VARIABLES = (
    ("𝜑", "ph"),
    ("𝜓", "ps"),
    ("𝜒", "ch"),
    ("𝜃", "th"),
    ("𝜏", "ta"),
    ("𝜂", "et"),
    ("𝜁", "ze"),
)
OPERATORS = {
    "→": "smm:imp",
    "↔": "smm:iff",
    "¬": "smm:not",
}


class FixtureError(RuntimeError):
    """The source corpus cannot produce the requested Prime fixture."""


def load_catalog_module(path: Path) -> Any:
    spec = importlib.util.spec_from_file_location("william_nil_bc", path)
    if spec is None or spec.loader is None:
        raise FixtureError(f"cannot load catalog helper: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def formula(node: Any, *, query_variables: bool) -> str:
    if isinstance(node, str):
        for source, target in VARIABLES:
            if node == source:
                return f"${target}" if query_variables else target
        return OPERATORS.get(node, node)
    if not isinstance(node, list) or not node:
        raise FixtureError(f"malformed formula node: {node!r}")
    if node[0] not in OPERATORS:
        raise FixtureError(f"unsupported formula operator: {node[0]!r}")
    return "(" + " ".join(
        [OPERATORS[node[0]]]
        + [formula(item, query_variables=query_variables) for item in node[1:]]
    ) + ")"


def variables_in(node: Any) -> list[str]:
    text = repr(node)
    return [target for source, target in VARIABLES if source in text]


def split_rule(node: Any) -> tuple[list[Any], Any]:
    if isinstance(node, list) and node and node[0] == "->":
        if len(node) < 3:
            raise FixtureError(f"malformed Horn formula: {node!r}")
        return list(node[1:-1]), node[-1]
    return [], node


def kernel_declaration(label: str, node: Any) -> str:
    premises, conclusion = split_rule(node)
    binders: list[str] = []
    variables = variables_in(node)
    if variables:
        binders.append(f"({' '.join(variables)} : smm:formula)")
    binders.extend(
        f"(p{index} : (smm:holds {formula(premise, query_variables=False)}))"
        for index, premise in enumerate(premises, start=1)
    )
    result = f"(smm:holds {formula(conclusion, query_variables=False)})"
    type_term = result if not binders else f"(-> {' '.join(binders)} {result})"
    return f"(: smm:{label} {type_term})"


def search_declaration(label: str, node: Any) -> str:
    premises, conclusion = split_rule(node)
    parameters = variables_in(node)
    parameter_data = (
        "(" + " ".join(f"${name}" for name in parameters) + ")"
        if parameters
        else "()"
    )
    result = (
        f"(app smm:holds {formula(conclusion, query_variables=True)})"
    )
    if premises:
        inputs = " ".join(
            f"(app smm:holds {formula(item, query_variables=True)})"
            for item in premises
        )
        declaration_type = f"(-> {inputs} {result})"
    else:
        declaration_type = result
    return (
        f"(: (proof:constructor smm:{label} {parameter_data})\n"
        f"   {declaration_type})"
    )


def render(
    *, catalog_module: Any, corpus: Path, goal: str
) -> str:
    formulas, indices = catalog_module.assertion_catalog(corpus)
    if goal not in indices:
        raise FixtureError(f"unknown goal: {goal}")
    goal_index = indices[goal]
    labels = [
        label
        for label, index in sorted(indices.items(), key=lambda item: item[1])
        if index < goal_index
    ]
    digest = hashlib.sha256(corpus.read_bytes()).hexdigest()
    lines = [
        "; Generated set.mm premise fixture for Prime authored chaining.",
        f"; Goal: {goal}",
        f"; Strict assertion prefix: {len(labels)} declarations",
        f"; Source SHA-256: {digest}",
        "",
    ]
    lines.extend(kernel_declaration(label, formulas[label]) for label in labels)
    lines.extend(["", f"(= ({goal}:candidate-declarations)", "   ("])
    for index, label in enumerate(labels):
        declaration = search_declaration(label, formulas[label])
        indent = "    " if index == 0 else "     "
        lines.extend(indent + line for line in declaration.splitlines())
    lines[-1] += "))"
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog-helper", type=Path, required=True)
    parser.add_argument("--assertion-corpus", type=Path, required=True)
    parser.add_argument("--goal", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    module = load_catalog_module(args.catalog_helper.resolve())
    output = render(
        catalog_module=module,
        corpus=args.assertion_corpus.resolve(),
        goal=args.goal,
    )
    args.output.write_text(output, encoding="utf-8")


if __name__ == "__main__":
    main()
