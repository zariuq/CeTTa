#!/usr/bin/env python3
"""Generate exact typed Prime qualification for Popper synthesis-sorted."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
from typing import Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import check_prime_popper_synthesis_manifest as corpus
import generate_prime_hopper_first_order as ground
from prime_iggp_generation import GenerationError, materialize_outputs


TASK = "synthesis-sorted"
REFERENCE_CLAUSES = (
    "f(V0):- tail(V0,V2),tail(V2,V1),empty(V1).",
    "f(V0):- head(V0,V2),tail(V0,V1),head(V1,V3),geq(V3,V2),f(V1).",
)
GEQ_FACT = re.compile(r"^geq\(([0-9]+),([0-9]+)\)\.$")


def parse_geq_facts(path: Path) -> set[tuple[int, int]]:
    facts: set[tuple[int, int]] = set()
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        match = GEQ_FACT.fullmatch(raw_line.strip())
        if match is not None:
            facts.add((int(match.group(1)), int(match.group(2))))
    return facts


def list_values(target: ground.Atom) -> tuple[int, ...]:
    if target.head != "f" or len(target.args) != 1:
        raise GenerationError(f"unexpected sorted target: {target}")
    values = target.args[0]
    if not isinstance(values, ground.ListTerm):
        raise GenerationError(f"sorted target is not a proper list: {target}")
    return tuple(ground.natural(value) for value in values.items)


def evaluate_reference(
    values: tuple[int, ...], facts: set[tuple[int, int]]
) -> tuple[int, tuple[tuple[int, int], ...]]:
    """Return proof count and the successful extensional facts it reaches.

    Popper's authored base clause accepts exactly two elements without
    consulting geq/2.  The recursive clause compares the first pair and then
    recurses on the tail.  This intentionally does not replace the source
    program with a conventional all-adjacent-pairs definition.
    """
    reached: list[tuple[int, int]] = []
    current = values
    while len(current) != 2:
        if len(current) < 2:
            return 0, tuple(reached)
        pair = (current[1], current[0])
        if pair not in facts:
            return 0, tuple(reached)
        reached.append(pair)
        current = current[1:]
    return 1, tuple(reached)


def load_sources(
    snapshot_root: Path, repo: Path
) -> tuple[
    tuple[tuple[str, ground.Atom], ...],
    set[tuple[int, int]],
    dict,
]:
    manifest = corpus.load_manifest(
        repo / "benchmarks/prime/ilp/popper_synthesis_manifest.json"
    )
    corpus.validate_manifest(manifest, repo)
    corpus.verify_snapshot(manifest, snapshot_root)
    entry = next(item for item in manifest["tasks"] if item["name"] == TASK)
    if tuple(entry["reference_clauses"]) != REFERENCE_CLAUSES:
        raise GenerationError("synthesis-sorted: authored program changed")

    task_root = snapshot_root / "examples" / TASK
    facts = parse_geq_facts(task_root / "bk.pl")
    expected_facts = {
        (left, right)
        for left in range(1, 101)
        for right in range(1, left + 1)
    }
    if facts != expected_facts:
        raise GenerationError(
            "synthesis-sorted: extensional geq/2 table is not the exact "
            "1..100 greater-than-or-equal relation"
        )

    examples = ground.parse_examples(task_root / "exs.pl")
    observed = {
        "positive": sum(polarity == "pos" for polarity, _ in examples),
        "negative": sum(polarity == "neg" for polarity, _ in examples),
    }
    if observed != entry["examples"]:
        raise GenerationError("synthesis-sorted: source example count changed")

    reachable: set[tuple[int, int]] = set()
    disagreements = []
    for polarity, target in examples:
        proof_count, reached = evaluate_reference(list_values(target), facts)
        reachable.update(reached)
        if (proof_count > 0) != (polarity == "pos"):
            disagreements.append((polarity, target, proof_count))
    if disagreements:
        raise GenerationError(
            "synthesis-sorted: authored program/example disagreement: "
            f"{disagreements[:1]}"
        )
    return examples, reachable, manifest


def geq_proof_name(left: int, right: int) -> str:
    return f"popper:sorted:geq:proof:{left}-ge-{right}"


def render_types(reachable: Iterable[tuple[int, int]]) -> str:
    facts = sorted(set(reachable))
    lines = [
        "; Proof-relevant image of Popper's extensional geq/2 table and",
        "; the authored recursive synthesis-sorted program.",
        "",
        "(: popper:sorted:geq",
        "  (-> (left : popper:numeral) (right : popper:numeral) (u 0)))",
        "(: popper:sorted:target",
        "  (-> (values : (list popper:numeral)) (u 0)))",
        "",
    ]
    for left, right in facts:
        lines.append(
            f"(: {geq_proof_name(left, right)} "
            f"(popper:sorted:geq popper:n{left} popper:n{right}))"
        )
    lines.extend(
        [
            "",
            "(: popper:sorted:proof:two",
            "  (-> (first : popper:numeral) (second : popper:numeral)",
            "      (first-tail-evidence :",
            "        (popper:list:tail popper:numeral",
            "          (list:cons popper:numeral first",
            "            (list:cons popper:numeral second",
            "              (list:nil popper:numeral)))",
            "          (list:cons popper:numeral second",
            "            (list:nil popper:numeral))))",
            "      (second-tail-evidence :",
            "        (popper:list:tail popper:numeral",
            "          (list:cons popper:numeral second",
            "            (list:nil popper:numeral))",
            "          (list:nil popper:numeral)))",
            "      (empty-evidence :",
            "        (popper:list:empty popper:numeral",
            "          (list:nil popper:numeral)))",
            "      (popper:sorted:target",
            "        (list:cons popper:numeral first",
            "          (list:cons popper:numeral second",
            "            (list:nil popper:numeral))))))",
            "(: popper:sorted:proof:step",
            "  (-> (current : popper:numeral) (next : popper:numeral)",
            "      (rest : (list popper:numeral))",
            "      (head-evidence :",
            "        (popper:list:head popper:numeral",
            "          (list:cons popper:numeral current",
            "            (list:cons popper:numeral next rest)) current))",
            "      (tail-evidence :",
            "        (popper:list:tail popper:numeral",
            "          (list:cons popper:numeral current",
            "            (list:cons popper:numeral next rest))",
            "          (list:cons popper:numeral next rest)))",
            "      (next-head-evidence :",
            "        (popper:list:head popper:numeral",
            "          (list:cons popper:numeral next rest) next))",
            "      (order-evidence : (popper:sorted:geq next current))",
            "      (recursive-evidence :",
            "        (popper:sorted:target",
            "          (list:cons popper:numeral next rest)))",
            "      (popper:sorted:target",
            "        (list:cons popper:numeral current",
            "          (list:cons popper:numeral next rest)))))",
            "",
        ]
    )
    return "\n".join(lines)


def block(lines: Iterable[str]) -> str:
    return "\n".join(f"      {line}" for line in lines)


def render_rules(reachable: Iterable[tuple[int, int]]) -> str:
    blocks = [
        block(
            [
                "(rm-block list-empty popper:sorted:list-empty",
                "  (quote (popper:list:proof:empty $element))",
                "  rm-nil",
                "  (quote (popper:list:empty $element (list:nil $element))))",
            ]
        ),
        block(
            [
                "(rm-block list-head popper:sorted:list-head",
                "  (quote (popper:list:proof:head $element $head $tail))",
                "  rm-nil",
                "  (quote (popper:list:head $element",
                "    (list:cons $element $head $tail) $head)))",
            ]
        ),
        block(
            [
                "(rm-block list-tail popper:sorted:list-tail",
                "  (quote (popper:list:proof:tail $element $head $tail))",
                "  rm-nil",
                "  (quote (popper:list:tail $element",
                "    (list:cons $element $head $tail) $tail)))",
            ]
        ),
    ]
    for left, right in sorted(set(reachable)):
        blocks.append(
            block(
                [
                    f"(rm-block geq-{left}-{right} "
                    f"popper:sorted:geq-{left}-{right}",
                    f"  (quote {geq_proof_name(left, right)})",
                    "  rm-nil",
                    f"  (quote (popper:sorted:geq "
                    f"popper:n{left} popper:n{right})))",
                ]
            )
        )
    blocks.extend(
        [
            block(
                [
                    "(rm-block sorted-two popper:sorted:two",
                    "  (quote (popper:sorted:proof:two $first $second",
                    "    (unquote $first-tail-evidence)",
                    "    (unquote $second-tail-evidence)",
                    "    (unquote $empty-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $first-tail-evidence",
                    "      (quote (popper:list:tail popper:numeral",
                    "        (list:cons popper:numeral $first",
                    "          (list:cons popper:numeral $second",
                    "            (list:nil popper:numeral)))",
                    "        (list:cons popper:numeral $second",
                    "          (list:nil popper:numeral)))))",
                    "    (rm-cons",
                    "      (rm-premise $second-tail-evidence",
                    "        (quote (popper:list:tail popper:numeral",
                    "          (list:cons popper:numeral $second",
                    "            (list:nil popper:numeral))",
                    "          (list:nil popper:numeral))))",
                    "      (rm-cons",
                    "        (rm-premise $empty-evidence",
                    "          (quote (popper:list:empty popper:numeral",
                    "            (list:nil popper:numeral))))",
                    "        rm-nil)))",
                    "  (quote (popper:sorted:target",
                    "    (list:cons popper:numeral $first",
                    "      (list:cons popper:numeral $second",
                    "        (list:nil popper:numeral))))))",
                ]
            ),
            block(
                [
                    "(rm-block sorted-step popper:sorted:step",
                    "  (quote (popper:sorted:proof:step $current $next $rest",
                    "    (unquote $head-evidence) (unquote $tail-evidence)",
                    "    (unquote $next-head-evidence)",
                    "    (unquote $order-evidence)",
                    "    (unquote $recursive-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $head-evidence",
                    "      (quote (popper:list:head popper:numeral",
                    "        (list:cons popper:numeral $current",
                    "          (list:cons popper:numeral $next $rest))",
                    "        $current)))",
                    "    (rm-cons",
                    "      (rm-premise $tail-evidence",
                    "        (quote (popper:list:tail popper:numeral",
                    "          (list:cons popper:numeral $current",
                    "            (list:cons popper:numeral $next $rest))",
                    "          (list:cons popper:numeral $next $rest))))",
                    "      (rm-cons",
                    "        (rm-premise $next-head-evidence",
                    "          (quote (popper:list:head popper:numeral",
                    "            (list:cons popper:numeral $next $rest) $next)))",
                    "        (rm-cons",
                    "          (rm-premise $order-evidence",
                    "            (quote (popper:sorted:geq $next $current)))",
                    "          (rm-cons",
                    "            (rm-premise $recursive-evidence",
                    "              (quote (popper:sorted:target",
                    "                (list:cons popper:numeral $next $rest))))",
                    "            rm-nil)))))",
                    "  (quote (popper:sorted:target",
                    "    (list:cons popper:numeral $current",
                    "      (list:cons popper:numeral $next $rest)))))",
                ]
            ),
        ]
    )
    return "\n".join(
        [
            "; Popper's authored synthesis-sorted program over the exact",
            "; reachable image of its extensional geq/2 background table.",
            "",
            "(= (popper:sorted:package)",
            "  (compile:rule-package popper-synthesis-sorted-v1",
            "    (rm-package",
            *blocks,
            "    )))",
            "",
        ]
    )


def render_list(values: Iterable[int]) -> str:
    rendered = "(list:nil popper:numeral)"
    for value in reversed(tuple(values)):
        rendered = (
            f"(list:cons popper:numeral popper:n{value} {rendered})"
        )
    return rendered


def render_fixture(
    examples: tuple[tuple[str, ground.Atom], ...],
    facts: set[tuple[int, int]],
    manifest: dict,
) -> tuple[str, str, dict[str, int]]:
    entry = next(item for item in manifest["tasks"] if item["name"] == TASK)
    fixture = [
        "; Exact qualification for Popper synthesis-sorted.",
        "; Its authored two-element base clause is preserved literally.",
        "",
        "!(import! &self ../../lib/ilp/prime_native_list_types.metta)",
        "!(import! &self ../../lib/ilp/popper_snapshot_numerals.metta)",
        "!(import! &self ../../lib/ilp/popper_native_list_relations.metta)",
        "!(import! &self ../../lib/ilp/popper_benchmark_classify.metta)",
        "!(import! &self ../../lib/ilp/popper_synthesis_sorted_types.metta)",
        "!(import! &self ../../lib/ilp/popper_synthesis_sorted_rules.metta)",
        "",
        "(= (popper:sorted:classify $name (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (popper:sorted:package) 256 10000000 4096 (quote $goal))",
        "    (popper:classify-occurrences",
        "      $name (quote $goal) $occurrences)))",
        "",
    ]
    expected = ["[()]" for _ in range(6)]
    counts = {
        "source_positive": 0,
        "source_negative": 0,
        "derived": 0,
        "not_derived": 0,
        "proof_occurrences": 0,
        "label_disagreements": 0,
    }
    for ordinal, (polarity, target) in enumerate(examples, 1):
        values = list_values(target)
        proof_count, _ = evaluate_reference(values, facts)
        name = f"popper:sorted:{polarity}-{ordinal}"
        fixture.extend(
            [
                f"!(popper:sorted:classify {name}",
                "  (quote (popper:sorted:target",
                f"    {render_list(values)})))",
            ]
        )
        if proof_count:
            expected.append(
                f"[(popper:case {name} derived 1 (True))]"
            )
            counts["derived"] += 1
            counts["proof_occurrences"] += 1
        else:
            expected.append(
                f"[(popper:case {name} not-derived 0 ())]"
            )
            counts["not_derived"] += 1
        counts[
            "source_positive" if polarity == "pos" else "source_negative"
        ] += 1
        if bool(proof_count) != (polarity == "pos"):
            counts["label_disagreements"] += 1
    if {
        "positive": counts["source_positive"],
        "negative": counts["source_negative"],
    } != entry["examples"]:
        raise GenerationError("synthesis-sorted: rendered source counts changed")
    return "\n".join(fixture) + "\n", "\n".join(expected) + "\n", counts


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument(
        "--types-output",
        type=Path,
        default=repo / "lib/ilp/popper_synthesis_sorted_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/popper_synthesis_sorted_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo
        / "examples/prime/popper_synthesis_sorted_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo
        / "examples/prime/popper_synthesis_sorted_ground_truth.expected",
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    try:
        examples, reachable, manifest = load_sources(args.snapshot_root, repo)
        fixture, expected, counts = render_fixture(
            examples, reachable, manifest
        )
        materialize_outputs(
            (
                (args.types_output, render_types(reachable)),
                (args.rules_output, render_rules(reachable)),
                (args.fixture_output, fixture),
                (args.expected_output, expected),
            ),
            args.check,
        )
    except (GenerationError, corpus.ManifestError, KeyError, OSError) as exc:
        print(f"FAIL: Popper sorted generation: {exc}", file=sys.stderr)
        return 1

    cases = counts["source_positive"] + counts["source_negative"]
    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} Popper sorted "
        f"qualification: {cases} source examples, "
        f"{counts['proof_occurrences']} proof occurrences, "
        f"{len(reachable)} reachable geq facts"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
