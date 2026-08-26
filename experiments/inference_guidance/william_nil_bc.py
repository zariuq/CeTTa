#!/usr/bin/env python3
"""Run WILLIAM-ranked set.mm premises through Nil's authored MeTTa chainer.

WILLIAM selects a fixed-size premise subset from a complete source family.
The selected clauses are then inserted in canonical source order into an
ordinary AtomSpace, and Nil's ordinary ``bc`` equations perform proof search.
The generated programs therefore test premise selection without relying on
AtomSpace declaration order.  Each program can be inspected and replayed
independently; the explicit selection bound is an incompleteness boundary.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
import subprocess
import sys
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from gen_semiring_fixtures import SExpr, parse_forms, sexpr

SCHEMA_SYMBOLS = frozenset({"𝜁", "𝜂", "𝜃", "𝜅", "𝜆", "𝜇", "𝜌", "𝜎", "𝜏", "𝜑", "𝜒", "𝜓"})


class ExperimentError(RuntimeError):
    """An input, execution, or proof-validation failure."""


@dataclass(frozen=True, slots=True)
class RunResult:
    proof: SExpr | None
    stdout: str
    stderr: str
    wall_seconds: float
    timed_out: bool = False


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def order_sha256(order: list[str]) -> str:
    payload = json.dumps(order, ensure_ascii=False, separators=(",", ":")).encode(
        "utf-8"
    )
    return hashlib.sha256(payload).hexdigest()


def parse_named_path(value: str) -> tuple[str, Path]:
    name, separator, raw_path = value.partition("=")
    if not separator or not name or not raw_path:
        raise argparse.ArgumentTypeError("expected NAME=PATH")
    return name, Path(raw_path)


def chainer_source(path: Path) -> str:
    """Extract the reusable relation definitions from Nil's demo file."""
    text = path.read_text(encoding="utf-8")
    marker = ";;;;;;;;;;;;;;;;;;;;;;;;;;\n;; ;;;;;;;;;;;;;;;;;;;; ;;\n;; ;; Knowledge base ;; ;;"
    prefix, separator, _examples = text.partition(marker)
    if not separator or "(= (bc " not in prefix:
        raise ExperimentError("the supplied Nil demo has no recognizable bc section")
    code_lines: list[str] = []
    for line in prefix.splitlines():
        code = line.split(";", 1)[0].rstrip()
        if code:
            code_lines.append(code)
        elif code_lines and code_lines[-1]:
            code_lines.append("")
    code = "\n".join(code_lines).strip() + "\n"
    parse_forms(code)
    return code


def assertion_catalog(path: Path) -> tuple[dict[str, SExpr], dict[str, int]]:
    formulas: dict[str, SExpr] = {}
    indices: dict[str, int] = {}
    for form in parse_forms(path.read_text(encoding="utf-8")):
        if (
            isinstance(form, list)
            and len(form) == 3
            and form[0] == "MkIndexed"
            and isinstance(form[2], list)
            and form[2]
            and form[2][0] in {"MkAxiom", "MkTheorem"}
        ):
            assertion = form[2]
            label = str(assertion[1])
            raw_index = form[1]
            if not isinstance(raw_index, str) or not raw_index.isdecimal():
                raise ExperimentError(f"assertion {label} has a nonnumeric index")
            index = int(raw_index)
            formulas[label] = assertion[-1]
            indices[label] = index
    if not formulas:
        raise ExperimentError("the assertion corpus contains no indexed assertions")
    return formulas, indices


def schematic(value: SExpr) -> SExpr:
    if isinstance(value, list):
        return [schematic(item) for item in value]
    return f"${value}" if value in SCHEMA_SYMBOLS else value


def declaration(label: str, formulas: dict[str, SExpr]) -> SExpr:
    try:
        formula = formulas[label]
    except KeyError as exc:
        raise ExperimentError(f"assertion corpus has no formula for {label}") from exc

    if isinstance(formula, list) and formula and formula[0] == "->":
        premises = formula[1:-1]
        if len(premises) > 5:
            raise ExperimentError(
                f"{label} has {len(premises)} premises; Nil's supplied bc supports five"
            )
        rule_type: SExpr = ["->"]
        assert isinstance(rule_type, list)
        rule_type.extend(
            [":", f"$premise{index}", schematic(premise)]
            for index, premise in enumerate(premises, start=1)
        )
        rule_type.append(schematic(formula[-1]))
    else:
        rule_type = schematic(formula)
    return [":", label, rule_type]


