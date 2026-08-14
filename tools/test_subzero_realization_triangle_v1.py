#!/usr/bin/env python3
"""Cross-check three independent executors of one authored Subzero presentation."""

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


def equality(pattern: str, template: str) -> str:
    return qapp(qsym("="), pattern, template)


def source_program(*payloads: str) -> str:
    result = "subzero-program-nil"
    for index, payload in reversed(tuple(enumerate(payloads))):
        result = (
            f"(subzero-program-cons (source-occurrence {index}) "
            f"{payload} {result})"
        )
    return result


def chart_query(program: str) -> str:
    return f"(subzero-evaluate {program} ?occurrence ?result)"


def chart_results(
    chart: Path, core: Path, observation: Path, query: str
) -> list[str]:
    raw = run(
        [
            str(chart),
            str(core),
            str(observation),
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
    results: list[str] = []
    for rendered in terms:
        if not isinstance(rendered, str):
            raise GateFailure(f"chart answer is not text: {rendered!r}")
        forms = sx.parse_sexprs(rendered, source="chart answer")
        if (
            len(forms) != 1
            or not isinstance(forms[0], tuple)
            or len(forms[0]) != 4
            or not isinstance(forms[0][0], sx.Symbol)
            or forms[0][0].text != "subzero-evaluate"
        ):
            raise GateFailure(f"chart returned a malformed entry answer: {rendered}")
        results.append(sx.render(forms[0][3]))
    return sorted(results)


def cli_results(cetta: Path, realization: str, program: str) -> str:
    return run(
        [
            str(cetta),
            "--lang",
            "subzero",
            "--gslt-realization",
            realization,
            "-e",
            program,
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--chart", type=Path, required=True)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--public-observation", type=Path, required=True)
    parser.add_argument("--compiled-runtime", type=Path, required=True)
    arguments = parser.parse_args()
    cetta = arguments.cetta.resolve()
    chart = arguments.chart.resolve()
    core = arguments.core.resolve()
    public_observation = arguments.public_observation.resolve()

    compiled_source = arguments.compiled_runtime.read_text(encoding="utf-8")
    forbidden = (
        "cetta_gslt_horn_query",
        "rename_vars(",
        "parse_metta_text(",
        "horn_solve(",
    )
    leaked = [name for name in forbidden if name in compiled_source]
    if leaked:
        raise GateFailure(
            "compiled executor re-entered reference machinery: "
            + ", ".join(leaked)
        )

    duplicate = source_program(
        equality(qsym("a"), qsym("b")),
        equality(qsym("a"), qsym("b")),
        qapp(qsym("!"), qsym("a")),
    )
    chart_duplicate = chart_results(
        chart,
        core,
        public_observation,
        chart_query(duplicate),
    )
    if chart_duplicate != [qsym("b"), qsym("b")]:
        raise GateFailure(f"chart changed duplicate occurrences: {chart_duplicate}")
    syntax_duplicate = "(= a b) (= a b) (! a)"
    horn = cli_results(cetta, "horn-reference", syntax_duplicate)
    compiled = cli_results(
        cetta, "compiled-worklist", syntax_duplicate
    )
    if horn != "[b, b]" or compiled != horn:
        raise GateFailure(
            f"public duplicate bags diverged: horn={horn!r} compiled={compiled!r}"
        )

    contextual = source_program(
        equality(qsym("a"), qsym("b")),
        qapp(qsym("!"), qapp(qsym("wrap"), qsym("a"))),
    )
    chart_contextual = chart_results(
        chart,
        core,
        public_observation,
        chart_query(contextual),
    )
    expected_contextual = qapp(qsym("wrap"), qsym("b"))
    if chart_contextual != [expected_contextual]:
        raise GateFailure(
            f"chart changed contextual rewriting: {chart_contextual}"
        )
    syntax_contextual = "(= a b) (! (wrap a))"
    horn = cli_results(cetta, "horn-reference", syntax_contextual)
    compiled = cli_results(
        cetta, "compiled-worklist", syntax_contextual
    )
    if horn != "[(wrap b)]" or compiled != horn:
        raise GateFailure(
            f"public contextual results diverged: horn={horn!r} "
            f"compiled={compiled!r}"
        )

    repeated = source_program(
        equality(qapp("(q-var q-zero)", "(q-var q-zero)"), qsym("same")),
        qapp(qsym("!"), qapp(qsym("a"), qsym("b"))),
    )
    if chart_results(
        chart,
        core,
        public_observation,
        chart_query(repeated),
    ):
        raise GateFailure("chart accepted an inconsistent repeated variable")
    syntax_repeated = "(= ($x $x) same) (! (a b))"
    horn = cli_results(cetta, "horn-reference", syntax_repeated)
    compiled = cli_results(
        cetta, "compiled-worklist", syntax_repeated
    )
    if horn != "[]" or compiled != horn:
        raise GateFailure(
            f"negative repeated-variable results diverged: "
            f"horn={horn!r} compiled={compiled!r}"
        )

    print(
        "(SubzeroRealizationTriangleV1Summary "
        "native-chart=1 horn-reference=1 compiled-worklist=1 "
        "duplicate-bag=2 contextual=1 repeated-negative=1)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError, sx.SchemaError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
