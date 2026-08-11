#!/usr/bin/env python3
"""Behavioral gate for the finite indexed GSLT-IL presentation."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

import gslt2parse_schema_v1 as sx


class GateFailure(RuntimeError):
    pass


def run(command: list[str]) -> str:
    completed = subprocess.run(
        command, text=True, capture_output=True, check=False
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"executor failed ({' '.join(command[:2])}):\n"
            f"{completed.stdout}{completed.stderr}"
        )
    return completed.stdout.strip()


def qsym(name: str) -> str:
    return f"(q-sym (q-str {json.dumps(name)}))"


def qlist(*items: str) -> str:
    result = "q-nil"
    for item in reversed(items):
        result = f"(q-cons {item} {result})"
    return result


def qapp(head: str, *arguments: str) -> str:
    return f"(q-app {head} {qlist(*arguments)})"


def equation(source: str, target: str) -> str:
    return qapp(qsym("="), source, target)


def space_rule(space: str, source: str, target: str) -> str:
    return qapp(qsym("in"), space, equation(source, target))


def route_decl(route: str, source_space: str, target_space: str) -> str:
    return qapp(qsym("route"), route, source_space, target_space)


def route_call(route: str, state: str) -> str:
    return qapp(route, state)


def route_rule(route: str, source: str, target: str) -> str:
    return equation(route_call(route, source), target)


def in_space(space: str, state: str) -> str:
    return qapp(qsym("in"), space, state)


def request(command: str) -> str:
    return qapp(qsym("!"), command)


def program(*payloads: str) -> str:
    result = "gslt-il-program-nil"
    for index, payload in reversed(tuple(enumerate(payloads))):
        result = (
            f"(gslt-il-program-cons row-{index} {payload} {result})"
        )
    return result


def chart_answers(
    chart: Path, semantics: Path, query: str
) -> list[tuple[sx.SExpr, ...]]:
    raw = run(
        [
            str(chart),
            str(semantics),
            "--query-text",
            query,
            "--timeout",
            "10",
            "--max-rounds",
            "100000",
        ]
    )
    try:
        receipt = json.loads(raw)
    except json.JSONDecodeError as error:
        raise GateFailure(f"chart emitted malformed JSON: {raw}") from error
    terms = receipt.get("terms")
    if not isinstance(terms, list):
        raise GateFailure(f"chart receipt omits answer terms: {receipt}")
    answers: list[tuple[sx.SExpr, ...]] = []
    for rendered in terms:
        if not isinstance(rendered, str):
            raise GateFailure(f"chart answer is not text: {rendered!r}")
        forms = sx.parse_sexprs(rendered, source="chart answer")
        if len(forms) != 1 or not isinstance(forms[0], tuple):
            raise GateFailure(f"chart returned malformed answer: {rendered}")
        answers.append(forms[0])
    return answers


def relation_answers(
    chart: Path,
    semantics: Path,
    relation: str,
    arity: int,
    query: str,
) -> list[tuple[sx.SExpr, ...]]:
    answers = chart_answers(chart, semantics, query)
    for answer in answers:
        if (
            len(answer) != arity + 1
            or not isinstance(answer[0], sx.Symbol)
            or answer[0].text != relation
        ):
            raise GateFailure(
                f"chart returned malformed {relation} answer: "
                f"{sx.render(answer)}"
            )
    return answers


def projected(
    answers: list[tuple[sx.SExpr, ...]], *indices: int
) -> list[tuple[str, ...]]:
    return sorted(
        tuple(sx.render(answer[index]) for index in indices)
        for answer in answers
    )


def require_equal(actual: object, expected: object, label: str) -> None:
    if actual != expected:
        raise GateFailure(
            f"{label}: expected {expected!r}, found {actual!r}"
        )


def produce(
    chart: Path, semantics: Path, source: str, request: str
) -> list[tuple[sx.SExpr, ...]]:
    return relation_answers(
        chart,
        semantics,
        "gslt-il-produce",
        4,
        f"(gslt-il-produce {source} {request} ?evidence ?result)",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--semantics", type=Path, required=True)
    arguments = parser.parse_args()
    chart = arguments.chart.resolve()
    semantics = arguments.semantics.resolve()

    space_0 = qsym("&stage-0")
    space_1 = qsym("&stage-1")
    ready = qsym("ready")
    done = qsym("done")
    mapped_ready = qsym("mapped-ready")
    mapped_done = qsym("mapped-done")
    route = qsym("grow")
    rows = (
        space_rule(space_0, ready, done),
        space_rule(space_0, ready, done),
        space_rule(space_1, mapped_ready, mapped_done),
        route_decl(route, space_0, space_1),
        route_rule(route, ready, mapped_ready),
        route_rule(route, done, mapped_done),
    )
    source = program(*rows)

    located_ready = in_space(space_0, ready)
    surface = request(located_ready)
    classified = relation_answers(
        chart,
        semantics,
        "gslt-il-classify",
        3,
        f"(gslt-il-classify request-zero {surface} ?request)",
    )
    expected_request = (
        f"(gslt-il-step-request request-zero {located_ready})"
    )
    require_equal(
        projected(classified, 3),
        [(expected_request,)],
        "step request classification",
    )
    unknown_surface = qapp(qsym("unknown-request"), located_ready)
    require_equal(
        chart_answers(
            chart,
            semantics,
            f"(gslt-il-classify request-zero {unknown_surface} ?request)",
        ),
        [],
        "unknown request inertness",
    )

    atoms = relation_answers(
        chart,
        semantics,
        "gslt-il-program-atom",
        3,
        f"(gslt-il-program-atom {source} ?occurrence ?atom)",
    )
    require_equal(
        projected(atoms, 2, 3),
        sorted((f"row-{index}", row) for index, row in enumerate(rows)),
        "occurrence-preserving program traversal",
    )

    space_rows = relation_answers(
        chart,
        semantics,
        "gslt-il-space-rule",
        5,
        f"(gslt-il-space-rule {source} ?occ ?space ?from ?to)",
    )
    require_equal(
        projected(space_rows, 2, 3, 4, 5),
        sorted(
            [
                ("row-0", space_0, ready, done),
                ("row-1", space_0, ready, done),
                ("row-2", space_1, mapped_ready, mapped_done),
            ]
        ),
        "space rule view",
    )

    route_declarations = relation_answers(
        chart,
        semantics,
        "gslt-il-route-decl",
        5,
        f"(gslt-il-route-decl {source} ?occ ?route ?source ?target)",
    )
    require_equal(
        projected(route_declarations, 2, 3, 4, 5),
        [("row-3", route, space_0, space_1)],
        "route declaration view",
    )

    route_rules = relation_answers(
        chart,
        semantics,
        "gslt-il-route-rule",
        5,
        f"(gslt-il-route-rule {source} ?occ ?route ?from ?to)",
    )
    require_equal(
        projected(route_rules, 2, 3, 4, 5),
        sorted(
            [
                ("row-4", route, ready, mapped_ready),
                ("row-5", route, done, mapped_done),
            ]
        ),
        "route rule view",
    )

    route_rows = relation_answers(
        chart,
        semantics,
        "gslt-il-route-row",
        8,
        "(gslt-il-route-row "
        f"{source} ?decl-occ ?rule-occ ?route ?source-space ?target-space "
        "?from ?to)",
    )
    require_equal(
        projected(route_rows, 2, 3, 4, 5, 6, 7, 8),
        sorted(
            [
                (
                    "row-3", "row-4", route, space_0, space_1,
                    ready, mapped_ready,
                ),
                (
                    "row-3", "row-5", route, space_0, space_1,
                    done, mapped_done,
                ),
            ]
        ),
        "elaborated route row view",
    )

    located_request = (
        f"(gslt-il-step-request request-in {located_ready})"
    )
    located_results = produce(chart, semantics, source, located_request)
    require_equal(
        projected(located_results, 4),
        [(in_space(space_0, done),), (in_space(space_0, done),)],
        "duplicate space rules preserve occurrences",
    )

    route_ready = route_call(route, ready)
    route_request = (
        f"(gslt-il-step-request request-route {route_ready})"
    )
    route_results = produce(chart, semantics, source, route_request)
    require_equal(
        projected(route_results, 4),
        sorted(
            [
                (route_call(route, done),),
                (route_call(route, done),),
                (in_space(space_1, mapped_ready),),
            ]
        ),
        "space and route successors",
    )

    route_done = route_call(route, done)
    compute_then_route = produce(
        chart,
        semantics,
        source,
        f"(gslt-il-step-request request-local {route_done})",
    )
    route_then_compute = produce(
        chart,
        semantics,
        source,
        "(gslt-il-step-request request-target "
        f"{in_space(space_1, mapped_ready)})",
    )
    expected_join = [(in_space(space_1, mapped_done),)]
    require_equal(
        projected(compute_then_route, 4),
        expected_join,
        "compute then route",
    )
    require_equal(
        projected(route_then_compute, 4),
        expected_join,
        "route then compute",
    )

    missing = route_call(qsym("missing-route"), qsym("absent"))
    require_equal(
        produce(
            chart,
            semantics,
            source,
            f"(gslt-il-step-request request-missing {missing})",
        ),
        [],
        "unregistered command inertness",
    )

    first_result = in_space(space_0, done)
    second_result = in_space(space_1, mapped_ready)
    bag = (
        "(gslt-il-produced-cons "
        f"(gslt-produced evidence-first {first_result}) "
        "(gslt-il-produced-cons "
        f"(gslt-produced evidence-second {second_result}) "
        "gslt-il-produced-nil))"
    )
    observed = relation_answers(
        chart,
        semantics,
        "gslt-il-observe",
        4,
        f"(gslt-il-observe {located_request} {bag} ?evidence ?result)",
    )
    require_equal(
        projected(observed, 4),
        sorted([(first_result,), (second_result,)]),
        "bag observation",
    )

    print(
        "(GsltIlSemanticsV1Summary rules=13 classification=2 "
        "program-occurrences=6 space-rules=3 route-declarations=1 "
        "route-rules=2 route-rows=2 successors=7 square=1 "
        "inert=1 observations=2)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError, sx.SchemaError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
