#!/usr/bin/env python3
"""Exercise every authored rule in Zero's revision-threaded emit profile."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess


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


def chart_count(chart: Path, sources: tuple[Path, ...], query: str) -> int:
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
    return len(terms)


def require_one(chart: Path, sources: tuple[Path, ...], query: str, label: str) -> None:
    count = chart_count(chart, sources, query)
    if count != 1:
        raise GateFailure(f"{label}: expected one answer, found {count}")


def qsym(name: str) -> str:
    return f"(q-sym (q-str {json.dumps(name)}))"


def qlist(*items: str) -> str:
    result = "q-nil"
    for item in reversed(items):
        result = f"(q-cons {item} {result})"
    return result


def qapp(head: str, *arguments: str) -> str:
    return f"(q-app {head} {qlist(*arguments)})"


def source_program(*payloads: str) -> str:
    result = "zero-program-nil"
    for index, payload in reversed(tuple(enumerate(payloads))):
        result = (
            f"(zero-program-cons (source-occurrence {index}) "
            f"{payload} {result})"
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--quote-match", type=Path, required=True)
    parser.add_argument("--query-kernel", type=Path, required=True)
    parser.add_argument("--observation", type=Path, required=True)
    parser.add_argument("--emit", type=Path, required=True)
    arguments = parser.parse_args()
    chart = arguments.chart.resolve()
    sources = (
        arguments.quote_match.resolve(),
        arguments.query_kernel.resolve(),
        arguments.observation.resolve(),
        arguments.emit.resolve(),
    )

    variable = "(q-var q-zero)"
    fact_pattern = qapp(qsym("fact"), variable)
    seen_template = qapp(qsym("seen"), variable)
    match = qapp(qsym("match"), qsym("&self"), fact_pattern, seen_template)
    quote = qapp(qsym("quote"), qsym("value"))
    emit = qapp(qsym("emit"), qsym("&self"), qapp(qsym("fact"), qsym("a")))
    add_atom = qapp(
        qsym("add-atom"), qsym("&self"), qapp(qsym("fact"), qsym("a"))
    )
    let = qapp(qsym("let"), variable, quote, qapp(qsym("quote"), variable))

    for syntax, label in (
        (match, "classify match"),
        (let, "classify let"),
        (emit, "classify emit"),
        (add_atom, "classify add-atom"),
        (quote, "classify quote"),
    ):
        require_one(
            chart, sources,
            f"(zero-classify request {syntax} ?classified)", label,
        )

    open_variable_cases = (
        (
            "(qmatch-open-variable q-zero qmatch-env-nil "
            "(q-var q-zero))",
            "open variable absent",
        ),
        (
            "(qmatch-open-variable q-zero "
            "(qmatch-env-cons q-zero value qmatch-env-nil) value)",
            "open variable here",
        ),
        (
            "(qmatch-open-variable q-zero "
            "(qmatch-env-cons (q-succ q-zero) later qmatch-env-nil) "
            "(q-var q-zero))",
            "open variable before",
        ),
        (
            "(qmatch-open-variable (q-succ q-zero) "
            "(qmatch-env-cons q-zero earlier qmatch-env-nil) "
            "(q-var (q-succ q-zero)))",
            "open variable after",
        ),
    )
    for query, label in open_variable_cases:
        require_one(chart, sources, query, label)

    environment = "(qmatch-env-cons q-zero value qmatch-env-nil)"
    atomic_substitutions = (
        ("(q-var q-zero)", "value", "variable"),
        ("(q-sym name)", "(q-sym name)", "symbol"),
        ("(q-int 7)", "(q-int 7)", "integer"),
        ('(q-str "text")', '(q-str "text")', "string"),
        ("q-empty", "q-empty", "empty expression"),
        ("(q-ground opaque)", "(q-ground opaque)", "ground value"),
    )
    for source, result, label in atomic_substitutions:
        require_one(
            chart, sources,
            f"(qmatch-substitute-open {source} {environment} {result})",
            f"open substitution {label}",
        )
    require_one(
        chart, sources,
        f"(qmatch-substitute-open-list q-nil {environment} q-nil)",
        "open list substitution empty",
    )
    require_one(
        chart, sources,
        f"(qmatch-substitute-open-list "
        f"(q-cons (q-var q-zero) q-nil) {environment} "
        f"(q-cons value q-nil))",
        "open list substitution step",
    )
    require_one(
        chart, sources,
        f"(qmatch-substitute-open "
        f"(q-app (q-sym f) (q-cons (q-var q-zero) q-nil)) "
        f"{environment} (q-app (q-sym f) (q-cons value q-nil)))",
        "open application substitution",
    )

    fact = qapp(qsym("fact"), qsym("a"))
    equation = qapp(
        qsym("="), qapp(qsym("f"), variable), qapp(qsym("g"), variable)
    )
    program = source_program(fact, equation)
    require_one(
        chart, sources,
        f"(zero-compute {program} request q-zero {quote} "
        f"?evidence {program} q-zero {qsym('value')})",
        "compute quote",
    )
    require_one(
        chart, sources,
        f"(zero-compute {program} request q-zero {match} "
        f"?evidence {program} q-zero {qapp(qsym('seen'), qsym('a'))})",
        "compute match",
    )
    evaluate = qapp(qsym("eval"), qapp(qsym("f"), qsym("a")))
    require_one(
        chart, sources,
        f"(zero-compute {program} request q-zero {evaluate} "
        f"?evidence {program} q-zero {qapp(qsym('g'), qsym('a'))})",
        "compute eval",
    )
    emitted_program = (
        f"(zero-program-cons (zero-emitted-occurrence request q-zero) "
        f"{fact} {program})"
    )
    for computation, label in (
        (emit, "compute emit"),
        (add_atom, "compute add-atom"),
    ):
        require_one(
            chart, sources,
            f"(zero-compute {program} request q-zero {computation} "
            f"?evidence {emitted_program} (q-succ q-zero) q-empty)",
            label,
        )
    require_one(
        chart, sources,
        f"(zero-compute {program} request q-zero {let} "
        f"?evidence {program} q-zero {qsym('value')})",
        "compute let",
    )
    request = f"(zero-interact-request request {quote})"
    require_one(
        chart, sources,
        f"(zero-produce {program} {request} ?evidence {qsym('value')})",
        "produce interaction",
    )
    produced = (
        "(zero-produced-cons (gslt-produced producer-evidence "
        f"{qsym('value')}) zero-produced-nil)"
    )
    require_one(
        chart, sources,
        f"(zero-observe {request} {produced} ?evidence {qsym('value')})",
        "observe interaction",
    )

    print("(MettaZeroEmitSemanticsV1Summary rules=26 witnessed=26)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
