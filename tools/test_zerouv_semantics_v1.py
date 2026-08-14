#!/usr/bin/env python3
"""Behavioral gate for the authored ZeroUV control presentation."""

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


def equality(left: str, right: str) -> str:
    return qapp(qsym("="), left, right)


def source_program(*payloads: str) -> str:
    result = "zero-program-nil"
    for index, payload in reversed(tuple(enumerate(payloads))):
        result = (
            f"(zero-program-cons (source-occurrence {index}) "
            f"{payload} {result})"
        )
    return result


def path(*states: str) -> str:
    result = qsym("end")
    for state in reversed(states):
        result = qapp(qsym("path"), state, result)
    return result


def fuel(steps: int) -> str:
    result = qsym("z")
    for _ in range(steps):
        result = qapp(qsym("s"), result)
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
        if not isinstance(rendered, str):
            raise GateFailure(f"chart answer is not text: {rendered!r}")
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
        "zerouv-produce",
        4,
        f"(zerouv-produce {program} {request} ?evidence ?result)",
    )


def produced_bag(answers: list[tuple[sx.SExpr, ...]]) -> str:
    result = "zerouv-produced-nil"
    for answer in reversed(answers):
        result = (
            "(zerouv-produced-cons "
            f"(gslt-produced {sx.render(answer[3])} "
            f"{sx.render(answer[4])}) {result})"
        )
    return result


def observe(
    chart: Path,
    sources: tuple[Path, ...],
    request: str,
    produced: list[tuple[sx.SExpr, ...]],
) -> list[tuple[sx.SExpr, ...]]:
    bag = produced_bag(produced)
    return relation_answers(
        chart,
        sources,
        "zerouv-observe",
        4,
        f"(zerouv-observe {request} {bag} ?evidence ?result)",
    )


