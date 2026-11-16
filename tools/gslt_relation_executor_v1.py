"""Run authored relation tests through the chart or generated PeTTa equations.

The PeTTa route calls the production compiler and runtime. It preserves answer
occurrences; the caller decides which order its declared observation requires.
"""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import subprocess
import sys

import gslt2parse_schema_v1 as sx

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests" / "support"))
from check_transform_swi_differential import read_forms, write_form  # noqa: E402


class ExecutorFailure(RuntimeError):
    pass


def completed(command: list[str], timeout: int, env=None) -> str:
    try:
        run = subprocess.run(command, text=True, capture_output=True,
                             timeout=timeout, env=env)
    except subprocess.TimeoutExpired as error:
        raise ExecutorFailure(f"relation execution unfinished after {timeout}s") from error
    if run.returncode != 0:
        raise ExecutorFailure(
            f"relation executor exited {run.returncode}:\n{run.stdout}{run.stderr}")
    return run.stdout.strip()


def answer_terms(rendered: list[str]) -> list[tuple[sx.SExpr, ...]]:
    answers = []
    for text in rendered:
        forms = sx.parse_sexprs(text, source="relation answer")
        if len(forms) != 1 or not isinstance(forms[0], tuple):
            raise ExecutorFailure(f"malformed relation answer: {text}")
        answers.append(forms[0])
    return answers


class PeTTaExecutor:
    def __init__(self, compiler: Path, runtime: Path, work: Path):
        self.compiler = compiler.resolve(strict=True)
        self.runtime = runtime.resolve(strict=True)
        self.work = work.resolve()
        self.work.mkdir(parents=True, exist_ok=True)
        self.compiler_identity = sha256(self.compiler.read_bytes()).hexdigest()
        self.programs: dict[tuple[tuple[str, str], ...], Path] = {}
        self.queries = 0

    def program(self, sources: tuple[Path, ...]) -> Path:
        identity = tuple((str(source.resolve()), sha256(source.read_bytes()).hexdigest())
                         for source in sources)
        if identity not in self.programs:
            key = sha256(json.dumps([self.compiler_identity, identity]).encode()).hexdigest()
            output = self.work / (key + ".program.metta")
            command = [str(self.compiler), "petta-direct"]
            for source in sources:
                command += ["--source", str(source.resolve())]
            completed(command + ["--out", str(output)], 60)
            self.programs[identity] = output
        return self.programs[identity]

    def answers(self, sources: tuple[Path, ...], query: str,
                timeout: int) -> list[tuple[sx.SExpr, ...]]:
        parsed = read_forms(query)
        if (len(parsed) != 1 or not isinstance(parsed[0], list) or not parsed[0]
                or not isinstance(parsed[0][0], str)):
            raise ExecutorFailure("expected one relation query")
        goal = parsed[0]
        relation = goal[0]
        variables: dict[str, str] = {}
        todo = [goal]
        while todo:
            items = todo.pop()
            for index, item in enumerate(items):
                if isinstance(item, list):
                    todo.append(item)
                elif item.startswith("?"):
                    items[index] = variables.setdefault(item, f"$query_v1_{len(variables)}")
        # Generic relation entries take one held Atom. A quote here would be
        # another source constructor, rather than activation of the goal.
        source = f"!(collapse (gslt:{relation} {write_form(goal)}))\n"
        key = sha256(source.encode()).hexdigest()
        path = self.work / (key + ".query.metta")
        path.write_text(source)
        output = completed([sys.executable, str(ROOT / "tests/support/run_gslt_program.py"),
                            str(self.runtime), "--lang", "petta",
                            str(self.program(sources)), str(path)], timeout,
                           dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1"))
        forms = read_forms(output)
        if len(forms) != 1 or not isinstance(forms[0], list):
            raise ExecutorFailure(f"runtime did not return one complete answer bag: {output}")
        self.queries += 1
        return answer_terms([write_form(term) for term in forms[0]])


def add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--petta-compiler", type=Path)
    parser.add_argument("--petta-runtime", type=Path)
    parser.add_argument("--petta-work", type=Path)


def select(arguments: argparse.Namespace) -> Path | PeTTaExecutor:
    selected = (arguments.petta_compiler, arguments.petta_runtime, arguments.petta_work)
    if any(selected):
        if not all(selected):
            raise ExecutorFailure("the PeTTa route requires compiler, runtime and work directory")
        return PeTTaExecutor(*selected)
    if arguments.chart is None:
        raise ExecutorFailure("select a chart executable or the PeTTa route")
    return arguments.chart.resolve(strict=True)


def command_arguments(arguments: argparse.Namespace) -> list[str]:
    selected = select(arguments)
    if isinstance(selected, PeTTaExecutor):
        return ["--petta-compiler", str(selected.compiler),
                "--petta-runtime", str(selected.runtime),
                "--petta-work", str(selected.work)]
    return ["--chart", str(selected)]


def answers(executor: Path | PeTTaExecutor, sources: tuple[Path, ...], query: str,
            *, timeout: int = 10, max_rounds: int = 100000) -> list[tuple[sx.SExpr, ...]]:
    if isinstance(executor, PeTTaExecutor):
        return executor.answers(sources, query, timeout)
    raw = completed([str(executor), *(str(source) for source in sources),
                     "--query-text", query, "--timeout", str(timeout),
                     "--max-rounds", str(max_rounds)], timeout + 5)
    try:
        receipt = json.loads(raw)
    except json.JSONDecodeError as error:
        raise ExecutorFailure(f"chart emitted malformed JSON: {raw}") from error
    terms = receipt.get("terms")
    if not isinstance(terms, list) or not all(isinstance(term, str) for term in terms):
        raise ExecutorFailure(f"chart receipt omits answer terms: {receipt}")
    outcome = receipt.get("outcome")
    if outcome not in ("NoAnswer", "Unique", "Ambiguous"):
        raise ExecutorFailure(f"chart relation query unfinished: {outcome}")
    if receipt.get("answers") != len(terms) or (
            outcome == "NoAnswer" and terms or outcome == "Unique" and len(terms) != 1
            or outcome == "Ambiguous" and len(terms) < 2):
        raise ExecutorFailure(f"inconsistent chart answer receipt: {receipt}")
    return answer_terms(terms)