def require_closed_goal(label: str, formula: SExpr) -> None:
    """Reject goals whose proof requires lambda introduction unsupported by bc."""
    if isinstance(formula, list) and formula and formula[0] == "->":
        raise ExperimentError(
            f"{label} has essential hypotheses; Nil's bc can consume that rule "
            "shape but cannot synthesize its outer lambda"
        )


def render_program(
    *,
    chainer: str,
    goal_label: str,
    goal_formula: SExpr,
    order_name: str,
    selected_labels: list[str],
    storage_order: list[str],
    available_candidates: int,
    formulas: dict[str, SExpr],
    depth: int,
) -> str:
    stored_declarations = "\n".join(
        f"!(add-atom &premises {sexpr(declaration(label, formulas))})"
        for label in storage_order
    )
    request = (
        "!(once\n"
        "  (bc &premises\n"
        f"      (fromNumber {depth})\n"
        f"      (: $proof {sexpr(goal_formula)})))"
    )
    header = (
        f"; Goal: {goal_label}\n"
        f"; Premise selector: {order_name}\n"
        f"; Available premises: {available_candidates}\n"
        f"; Selected premises: {len(selected_labels)}\n"
    )
    return (
        header
        + chainer
        + "\n!(bind! &premises (new-space))\n"
        + stored_declarations
        + "\n\n"
        + request
        + "\n"
    )


def parse_proof(stdout: str, goal_formula: SExpr) -> SExpr | None:
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    for line in reversed(lines):
        try:
            forms = parse_forms(line)
        except Exception as exc:
            raise ExperimentError(f"cannot parse runtime result: {line}") from exc
        if len(forms) != 1:
            raise ExperimentError(f"runtime emitted a composite result line: {line}")
        result = forms[0]
        if not isinstance(result, list) or len(result) != 3 or result[0] != ":":
            continue
        if result[2] != goal_formula:
            raise ExperimentError(f"chainer returned the wrong theorem: {sexpr(result[2])}")
        return result[1]
    return None


