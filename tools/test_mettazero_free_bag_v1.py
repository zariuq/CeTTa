#!/usr/bin/env python3
"""Adversarial checks for the first executable MeTTa Zero candidate."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

import gslt2parse_schema_v1 as sx


class GateFailure(RuntimeError):
    pass


def run_query(
    chart: Path,
    core: Path,
    query: str,
    observation: Path | None = None,
) -> dict[str, object]:
    presentations = [str(chart), str(core)]
    if observation is not None:
        presentations.append(str(observation))
    completed = subprocess.run(
        [
            *presentations,
            "--query-text",
            query,
            "--timeout",
            "10",
            "--max-rounds",
            "100000",
        ],
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"generic GSLT execution failed:\n{completed.stdout}\n"
            f"{completed.stderr}"
        )
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure(f"malformed chart result: {completed.stdout}") from error
    if not isinstance(result, dict):
        raise GateFailure(f"chart result is not an object: {result!r}")
    return result


def expect(result: dict[str, object], outcome: str, answers: int) -> None:
    if result.get("outcome") != outcome or result.get("answers") != answers:
        raise GateFailure(
            f"expected {outcome}/{answers}, got "
            f"{result.get('outcome')}/{result.get('answers')}: {result}"
        )


def rendered_answers(result: dict[str, object]) -> list[tuple[sx.SExpr, ...]]:
    raw = result.get("terms")
    if not isinstance(raw, list):
        raise GateFailure(f"result has no term list: {result}")
    answers: list[tuple[sx.SExpr, ...]] = []
    for text in raw:
        if not isinstance(text, str):
            raise GateFailure(f"non-text answer: {text!r}")
        parsed = sx.parse_sexprs(text, source="MeTTa Zero answer")
        if len(parsed) != 1 or not isinstance(parsed[0], tuple):
            raise GateFailure(f"malformed answer term: {text}")
        answers.append(parsed[0])
    return answers


def observation_pairs(result: dict[str, object]) -> set[tuple[str, str]]:
    pairs: set[tuple[str, str]] = set()
    for answer in rendered_answers(result):
        if (
            len(answer) != 5
            or not isinstance(answer[0], sx.Symbol)
            or answer[0].text != "zero-step"
        ):
            raise GateFailure(f"unexpected answer shape: {sx.render(answer)}")
        pairs.add((sx.render(answer[3]), sx.render(answer[4])))
    return pairs


def qsym(name: str) -> str:
    return f"(q-sym (q-str {json.dumps(name)}))"


def qlist(*items: str) -> str:
    result = "q-nil"
    for item in reversed(items):
        result = f"(q-cons {item} {result})"
    return result


def qapp(head: str, *arguments: str) -> str:
    return f"(q-app {head} {qlist(*arguments)})"


def equality(pattern: str, template: str) -> str:
    return qapp(qsym("="), pattern, template)


def program(*occurrences: tuple[str, str]) -> str:
    result = "zero-program-nil"
    for occurrence, atom in reversed(occurrences):
        result = f"(zero-program-cons {occurrence} {atom} {result})"
    return result


def step(program_value: str, subject: str) -> str:
    return f"(zero-step {program_value} {subject} ?occurrence ?result)"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument(
        "--core",
        type=Path,
        default=(
            Path(__file__).resolve().parents[1]
            / "langdef/zero/semantics/free_bag_rewrite_core_v1.metta"
        ),
    )
    parser.add_argument(
        "--observation",
        type=Path,
        default=(
            Path(__file__).resolve().parents[1]
            / "langdef/zero/semantics/one_step_observation_v1.metta"
        ),
    )
    parser.add_argument(
        "--public-observation",
        type=Path,
        default=(
            Path(__file__).resolve().parents[1]
            / "langdef/zero/semantics/public_result_bag_v1.metta"
        ),
    )
    arguments = parser.parse_args()

    variable = "(q-var q-zero)"
    f_variable = qapp(qsym("f"), variable)
    g_variable = qapp(qsym("g"), variable)
    f_a = qapp(qsym("f"), qsym("a"))
    g_a = qapp(qsym("g"), qsym("a"))
    substitution_program = program(("r-substitute", equality(f_variable, g_variable)))
    substituted = run_query(
        arguments.chart, arguments.core, step(substitution_program, f_a)
    )
    expect(substituted, "Unique", 1)
    pairs = observation_pairs(substituted)
    if len(pairs) != 1 or next(iter(pairs))[1] != g_a:
        raise GateFailure(f"substitution result changed: {pairs}")

    pair_repeated = qapp(qsym("pair"), variable, variable)
    repeated_program = program(
        ("r-repeat", equality(pair_repeated, qsym("same")))
    )
    repeated_positive = run_query(
        arguments.chart,
        arguments.core,
        step(repeated_program, qapp(qsym("pair"), qsym("a"), qsym("a"))),
    )
    expect(repeated_positive, "Unique", 1)
    repeated_negative = run_query(
        arguments.chart,
        arguments.core,
        step(repeated_program, qapp(qsym("pair"), qsym("a"), qsym("b"))),
    )
    expect(repeated_negative, "NoAnswer", 0)

    index_base = run_query(
        arguments.chart,
        arguments.core,
        "(zero-index-lt q-zero (q-succ q-zero))",
    )
    expect(index_base, "Unique", 1)
    index_step = run_query(
        arguments.chart,
        arguments.core,
        "(zero-index-lt (q-succ q-zero) "
        "(q-succ (q-succ q-zero)))",
    )
    expect(index_step, "Unique", 1)
    bind_before = run_query(
        arguments.chart,
        arguments.core,
        "(zero-bind q-zero value-zero "
        "(zero-env-cons (q-succ q-zero) value-one zero-env-nil) "
        "?result)",
    )
    expect(bind_before, "Unique", 1)
    bind_after = run_query(
        arguments.chart,
        arguments.core,
        "(zero-bind (q-succ q-zero) value-one "
        "(zero-env-cons q-zero value-zero zero-env-nil) ?result)",
    )
    expect(bind_after, "Unique", 1)
    lookup_after = run_query(
        arguments.chart,
        arguments.core,
        "(zero-lookup (q-succ q-zero) "
        "(zero-env-cons q-zero value-zero "
        "(zero-env-cons (q-succ q-zero) value-one zero-env-nil)) "
        "value-one)",
    )
    expect(lookup_after, "Unique", 1)

    integer_program = program(
        ("r-integer", equality("(q-int 1)", "(q-int 2)"))
    )
    integer_result = run_query(
        arguments.chart,
        arguments.core,
        step(integer_program, "(q-int 1)"),
    )
    expect(integer_result, "Unique", 1)
    if {result for _, result in observation_pairs(integer_result)} != {
        "(q-int 2)"
    }:
        raise GateFailure("integer matching or substitution changed")

    string_program = program(
        (
            "r-string",
            equality('(q-str "left")', '(q-str "right")'),
        )
    )
    string_result = run_query(
        arguments.chart,
        arguments.core,
        step(string_program, '(q-str "left")'),
    )
    expect(string_result, "Unique", 1)
    if {result for _, result in observation_pairs(string_result)} != {
        '(q-str "right")'
    }:
        raise GateFailure("string matching or substitution changed")

    contextual_program = program(
        ("r-context", equality(qsym("a"), qsym("b")))
    )
    contextual_subject = qapp(
        qsym("wrap"), qsym("a"), qapp(qsym("inner"), qsym("a"))
    )
    contextual = run_query(
        arguments.chart,
        arguments.core,
        step(contextual_program, contextual_subject),
    )
    expect(contextual, "Ambiguous", 2)
    context_paths = {path for path, _ in observation_pairs(contextual)}
    if not any("(zero-in-argument q-zero" in path for path in context_paths):
        raise GateFailure(f"first argument occurrence is absent: {context_paths}")
    if not any("(q-succ q-zero)" in path for path in context_paths):
        raise GateFailure(f"nested argument occurrence is absent: {context_paths}")

    head_program = program(
        ("r-head", equality(qsym("f"), qsym("g")))
    )
    head_result = run_query(
        arguments.chart,
        arguments.core,
        step(head_program, qapp(qsym("f"), qsym("a"))),
    )
    expect(head_result, "Unique", 1)
    head_pairs = observation_pairs(head_result)
    if len(head_pairs) != 1 or next(iter(head_pairs))[1] != qapp(
        qsym("g"), qsym("a")
    ):
        raise GateFailure("application-head contextual rewrite changed")

    duplicate_rule = equality(qsym("a"), qsym("b"))
    forward = program(("r-one", duplicate_rule), ("r-two", duplicate_rule))
    reverse = program(("r-two", duplicate_rule), ("r-one", duplicate_rule))
    forward_result = run_query(
        arguments.chart, arguments.core, step(forward, qsym("a"))
    )
    reverse_result = run_query(
        arguments.chart, arguments.core, step(reverse, qsym("a"))
    )
    expect(forward_result, "Ambiguous", 2)
    expect(reverse_result, "Ambiguous", 2)
    if observation_pairs(forward_result) != observation_pairs(reverse_result):
        raise GateFailure("program representation order changed the step relation")
    if {result for _, result in observation_pairs(forward_result)} != {qsym("b")}:
        raise GateFailure("duplicate occurrences changed their common reduct")

    inert = run_query(
        arguments.chart,
        arguments.core,
        step(contextual_program, qapp(qsym("unknown"), qsym("c"))),
    )
    expect(inert, "NoAnswer", 0)

    data_only = program(("data-occurrence", qapp(qsym("data"), qsym("a"))))
    data_does_not_execute = run_query(
        arguments.chart, arguments.core, step(data_only, qsym("a"))
    )
    expect(data_does_not_execute, "NoAnswer", 0)

    unbound_template = program(
        (
            "r-unbound-template",
            equality(qsym("a"), "(q-var q-zero)"),
        )
    )
    unbound_result = run_query(
        arguments.chart, arguments.core, step(unbound_template, qsym("a"))
    )
    expect(unbound_result, "NoAnswer", 0)

    empty_program = program(
        ("r-empty", equality("q-empty", qsym("empty-result")))
    )
    empty_result = run_query(
        arguments.chart, arguments.core, step(empty_program, "q-empty")
    )
    expect(empty_result, "Unique", 1)
    if {result for _, result in observation_pairs(empty_result)} != {
        qsym("empty-result")
    }:
        raise GateFailure("empty expression did not remain ordinary inert data")

    empty_template_program = program(
        ("r-empty-template", equality(qsym("make-empty"), "q-empty"))
    )
    empty_template = run_query(
        arguments.chart,
        arguments.core,
        step(empty_template_program, qsym("make-empty")),
    )
    expect(empty_template, "Unique", 1)
    if {result for _, result in observation_pairs(empty_template)} != {
        "q-empty"
    }:
        raise GateFailure("empty expression template substitution changed")

    opaque_ground = "(q-ground opaque-scalar)"
    ground_program = program(
        ("r-ground", equality(opaque_ground, qsym("ground-result")))
    )
    ground_positive = run_query(
        arguments.chart, arguments.core, step(ground_program, opaque_ground)
    )
    expect(ground_positive, "Unique", 1)
    ground_negative = run_query(
        arguments.chart,
        arguments.core,
        step(ground_program, "(q-ground other-scalar)"),
    )
    expect(ground_negative, "NoAnswer", 0)

    ground_template_program = program(
        (
            "r-ground-template",
            equality(qsym("make-ground"), opaque_ground),
        )
    )
    ground_template = run_query(
        arguments.chart,
        arguments.core,
        step(ground_template_program, qsym("make-ground")),
    )
    expect(ground_template, "Unique", 1)
    if {result for _, result in observation_pairs(ground_template)} != {
        opaque_ground
    }:
        raise GateFailure("opaque ground template substitution changed")

    request_program = program(
        ("r-request-rule", equality(f_variable, g_variable)),
        ("request-one", qapp(qsym("!"), f_a)),
    )
    observed = run_query(
        arguments.chart,
        arguments.core,
        f"(zero-observe {request_program} ?request ?step ?result)",
        arguments.observation,
    )
    expect(observed, "Unique", 1)
    observed_terms = rendered_answers(observed)
    if len(observed_terms) != 1 or sx.render(observed_terms[0][-1]) != g_a:
        raise GateFailure("one-step observation did not publish the core reduct")

    duplicate_request_program = program(
        ("r-public-one", equality(qsym("a"), qsym("b"))),
        ("r-public-two", equality(qsym("a"), qsym("b"))),
        ("request-public", qapp(qsym("!"), qsym("a"))),
    )
    public_result = run_query(
        arguments.chart,
        arguments.core,
        f"(zero-evaluate {duplicate_request_program} ?occurrence ?result)",
        arguments.public_observation,
    )
    expect(public_result, "Ambiguous", 2)
    public_terms = rendered_answers(public_result)
    expected_public = qsym("b")
    if any(
        len(term) != 4 or sx.render(term[3]) != expected_public
        for term in public_terms
    ):
        raise GateFailure(
            "public observation exposed proof identity or changed its result bag"
        )
    public_occurrences = {sx.render(term[2]) for term in public_terms}
    if len(public_occurrences) != 2:
        raise GateFailure(
            "public occurrence carrier collapsed duplicate derivations"
        )

    print(
        "(MettaZeroFreeBagV1Summary "
        "substitution=1 repeated-positive=1 repeated-negative=1 "
        "ordered-environment=5 integer=1 string=1 "
        "contextual-occurrences=2 duplicate-rule-occurrences=2 "
        "application-head=1 "
        "order-invariance=1 inert-ignorance=1 data-inertness=1 "
        "unbound-template-rejected=1 one-step-observation=1 "
        "empty-expression=1 empty-template=1 opaque-ground-positive=1 "
        "opaque-ground-negative=1 opaque-ground-template=1 "
        "public-result-multiplicity=2)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GateFailure as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
