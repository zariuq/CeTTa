#!/usr/bin/env python3
"""Exercise every new authored rule in Zero's provider-backed profile."""

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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--quote-match", type=Path, required=True)
    parser.add_argument("--query-kernel", type=Path, required=True)
    parser.add_argument("--observation", type=Path, required=True)
    parser.add_argument("--open-substitution", type=Path, required=True)
    parser.add_argument("--support-indexed-abt", type=Path, required=True)
    parser.add_argument("--interact", type=Path, required=True)
    parser.add_argument("--provider", type=Path, required=True)
    arguments = parser.parse_args()
    chart = arguments.chart.resolve()
    sources = (
        arguments.quote_match.resolve(),
        arguments.query_kernel.resolve(),
        arguments.observation.resolve(),
        arguments.open_substitution.resolve(),
        arguments.support_indexed_abt.resolve(),
        arguments.interact.resolve(),
        arguments.provider.resolve(),
    )

    variable = "(q-var q-zero)"
    fact_pattern = qapp(qsym("fact"), variable)
    seen_template = qapp(qsym("seen"), variable)
    match = qapp(qsym("match"), qsym("&self"), fact_pattern, seen_template)
    quote = qapp(qsym("quote"), qsym("value"))
    emitted_fact = qapp(qsym("fact"), qsym("new"))
    emit = qapp(qsym("emit"), qsym("&self"), emitted_fact)
    add_atom = qapp(qsym("add-atom"), qsym("&self"), emitted_fact)
    let = qapp(qsym("let"), variable, quote, qapp(qsym("quote"), variable))

    for surface, label in (
        (match, "classify match"),
        (let, "classify let"),
        (emit, "classify emit"),
        (add_atom, "classify add-atom"),
        (quote, "classify quote"),
    ):
        require_one(
            chart,
            sources,
            f"(zero-classify request {surface} ?classified)",
            label,
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

    support_environment = (
        "(qmatch-supported-env-cons q-zero q-zero value "
        "qmatch-supported-env-nil)"
    )
    support_next_environment = (
        "(qmatch-supported-env-cons (q-succ q-zero) q-zero later "
        "qmatch-supported-env-nil)"
    )
    support_inserted_before = (
        "(qmatch-supported-env-cons q-zero q-zero value "
        f"{support_next_environment})"
    )
    support_inserted_after = (
        "(qmatch-supported-env-cons q-zero q-zero earlier "
        "(qmatch-supported-env-cons (q-succ q-zero) q-zero value "
        "qmatch-supported-env-nil))"
    )
    support_rule_witnesses = (
        (
            "(qabt-nat-add (q-succ q-zero) q-zero (q-succ q-zero))",
            "support depth addition successor",
        ),
        (
            "(qmatch-supported-bind q-zero q-zero value "
            f"{support_environment} {support_environment} "
            "(qmatch-supported-binding q-zero q-zero value))",
            "support binding existing",
        ),
        (
            "(qmatch-supported-bind q-zero q-zero value "
            f"{support_next_environment} {support_inserted_before} "
            "(qmatch-supported-binding q-zero q-zero value))",
            "support binding before",
        ),
        (
            "(qmatch-supported-bind (q-succ q-zero) q-zero value "
            "(qmatch-supported-env-cons q-zero q-zero earlier "
            "qmatch-supported-env-nil) "
            f"{support_inserted_after} "
            "(qmatch-supported-binding (q-succ q-zero) q-zero value))",
            "support binding after",
        ),
        (
            "(qmatch-supported-open-variable q-zero q-zero "
            "qmatch-supported-env-nil (q-var q-zero) "
            "(qmatch-supported-unbound q-zero))",
            "support open variable absent",
        ),
        (
            "(qmatch-supported-open-variable q-zero q-zero "
            f"{support_next_environment} (q-var q-zero) "
            "(qmatch-supported-unbound q-zero))",
            "support open variable before",
        ),
        (
            "(qmatch-supported-open-variable (q-succ q-zero) q-zero "
            "(qmatch-supported-env-cons q-zero q-zero earlier "
            "qmatch-supported-env-nil) (q-var (q-succ q-zero)) "
            "(qmatch-supported-unbound (q-succ q-zero)))",
            "support open variable after",
        ),
        (
            "(qmatch-supported-term (q-int 7) (q-int 7) q-zero "
            "qmatch-supported-env-nil qmatch-supported-env-nil)",
            "support match integer",
        ),
        (
            '(qmatch-supported-term (q-str "text") (q-str "text") '
            "q-zero qmatch-supported-env-nil qmatch-supported-env-nil)",
            "support match string",
        ),
        (
            "(qmatch-supported-term q-empty q-empty q-zero "
            "qmatch-supported-env-nil qmatch-supported-env-nil)",
            "support match empty expression",
        ),
        (
            "(qmatch-supported-term (q-ground opaque) (q-ground opaque) "
            "q-zero qmatch-supported-env-nil qmatch-supported-env-nil)",
            "support match ground value",
        ),
        (
            f"(qmatch-substitute-supported (q-int 7) {support_environment} "
            "q-zero (q-int 7))",
            "support substitution integer",
        ),
        (
            f'(qmatch-substitute-supported (q-str "text") '
            f'{support_environment} q-zero (q-str "text"))',
            "support substitution string",
        ),
        (
            f"(qmatch-substitute-supported q-empty {support_environment} "
            "q-zero q-empty)",
            "support substitution empty expression",
        ),
        (
            "(qmatch-substitute-supported (q-ground opaque) "
            f"{support_environment} q-zero (q-ground opaque))",
            "support substitution ground value",
        ),
    )
    for query, label in support_rule_witnesses:
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
            chart,
            sources,
            f"(qmatch-substitute-open {source} {environment} {result})",
            f"open substitution {label}",
        )
    require_one(
        chart,
        sources,
        f"(qmatch-substitute-open-list q-nil {environment} q-nil)",
        "open list substitution empty",
    )
    require_one(
        chart,
        sources,
        "(qmatch-substitute-open-list "
        f"(q-cons (q-var q-zero) q-nil) {environment} "
        "(q-cons value q-nil))",
        "open list substitution step",
    )
    require_one(
        chart,
        sources,
        "(qmatch-substitute-open "
        f"(q-app (q-sym f) (q-cons (q-var q-zero) q-nil)) {environment} "
        "(q-app (q-sym f) (q-cons value q-nil)))",
        "open application substitution",
    )

    require_one(
        chart,
        sources,
        f"(zero-compute world-0 request q-zero {quote} "
        f"?evidence world-0 q-zero {qsym('value')})",
        "compute quote",
    )
    require_one(
        chart,
        sources,
        f"(zero-compute world-0 request q-zero {match} "
        f"?evidence world-0 q-zero {qapp(qsym('seen'), qsym('a'))})",
        "compute match",
    )
    evaluate_equation = qapp(qsym("eval"), qapp(qsym("f"), qsym("a")))
    require_one(
        chart,
        sources,
        f"(zero-compute world-0 request q-zero {evaluate_equation} "
        f"?evidence world-0 q-zero {qapp(qsym('g'), qsym('a'))})",
        "compute eval equation",
    )
    evaluate_ground = qapp(qsym("eval"), qsym("native"))
    require_one(
        chart,
        sources,
        f"(zero-compute world-0 request q-zero {evaluate_ground} "
        f"?evidence world-0 q-zero {qsym('grounded')})",
        "compute eval ground",
    )
    for computation, label in (
        (emit, "compute emit"),
        (add_atom, "compute add-atom"),
    ):
        require_one(
            chart,
            sources,
            f"(zero-compute world-0 request q-zero {computation} "
            f"?evidence world-1 (q-succ q-zero) q-empty)",
            label,
        )
    require_one(
        chart,
        sources,
        f"(zero-compute world-0 request q-zero {let} "
        f"?evidence world-0 q-zero {qsym('value')})",
        "compute let",
    )
    request = f"(zero-interact-request request {quote})"
    require_one(
        chart,
        sources,
        f"(zero-produce test-program {request} ?evidence {qsym('value')})",
        "produce interaction",
    )
    produced = (
        "(zero-produced-cons (gslt-produced producer-evidence "
        f"{qsym('value')}) zero-produced-nil)"
    )
    require_one(
        chart,
        sources,
        f"(zero-observe {request} {produced} ?evidence {qsym('value')})",
        "observe interaction",
    )

    print("(MettaZeroInteractSemanticsV1Summary rules=55 witnessed=55)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
