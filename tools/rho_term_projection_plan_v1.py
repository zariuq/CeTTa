#!/usr/bin/env python3
"""Compile rho runtime/projection facts into generic term-projection plans."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

import gslt2parse_schema_v1 as schema


ROOT = Path(__file__).resolve().parents[1]
RUNTIME_ABI = (
    ROOT
    / "experiments"
    / "gslt2parse_foundation"
    / "presentations"
    / "shared"
    / "rho_runtime_term_abi_v1.metta"
)
ABSTRACT_SYNTAX = (
    ROOT
    / "experiments"
    / "gslt2parse_foundation"
    / "presentations"
    / "shared"
    / "rho_abstract_syntax_v1.metta"
)
SURFACES = ("rho", "mrho")


class PlanError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class RuntimeShape:
    constructor: str
    kind: str
    head: str
    child_count: int | str
    binder_index: int | str


@dataclass(frozen=True, slots=True)
class AbstractArgument:
    kind: str
    sort: str
    binder_sort: str | None = None


@dataclass(frozen=True, slots=True)
class AbstractShape:
    constructor: str
    result_sort: str
    arguments: tuple[AbstractArgument, ...]


def symbol(term: schema.SExpr, context: str) -> str:
    if not isinstance(term, schema.Symbol):
        raise PlanError(f"{context}: expected symbol, found {schema.render(term)}")
    return term.text


def form(term: schema.SExpr, context: str) -> tuple[schema.SExpr, ...]:
    if not isinstance(term, tuple):
        raise PlanError(f"{context}: expected form, found {schema.render(term)}")
    return term


def facts(
    presentation: schema.Presentation, relation: str, arity: int
) -> list[tuple[schema.SExpr, ...]]:
    found: list[tuple[schema.SExpr, ...]] = []
    for rule in presentation.rules:
        head = form(rule.head, f"rule {rule.name}")
        if head and isinstance(head[0], schema.Symbol) and head[0].text == relation:
            if len(head) != arity + 1:
                raise PlanError(f"{relation}: wrong fact arity in {rule.name}")
            if rule.body:
                raise PlanError(f"{relation}: non-fact rule {rule.name}")
            found.append(head[1:])
    return found


def decode_list(term: schema.SExpr, context: str) -> list[schema.SExpr]:
    values: list[schema.SExpr] = []
    cursor = term
    while not (
        isinstance(cursor, schema.Symbol)
        and cursor.text == "rho-projection-nil"
    ):
        items = form(cursor, context)
        if (
            len(items) != 3
            or not isinstance(items[0], schema.Symbol)
            or items[0].text != "rho-projection-cons"
        ):
            raise PlanError(f"{context}: malformed projection list")
        values.append(items[1])
        cursor = items[2]
    return values


def encode_list(values: list[schema.SExpr]) -> schema.SExpr:
    result: schema.SExpr = schema.Symbol("ptp-nil")
    for value in reversed(values):
        result = (schema.Symbol("ptp-cons"), value, result)
    return result


def load_runtime_shapes(
    runtime: schema.Presentation,
) -> dict[str, RuntimeShape]:
    shapes: dict[str, RuntimeShape] = {}
    for row in facts(runtime, "rho-runtime-shape", 5):
        constructor = symbol(row[0], "runtime constructor")
        if constructor in shapes:
            raise PlanError(f"duplicate runtime mapping for {constructor}")
        child_count: int | str = (
            row[3] if isinstance(row[3], int) else symbol(row[3], "child count")
        )
        binder_index: int | str = (
            row[4] if isinstance(row[4], int) else symbol(row[4], "binder index")
        )
        shapes[constructor] = RuntimeShape(
            constructor=constructor,
            kind=symbol(row[1], "runtime kind"),
            head=symbol(row[2], "runtime head"),
            child_count=child_count,
            binder_index=binder_index,
        )
    return shapes


def decode_abstract_argument(
    term: schema.SExpr, context: str
) -> AbstractArgument:
    items = form(term, context)
    if len(items) != 2:
        raise PlanError(f"{context}: malformed abstract argument")
    tag = symbol(items[0], context)
    if tag == "rho-simple":
        value = items[1]
        if isinstance(value, schema.Symbol):
            return AbstractArgument("simple", value.text)
        collection = form(value, context)
        if (
            len(collection) != 3
            or symbol(collection[0], context) != "rho-collection"
            or symbol(collection[1], context) != "hash-bag"
        ):
            raise PlanError(f"{context}: unsupported simple argument")
        return AbstractArgument("collection", symbol(collection[2], context))
    if tag == "rho-abstraction":
        arrow = form(items[1], context)
        if len(arrow) != 3 or symbol(arrow[0], context) != "rho-arrow":
            raise PlanError(f"{context}: malformed abstraction")
        return AbstractArgument(
            "abstraction",
            symbol(arrow[2], context),
            symbol(arrow[1], context),
        )
    raise PlanError(f"{context}: unknown abstract argument kind {tag}")


def decode_abstract_arguments(
    term: schema.SExpr, context: str
) -> tuple[AbstractArgument, ...]:
    arguments: list[AbstractArgument] = []
    cursor = term
    while not (
        isinstance(cursor, schema.Symbol) and cursor.text == "rho-no-args"
    ):
        items = form(cursor, context)
        if len(items) != 3 or symbol(items[0], context) != "rho-args":
            raise PlanError(f"{context}: malformed abstract argument spine")
        arguments.append(decode_abstract_argument(items[1], context))
        cursor = items[2]
    return tuple(arguments)


def load_abstract_shapes(
    abstract: schema.Presentation,
) -> dict[str, AbstractShape]:
    shapes: dict[str, AbstractShape] = {}
    for row in facts(abstract, "rho-constructor", 3):
        constructor = symbol(row[0], "abstract constructor")
        if constructor in shapes:
            raise PlanError(f"duplicate abstract constructor {constructor}")
        shapes[constructor] = AbstractShape(
            constructor,
            symbol(row[1], f"{constructor} result sort"),
            decode_abstract_arguments(row[2], f"{constructor} arguments"),
        )
    if not shapes:
        raise PlanError("abstract signature has no constructors")
    return shapes


def projection_sorts(
    shapes: dict[str, AbstractShape], runtime: dict[str, RuntimeShape]
) -> tuple[str, str]:
    binder_sorts: set[str] = set()
    for constructor, runtime_shape in runtime.items():
        if runtime_shape.kind != "binder":
            continue
        abstract_shape = shapes.get(constructor)
        if abstract_shape is None:
            raise PlanError(f"missing abstract binder {constructor}")
        abstractions = [
            argument
            for argument in abstract_shape.arguments
            if argument.kind == "abstraction"
        ]
        if len(abstractions) != 1 or abstractions[0].binder_sort is None:
            raise PlanError(f"{constructor}: expected one abstract binder")
        binder_sorts.add(abstractions[0].binder_sort)
    if len(binder_sorts) != 1:
        raise PlanError("projection requires exactly one variable sort")
    variable_sort = next(iter(binder_sorts))
    document_sorts = {
        shape.result_sort for shape in shapes.values()
    } - {variable_sort}
    if len(document_sorts) != 1:
        raise PlanError("projection requires exactly one document sort")
    return next(iter(document_sorts)), variable_sort


def compile_plan(
    surface: str,
    runtime_path: Path = RUNTIME_ABI,
    abstract_path: Path = ABSTRACT_SYNTAX,
) -> schema.SExpr:
    if surface not in SURFACES:
        raise PlanError(f"unknown rho projection surface: {surface}")
    runtime = schema.parse_presentation(runtime_path)
    abstract = schema.parse_presentation(abstract_path)
    shapes = load_runtime_shapes(runtime)
    abstract_shapes = load_abstract_shapes(abstract)
    if set(shapes) != set(abstract_shapes):
        raise PlanError(
            "runtime/abstract constructor sets differ: "
            f"{sorted(shapes)} != {sorted(abstract_shapes)}"
        )
    document_sort, variable_sort = projection_sorts(abstract_shapes, shapes)
    surface_rows = [
        row
        for row in facts(runtime, "rho-runtime-projection-surface", 10)
        if symbol(row[0], "projection surface") == surface
    ]
    if len(surface_rows) != 1:
        raise PlanError(f"{surface}: expected exactly one projection-surface fact")
    row = surface_rows[0]
    wrappers = decode_list(row[7], f"{surface} text wrappers")
    variables = decode_list(row[8], f"{surface} variable labels")
    if not wrappers or not variables:
        raise PlanError(f"{surface}: empty text-wrapper or variable-label set")
    if len({schema.render(value) for value in wrappers}) != len(wrappers):
        raise PlanError(f"{surface}: duplicate text wrapper")
    if len({schema.render(value) for value in variables}) != len(variables):
        raise PlanError(f"{surface}: duplicate variable label")
    wrapper_names = {symbol(value, f"{surface} text wrapper") for value in wrappers}
    variable_names = {
        symbol(value, f"{surface} variable label") for value in variables
    }
    binder_wrapper = symbol(row[9], f"{surface} binder wrapper")
    if not variable_names <= wrapper_names:
        raise PlanError(f"{surface}: variable label is not a text wrapper")
    if binder_wrapper not in wrapper_names:
        raise PlanError(f"{surface}: binder label is not a text wrapper")
    if binder_wrapper in variable_names:
        raise PlanError(f"{surface}: binder label overlaps a variable label")

    source_rules: dict[str, schema.SExpr] = {}
    for raw_surface, raw_constructor, raw_shape in facts(
        runtime, "rho-runtime-projection-rule", 3
    ):
        if symbol(raw_surface, "projection rule surface") != surface:
            continue
        constructor = symbol(raw_constructor, "projection constructor")
        if constructor in source_rules:
            raise PlanError(f"{surface}: duplicate projection rule {constructor}")
        source_rules[constructor] = raw_shape
    if set(source_rules) != set(shapes):
        raise PlanError(
            f"{surface}: projection/runtime constructor sets differ: "
            f"{sorted(source_rules)} != {sorted(shapes)}"
        )

    compiled_rules: list[schema.SExpr] = []
    for constructor in sorted(source_rules):
        source_shape = source_rules[constructor]
        target = shapes[constructor]
        abstract_shape = abstract_shapes[constructor]
        label = schema.Symbol(constructor)
        head = schema.Symbol(target.head)
        result_sort = schema.Symbol(abstract_shape.result_sort)
        if isinstance(source_shape, schema.Symbol):
            if source_shape.text == "rho-projection-symbol":
                if (
                    target.kind != "symbol"
                    or target.child_count != 0
                    or abstract_shape.arguments
                ):
                    raise PlanError(f"{surface}/{constructor}: symbol shape mismatch")
                compiled = (
                    schema.Symbol("ptp-symbol-rule"),
                    label,
                    head,
                    result_sort,
                )
            elif source_shape.text == "rho-projection-monoid":
                if (
                    target.kind != "variadic"
                    or len(abstract_shape.arguments) != 1
                    or abstract_shape.arguments[0].kind != "collection"
                ):
                    raise PlanError(f"{surface}/{constructor}: monoid shape mismatch")
                policies = [
                    policy
                    for policy in facts(runtime, "rho-runtime-collection-policy", 4)
                    if symbol(policy[0], "collection constructor") == constructor
                ]
                if len(policies) != 1:
                    raise PlanError(
                        f"{surface}/{constructor}: missing collection policy"
                    )
                policy = policies[0]
                if (
                    symbol(policy[1], "collection kind") != "hash-bag"
                    or symbol(policy[2], "collection head") != target.head
                ):
                    raise PlanError(
                        f"{surface}/{constructor}: collection target mismatch"
                    )
                compiled = (
                    schema.Symbol("ptp-monoid-rule"),
                    label,
                    head,
                    result_sort,
                    policy[3],
                    schema.Symbol(abstract_shape.arguments[0].sort),
                )
            else:
                raise PlanError(
                    f"{surface}/{constructor}: unknown projection rule shape"
                )
        else:
            items = form(source_shape, f"{surface}/{constructor} source shape")
            tag = symbol(items[0], f"{surface}/{constructor} source tag")
            if tag == "rho-projection-fixed" and len(items) == 2:
                if (
                    target.kind != "fixed"
                    or any(
                        argument.kind != "simple"
                        for argument in abstract_shape.arguments
                    )
                ):
                    raise PlanError(f"{surface}/{constructor}: fixed shape mismatch")
                indexes = decode_list(items[1], f"{surface}/{constructor} indexes")
                if any(not isinstance(index, int) for index in indexes):
                    raise PlanError(f"{surface}/{constructor}: noninteger index")
                if target.child_count != len(indexes):
                    raise PlanError(f"{surface}/{constructor}: fixed arity mismatch")
                if sorted(indexes) != list(range(len(indexes))):
                    raise PlanError(
                        f"{surface}/{constructor}: indexes are not a permutation"
                    )
                compiled = (
                    schema.Symbol("ptp-fixed-rule"),
                    label,
                    head,
                    result_sort,
                    encode_list(indexes),
                    encode_list(
                        [
                            schema.Symbol(argument.sort)
                            for argument in abstract_shape.arguments
                        ]
                    ),
                )
            elif tag == "rho-projection-quote" and len(items) == 2:
                if (
                    target.kind != "fixed"
                    or target.child_count != 1
                    or len(abstract_shape.arguments) != 1
                    or abstract_shape.arguments[0].kind != "simple"
                ):
                    raise PlanError(f"{surface}/{constructor}: quote shape mismatch")
                if not isinstance(items[1], int) or items[1] != 0:
                    raise PlanError(f"{surface}/{constructor}: quote index changed")
                compiled = (
                    schema.Symbol("ptp-quote-rule"),
                    label,
                    head,
                    result_sort,
                    items[1],
                    schema.Symbol(abstract_shape.arguments[0].sort),
                )
            elif tag == "rho-projection-binder" and len(items) == 4:
                simple_arguments = [
                    argument
                    for argument in abstract_shape.arguments
                    if argument.kind == "simple"
                ]
                abstractions = [
                    argument
                    for argument in abstract_shape.arguments
                    if argument.kind == "abstraction"
                ]
                if (
                    target.kind != "binder"
                    or target.child_count != 3
                    or target.binder_index != 1
                    or len(simple_arguments) != 1
                    or len(abstractions) != 1
                    or abstractions[0].binder_sort is None
                ):
                    raise PlanError(f"{surface}/{constructor}: binder shape mismatch")
                indexes = list(items[1:])
                if any(not isinstance(index, int) for index in indexes):
                    raise PlanError(f"{surface}/{constructor}: noninteger binder index")
                if sorted(indexes) != [0, 1, 2]:
                    raise PlanError(
                        f"{surface}/{constructor}: binder indexes are not bijective"
                    )
                compiled = (
                    schema.Symbol("ptp-binder-rule"),
                    label,
                    head,
                    result_sort,
                    *indexes,
                    schema.Symbol(simple_arguments[0].sort),
                    schema.Symbol(abstractions[0].binder_sort),
                    schema.Symbol(abstractions[0].sort),
                )
            else:
                raise PlanError(
                    f"{surface}/{constructor}: malformed projection rule shape"
                )
        compiled_rules.append(compiled)

    return (
        schema.Symbol("ptp-plan-v1"),
        row[1],
        row[2],
        schema.Symbol(document_sort),
        schema.Symbol(variable_sort),
        *row[3:7],
        encode_list(wrappers),
        encode_list(variables),
        row[9],
        encode_list(compiled_rules),
    )


def compile_plans(
    runtime_path: Path = RUNTIME_ABI,
    abstract_path: Path = ABSTRACT_SYNTAX,
) -> dict[str, schema.SExpr]:
    return {
        surface: compile_plan(surface, runtime_path, abstract_path)
        for surface in SURFACES
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--surface", choices=SURFACES, required=True)
    parser.add_argument("--runtime-abi", type=Path, default=RUNTIME_ABI)
    parser.add_argument("--abstract-syntax", type=Path, default=ABSTRACT_SYNTAX)
    arguments = parser.parse_args()
    print(
        schema.render(
            compile_plan(
                arguments.surface,
                arguments.runtime_abi,
                arguments.abstract_syntax,
            )
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
