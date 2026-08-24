#!/usr/bin/env python3
"""Generate exact typed Prime qualification for two recursive Popper tasks."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
from typing import Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import check_prime_popper_synthesis_manifest as corpus
import generate_prime_hopper_first_order as ground
from prime_iggp_generation import GenerationError, materialize_outputs


TASKS = ("synthesis-alleven", "synthesis-dropk")
REFERENCE_CLAUSES = {
    "synthesis-alleven": (
        "f(V0):- empty(V0).",
        "f(V0):- tail(V0,V2),head(V0,V1),even(V1),f(V2).",
    ),
    "synthesis-dropk": (
        "f(V0,V1,V2):- tail(V0,V2),one(V1).",
        "f(V0,V1,V2):- decrement(V1,V4),f(V0,V4,V3),tail(V3,V2).",
    ),
}
REQUIRED_BACKGROUND = {
    "synthesis-alleven": (
        "tail([_|T],T).",
        "head([H|_],H).",
        "empty([]).",
        "even(A):-0isAmod2.",
    ),
    "synthesis-dropk": (
        "decrement(A,B):-succ(B,A).",
        "tail([_|T],T).",
        "one(1).",
    ),
}


def compact_source(path: Path) -> str:
    return "".join(
        "".join(line.split("%", 1)[0].split())
        for line in path.read_text(encoding="utf-8").splitlines()
    )


def list_naturals(term: ground.Term) -> tuple[int, ...]:
    if not isinstance(term, ground.ListTerm):
        raise GenerationError(f"expected a proper natural list, found {term}")
    return tuple(ground.natural(item) for item in term.items)


def proof_count(task: str, target: ground.Atom) -> int:
    if task == "synthesis-alleven" and len(target.args) == 1:
        return int(all(value % 2 == 0 for value in list_naturals(target.args[0])))
    if task == "synthesis-dropk" and len(target.args) == 3:
        source = list_naturals(target.args[0])
        amount = ground.natural(target.args[1])
        result = list_naturals(target.args[2])
        return int(amount >= 1 and amount <= len(source) and source[amount:] == result)
    raise GenerationError(f"unsupported target for {task}: {target}")


def render_nat(value: int) -> str:
    if value < 0:
        raise GenerationError(f"negative natural {value}")
    rendered = "popper:nat:zero"
    for _ in range(value):
        rendered = f"(popper:nat:succ {rendered})"
    return rendered


def render_list(values: Iterable[int]) -> str:
    rendered = "(list:nil popper:nat)"
    for value in reversed(tuple(values)):
        rendered = (
            f"(list:cons popper:nat {render_nat(value)} {rendered})"
        )
    return rendered


def target_term(task: str, target: ground.Atom) -> str:
    if task == "synthesis-alleven":
        return f"(popper:all-even:f {render_list(list_naturals(target.args[0]))})"
    if task == "synthesis-dropk":
        return (
            f"(popper:drop-k:f {render_list(list_naturals(target.args[0]))} "
            f"{render_nat(ground.natural(target.args[1]))} "
            f"{render_list(list_naturals(target.args[2]))})"
        )
    raise GenerationError(f"unsupported task {task}")


def load_sources(
    snapshot_root: Path, repo: Path
) -> tuple[dict[str, tuple[tuple[str, ground.Atom], ...]], dict]:
    manifest = corpus.load_manifest(
        repo / "benchmarks/prime/ilp/popper_synthesis_manifest.json"
    )
    corpus.validate_manifest(manifest, repo)
    corpus.verify_snapshot(manifest, snapshot_root)
    entries = {entry["name"]: entry for entry in manifest["tasks"]}
    examples: dict[str, tuple[tuple[str, ground.Atom], ...]] = {}
    for task in TASKS:
        entry = entries[task]
        if tuple(entry["reference_clauses"]) != REFERENCE_CLAUSES[task]:
            raise GenerationError(f"{task}: authored reference program changed")
        task_root = snapshot_root / "examples" / task
        background = compact_source(task_root / "bk.pl")
        for required in REQUIRED_BACKGROUND[task]:
            if required not in background:
                raise GenerationError(
                    f"{task}: required background relation changed: {required}"
                )
        parsed = ground.parse_examples(task_root / "exs.pl")
        observed = {
            "positive": sum(polarity == "pos" for polarity, _ in parsed),
            "negative": sum(polarity == "neg" for polarity, _ in parsed),
        }
        if observed != entry["examples"]:
            raise GenerationError(f"{task}: source example count changed")
        disagreements = [
            (polarity, target, proof_count(task, target))
            for polarity, target in parsed
            if (proof_count(task, target) > 0) != (polarity == "pos")
        ]
        if disagreements:
            raise GenerationError(
                f"{task}: program/example disagreement: {disagreements[:1]}"
            )
        examples[task] = parsed
    return examples, manifest


def render_types() -> str:
    return """; Structural naturals and recursive evidence for two Popper tasks.
