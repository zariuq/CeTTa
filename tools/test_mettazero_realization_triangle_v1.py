#!/usr/bin/env python3
"""Cross-check chart, Horn-reference, and compiled-worklist MeTTa Zero."""

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
            raise GateFailure(f"chart returned a malformed answer: {rendered}")
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


def require_count(
    chart: Path,
    sources: tuple[Path, ...],
    query: str,
    expected: int,
    label: str,
) -> None:
    answers = chart_answers(chart, sources, query)
    if len(answers) != expected:
        raise GateFailure(
            f"{label}: expected {expected} answers, found {len(answers)}"
        )


def produced_bag(answers: list[tuple[sx.SExpr, ...]]) -> str:
    bag = "zero-produced-nil"
    for answer in reversed(answers):
        evidence = sx.render(answer[3])
        result = sx.render(answer[4])
        bag = (
            "(zero-produced-cons "
            f"(gslt-produced {evidence} {result}) {bag})"
        )
    return bag


def produce(
    chart: Path,
    sources: tuple[Path, ...],
    program: str,
    request: str,
) -> list[tuple[sx.SExpr, ...]]:
    return relation_answers(
        chart,
        sources,
        "zero-produce",
        4,
        f"(zero-produce {program} {request} ?evidence ?result)",
    )


def observe(
    chart: Path,
    sources: tuple[Path, ...],
    request: str,
    produced: list[tuple[sx.SExpr, ...]],
) -> list[str]:
    answers = relation_answers(
        chart,
        sources,
        "zero-observe",
        4,
        f"(zero-observe {request} {produced_bag(produced)} "
        "?evidence ?result)",
    )
    return sorted(sx.render(answer[4]) for answer in answers)


def cli(cetta: Path, realization: str, program: str) -> str:
    return run(
        [
            str(cetta),
            "--lang",
            "zero",
            "--gslt-realization",
            realization,
            "-e",
            program,
        ]
    )


