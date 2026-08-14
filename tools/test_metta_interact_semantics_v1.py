#!/usr/bin/env python3
"""Behavioral gate for the authored proof-relevant interaction language."""

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


def fuel(amount: int) -> str:
    result = qsym("z")
    for _ in range(amount):
        result = qapp(qsym("s"), result)
    return result


def site(
    revision: str, name: str, pattern: str, template: str, cost: str
) -> str:
    return qapp(qsym("site"), revision, name, pattern, template, cost)


def source_program(*payloads: str) -> str:
    result = "interact-program-nil"
    for index, payload in reversed(tuple(enumerate(payloads))):
        result = (
            f"(interact-program-cons (gslt-source-occurrence {index}) "
            f"{payload} {result})"
        )
    return result


def chart_answers(
    chart: Path, sources: tuple[Path, ...], query: str
) -> list[tuple[sx.SExpr, ...]]:
    raw = run(
        [
            str(chart),
            *(str(source) for source in sources),
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
        forms = sx.parse_sexprs(rendered, source="chart answer")
        if len(forms) != 1 or not isinstance(forms[0], tuple):
            raise GateFailure(f"chart returned malformed answer: {rendered}")
        answers.append(forms[0])
    return answers


def relation_answers(
    chart: Path,
    sources: tuple[Path, ...],
    relation: str,
    arity: int,
    query: str,
) -> list[tuple[sx.SExpr, ...]]:
    answers = chart_answers(chart, sources, query)
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
    chart: Path,
    sources: tuple[Path, ...],
    program: str,
    request: str,
) -> list[tuple[sx.SExpr, ...]]:
    return relation_answers(
        chart,
        sources,
        "interact-produce",
        4,
        f"(interact-produce {program} {request} ?evidence ?result)",
    )


def produced_bag(answers: list[tuple[sx.SExpr, ...]]) -> str:
    result = "interact-produced-nil"
    for answer in reversed(answers):
        result = (
            "(interact-produced-cons "
            f"(gslt-produced {sx.render(answer[3])} "
            f"{sx.render(answer[4])}) {result})"
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--quote-match", type=Path, required=True)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--sequence", type=Path, required=True)
    arguments = parser.parse_args()
    chart = arguments.chart.resolve()
    sources = (
        arguments.quote_match.resolve(),
        arguments.core.resolve(),
        arguments.sequence.resolve(),
    )

    r1 = qsym("r1")
    r2 = qsym("r2")
    inspect = qsym("inspect")
    solve = qsym("solve")
    observe = qsym("observe")
    alpha = qsym("alpha")
    task_alpha = qapp(qsym("task"), alpha)
    checked_alpha = qapp(qsym("checked"), alpha)
    answer_alpha = qapp(qsym("answer"), alpha)
    variable = "(q-var q-zero)"
    task_var = qapp(qsym("task"), variable)
    checked_var = qapp(qsym("checked"), variable)
    answer_var = qapp(qsym("answer"), variable)
    state_var = qapp(qsym("state"), variable)
    seen_var = qapp(qsym("seen"), variable)
    state_warm = qapp(qsym("state"), qsym("warm"))
    seen_warm = qapp(qsym("seen"), qsym("warm"))
    program = source_program(
        qapp(qsym("learned"), site(r1, qsym("forged"), task_var,
                                   qsym("bad"), fuel(0))),
        site(r1, inspect, task_var, checked_var, fuel(1)),
        site(r1, solve, checked_var, answer_var, fuel(2)),
        site(r1, qsym("mirror"), task_var, checked_var, fuel(2)),
        site(r1, observe, state_var, seen_var, fuel(0)),
        site(r2, inspect, task_var, qsym("wrong-revision"), fuel(0)),
    )

    events_syntax = qapp(qsym("!"), qapp(qsym("events"), r1, task_alpha))
    fire_syntax = qapp(
        qsym("!"), qapp(qsym("fire"), r1, fuel(1), task_alpha, inspect)
    )
    plan = qapp(
        qsym("then"), inspect,
        qapp(qsym("then"), solve, qsym("end")),
    )
    run_syntax = qapp(
        qsym("!"), qapp(qsym("run"), r1, fuel(3), task_alpha, plan)
    )
    for syntax, constructor in (
        (events_syntax, "interact-events-request"),
        (fire_syntax, "interact-fire-request"),
        (run_syntax, "interact-run-request"),
    ):
        answers = relation_answers(
            chart,
            sources,
            "interact-classify",
            3,
            f"(interact-classify request-zero {syntax} ?request)",
        )
        if len(answers) != 1 or not sx.render(answers[0][3]).startswith(
            f"({constructor} "
        ):
            raise GateFailure(
                f"classification did not produce {constructor}: "
                f"{[sx.render(answer) for answer in answers]}"
            )
    require_equal(
        chart_answers(
            chart,
            sources,
            f"(interact-classify request-zero "
            f"{qapp(qsym('!'), qapp(qsym('unknown'), task_alpha))} ?request)",
        ),
        [],
        "unknown command remains unclassified",
    )

    events_request = f"(interact-events-request request-zero {r1} {task_alpha})"
    events = produce(chart, sources, program, events_request)
    expected_events = [
        qapp(qsym("event"), r1, inspect, "(q-int 1)", task_alpha,
             checked_alpha, fuel(1)),
        qapp(qsym("event"), r1, qsym("mirror"), "(q-int 3)", task_alpha,
             checked_alpha, fuel(2)),
    ]
    require_equal(
        projected(events, 4),
        sorted((event,) for event in expected_events),
        "occurrence-distinct event frontier",
    )

    observed = relation_answers(
        chart,
        sources,
        "interact-observe",
        4,
        f"(interact-observe {events_request} {produced_bag(events)} "
        "?evidence ?result)",
    )
    require_equal(
        projected(observed, 4),
        sorted((event,) for event in expected_events),
        "bag observer preserves every event occurrence",
    )

    fire_request = (
        f"(interact-fire-request request-zero {r1} {fuel(1)} "
        f"{task_alpha} {inspect})"
    )
    fired = produce(chart, sources, program, fire_request)
    require_equal(
        projected(fired, 4),
        [(
            qapp(qsym("continue"), checked_alpha, fuel(0),
                 expected_events[0]),
        )],
        "affordable event continuation",
    )
    dear_request = (
        f"(interact-fire-request request-zero {r1} {fuel(1)} "
        f"{task_alpha} {qsym('mirror')})"
    )
    require_equal(
        produce(chart, sources, program, dear_request),
        [],
        "insufficient budget declines",
    )

    zero_request = (
        f"(interact-fire-request request-zero {r1} {fuel(0)} "
        f"{state_warm} {observe})"
    )
    zero_fired = produce(chart, sources, program, zero_request)
    if len(zero_fired) != 1 or seen_warm not in sx.render(zero_fired[0][4]):
        raise GateFailure("zero-cost event did not preserve the budget")

    run_request = (
        f"(interact-run-request request-zero {r1} {fuel(3)} "
        f"{task_alpha} {plan})"
    )
    ran = produce(chart, sources, program, run_request)
    if len(ran) != 1:
        raise GateFailure("continued sequence did not produce one result")
    rendered_run = sx.render(ran[0][4])
    if answer_alpha not in rendered_run or fuel(0) not in rendered_run:
        raise GateFailure(f"continued sequence has wrong result: {rendered_run}")

    end_request = (
        f"(interact-run-request request-zero {r1} {fuel(2)} "
        f"{task_alpha} {qsym('end')})"
    )
    ended = produce(chart, sources, program, end_request)
    require_equal(
        projected(ended, 4),
        [(qapp(qsym("finished"), task_alpha, fuel(2), qsym("end")),)],
        "empty continuation is identity",
    )

    forged_request = (
        f"(interact-fire-request request-zero {r1} {fuel(0)} "
        f"{task_alpha} {qsym('forged')})"
    )
    wrong_revision = (
        f"(interact-fire-request request-zero {r2} {fuel(0)} "
        f"{task_alpha} {inspect})"
    )
    require_equal(
        produce(chart, sources, program, forged_request),
        [],
        "nested learned site cannot authorize",
    )
    wrong = produce(chart, sources, program, wrong_revision)
    if len(wrong) != 1 or "wrong-revision" not in sx.render(wrong[0][4]):
        raise GateFailure("revision citation did not isolate the site catalog")

    print(
        "(MettaInteractionSemanticsV1Summary "
        "classifiers=3 events=2 occurrence-distinct=2 "
        "cost-positive=2 cost-negative=1 sequence=2 "
        "authority-negative=1 revision-isolated=1 observer=2)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