; Parity and decrement are defined for every constructor-shaped natural, not
; only for the finite values occurring in the benchmark corpus.

(: popper:nat (u 0))
(: popper:nat:zero popper:nat)
(: popper:nat:succ (-> (before : popper:nat) popper:nat))

(: popper:nat:even (-> (value : popper:nat) (u 0)))
(: popper:nat:even:zero
  (popper:nat:even popper:nat:zero))
(: popper:nat:even:step
  (-> (before : popper:nat)
      (before-evidence : (popper:nat:even before))
      (popper:nat:even
        (popper:nat:succ (popper:nat:succ before)))))

(: popper:nat:one (-> (value : popper:nat) (u 0)))
(: popper:nat:one:evidence
  (popper:nat:one (popper:nat:succ popper:nat:zero)))
(: popper:nat:decrement
  (-> (current : popper:nat) (previous : popper:nat) (u 0)))
(: popper:nat:decrement:evidence
  (-> (previous : popper:nat)
      (popper:nat:decrement (popper:nat:succ previous) previous)))

(: popper:all-even:f
  (-> (values : (list popper:nat)) (u 0)))
(: popper:all-even:proof:empty
  (-> (empty-evidence :
        (rel:list:empty popper:nat (list:nil popper:nat)))
      (popper:all-even:f (list:nil popper:nat))))
(: popper:all-even:proof:step
  (-> (head : popper:nat) (tail : (list popper:nat))
      (head-evidence :
        (rel:list:head popper:nat
          (list:cons popper:nat head tail) head))
      (tail-evidence :
        (rel:list:tail popper:nat
          (list:cons popper:nat head tail) tail))
      (even-evidence : (popper:nat:even head))
      (recursive-evidence : (popper:all-even:f tail))
      (popper:all-even:f (list:cons popper:nat head tail))))

(: popper:drop-k:f
  (-> (source : (list popper:nat)) (amount : popper:nat)
      (result : (list popper:nat)) (u 0)))
(: popper:drop-k:proof:one
  (-> (source : (list popper:nat)) (result : (list popper:nat))
      (one-evidence :
        (popper:nat:one (popper:nat:succ popper:nat:zero)))
      (tail-evidence : (rel:list:tail popper:nat source result))
      (popper:drop-k:f source
        (popper:nat:succ popper:nat:zero) result)))
(: popper:drop-k:proof:step
  (-> (source : (list popper:nat))
      (amount : popper:nat) (previous : popper:nat)
      (intermediate : (list popper:nat))
      (result : (list popper:nat))
      (decrement-evidence :
        (popper:nat:decrement amount previous))
      (recursive-evidence :
        (popper:drop-k:f source previous intermediate))
      (tail-evidence :
        (rel:list:tail popper:nat intermediate result))
      (popper:drop-k:f source amount result)))
