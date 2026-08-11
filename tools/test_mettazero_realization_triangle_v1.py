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


def cli(
    cetta: Path, realization: str, program: str,
    profile: str | None = None,
) -> str:
    command = [str(cetta), "--lang", "zero"]
    if profile:
        command.extend(("--profile", profile))
    command.extend(("--gslt-realization", realization, "-e", program))
    return run(command)


def require_cli_pair(
    cetta: Path, program: str, expected: str,
    profile: str | None = None,
) -> None:
    horn = cli(cetta, "horn-reference", program, profile)
    compiled = cli(cetta, "compiled-worklist", program, profile)
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
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--ground-capability", type=Path, required=True)
    arguments = parser.parse_args()
    cetta = arguments.cetta.resolve()
    chart = arguments.chart.resolve()
    sources = (
        arguments.quote_match.resolve(),
        arguments.query_kernel.resolve(),
        arguments.observation.resolve(),
    )
    runner_sources = (*sources, arguments.runner.resolve())

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
    evaluate_surface = qapp(qsym("eval"), subject)
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
    require_cli_pair(cetta, "(= (f $x) (g $x)) (eval (f a))", "[(g a)]")

    reify_evaluate_surface = qapp(qsym("reify"), evaluate_surface)
    classified_reify_evaluate = relation_answers(
        chart,
        sources,
        "zero-classify",
        3,
        f"(zero-classify request-reify-evaluate "
        f"{reify_evaluate_surface} ?request)",
    )
    if len(classified_reify_evaluate) != 1:
        raise GateFailure("evaluation reification classification changed")
    reify_evaluate_request = sx.render(classified_reify_evaluate[0][3])
    reified_evaluation = observe(
        chart,
        sources,
        reify_evaluate_request,
        produce(chart, sources, program, reify_evaluate_request),
    )
    if reified_evaluation != [qapp(qapp(qsym("g"), qsym("a")))]:
        raise GateFailure(
            f"chart changed evaluation reification: {reified_evaluation}"
        )
    require_cli_pair(
        cetta,
        "(= (f $x) (g $x)) (reify (eval (f a)))",
        "[((g a))]",
    )

    reify_query_surface = qapp(qsym("reify"), query_surface)
    classified_reify_query = relation_answers(
        chart,
        sources,
        "zero-classify",
        3,
        f"(zero-classify request-reify-query "
        f"{reify_query_surface} ?request)",
    )
    if len(classified_reify_query) != 1:
        raise GateFailure("query reification classification changed")
    reify_query_request = sx.render(classified_reify_query[0][3])
    reified_query = observe(
        chart,
        sources,
        reify_query_request,
        produce(chart, sources, program, reify_query_request),
    )
    if reified_query != [qapp(qsym("hit"), qsym("hit"))]:
        raise GateFailure(
            f"chart changed reified query multiplicity: {reified_query}"
        )
    require_cli_pair(
        cetta,
        "(fact hit) (fact hit) "
        "(reify (zero-query (fact $x) $x))",
        "[(hit hit)]",
    )

    inert_request = f"(zero-evaluate-request request-two {qsym('unknown')})"
    inert_produced = produce(chart, sources, program, inert_request)
    if inert_produced:
        raise GateFailure("unknown subject unexpectedly produced an answer")
    if observe(chart, sources, inert_request, inert_produced) != [
        qsym("unknown")
    ]:
        raise GateFailure("closed empty production did not retain inertness")
    require_cli_pair(cetta, "(eval unknown)", "[unknown]")

    reify_inert_request = (
        f"(zero-reify-evaluate-request request-reify-inert "
        f"{qsym('unknown')})"
    )
    reify_inert_produced = produce(
        chart, sources, program, reify_inert_request
    )
    if reify_inert_produced:
        raise GateFailure("reified unknown unexpectedly produced an answer")
    if observe(
        chart, sources, reify_inert_request, reify_inert_produced
    ) != [qapp(qsym("unknown"))]:
        raise GateFailure("reified inert evaluation lost its singleton")
    require_cli_pair(cetta, "(reify (eval unknown))", "[(unknown)]")

    empty_request = (
        f"(zero-query-request request-three {qsym('absent')} {qsym('hit')})"
    )
    empty_produced = produce(chart, sources, program, empty_request)
    if empty_produced or observe(chart, sources, empty_request, empty_produced):
        raise GateFailure("empty query was confused with inert evaluation")
    require_cli_pair(cetta, "fact (zero-query absent hit)", "[]")

    reify_empty_request = (
        f"(zero-reify-query-request request-reify-empty "
        f"{qsym('absent')} {qsym('hit')})"
    )
    reify_empty_produced = produce(
        chart, sources, program, reify_empty_request
    )
    if reify_empty_produced or observe(
        chart, sources, reify_empty_request, reify_empty_produced
    ) != ["q-empty"]:
        raise GateFailure("empty query did not reify as the empty datum")
    require_cli_pair(
        cetta, "(reify (zero-query absent hit))", "[()]"
    )
    require_cli_pair(
        cetta, "(= a hit) (collapse (eval a))", "[]"
    )

    ground_request = (
        f"(zero-evaluate-request request-four {qsym('native')})"
    )
    grounded = produce(chart, ground_sources, program, ground_request)
    if observe(chart, ground_sources, ground_request, grounded) != [
        qsym("grounded")
    ]:
        raise GateFailure("declared ground capability did not cross the portal")
    require_cli_pair(cetta, "(eval native)", "[native]")

    runner_subject = qsym("a")
    runner_continuation = qsym("zero-halt")
    runner_surface = qapp(
        qsym("zero-step"),
        qapp(qsym("zero-pending"), runner_subject, runner_continuation),
    )
    classified_runner = relation_answers(
        chart,
        runner_sources,
        "zero-classify",
        3,
        f"(zero-classify request-runner {runner_surface} ?request)",
    )
    if len(classified_runner) != 1:
        raise GateFailure("semantic runner classification changed")
    runner_request = sx.render(classified_runner[0][3])
    runner_program = source_program(
        equality(qsym("a"), qsym("b")),
        equality(qsym("a"), qsym("b")),
    )
    runner_produced = produce(
        chart, runner_sources, runner_program, runner_request
    )
    if observe(
        chart, runner_sources, runner_request, runner_produced
    ) != [
        qapp(qsym("zero-pending"), qsym("b"), runner_continuation),
        qapp(qsym("zero-pending"), qsym("b"), runner_continuation),
    ]:
        raise GateFailure("semantic runner changed pending multiplicity")
    require_cli_pair(
        cetta,
        "(= a b) (= a b) (zero-step (zero-pending a zero-halt))",
        "[(zero-pending b zero-halt), (zero-pending b zero-halt)]",
        "exp",
    )

    quiescent_request = (
        f"(zero-step-request request-quiescent {qsym('unknown')} "
        f"{runner_continuation})"
    )
    quiescent_produced = produce(
        chart, runner_sources, runner_program, quiescent_request
    )
    if quiescent_produced or observe(
        chart, runner_sources, quiescent_request, quiescent_produced
    ) != [qapp(qsym("zero-completed"), qsym("unknown"))]:
        raise GateFailure("semantic runner changed completed quiescence")
    require_cli_pair(
        cetta,
        "(zero-step (zero-pending unknown zero-halt))",
        "[(zero-completed unknown)]",
        "exp",
    )

    template = qapp(qsym("wrap"), "(q-var q-zero)")
    next_continuation = qsym("zero-halt")
    then_continuation = qapp(
        qsym("zero-then"), template, next_continuation
    )
    then_request = (
        f"(zero-step-request request-then {qsym('unknown')} "
        f"{then_continuation})"
    )
    then_produced = produce(
        chart, runner_sources, runner_program, then_request
    )
    if then_produced or observe(
        chart, runner_sources, then_request, then_produced
    ) != [
        qapp(
            qsym("zero-pending"),
            qapp(qsym("wrap"), qsym("unknown")),
            next_continuation,
        )
    ]:
        raise GateFailure("semantic runner changed continuation substitution")
    require_cli_pair(
        cetta,
        "(zero-step (zero-pending unknown "
        "(zero-then (wrap $x) zero-halt)))",
        "[(zero-pending (wrap unknown) zero-halt)]",
        "exp",
    )

    require_cli_pair(
        cetta,
        "(= a b) (zero-step (zero-pending a zero-halt))",
        "[]",
    )

    print(
        "(MettaZeroRealizationTriangleV1Summary "
        "native-chart=1 horn-reference=1 compiled-worklist=1 "
        "classification=4 query-multiplicity=2 reflection=3 "
        "query-derived-evaluation=1 inert-completion=1 empty-query=1 "
        "reify=4 collapse-not-zero=1 ground-capability=1 "
        "semantic-runner=4 profile-isolation=1 structural-cases=20)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError, sx.SchemaError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