def require_cli_pair(cetta: Path, program: str, expected: str) -> None:
    horn = cli(cetta, "horn-reference", program)
    compiled = cli(cetta, "compiled-worklist", program)
    if horn != expected or compiled != horn:
        raise GateFailure(
            f"public realizations diverged: horn={horn!r} "
            f"compiled={compiled!r} expected={expected!r}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--quote-match", type=Path, required=True)
    parser.add_argument("--query-kernel", type=Path, required=True)
    parser.add_argument("--observation", type=Path, required=True)
    parser.add_argument("--ground-capability", type=Path, required=True)
    arguments = parser.parse_args()
    cetta = arguments.cetta.resolve()
    chart = arguments.chart.resolve()
    sources = (
        arguments.quote_match.resolve(),
        arguments.query_kernel.resolve(),
        arguments.observation.resolve(),
    )

    ground_sources = (*sources, arguments.ground_capability.resolve())

    # Exercise every structural case independently so the shared quotation
    # component cannot retain dead or accidentally host-supplied behavior.
    require_count(
        chart, sources,
        "(qmatch-index-lt q-zero (q-succ q-zero))", 1,
        "index base",
    )
    require_count(
        chart, sources,
        "(qmatch-index-lt (q-succ q-zero) "
        "(q-succ (q-succ q-zero)))", 1,
        "index step",
    )
    require_count(
        chart, sources,
        "(qmatch-bind q-zero value qmatch-env-nil ?after)", 1,
        "empty binding",
    )
    require_count(
        chart, sources,
        "(qmatch-bind q-zero value "
        "(qmatch-env-cons q-zero value qmatch-env-nil) ?after)", 1,
        "existing binding",
    )
    require_count(
        chart, sources,
        "(qmatch-bind q-zero value-zero "
        "(qmatch-env-cons (q-succ q-zero) value-one qmatch-env-nil) "
        "?after)", 1,
        "binding insertion before",
    )
    require_count(
        chart, sources,
        "(qmatch-bind (q-succ q-zero) value-one "
        "(qmatch-env-cons q-zero value-zero qmatch-env-nil) ?after)", 1,
        "binding insertion after",
    )
    require_count(
        chart, sources,
        "(qmatch-lookup q-zero "
        "(qmatch-env-cons q-zero value qmatch-env-nil) value)", 1,
        "lookup here",
    )
    require_count(
        chart, sources,
        "(qmatch-lookup (q-succ q-zero) "
        "(qmatch-env-cons q-zero value-zero "
        "(qmatch-env-cons (q-succ q-zero) value-one qmatch-env-nil)) "
        "value-one)", 1,
        "lookup after",
    )
    atomic_cases = (
        ("(q-sym name)", "symbol"),
        ("(q-int 7)", "integer"),
        ('(q-str "text")', "string"),
        ("q-empty", "empty expression"),
        ("(q-ground opaque)", "opaque ground"),
    )
    for term, label in atomic_cases:
        require_count(
            chart, sources,
            f"(qmatch-term {term} {term} qmatch-env-nil qmatch-env-nil)",
            1, f"{label} matching",
        )
        require_count(
            chart, sources,
            f"(qmatch-substitute {term} qmatch-env-nil {term})",
            1, f"{label} substitution",
        )
    require_count(
        chart, sources,
        "(qmatch-term (q-var q-zero) value qmatch-env-nil ?after)",
        1, "variable matching",
    )
    require_count(
        chart, sources,
        "(qmatch-substitute (q-var q-zero) "
        "(qmatch-env-cons q-zero value qmatch-env-nil) value)",
        1, "variable substitution",
    )
    quoted_application = (
        '(q-app (q-sym (q-str "f")) (q-cons (q-int 1) q-nil))'
    )
    require_count(
        chart, sources,
        f"(qmatch-term {quoted_application} {quoted_application} "
        "qmatch-env-nil qmatch-env-nil)",
        1, "application and list matching",
    )
    require_count(
        chart, sources,
        f"(qmatch-substitute {quoted_application} qmatch-env-nil "
        f"{quoted_application})",
        1, "application and list substitution",
    )

    variable = "(q-var q-zero)"
    equation = equality(
        qapp(qsym("f"), variable), qapp(qsym("g"), variable)
    )
    program = source_program(qsym("fact"), qsym("fact"), equation)

    query_surface = qapp(qsym("zero-query"), qsym("fact"), qsym("hit"))
    classified_query = relation_answers(
        chart,
        sources,
        "zero-classify",
        3,
        f"(zero-classify request-zero {query_surface} ?request)",
    )
    if len(classified_query) != 1:
        raise GateFailure("query request classification changed")
    query_request = sx.render(classified_query[0][3])
    query_produced = produce(chart, sources, program, query_request)
    if observe(chart, sources, query_request, query_produced) != [
        qsym("hit"),
        qsym("hit"),
    ]:
        raise GateFailure("chart changed query multiplicity")
    require_cli_pair(
        cetta,
        "fact fact (zero-query fact hit)",
        "[hit, hit]",
    )

    reflected_request = (
        f"(zero-query-request request-reflect {variable} {variable})"
    )
    reflected = observe(
        chart,
        sources,
        reflected_request,
        produce(chart, sources, program, reflected_request),
    )
    if reflected != sorted((qsym("fact"), qsym("fact"), equation)):
        raise GateFailure(f"chart changed reflective querying: {reflected}")
    require_cli_pair(
        cetta,
        "fact (= a b) (zero-query $atom $atom)",
        "[fact, (= a b)]",
    )

    subject = qapp(qsym("f"), qsym("a"))
    evaluate_surface = qapp(qsym("!"), subject)
    classified_evaluate = relation_answers(
        chart,
        sources,
        "zero-classify",
        3,
        f"(zero-classify request-one {evaluate_surface} ?request)",
    )
    if len(classified_evaluate) != 1:
        raise GateFailure("evaluation request classification changed")
    evaluate_request = sx.render(classified_evaluate[0][3])
    evaluated = observe(
        chart,
        sources,
        evaluate_request,
        produce(chart, sources, program, evaluate_request),
    )
    if evaluated != [qapp(qsym("g"), qsym("a"))]:
        raise GateFailure(f"chart changed query-derived evaluation: {evaluated}")
    require_cli_pair(cetta, "(= (f $x) (g $x)) (! (f a))", "[(g a)]")

    inert_request = f"(zero-evaluate-request request-two {qsym('unknown')})"
    inert_produced = produce(chart, sources, program, inert_request)
    if inert_produced:
        raise GateFailure("unknown subject unexpectedly produced an answer")
    if observe(chart, sources, inert_request, inert_produced) != [
        qsym("unknown")
    ]:
        raise GateFailure("closed empty production did not retain inertness")
    require_cli_pair(cetta, "(! unknown)", "[unknown]")

    empty_request = (
        f"(zero-query-request request-three {qsym('absent')} {qsym('hit')})"
    )
    empty_produced = produce(chart, sources, program, empty_request)
    if empty_produced or observe(chart, sources, empty_request, empty_produced):
        raise GateFailure("empty query was confused with inert evaluation")
    require_cli_pair(cetta, "fact (zero-query absent hit)", "[]")

    ground_request = (
        f"(zero-evaluate-request request-four {qsym('native')})"
    )
    grounded = produce(chart, ground_sources, program, ground_request)
    if observe(chart, ground_sources, ground_request, grounded) != [
        qsym("grounded")
    ]:
        raise GateFailure("declared ground capability did not cross the portal")
    require_cli_pair(cetta, "(! native)", "[native]")

    print(
        "(MettaZeroRealizationTriangleV1Summary "
        "native-chart=1 horn-reference=1 compiled-worklist=1 "
        "classification=2 query-multiplicity=2 reflection=3 "
        "query-derived-evaluation=1 inert-completion=1 empty-query=1 "
        "ground-capability=1 structural-cases=20)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError, sx.SchemaError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