"""


def block(lines: Iterable[str]) -> str:
    return "\n".join(f"      {line}" for line in lines)


def render_rules() -> str:
    blocks = [
        block(
            [
                "(rm-block list-empty popper:recursive:list-empty",
                "  (quote (rel:list:empty-proof $element))",
                "  rm-nil",
                "  (quote (rel:list:empty $element (list:nil $element))))",
            ]
        ),
        block(
            [
                "(rm-block list-head popper:recursive:list-head",
                "  (quote (rel:list:head-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:head $element",
                "    (list:cons $element $head $tail) $head)))",
            ]
        ),
        block(
            [
                "(rm-block list-tail popper:recursive:list-tail",
                "  (quote (rel:list:tail-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:tail $element",
                "    (list:cons $element $head $tail) $tail)))",
            ]
        ),
        block(
            [
                "(rm-block even-zero popper:recursive:even-zero",
                "  (quote popper:nat:even:zero)",
                "  rm-nil",
                "  (quote (popper:nat:even popper:nat:zero)))",
            ]
        ),
        block(
            [
                "(rm-block even-step popper:recursive:even-step",
                "  (quote (popper:nat:even:step",
                "    $before (unquote $before-evidence)))",
                "  (rm-cons",
                "    (rm-premise $before-evidence",
                "      (quote (popper:nat:even $before)))",
                "    rm-nil)",
                "  (quote (popper:nat:even",
                "    (popper:nat:succ (popper:nat:succ $before)))))",
            ]
        ),
        block(
            [
                "(rm-block one popper:recursive:one",
                "  (quote popper:nat:one:evidence)",
                "  rm-nil",
                "  (quote (popper:nat:one",
                "    (popper:nat:succ popper:nat:zero))))",
            ]
        ),
        block(
            [
                "(rm-block decrement popper:recursive:decrement",
                "  (quote (popper:nat:decrement:evidence $previous))",
                "  rm-nil",
                "  (quote (popper:nat:decrement",
                "    (popper:nat:succ $previous) $previous)))",
            ]
        ),
        block(
            [
                "(rm-block all-even-empty popper:recursive:all-even-empty",
                "  (quote (popper:all-even:proof:empty",
                "    (unquote $empty-evidence)))",
                "  (rm-cons",
                "    (rm-premise $empty-evidence",
                "      (quote (rel:list:empty popper:nat",
                "        (list:nil popper:nat))))",
                "    rm-nil)",
                "  (quote (popper:all-even:f (list:nil popper:nat))))",
            ]
        ),
        block(
            [
                "(rm-block all-even-step popper:recursive:all-even-step",
                "  (quote (popper:all-even:proof:step $head $tail",
                "    (unquote $head-evidence) (unquote $tail-evidence)",
                "    (unquote $even-evidence)",
                "    (unquote $recursive-evidence)))",
                "  (rm-cons",
                "    (rm-premise $head-evidence",
                "      (quote (rel:list:head popper:nat",
                "        (list:cons popper:nat $head $tail) $head)))",
                "    (rm-cons",
                "      (rm-premise $tail-evidence",
                "        (quote (rel:list:tail popper:nat",
                "          (list:cons popper:nat $head $tail) $tail)))",
                "      (rm-cons",
                "        (rm-premise $even-evidence",
                "          (quote (popper:nat:even $head)))",
                "        (rm-cons",
                "          (rm-premise $recursive-evidence",
                "            (quote (popper:all-even:f $tail)))",
                "          rm-nil))))",
                "  (quote (popper:all-even:f",
                "    (list:cons popper:nat $head $tail))))",
            ]
        ),
        block(
            [
                "(rm-block drop-k-one popper:recursive:drop-k-one",
                "  (quote (popper:drop-k:proof:one $source $result",
                "    (unquote $one-evidence) (unquote $tail-evidence)))",
                "  (rm-cons",
                "    (rm-premise $one-evidence",
                "      (quote (popper:nat:one",
                "        (popper:nat:succ popper:nat:zero))))",
                "    (rm-cons",
                "      (rm-premise $tail-evidence",
                "        (quote (rel:list:tail popper:nat $source $result)))",
                "      rm-nil))",
                "  (quote (popper:drop-k:f $source",
                "    (popper:nat:succ popper:nat:zero) $result)))",
            ]
        ),
        block(
            [
                "(rm-block drop-k-step popper:recursive:drop-k-step",
                "  (quote (popper:drop-k:proof:step",
                "    $source $amount $previous $intermediate $result",
                "    (unquote $decrement-evidence)",
                "    (unquote $recursive-evidence)",
                "    (unquote $tail-evidence)))",
                "  (rm-cons",
                "    (rm-premise $decrement-evidence",
                "      (quote (popper:nat:decrement $amount $previous)))",
                "    (rm-cons",
                "      (rm-premise $recursive-evidence",
                "        (quote (popper:drop-k:f",
                "          $source $previous $intermediate)))",
                "      (rm-cons",
                "        (rm-premise $tail-evidence",
                "          (quote (rel:list:tail popper:nat",
                "            $intermediate $result)))",
                "        rm-nil)))",
                "  (quote (popper:drop-k:f $source $amount $result)))",
            ]
        ),
    ]
    return "\n".join(
        [
            "; Proof-producing recursive arithmetic programs.",
            "; The rule machine searches; Prime checks every emitted proof.",
            "",
            "(= (popper:recursive-arithmetic:package)",
            "  (compile:rule-package popper-recursive-arithmetic-v1",
            "    (rm-package",
            *blocks,
            "    )))",
            "",
        ]
    )


def render_fixture(
    examples: dict[str, tuple[tuple[str, ground.Atom], ...]], manifest: dict
) -> tuple[str, str, dict[str, dict[str, int]]]:
    entries = {entry["name"]: entry for entry in manifest["tasks"]}
    fixture = [
        "; Exact qualification for recursive parity and drop-k.",
        "; Complete non-derivation remains distinct from type refutation.",
        "",
        "!(import! &self ../../lib/ilp/prime_native_list_types.metta)",
        "!(import! &self ../../lib/ilp/prime_relational_combinators_types.metta)",
        "!(import! &self ../../lib/ilp/popper_synthesis_recursive_arithmetic_types.metta)",
        "!(import! &self ../../lib/ilp/popper_synthesis_recursive_arithmetic_rules.metta)",
        "",
        "(= (popper:recursive:proof-checks (quote $goal) $occurrences)",
        "  (collapse",
        "    (let (occurrence $proof-data) (superpose $occurrences)",
        "      (type:check (unquote $proof-data) $goal))))",
        "",
        "(= (popper:recursive:classify $name (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (popper:recursive-arithmetic:package)",
        "      256 10000000 4096 (quote $goal))",
        "    (let $count (- (size-atom $occurrences) 1)",
        "      (let $checks",
        "        (popper:recursive:proof-checks (quote $goal) $occurrences)",
        "        (if (== $count 0)",
        "            (popper:recursive:case $name not-derived $count $checks)",
        "            (popper:recursive:case $name derived $count $checks))))))",
        "",
    ]
    expected = ["[()]" for _ in range(4)]
    counts: dict[str, dict[str, int]] = {}
    for task in TASKS:
        positive = 0
        negative = 0
        derived = 0
        not_derived = 0
        occurrences = 0
        short = task.removeprefix("synthesis-")
        for ordinal, (polarity, target) in enumerate(examples[task], 1):
            count = proof_count(task, target)
            name = f"popper:{short}:{polarity}-{ordinal}"
            fixture.extend(
                [
                    f"!(popper:recursive:classify {name}",
                    f"  (quote {target_term(task, target)}))",
                ]
            )
            checks = " ".join("True" for _ in range(count))
            if count:
                expected.append(
                    f"[(popper:recursive:case {name} derived "
                    f"{count} ({checks}))]"
                )
                derived += 1
                occurrences += count
            else:
                expected.append(
                    f"[(popper:recursive:case {name} not-derived 0 ())]"
                )
                not_derived += 1
            positive += int(polarity == "pos")
            negative += int(polarity == "neg")
        if {"positive": positive, "negative": negative} != entries[task]["examples"]:
            raise GenerationError(f"{task}: source counts changed")
        counts[task] = {
            "source_positive": positive,
            "source_negative": negative,
            "derived": derived,
            "not_derived": not_derived,
            "proof_occurrences": occurrences,
            "label_disagreements": 0,
        }
    return "\n".join(fixture) + "\n", "\n".join(expected) + "\n", counts


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument(
        "--types-output",
        type=Path,
        default=repo / "lib/ilp/popper_synthesis_recursive_arithmetic_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/popper_synthesis_recursive_arithmetic_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo
        / "examples/prime/popper_synthesis_recursive_arithmetic_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo
        / "examples/prime/popper_synthesis_recursive_arithmetic_ground_truth.expected",
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    try:
        examples, manifest = load_sources(args.snapshot_root, repo)
        fixture, expected, counts = render_fixture(examples, manifest)
        materialize_outputs(
            (
                (args.types_output, render_types()),
                (args.rules_output, render_rules()),
                (args.fixture_output, fixture),
                (args.expected_output, expected),
            ),
            args.check,
        )
    except (GenerationError, corpus.ManifestError, KeyError, OSError) as exc:
        print(f"FAIL: Popper recursive-arithmetic generation: {exc}", file=sys.stderr)
        return 1

    cases = sum(
        item["source_positive"] + item["source_negative"]
        for item in counts.values()
    )
    proofs = sum(item["proof_occurrences"] for item in counts.values())
    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        f"Popper recursive-arithmetic qualification: {len(TASKS)} tasks, "
        f"{cases} source examples, {proofs} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