def run_program(
    *,
    runtime: Path,
    runtime_kind: str,
    profile: str | None,
    fuel: int | None,
    timeout_seconds: float,
    program: str,
) -> RunResult:
    if runtime_kind == "cetta":
        if profile is None or fuel is None:
            raise ExperimentError("CeTTa requires an explicit profile and fuel")
        command = [
            str(runtime),
            "--lang",
            "petta",
            "--profile",
            profile,
            "--fuel",
            str(fuel),
            "--quiet",
            "/dev/stdin",
        ]
    elif runtime_kind == "petta":
        if profile is not None or fuel is not None:
            raise ExperimentError("SWI-PeTTa has no profile or engine-fuel argument")
        command = ["bash", str(runtime), "--silent", "/dev/stdin"]
    else:
        raise ExperimentError(f"unknown runtime kind: {runtime_kind}")
    started = time.perf_counter()
    try:
        completed = subprocess.run(
            command,
            input=program,
            text=True,
            capture_output=True,
            timeout=timeout_seconds,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        def timeout_text(value: str | bytes | None) -> str:
            if value is None:
                return ""
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value

        return RunResult(
            proof=None,
            stdout=timeout_text(exc.stdout),
            stderr=timeout_text(exc.stderr),
            wall_seconds=time.perf_counter() - started,
            timed_out=True,
        )
    wall_seconds = time.perf_counter() - started
    if completed.returncode != 0:
        raise ExperimentError(
            f"{runtime_kind} runtime exited {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    return RunResult(
        proof=None,
        stdout=completed.stdout,
        stderr=completed.stderr,
        wall_seconds=wall_seconds,
    )


def checked_run(
    *,
    runtime: Path,
    runtime_kind: str,
    profile: str | None,
    fuel: int | None,
    timeout_seconds: float,
    program: str,
    goal_formula: SExpr,
) -> RunResult:
    result = run_program(
        runtime=runtime,
        runtime_kind=runtime_kind,
        profile=profile,
        fuel=fuel,
        timeout_seconds=timeout_seconds,
        program=program,
    )
    if result.timed_out:
        return result
    return RunResult(
        proof=parse_proof(result.stdout, goal_formula),
        stdout=result.stdout,
        stderr=result.stderr,
        wall_seconds=result.wall_seconds,
    )


def proof_depth(proof: SExpr) -> int:
    if not isinstance(proof, list):
        return 0
    return 1 + max((proof_depth(item) for item in proof[1:]), default=0)


def proof_symbols(proof: SExpr) -> list[str]:
    """Return theorem labels occurring in a proof application tree."""
    if not isinstance(proof, list) or not proof:
        return [proof] if isinstance(proof, str) else []
    head, *arguments = proof
    labels = [head] if isinstance(head, str) else []
    for argument in arguments:
        labels.extend(proof_symbols(argument))
    return labels


def ranked_order(
    record: dict[str, Any], model: Any, formulas: dict[str, SExpr]
) -> list[str]:
    theory = list(record["theory"])
    scorable = [label for label in theory if label in formulas]
    unscorable = [label for label in theory if label not in formulas]
    scores = model.predict(
        [record["goal_formula"]] * len(scorable),
        [formulas[label] for label in scorable],
    )
    ranked = sorted(
        enumerate(zip(scorable, scores.tolist(), strict=True)),
        key=lambda item: (-item[1][1], item[0]),
    )
    return [pair[0] for _index, pair in ranked] + unscorable


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    runtime = parser.add_mutually_exclusive_group(required=True)
    runtime.add_argument("--cetta", type=Path)
    runtime.add_argument("--petta-run", type=Path)
    parser.add_argument("--profile")
    parser.add_argument("--infcontrol-repo", type=Path, required=True)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--assertion-corpus", type=Path, required=True)
    parser.add_argument("--nil-demo", type=Path, required=True)
    parser.add_argument(
        "--model", type=parse_named_path, action="append", required=True
    )
    parser.add_argument("--goal", action="append", required=True)
    parser.add_argument("--depth", type=int, required=True)
    parser.add_argument("--candidate-limit", type=int, required=True)
    parser.add_argument("--fuel", type=int)
    parser.add_argument("--timeout-seconds", type=float, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    if args.depth < 0 or args.candidate_limit <= 0 or args.timeout_seconds <= 0:
        raise ExperimentError(
            "depth, candidate limit, and timeout must be explicit valid bounds"
        )
    if args.repeats <= 0:
        raise ExperimentError("repeats must be positive")
    if args.cetta is not None:
        if args.profile is None or args.fuel is None or args.fuel <= 0:
            raise ExperimentError("CeTTa requires explicit --profile and positive --fuel")
        runtime_kind = "cetta"
        runtime_path = args.cetta.resolve()
    else:
        if args.profile is not None or args.fuel is not None:
            raise ExperimentError("do not pass CeTTa profile/fuel to SWI-PeTTa")
        runtime_kind = "petta"
        runtime_path = args.petta_run.resolve()

    sys.path.insert(0, str(args.infcontrol_repo.parent.resolve()))
    from infcontrol.dataset import Dataset
    from infcontrol.model import PredictionModel
    from infcontrol.protocol import model_sha256

    dataset = Dataset(args.dataset.resolve())
    formulas, assertion_indices = assertion_catalog(args.assertion_corpus.resolve())
    chainer = chainer_source(args.nil_demo.resolve())
    models = [
        (name, PredictionModel.load(path.resolve()), path.resolve())
        for name, path in args.model
    ]

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    reports: list[dict[str, Any]] = []

    for goal_label in args.goal:
        try:
            record = dataset.get(goal_label)
            family_source = "trace-dataset theory"
        except KeyError:
            try:
                goal_index = assertion_indices[goal_label]
                goal_formula = formulas[goal_label]
            except KeyError as exc:
                raise ExperimentError(f"unknown goal: {goal_label}") from exc
            record = {
                "label": goal_label,
                "goal_formula": goal_formula,
                "theory": [
                    label
                    for label, index in sorted(
                        assertion_indices.items(), key=lambda item: item[1]
                    )
                    if index < goal_index
                ],
            }
            family_source = "assertion-corpus strict index prefix"
        goal_formula = record["goal_formula"]
        require_closed_goal(goal_label, goal_formula)
        orders = [("baseline", list(record["theory"]), None)]
        orders.extend(
            (name, ranked_order(record, model, formulas), (path, model))
            for name, model, path in models
        )

        for order_name, order, model_info in orders:
            if Counter(order) != Counter(record["theory"]):
                raise ExperimentError(
                    f"{goal_label}/{order_name} is not a permutation of the baseline KB"
                )

            selected_labels = order[: args.candidate_limit]
            selected_set = set(selected_labels)
            storage_order = [
                label for label in record["theory"] if label in selected_set
            ]
            if Counter(storage_order) != Counter(selected_labels):
                raise ExperimentError(
                    f"{goal_label}/{order_name} selection changed candidate identity"
                )
            full_program = render_program(
                chainer=chainer,
                goal_label=goal_label,
                goal_formula=goal_formula,
                order_name=order_name,
                selected_labels=selected_labels,
                storage_order=storage_order,
                available_candidates=len(order),
                formulas=formulas,
                depth=args.depth,
            )

            def run(
                program: str,
                *,
                current_goal_formula: SExpr = goal_formula,
            ) -> RunResult:
                return checked_run(
                    runtime=runtime_path,
                    runtime_kind=runtime_kind,
                    profile=args.profile,
                    fuel=args.fuel,
                    timeout_seconds=args.timeout_seconds,
                    program=program,
                    goal_formula=current_goal_formula,
                )

            full_runs = [run(full_program) for _ in range(args.repeats)]

            stem = f"{goal_label}-{order_name}"
            full_path = output_dir / f"{stem}-full.metta"
            full_path.write_text(full_program, encoding="utf-8")
            (output_dir / f"{stem}.out").write_text(
                full_runs[-1].stdout, encoding="utf-8"
            )
            (output_dir / f"{stem}.err").write_text(
                full_runs[-1].stderr, encoding="utf-8"
            )

            proofs = [result.proof for result in full_runs if result.proof is not None]
            proof = proofs[-1] if proofs else None
            model_digest = None
            if model_info is not None:
                model_path, _model = model_info
                model_digest = model_sha256(model_path)
            ranks = {label: index for index, label in enumerate(order, start=1)}
            used_labels = proof_symbols(proof) if proof is not None else []
            used_ranks = {
                label: ranks[label]
                for label in dict.fromkeys(used_labels)
                if label in ranks
            }
            statuses = [
                "timeout" if result.timed_out else "proof" if result.proof is not None else "no-proof"
                for result in full_runs
            ]
            reports.append(
                {
                    "goal": goal_label,
                    "candidate_family_source": family_source,
                    "order": order_name,
                    "available_candidates": len(order),
                    "selected_candidates": len(selected_labels),
                    "selection_is_complete": len(selected_labels) == len(order),
                    "full_ranking_is_baseline_permutation": True,
                    "selected_labels": selected_labels,
                    "order_sha256": order_sha256(order),
                    "run_statuses": statuses,
                    "proof": sexpr(proof) if proof is not None else None,
                    "proof_application_depth": proof_depth(proof) if proof is not None else None,
                    "used_premise_ranks": used_ranks,
                    "maximum_used_premise_rank": max(used_ranks.values(), default=None),
                    "median_seconds": statistics.median(
                        result.wall_seconds for result in full_runs
                    ),
                    "model_sha256": model_digest,
                    "program": full_path.name,
                }
            )
            print(
                f"{goal_label}\t{order_name}\t{len(order)}\t"
                f"{len(selected_labels)}\t"
                f"{','.join(statuses)}\t{sexpr(proof) if proof is not None else '-'}"
            )

    summary = {
        "schema": "william.nil-bc.premise-prefilter.v1",
        "baseline_order": "dataset declaration order",
        "language": "petta",
        "runtime": runtime_kind,
        "profile": args.profile,
        "depth": args.depth,
        "candidate_limit": args.candidate_limit,
        "fuel": args.fuel,
        "harness_timeout_seconds": args.timeout_seconds,
        "repeats": args.repeats,
        "dataset_sha256": sha256_file(args.dataset.resolve()),
        "assertion_corpus_sha256": sha256_file(args.assertion_corpus.resolve()),
        "nil_demo_sha256": sha256_file(args.nil_demo.resolve()),
        "results": reports,
    }
    (output_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
