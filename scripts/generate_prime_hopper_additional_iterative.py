#!/usr/bin/env python3
"""Generate typed Prime qualification for Hopper's iterative task trio."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
from typing import Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import check_prime_hopper_table1_manifest as corpus
import generate_prime_hopper_first_order as base
import generate_prime_hopper_higher_order as higher
from prime_iggp_generation import GenerationError, fact_block, materialize_outputs


TASKS = ("rotateN", "dropLastK", "addN")
SOURCE_VARIANT = "ho"
PROGRAMS = {
    "rotateN": {
        "ho": (
            "f(A,B,C):-ite_a(B,A,C).",
            "ite_p_a(A,B):-tail(A,C),head(A,D),app(C,D,B).",
        ),
        "ho-opt": (
            "f(A,B,C):-ite_a(B,A,C).",
            "ite_p_a(A,B):-tail(A,C),head(A,D),app(C,D,B).",
        ),
    },
    "dropLastK": {
        "ho": (
            "f(A,B,C):-eqs(C,B),z(A).",
            "f(A,B,C):-pred(A,E),map(map_p_a,B,D),f(E,D,C).",
            "map_p_a(A,B):-reverse(A,D),tail(D,C),reverse(C,B).",
        ),
        "ho-opt": (
            "f(A,B,C):-eqs(C,B),z(A).",
            "f(A,B,C):-pred(A,E),map(map_p_a,B,D),f(E,D,C).",
            "map_p_a(A,B):-reverse(A,D),tail(D,C),reverse(C,B).",
        ),
    },
    "addN": {
        "ho": (
            "map_p_a(A,B):-suc(A,B).",
            "caseint_q_a(A,B,C):-map_a(B,D),caseint_a(A,D,C).",
            "f(A,B,C):-caseint_a(A,B,C).",
            "caseint_p_a(A,B):-eq(A,B).",
        ),
        "ho-opt": (
            "map_p_a(A,B):-suc(A,B).",
            "caseint_q_a(A,B,C):-map_a(B,D),caseint_a(A,D,C).",
            "f(A,B,C):-caseint_a(A,B,C).",
            "caseint_p_a(A,B):-eqs(A,B).",
        ),
    },
}
EXPECTED_TARGETS = {
    "rotateN": ("integer", "list", "list"),
    "dropLastK": ("integer", "dlist", "dlist"),
    "addN": ("integer", "list", "list"),
}
MAX_NAT = 17


def target_term(task: str, target: base.Atom) -> str:
    if len(target.args) != 3:
        raise GenerationError(f"{task}: target arity changed")
    count = base.nat_name(base.natural(target.args[0]))
    if task == "rotateN":
        source = base.render_list(base.list_items(target.args[1]), "hopper:nat")
        result = base.render_list(base.list_items(target.args[2]), "hopper:nat")
        return f"(hopper:rotate-n:f {count} {source} {result})"
    if task == "dropLastK":
        source = higher.render_nested_list(higher.list_of_lists(target.args[1]))
        result = higher.render_nested_list(higher.list_of_lists(target.args[2]))
        return f"(hopper:drop-last-k:f {count} {source} {result})"
    if task == "addN":
        source = base.render_list(base.list_items(target.args[1]), "hopper:nat")
        result = base.render_list(base.list_items(target.args[2]), "hopper:nat")
        return f"(hopper:add-n:f {count} {source} {result})"
    raise GenerationError(f"unsupported iterative Hopper task {task}")


def proof_count(task: str, target: base.Atom) -> int:
    if len(target.args) != 3:
        raise GenerationError(f"{task}: target arity changed")
    count = base.natural(target.args[0])
    if task == "rotateN":
        source = base.list_items(target.args[1])
        expected = base.list_items(target.args[2])
        if not source:
            return int(count == 0 and not expected)
        offset = count % len(source)
        return int(source[offset:] + source[:offset] == expected)
    if task == "dropLastK":
        source = higher.list_of_lists(target.args[1])
        expected = higher.list_of_lists(target.args[2])
        if any(count > len(inner) for inner in source):
            return 0
        return int(tuple(inner[: len(inner) - count] for inner in source) == expected)
    if task == "addN":
        source = tuple(int(value) for value in base.list_items(target.args[1]))
        expected = tuple(int(value) for value in base.list_items(target.args[2]))
        return int(tuple(value + count for value in source) == expected)
    raise GenerationError(f"unsupported iterative Hopper task {task}")


def load_sources(
    snapshot_root: Path, repo: Path
) -> tuple[dict[str, tuple[tuple[str, base.Atom], ...]], dict]:
    manifest = corpus.load_manifest(
        repo / "benchmarks/prime/ilp/hopper_table1_manifest.json"
    )
    corpus.validate_manifest(manifest, repo)
    corpus.verify_snapshot(manifest, snapshot_root)
    entries = {entry["name"]: entry for entry in manifest["tasks"]}
    examples: dict[str, tuple[tuple[str, base.Atom], ...]] = {}
    for task in TASKS:
        entry = entries[task]
        variants = entry["variants"]
        selected = variants[SOURCE_VARIANT]
        if tuple(selected["target"]["types"]) != EXPECTED_TARGETS[task]:
            raise GenerationError(f"{task}: target type profile changed")
        for variant in ("ho", "ho-opt"):
            best = variants[variant]["best_program"]
            if best is None or tuple(best["clauses"]) != PROGRAMS[task][variant]:
                raise GenerationError(
                    f"{task}/{variant}: authored best program changed"
                )
        if variants["fo"]["best_program"] is not None:
            raise GenerationError(
                f"{task}/fo: an authored best program unexpectedly appeared"
            )
        example_digests = {
            variants[variant]["files"]["exs.pl"]
            for variant in ("fo", "ho", "ho-opt")
        }
        if len(example_digests) != 1:
            raise GenerationError(f"{task}: FO/HO example corpus drift")
        path = (
            snapshot_root
            / "examples"
            / task
            / selected["path"]
            / "exs.pl"
        )
        parsed = base.parse_examples(path)
        observed = {
            "positive": sum(polarity == "pos" for polarity, _ in parsed),
            "negative": sum(polarity == "neg" for polarity, _ in parsed),
        }
        if observed != selected["examples"]:
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
    return "\n".join(
        [
            "; Typed vocabulary for three Hopper programs realized by iteration.",
            "; Each iteration step remains an ordinary proof-relevant relation.",
            "",
            "(: hopper:rotate-n:step",
            "  (-> (source : (list hopper:nat))",
            "      (target : (list hopper:nat)) (u 0)))",
            "(: hopper:rotate-n:step-proof",
            "  (-> (source : (list hopper:nat)) (head : hopper:nat)",
            "      (tail : (list hopper:nat))",
            "      (target : (list hopper:nat))",
            "      (head-evidence : (rel:list:head hopper:nat source head))",
            "      (tail-evidence : (rel:list:tail hopper:nat source tail))",
            "      (snoc-evidence : (rel:list:snoc hopper:nat tail head target))",
            "      (hopper:rotate-n:step source target)))",
            "(: hopper:rotate-n:f",
            "  (-> (count : hopper:nat) (source : (list hopper:nat))",
            "      (target : (list hopper:nat)) (u 0)))",
            "(: hopper:rotate-n:proof",
            "  (-> (count : hopper:nat) (source : (list hopper:nat))",
            "      (target : (list hopper:nat))",
            "      (evidence :",
            "        (rel:iterate (list hopper:nat) hopper:nat",
            "          hopper:rotate-n:step hopper:nat:pred hopper:nat:n0",
            "          source count target))",
            "      (hopper:rotate-n:f count source target)))",
            "",
            "(: hopper:drop-last-k:step",
            "  (-> (source : (list (list hopper:atom)))",
            "      (target : (list (list hopper:atom))) (u 0)))",
            "(: hopper:drop-last-k:step-proof",
            "  (-> (source : (list (list hopper:atom)))",
            "      (target : (list (list hopper:atom)))",
            "      (evidence :",
            "        (map-rel (list hopper:atom) (list hopper:atom)",
            "          hopper:drop-last:element source target))",
            "      (hopper:drop-last-k:step source target)))",
            "(: hopper:drop-last-k:f",
            "  (-> (count : hopper:nat)",
            "      (source : (list (list hopper:atom)))",
            "      (target : (list (list hopper:atom))) (u 0)))",
            "(: hopper:drop-last-k:proof",
            "  (-> (count : hopper:nat)",
            "      (source : (list (list hopper:atom)))",
            "      (target : (list (list hopper:atom)))",
            "      (evidence :",
            "        (rel:iterate (list (list hopper:atom)) hopper:nat",
            "          hopper:drop-last-k:step hopper:nat:pred hopper:nat:n0",
            "          source count target))",
            "      (hopper:drop-last-k:f count source target)))",
            "",
            "(: hopper:add-n:step",
            "  (-> (source : (list hopper:nat))",
            "      (target : (list hopper:nat)) (u 0)))",
            "(: hopper:add-n:step-proof",
            "  (-> (source : (list hopper:nat))",
            "      (target : (list hopper:nat))",
            "      (evidence :",
            "        (map-rel hopper:nat hopper:nat hopper:nat:succ",
            "          source target))",
            "      (hopper:add-n:step source target)))",
            "(: hopper:add-n:f",
            "  (-> (count : hopper:nat) (source : (list hopper:nat))",
            "      (target : (list hopper:nat)) (u 0)))",
            "(: hopper:add-n:proof",
            "  (-> (count : hopper:nat) (source : (list hopper:nat))",
            "      (target : (list hopper:nat))",
            "      (evidence :",
            "        (rel:iterate (list hopper:nat) hopper:nat",
            "          hopper:add-n:step hopper:nat:pred hopper:nat:n0",
            "          source count target))",
            "      (hopper:add-n:f count source target)))",
            "",
        ]
    )


def block(lines: Iterable[str]) -> str:
    return "\n".join(f"      {line}" for line in lines)


def render_rules() -> str:
    blocks = [
        block(
            [
                "(rm-block list-head hopper:iter:list-head",
                "  (quote (rel:list:head-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:head $element",
                "    (list:cons $element $head $tail) $head)))",
            ]
        ),
        block(
            [
                "(rm-block list-tail hopper:iter:list-tail",
                "  (quote (rel:list:tail-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:tail $element",
                "    (list:cons $element $head $tail) $tail)))",
            ]
        ),
        block(
            [
                "(rm-block snoc-nil hopper:iter:snoc-nil",
                "  (quote (rel:list:snoc:nil $element $last))",
                "  rm-nil",
                "  (quote (rel:list:snoc $element (list:nil $element) $last",
                "    (list:cons $element $last (list:nil $element)))))",
            ]
        ),
        block(
            [
                "(rm-block snoc-cons hopper:iter:snoc-cons",
                "  (quote (rel:list:snoc:cons $element $head $tail $last",
                "    $result-tail (unquote $tail-evidence)))",
                "  (rm-cons",
                "    (rm-premise $tail-evidence",
                "      (quote (rel:list:snoc $element $tail $last $result-tail)))",
                "    rm-nil)",
                "  (quote (rel:list:snoc $element",
                "    (list:cons $element $head $tail) $last",
                "    (list:cons $element $head $result-tail))))",
            ]
        ),
    ]
    blocks.extend(
        fact_block(
            f"nat-succ-{value}-{value + 1}",
            f"hopper:iter:nat-succ-{value}-{value + 1}",
            f"hopper:nat:proof:succ-{value}-{value + 1}",
            f"(hopper:nat:succ {base.nat_name(value)} {base.nat_name(value + 1)})",
        )
        for value in range(MAX_NAT)
    )
    blocks.extend(
        [
            block(
                [
                    "(rm-block nat-pred hopper:iter:nat-pred",
                    "  (quote (hopper:nat:proof:pred-from-succ",
                    "    $later $earlier (unquote $successor-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $successor-evidence",
                    "      (quote (hopper:nat:succ $earlier $later)))",
                    "    rm-nil)",
                    "  (quote (hopper:nat:pred $later $earlier)))",
                ]
            ),
            block(
                [
                    "(rm-block iterate-zero hopper:iter:iterate-zero",
                    "  (quote (rel:iterate:zero $value $counter $step",
                    "    $predecessor $zero $source))",
                    "  rm-nil",
                    "  (quote (rel:iterate $value $counter $step $predecessor",
                    "    $zero $source $zero $source)))",
                ]
            ),
            block(
                [
                    "(rm-block iterate-step hopper:iter:iterate-step",
                    "  (quote (rel:iterate:step $value $counter $step",
                    "    $predecessor $zero $source $next $later $earlier $target",
                    "    (unquote $predecessor-evidence)",
                    "    (unquote $step-evidence)",
                    "    (unquote $recursive-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $predecessor-evidence",
                    "      (quote ($predecessor $later $earlier)))",
                    "    (rm-cons",
                    "      (rm-premise $step-evidence",
                    "        (quote ($step $source $next)))",
                    "      (rm-cons",
                    "        (rm-premise $recursive-evidence",
                    "          (quote (rel:iterate $value $counter $step",
                    "            $predecessor $zero $next $earlier $target)))",
                    "        rm-nil)))",
                    "  (quote (rel:iterate $value $counter $step $predecessor",
                    "    $zero $source $later $target)))",
                ]
            ),
            block(
                [
                    "(rm-block map-rel-nil hopper:iter:map-rel-nil",
                    "  (quote (map-rel:nil $source $target $relation))",
                    "  rm-nil",
                    "  (quote (map-rel $source $target $relation",
                    "    (list:nil $source) (list:nil $target))))",
                ]
            ),
            block(
                [
                    "(rm-block map-rel-cons hopper:iter:map-rel-cons",
                    "  (quote (map-rel:cons $source $target $relation",
                    "    $source-head $target-head $source-tail $target-tail",
                    "    (unquote $head-evidence) (unquote $tail-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $head-evidence",
                    "      (quote ($relation $source-head $target-head)))",
                    "    (rm-cons",
                    "      (rm-premise $tail-evidence",
                    "        (quote (map-rel $source $target $relation",
                    "          $source-tail $target-tail)))",
                    "      rm-nil))",
                    "  (quote (map-rel $source $target $relation",
                    "    (list:cons $source $source-head $source-tail)",
                    "    (list:cons $target $target-head $target-tail))))",
                ]
            ),
            block(
                [
                    "(rm-block fold-nil hopper:iter:fold-nil",
                    "  (quote (rel:fold:nil $element $accumulator $step $before))",
                    "  rm-nil",
                    "  (quote (rel:fold $element $accumulator $step $before",
                    "    (list:nil $element) $before)))",
                ]
            ),
            block(
                [
                    "(rm-block fold-cons hopper:iter:fold-cons",
                    "  (quote (rel:fold:cons $element $accumulator $step",
                    "    $before $head $tail $next $after",
                    "    (unquote $step-evidence) (unquote $tail-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $step-evidence",
                    "      (quote ($step $before $head $next)))",
                    "    (rm-cons",
                    "      (rm-premise $tail-evidence",
                    "        (quote (rel:fold $element $accumulator $step",
                    "          $next $tail $after)))",
                    "      rm-nil))",
                    "  (quote (rel:fold $element $accumulator $step $before",
                    "    (list:cons $element $head $tail) $after)))",
                ]
            ),
            block(
                [
                    "(rm-block reverse-step hopper:iter:reverse-step",
                    "  (quote (hopper:reverse:step-proof $before $head",
                    "    (rel:list:head-proof hopper:atom $head $before)",
                    "    (rel:list:tail-proof hopper:atom $head $before)))",
                    "  rm-nil",
                    "  (quote (hopper:reverse:step $before $head",
                    "    (list:cons hopper:atom $head $before))))",
                ]
            ),
            block(
                [
                    "(rm-block reverse hopper:iter:reverse",
                    "  (quote (hopper:reverse:proof $source $target",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:fold hopper:atom (list hopper:atom)",
                    "        hopper:reverse:step (list:nil hopper:atom)",
                    "        $source $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:reverse:f $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block drop-last-element hopper:iter:drop-last-element",
                    "  (quote (hopper:drop-last:element-proof",
                    "    $source $reversed $trimmed $target",
                    "    (unquote $source-reversal) (unquote $tail-evidence)",
                    "    (unquote $target-reversal)))",
                    "  (rm-cons",
                    "    (rm-premise $source-reversal",
                    "      (quote (hopper:reverse:f $source $reversed)))",
                    "    (rm-cons",
                    "      (rm-premise $tail-evidence",
                    "        (quote (rel:list:tail hopper:atom $reversed $trimmed)))",
                    "      (rm-cons",
                    "        (rm-premise $target-reversal",
                    "          (quote (hopper:reverse:f $trimmed $target)))",
                    "        rm-nil)))",
                    "  (quote (hopper:drop-last:element $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block rotate-step hopper:iter:rotate-step",
                    "  (quote (hopper:rotate-n:step-proof",
                    "    $source $head $tail $target",
                    "    (unquote $head-evidence) (unquote $tail-evidence)",
                    "    (unquote $snoc-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $head-evidence",
                    "      (quote (rel:list:head hopper:nat $source $head)))",
                    "    (rm-cons",
                    "      (rm-premise $tail-evidence",
                    "        (quote (rel:list:tail hopper:nat $source $tail)))",
                    "      (rm-cons",
                    "        (rm-premise $snoc-evidence",
                    "          (quote (rel:list:snoc hopper:nat $tail $head $target)))",
                    "        rm-nil)))",
                    "  (quote (hopper:rotate-n:step $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block drop-last-k-step hopper:iter:drop-last-k-step",
                    "  (quote (hopper:drop-last-k:step-proof $source $target",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (map-rel (list hopper:atom) (list hopper:atom)",
                    "        hopper:drop-last:element $source $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:drop-last-k:step $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block add-n-step hopper:iter:add-n-step",
                    "  (quote (hopper:add-n:step-proof $source $target",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (map-rel hopper:nat hopper:nat hopper:nat:succ",
                    "        $source $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:add-n:step $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block target-rotate hopper:iter:target-rotate",
                    "  (quote (hopper:rotate-n:proof $count $source $target",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:iterate (list hopper:nat) hopper:nat",
                    "        hopper:rotate-n:step hopper:nat:pred hopper:nat:n0",
                    "        $source $count $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:rotate-n:f $count $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block target-drop-last-k hopper:iter:target-drop-last-k",
                    "  (quote (hopper:drop-last-k:proof $count $source $target",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:iterate (list (list hopper:atom)) hopper:nat",
                    "        hopper:drop-last-k:step hopper:nat:pred hopper:nat:n0",
                    "        $source $count $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:drop-last-k:f $count $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block target-add-n hopper:iter:target-add-n",
                    "  (quote (hopper:add-n:proof $count $source $target",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:iterate (list hopper:nat) hopper:nat",
                    "        hopper:add-n:step hopper:nat:pred hopper:nat:n0",
                    "        $source $count $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:add-n:f $count $source $target)))",
                ]
            ),
        ]
    )
    return "\n".join(
        [
            "; Proof-producing realization of three authored iterative programs.",
            "; Shared iterate/map-rel/snoc evidence remains visible in every result.",
            "",
            "(= (hopper:table1:additional-iterative:package)",
            "  (compile:rule-package hopper-table1-additional-iterative-v1",
            "    (rm-package",
            *blocks,
            "    )))",
            "",
        ]
    )


def render_fixture(
    examples: dict[str, tuple[tuple[str, base.Atom], ...]], manifest: dict
) -> tuple[str, str, dict[str, dict[str, int]]]:
    entries = {entry["name"]: entry for entry in manifest["tasks"]}
    fixture = [
        "; Exact qualification for three Hopper programs expressed by iteration.",
        "; Every proof occurrence is checked against its indexed target.",
        "",
        "!(import! &self ../../lib/ilp/prime_native_list_types.metta)",
        "!(import! &self ../../lib/ilp/prime_native_list_relator_types.metta)",
        "!(import! &self ../../lib/ilp/prime_relational_combinators_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_first_order_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_higher_order_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_additional_iterative_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_additional_iterative_rules.metta)",
        "",
        "(= (hopper:iter:proof-checks (quote $goal) $occurrences)",
        "  (collapse",
        "    (let (occurrence $proof-data) (superpose $occurrences)",
        "      (type:check (unquote $proof-data) $goal))))",
        "",
        "(= (hopper:iter:classify $name (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (hopper:table1:additional-iterative:package)",
        "      1024 30000000 16384 (quote $goal))",
        "    (let $count (- (size-atom $occurrences) 1)",
        "      (let $checks (hopper:iter:proof-checks (quote $goal) $occurrences)",
        "        (if (== $count 0)",
        "            (hopper:iter:case $name not-derived $count $checks)",
        "            (hopper:iter:case $name derived $count $checks))))))",
        "",
    ]
    expected = ["[()]" for _ in range(7)]
    counts: dict[str, dict[str, int]] = {}
    for task in TASKS:
        source_positive = 0
        source_negative = 0
        derived = 0
        not_derived = 0
        proof_occurrences = 0
        for ordinal, (polarity, target) in enumerate(examples[task], 1):
            count = proof_count(task, target)
            name = f"hopper:{task.lower()}:ho:{polarity}-{ordinal}"
            goal = target_term(task, target)
            fixture.extend(
                [
                    f"!(hopper:iter:classify {name}",
                    f"  (quote {goal}))",
                ]
            )
            checks = " ".join("True" for _ in range(count))
            if count:
                expected.append(
                    f"[(hopper:iter:case {name} derived {count} ({checks}))]"
                )
                derived += 1
                proof_occurrences += count
            else:
                expected.append(
                    f"[(hopper:iter:case {name} not-derived 0 ())]"
                )
                not_derived += 1
            source_positive += int(polarity == "pos")
            source_negative += int(polarity == "neg")
        source_counts = entries[task]["variants"][SOURCE_VARIANT]["examples"]
        if (
            source_positive != source_counts["positive"]
            or source_negative != source_counts["negative"]
        ):
            raise GenerationError(f"{task}: source example counts changed")
        counts[task] = {
            "source_positive": source_positive,
            "source_negative": source_negative,
            "derived": derived,
            "not_derived": not_derived,
            "proof_occurrences": proof_occurrences,
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
        default=repo / "lib/ilp/hopper_table1_additional_iterative_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/hopper_table1_additional_iterative_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo / "examples/prime/hopper_table1_additional_iterative_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo / "examples/prime/hopper_table1_additional_iterative_ground_truth.expected",
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
        print(f"FAIL: Hopper iterative generation: {exc}", file=sys.stderr)
        return 1

    cases = sum(
        item["source_positive"] + item["source_negative"]
        for item in counts.values()
    )
    proofs = sum(item["proof_occurrences"] for item in counts.values())
    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        f"Hopper iterative qualification: {len(TASKS)} tasks, "
        f"{cases} source examples, {proofs} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