def classified(
    chart: Path,
    sources: tuple[Path, ...],
    syntax: str,
) -> list[tuple[sx.SExpr, ...]]:
    return relation_answers(
        chart,
        sources,
        "zerouv-classify",
        3,
        f"(zerouv-classify request-zero {syntax} ?request)",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--quote-match", type=Path, required=True)
    parser.add_argument("--query-kernel", type=Path, required=True)
    parser.add_argument("--control", type=Path, required=True)
    arguments = parser.parse_args()
    chart = arguments.chart.resolve()
    sources = (
        arguments.quote_match.resolve(),
        arguments.query_kernel.resolve(),
        arguments.control.resolve(),
    )

    a = qsym("a")
    b = qsym("b")
    c = qsym("c")
    d = qsym("d")
    e = qsym("e")
    loop = qsym("loop")
    off = qsym("off")
    on = qsym("on")
    cold = qsym("cold")
    warm = qsym("warm")
    walk = qsym("walk")
    bad = qsym("bad")
    toggle = qsym("toggle")
    program = source_program(
        equality(a, b),
        equality(b, c),
        equality(loop, loop),
        equality(d, e),
        equality(d, e),
        equality(
            qapp(walk, cold, a),
            qapp(qsym("choose"), warm, b),
        ),
        equality(
            qapp(walk, warm, b),
            qapp(qsym("choose"), cold, c),
        ),
        equality(
            qapp(bad, cold, a),
            qapp(qsym("choose"), cold, c),
        ),
        equality(off, on),
        equality(on, off),
        equality(qapp(toggle, off), on),
        equality(qapp(toggle, on), off),
        qapp(qsym("accept"), toggle, on),
    )

    step_syntax = qapp(qsym("!"), qapp(qsym("step"), a))
    reach_syntax = qapp(qsym("!"), qapp(qsym("reach"), fuel(2), a))
    seek_syntax = qapp(
        qsym("!"), qapp(qsym("seek"), fuel(2), c, a)
    )
    follow_syntax = qapp(
        qsym("!"), qapp(qsym("follow"), walk, cold, fuel(2), a)
    )
    cycle = path(off, on, off)
    recur_syntax = qapp(
        qsym("!"), qapp(qsym("recur"), toggle, cycle, on)
    )
    for syntax, constructor in (
        (step_syntax, "zerouv-step-request"),
        (reach_syntax, "zerouv-reach-request"),
        (seek_syntax, "zerouv-seek-request"),
        (follow_syntax, "zerouv-follow-request"),
        (recur_syntax, "zerouv-cycle-request"),
    ):
        answers = classified(chart, sources, syntax)
        if len(answers) != 1 or sx.render(answers[0][3]).split(" ", 1)[0] != (
            f"({constructor}"
        ):
            raise GateFailure(
                f"classification did not produce {constructor}: "
                f"{[sx.render(answer) for answer in answers]}"
            )
    unknown = qapp(qsym("!"), qapp(qsym("unknown"), a))
    require_equal(
        chart_answers(
            chart,
            sources,
            f"(zerouv-classify request-zero {unknown} ?request)",
        ),
        [],
        "unknown command remains outside the request relation",
    )

    step_a = f"(zerouv-step-request request-zero {a})"
    produced = produce(chart, sources, program, step_a)
    require_equal(projected(produced, 4), [(b,)], "productive step")
    require_equal(
        projected(observe(chart, sources, step_a, produced), 4),
        [(qapp(qsym("next"), b),)],
        "productive step observation",
    )

    step_c = f"(zerouv-step-request request-zero {c})"
    produced = produce(chart, sources, program, step_c)
    require_equal(produced, [], "quiescent kernel production")
    require_equal(
        projected(observe(chart, sources, step_c, produced), 4),
        [(qapp(qsym("done"), c),)],
        "exact quiescence observation",
    )

    step_loop = f"(zerouv-step-request request-zero {loop})"
    produced = produce(chart, sources, program, step_loop)
    require_equal(projected(produced, 4), [(loop,)], "self-loop step")
    require_equal(
        projected(observe(chart, sources, step_loop, produced), 4),
        [(qapp(qsym("next"), loop),)],
        "self-loop is not completion",
    )

    step_d = f"(zerouv-step-request request-zero {d})"
    produced = produce(chart, sources, program, step_d)
    require_equal(
        projected(produced, 4), [(e,), (e,)],
        "duplicate productive occurrences",
    )
    require_equal(
        projected(observe(chart, sources, step_d, produced), 4),
        [(qapp(qsym("next"), e),), (qapp(qsym("next"), e),)],
        "duplicate observation occurrences",
    )

    reach = (
        f"(zerouv-reach-request request-zero {fuel(2)} {a} {a})"
    )
    produced = produce(chart, sources, program, reach)
    reach_result = qapp(qsym("reachable"), c, path(a, b, c))
    require_equal(projected(produced, 4), [(reach_result,)], "finite path")
    require_equal(
        projected(observe(chart, sources, reach, produced), 4),
        [(reach_result,)],
        "finite path observation",
    )

    seek = f"(zerouv-seek-request request-zero {fuel(2)} {c} {a} {a})"
    produced = produce(chart, sources, program, seek)
    seek_result = qapp(qsym("found"), c, path(a, b, c))
    require_equal(projected(produced, 4), [(seek_result,)], "finite seek")
    require_equal(
        projected(observe(chart, sources, seek, produced), 4),
        [(seek_result,)],
        "finite seek observation",
    )
    short_seek = (
        f"(zerouv-seek-request request-zero {fuel(1)} {c} {a} {a})"
    )
    require_equal(
        produce(chart, sources, program, short_seek),
        [],
        "insufficient seek fuel does not claim failure or completion",
    )

    follow = (
        f"(zerouv-follow-request request-zero {walk} {cold} "
        f"{fuel(2)} {a} {a})"
    )
    produced = produce(chart, sources, program, follow)
    follow_result = qapp(qsym("residual"), c, path(a, b, c))
    require_equal(
        projected(produced, 4), [(follow_result,)],
        "memoryful controller",
    )
    require_equal(
        projected(observe(chart, sources, follow, produced), 4),
        [(follow_result,)],
        "memoryful controller observation",
    )
    forged = (
        f"(zerouv-follow-request request-zero {bad} {cold} "
        f"{fuel(1)} {a} {a})"
    )
    require_equal(
        produce(chart, sources, program, forged),
        [],
        "controller cannot invent a semantic edge",
    )

    recur = f"(zerouv-cycle-request request-zero {toggle} {cycle} {on})"
    produced = produce(chart, sources, program, recur)
    recurrent_result = qapp(qsym("recurrent"), toggle, off, on, cycle)
    require_equal(
        projected(produced, 4), [(recurrent_result,)],
        "checked recurrent lasso",
    )
    require_equal(
        projected(observe(chart, sources, recur, produced), 4),
        [(recurrent_result,)],
        "checked recurrent lasso observation",
    )
    rejects = (
        f"(zerouv-cycle-request request-zero {toggle} {cycle} {c})"
    )
    require_equal(
        produce(chart, sources, program, rejects),
        [],
        "unadmitted recurrent objective",
    )

    print(
        "(ZeroUVSemanticsV1Summary "
        "classification=5 unknown=1 step=3 multiplicity=2 "
        "reach=1 seek=2 controller=2 recurrence=2 observation=5)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
