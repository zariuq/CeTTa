#!/usr/bin/env python3
"""Generate typed Prime qualification for four relational-recursion tasks."""

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
from prime_iggp_generation import GenerationError, fact_block, materialize_outputs


TASKS = ("repeatN", "allSeqN", "firstHalf", "mulFromSuc")
SOURCE_VARIANT = "ho"
PROGRAMS = {
    "repeatN": (
        "f(A,B,C):-empty(D),ite_a(D,B,C,A).",
        "ite_p_a(A,B,C):-app(A,C,B).",
    ),
    "allSeqN": (
        "ite_p_a(A,B):-suc(A,B).",
        "f(A,B):-zero(C),ite_a(C,A,D),map_a(D,B).",
        "map_p_a(A,B):-zero(C),ite_a(C,A,B).",
    ),
    "firstHalf": (
        "ite_p_a(A,B):-pred(A,C),pred(C,B).",
        "ite_q_a(A,B,C):-head(A,B),tail(A,C).",
        "f(A,B):-len(A,C),ite_a(A,C,B).",
    ),
    "mulFromSuc": (
        "ite_p_a(A,B,C):-ite_a(A,C,B).",
        "f(A,B,C):-zero(D),ite_a(D,A,C,B).",
        "ite_p_a(A,B):-suc(A,B).",
    ),
}
EXPECTED_TARGETS = {
    "repeatN": ("list", "integer", "list"),
    "allSeqN": ("element", "list"),
    "firstHalf": ("list", "list"),
    "mulFromSuc": ("integer", "integer", "integer"),
}
MAX_NAT = 36


def nested_naturals(term: base.Term) -> int | tuple:
    if isinstance(term, base.Atom):
        return base.natural(term)
    if isinstance(term, base.ListTerm):
        return tuple(nested_naturals(item) for item in term.items)
    raise GenerationError(f"unsupported ground term {term}")


def list_depth(value: tuple) -> int:
    if not value:
        return 1
    if all(isinstance(item, int) for item in value):
        return 1
    if all(isinstance(item, tuple) for item in value):
        depths = {list_depth(item) for item in value}
        if len(depths) == 1:
            return 1 + depths.pop()
    raise GenerationError(f"nonuniform nested list {value}")


def list_type(depth: int) -> str:
    result = "hopper:nat"
    for _ in range(depth):
        result = f"(list {result})"
    return result


def render_nested_list(value: tuple, depth: int) -> str:
    if list_depth(value) != depth and value:
        raise GenerationError(
            f"expected nested-list depth {depth}, found {value}"
        )
    element = list_type(depth - 1)
    result = f"(list:nil {element})"
    for item in reversed(value):
        rendered = (
            base.nat_name(item)
            if depth == 1
            else render_nested_list(item, depth - 1)
        )
        result = f"(list:cons {element} {rendered} {result})"
    return result


def target_term(task: str, target: base.Atom) -> str:
    if task == "repeatN":
        if len(target.args) != 3:
            raise GenerationError("repeatN: target arity changed")
        source = nested_naturals(target.args[0])
        result = nested_naturals(target.args[2])
        if not isinstance(source, tuple) or not isinstance(result, tuple):
            raise GenerationError("repeatN: expected list arguments")
        depth = list_depth(source)
        if result and list_depth(result) != depth + 1:
            raise GenerationError("repeatN: result nesting changed")
        element = list_type(depth - 1)
        return (
            f"(hopper:repeat-n:f {element} "
            f"{render_nested_list(source, depth)} "
            f"{base.nat_name(base.natural(target.args[1]))} "
            f"{render_nested_list(result, depth + 1)})"
        )
    if task == "allSeqN":
        if len(target.args) != 2:
            raise GenerationError("allSeqN: target arity changed")
        result = nested_naturals(target.args[1])
        if not isinstance(result, tuple):
            raise GenerationError("allSeqN: expected list result")
        depth = 2 if not result else list_depth(result)
        return (
            f"(hopper:all-seq-n:f "
            f"{base.nat_name(base.natural(target.args[0]))} "
            f"{render_nested_list(result, depth)})"
        )
    if task == "firstHalf":
        if len(target.args) != 2:
            raise GenerationError("firstHalf: target arity changed")
        source = base.render_list(
            base.list_items(target.args[0]), "hopper:atom"
        )
        result = base.render_list(
            base.list_items(target.args[1]), "hopper:atom"
        )
        return f"(hopper:first-half:f {source} {result})"
    if task == "mulFromSuc":
        if len(target.args) != 3:
            raise GenerationError("mulFromSuc: target arity changed")
        left, right, result = (base.natural(arg) for arg in target.args)
        return (
            f"(hopper:mul-from-suc:f {base.nat_name(left)} "
            f"{base.nat_name(right)} {base.nat_name(result)})"
        )
    raise GenerationError(f"unsupported relational-recursion task {task}")


def proof_count(task: str, target: base.Atom) -> int:
    if task == "repeatN":
        source = nested_naturals(target.args[0])
        count = base.natural(target.args[1])
        result = nested_naturals(target.args[2])
        return int(isinstance(source, tuple) and result == (source,) * count)
    if task == "allSeqN":
        count = base.natural(target.args[0])
        result = nested_naturals(target.args[1])
        expected = tuple(tuple(range(1, value + 1)) for value in range(1, count + 1))
        return int(result == expected)
    if task == "firstHalf":
        source = base.list_items(target.args[0])
        result = base.list_items(target.args[1])
        minimum = (len(source) + 1) // 2
        return int(
            minimum <= len(result) <= len(source)
            and result == source[: len(result)]
        )
    if task == "mulFromSuc":
        left, right, result = (base.natural(arg) for arg in target.args)
        return int(left * right == result)
    raise GenerationError(f"unsupported relational-recursion task {task}")


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
            if best is None or tuple(best["clauses"]) != PROGRAMS[task]:
                raise GenerationError(
                    f"{task}/{variant}: authored best program changed"
                )
        selected_path = (
            snapshot_root / "examples" / task / selected["path"] / "exs.pl"
        )
        parsed = base.parse_examples(selected_path)
        for variant in ("fo", "ho", "ho-opt"):
            variant_path = (
                snapshot_root
                / "examples"
                / task
                / variants[variant]["path"]
                / "exs.pl"
            )
            if base.parse_examples(variant_path) != parsed:
                raise GenerationError(
                    f"{task}: FO/HO example semantics changed"
                )
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
            "; Typed programs for source-authored relational recursion.",
            "; Environments and intermediate states remain explicit evidence indices.",
            "",
            "(: hopper:nat:pred-clamped",
            "  (-> (later : hopper:nat) (earlier : hopper:nat) (u 0)))",
            "(: hopper:nat:proof:pred-clamped-zero",
            "  (hopper:nat:pred-clamped hopper:nat:n0 hopper:nat:n0))",
            "(: hopper:nat:proof:pred-clamped-positive",
            "  (-> (later : hopper:nat) (earlier : hopper:nat)",
            "      (evidence : (hopper:nat:pred later earlier))",
            "      (hopper:nat:pred-clamped later earlier)))",
            "",
            "(: hopper:repeat-n:f",
            "  (-> (element : (u $element-level))",
            "      (source : (list element)) (count : hopper:nat)",
            "      (target : (list (list element))) (u $element-level)))",
            "(: hopper:repeat-n:proof",
            "  (-> (element : (u $element-level))",
            "      (source : (list element)) (count : hopper:nat)",
            "      (target : (list (list element)))",
            "      (evidence :",
            "        (rel:list:repeat (list element) hopper:nat",
            "          hopper:nat:pred hopper:nat:n0 source",
            "          (list:nil (list element)) count target))",
            "      (hopper:repeat-n:f element source count target)))",
            "",
            "(: hopper:all-seq-n:successor-emit",
            "  (-> (current : hopper:nat) (head : hopper:nat)",
            "      (next : hopper:nat) (u 0)))",
            "(: hopper:all-seq-n:successor-emit-proof",
            "  (-> (current : hopper:nat) (next : hopper:nat)",
            "      (evidence : (hopper:nat:succ current next))",
            "      (hopper:all-seq-n:successor-emit current next next)))",
            "(: hopper:all-seq-n:prefix",
            "  (-> (count : hopper:nat) (values : (list hopper:nat)) (u 0)))",
            "(: hopper:all-seq-n:prefix-proof",
            "  (-> (count : hopper:nat) (values : (list hopper:nat))",
            "      (evidence :",
            "        (rel:unfold hopper:nat hopper:nat hopper:nat",
            "          hopper:nat:pred hopper:all-seq-n:successor-emit",
            "          hopper:nat:n0 hopper:nat:n0 count values))",
            "      (hopper:all-seq-n:prefix count values)))",
            "(: hopper:all-seq-n:f",
            "  (-> (count : hopper:nat)",
            "      (values : (list (list hopper:nat))) (u 0)))",
            "(: hopper:all-seq-n:proof",
            "  (-> (count : hopper:nat) (indices : (list hopper:nat))",
            "      (values : (list (list hopper:nat)))",
            "      (scan-evidence :",
            "        (rel:unfold hopper:nat hopper:nat hopper:nat",
            "          hopper:nat:pred hopper:all-seq-n:successor-emit",
            "          hopper:nat:n0 hopper:nat:n0 count indices))",
            "      (map-evidence :",
            "        (map-rel hopper:nat (list hopper:nat)",
            "          hopper:all-seq-n:prefix indices values))",
            "      (hopper:all-seq-n:f count values)))",
            "",
            "(: hopper:first-half:advance",
            "  (-> (later : hopper:nat) (earlier : hopper:nat) (u 0)))",
            "(: hopper:first-half:advance-proof",
            "  (-> (later : hopper:nat) (middle : hopper:nat)",
            "      (earlier : hopper:nat)",
            "      (first : (hopper:nat:pred-clamped later middle))",
            "      (second : (hopper:nat:pred-clamped middle earlier))",
            "      (hopper:first-half:advance later earlier)))",
            "(: hopper:first-half:emit",
            "  (-> (current : (list hopper:atom)) (head : hopper:atom)",
            "      (next : (list hopper:atom)) (u 0)))",
            "(: hopper:first-half:emit-proof",
            "  (-> (current : (list hopper:atom)) (head : hopper:atom)",
            "      (next : (list hopper:atom))",
            "      (head-evidence : (rel:list:head hopper:atom current head))",
            "      (tail-evidence : (rel:list:tail hopper:atom current next))",
            "      (hopper:first-half:emit current head next)))",
            "(: hopper:first-half:f",
            "  (-> (source : (list hopper:atom))",
            "      (target : (list hopper:atom)) (u 0)))",
            "(: hopper:first-half:proof",
            "  (-> (source : (list hopper:atom)) (count : hopper:nat)",
            "      (target : (list hopper:atom))",
            "      (length-evidence : (hopper:length:f source count))",
            "      (unfold-evidence :",
            "        (rel:unfold (list hopper:atom) hopper:nat hopper:atom",
            "          hopper:first-half:advance hopper:first-half:emit",
            "          hopper:nat:n0 source count target))",
            "      (hopper:first-half:f source target)))",
            "",
            "(: hopper:nat:add-from-suc",
            "  (-> (amount : hopper:nat) (source : hopper:nat)",
            "      (target : hopper:nat) (u 0)))",
            "(: hopper:nat:add-from-suc-proof",
            "  (-> (amount : hopper:nat) (source : hopper:nat)",
            "      (target : hopper:nat)",
            "      (evidence :",
            "        (rel:iterate hopper:nat hopper:nat hopper:nat:succ",
            "          hopper:nat:pred hopper:nat:n0 source amount target))",
            "      (hopper:nat:add-from-suc amount source target)))",
            "(: hopper:mul-from-suc:f",
            "  (-> (left : hopper:nat) (right : hopper:nat)",
            "      (target : hopper:nat) (u 0)))",
            "(: hopper:mul-from-suc:proof",
            "  (-> (left : hopper:nat) (right : hopper:nat)",
            "      (target : hopper:nat)",
            "      (evidence :",
            "        (rel:iterate-with hopper:nat hopper:nat hopper:nat",
            "          hopper:nat:add-from-suc hopper:nat:pred hopper:nat:n0",
            "          right hopper:nat:n0 left target))",
            "      (hopper:mul-from-suc:f left right target)))",
            "",
        ]
    )


def block(lines: Iterable[str]) -> str:
    return "\n".join(f"      {line}" for line in lines)


def render_rules() -> str:
    blocks = [
        block(
            [
                "(rm-block list-head hopper:rec:list-head",
                "  (quote (rel:list:head-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:head $element",
                "    (list:cons $element $head $tail) $head)))",
            ]
        ),
        block(
            [
                "(rm-block list-tail hopper:rec:list-tail",
                "  (quote (rel:list:tail-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:tail $element",
                "    (list:cons $element $head $tail) $tail)))",
            ]
        ),
        block(
            [
                "(rm-block snoc-nil hopper:rec:snoc-nil",
                "  (quote (rel:list:snoc:nil $element $last))",
                "  rm-nil",
                "  (quote (rel:list:snoc $element (list:nil $element) $last",
                "    (list:cons $element $last (list:nil $element)))))",
            ]
        ),
        block(
            [
                "(rm-block snoc-cons hopper:rec:snoc-cons",
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
            f"hopper:rec:nat-succ-{value}-{value + 1}",
            f"hopper:nat:proof:succ-{value}-{value + 1}",
            f"(hopper:nat:succ {base.nat_name(value)} {base.nat_name(value + 1)})",
        )
        for value in range(MAX_NAT)
    )
    blocks.extend(
        [
            fact_block(
                "nat-pred-clamped-zero",
                "hopper:rec:nat-pred-clamped-zero",
                "hopper:nat:proof:pred-clamped-zero",
                "(hopper:nat:pred-clamped hopper:nat:n0 hopper:nat:n0)",
            ),
            block(
                [
                    "(rm-block nat-pred hopper:rec:nat-pred",
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
                    "(rm-block nat-pred-clamped-positive",
                    "  hopper:rec:nat-pred-clamped-positive",
                    "  (quote (hopper:nat:proof:pred-clamped-positive",
                    "    $later $earlier (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (hopper:nat:pred $later $earlier)))",
                    "    rm-nil)",
                    "  (quote (hopper:nat:pred-clamped $later $earlier)))",
                ]
            ),
            block(
                [
                    "(rm-block iterate-zero hopper:rec:iterate-zero",
                    "  (quote (rel:iterate:zero $value $counter $step",
                    "    $predecessor $zero $source))",
                    "  rm-nil",
                    "  (quote (rel:iterate $value $counter $step $predecessor",
                    "    $zero $source $zero $source)))",
                ]
            ),
            block(
                [
                    "(rm-block iterate-step hopper:rec:iterate-step",
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
                    "(rm-block iterate-with-zero hopper:rec:iterate-with-zero",
                    "  (quote (rel:iterate-with:zero $environment $value $counter",
                    "    $step $predecessor $zero $parameter $source))",
                    "  rm-nil",
                    "  (quote (rel:iterate-with $environment $value $counter",
                    "    $step $predecessor $zero $parameter",
                    "    $source $zero $source)))",
                ]
            ),
            block(
                [
                    "(rm-block iterate-with-step hopper:rec:iterate-with-step",
                    "  (quote (rel:iterate-with:step $environment $value $counter",
                    "    $step $predecessor $zero $parameter $source $next",
                    "    $later $earlier $target",
                    "    (unquote $predecessor-evidence)",
                    "    (unquote $step-evidence)",
                    "    (unquote $recursive-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $predecessor-evidence",
                    "      (quote ($predecessor $later $earlier)))",
                    "    (rm-cons",
                    "      (rm-premise $step-evidence",
                    "        (quote ($step $parameter $source $next)))",
                    "      (rm-cons",
                    "        (rm-premise $recursive-evidence",
                    "          (quote (rel:iterate-with $environment $value $counter",
                    "            $step $predecessor $zero $parameter",
                    "            $next $earlier $target)))",
                    "        rm-nil)))",
                    "  (quote (rel:iterate-with $environment $value $counter",
                    "    $step $predecessor $zero $parameter",
                    "    $source $later $target)))",
                ]
            ),
            block(
                [
                    "(rm-block list-repeat-zero hopper:rec:list-repeat-zero",
                    "  (quote (rel:list:repeat:zero $element $counter",
                    "    $predecessor $zero $item $before))",
                    "  rm-nil",
                    "  (quote (rel:list:repeat $element $counter",
                    "    $predecessor $zero $item $before $zero $before)))",
                ]
            ),
            block(
                [
                    "(rm-block list-repeat-step hopper:rec:list-repeat-step",
                    "  (quote (rel:list:repeat:step $element $counter",
                    "    $predecessor $zero $item $before $next",
                    "    $later $earlier $after",
                    "    (unquote $predecessor-evidence)",
                    "    (unquote $append-evidence)",
                    "    (unquote $recursive-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $predecessor-evidence",
                    "      (quote ($predecessor $later $earlier)))",
                    "    (rm-cons",
                    "      (rm-premise $append-evidence",
                    "        (quote (rel:list:snoc $element $before $item $next)))",
                    "      (rm-cons",
                    "        (rm-premise $recursive-evidence",
                    "          (quote (rel:list:repeat $element $counter",
                    "            $predecessor $zero $item $next $earlier $after)))",
                    "        rm-nil)))",
                    "  (quote (rel:list:repeat $element $counter",
                    "    $predecessor $zero $item $before $later $after)))",
                ]
            ),
            block(
                [
                    "(rm-block unfold-zero hopper:rec:unfold-zero",
                    "  (quote (rel:unfold:zero $state $counter $output",
                    "    $advance $emit $zero $current))",
                    "  rm-nil",
                    "  (quote (rel:unfold $state $counter $output",
                    "    $advance $emit $zero $current $zero",
                    "    (list:nil $output))))",
                ]
            ),
            block(
                [
                    "(rm-block unfold-step hopper:rec:unfold-step",
                    "  (quote (rel:unfold:step $state $counter $output",
                    "    $advance $emit $zero $current $head $next",
                    "    $later $earlier $tail",
                    "    (unquote $advance-evidence)",
                    "    (unquote $emit-evidence)",
                    "    (unquote $recursive-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $advance-evidence",
                    "      (quote ($advance $later $earlier)))",
                    "    (rm-cons",
                    "      (rm-premise $emit-evidence",
                    "        (quote ($emit $current $head $next)))",
                    "      (rm-cons",
                    "        (rm-premise $recursive-evidence",
                    "          (quote (rel:unfold $state $counter $output",
                    "            $advance $emit $zero $next $earlier $tail)))",
                    "        rm-nil)))",
                    "  (quote (rel:unfold $state $counter $output",
                    "    $advance $emit $zero $current $later",
                    "    (list:cons $output $head $tail))))",
                ]
            ),
            block(
                [
                    "(rm-block map-rel-nil hopper:rec:map-rel-nil",
                    "  (quote (map-rel:nil $source $target $relation))",
                    "  rm-nil",
                    "  (quote (map-rel $source $target $relation",
                    "    (list:nil $source) (list:nil $target))))",
                ]
            ),
            block(
                [
                    "(rm-block map-rel-cons hopper:rec:map-rel-cons",
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
                    "(rm-block fold-nil hopper:rec:fold-nil",
                    "  (quote (rel:fold:nil $element $accumulator $step $before))",
                    "  rm-nil",
                    "  (quote (rel:fold $element $accumulator $step $before",
                    "    (list:nil $element) $before)))",
                ]
            ),
            block(
                [
                    "(rm-block fold-cons hopper:rec:fold-cons",
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
                    "(rm-block length-step hopper:rec:length-step",
                    "  (quote (hopper:length:step-proof $before $head $after",
                    "    (unquote $successor-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $successor-evidence",
                    "      (quote (hopper:nat:succ $before $after)))",
                    "    rm-nil)",
                    "  (quote (hopper:length:step $before $head $after)))",
                ]
            ),
            block(
                [
                    "(rm-block length hopper:rec:length",
                    "  (quote (hopper:length:proof $values $length",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:fold hopper:atom hopper:nat",
                    "        hopper:length:step hopper:nat:n0 $values $length)))",
                    "    rm-nil)",
                    "  (quote (hopper:length:f $values $length)))",
                ]
            ),
            block(
                [
                    "(rm-block repeat-n hopper:rec:repeat-n",
                    "  (quote (hopper:repeat-n:proof $element $source $count",
                    "    $target (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:list:repeat (list $element) hopper:nat",
                    "        hopper:nat:pred hopper:nat:n0 $source",
                    "        (list:nil (list $element)) $count $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:repeat-n:f $element $source $count $target)))",
                ]
            ),
            block(
                [
                    "(rm-block successor-emit hopper:rec:successor-emit",
                    "  (quote (hopper:all-seq-n:successor-emit-proof",
                    "    $current $next (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (hopper:nat:succ $current $next)))",
                    "    rm-nil)",
                    "  (quote (hopper:all-seq-n:successor-emit",
                    "    $current $next $next)))",
                ]
            ),
            block(
                [
                    "(rm-block all-seq-prefix hopper:rec:all-seq-prefix",
                    "  (quote (hopper:all-seq-n:prefix-proof $count $values",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:unfold hopper:nat hopper:nat hopper:nat",
                    "        hopper:nat:pred hopper:all-seq-n:successor-emit",
                    "        hopper:nat:n0 hopper:nat:n0 $count $values)))",
                    "    rm-nil)",
                    "  (quote (hopper:all-seq-n:prefix $count $values)))",
                ]
            ),
            block(
                [
                    "(rm-block all-seq-n hopper:rec:all-seq-n",
                    "  (quote (hopper:all-seq-n:proof $count $indices $values",
                    "    (unquote $scan-evidence) (unquote $map-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $scan-evidence",
                    "      (quote (rel:unfold hopper:nat hopper:nat hopper:nat",
                    "        hopper:nat:pred hopper:all-seq-n:successor-emit",
                    "        hopper:nat:n0 hopper:nat:n0 $count $indices)))",
                    "    (rm-cons",
                    "      (rm-premise $map-evidence",
                    "        (quote (map-rel hopper:nat (list hopper:nat)",
                    "          hopper:all-seq-n:prefix $indices $values)))",
                    "      rm-nil))",
                    "  (quote (hopper:all-seq-n:f $count $values)))",
                ]
            ),
            block(
                [
                    "(rm-block first-half-advance hopper:rec:first-half-advance",
                    "  (quote (hopper:first-half:advance-proof",
                    "    $later $middle $earlier",
                    "    (unquote $first) (unquote $second)))",
                    "  (rm-cons",
                    "    (rm-premise $first",
                    "      (quote (hopper:nat:pred-clamped $later $middle)))",
                    "    (rm-cons",
                    "      (rm-premise $second",
                    "        (quote (hopper:nat:pred-clamped $middle $earlier)))",
                    "      rm-nil))",
                    "  (quote (hopper:first-half:advance $later $earlier)))",
                ]
            ),
            block(
                [
                    "(rm-block first-half-emit hopper:rec:first-half-emit",
                    "  (quote (hopper:first-half:emit-proof",
                    "    $current $head $next",
                    "    (unquote $head-evidence) (unquote $tail-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $head-evidence",
                    "      (quote (rel:list:head hopper:atom $current $head)))",
                    "    (rm-cons",
                    "      (rm-premise $tail-evidence",
                    "        (quote (rel:list:tail hopper:atom $current $next)))",
                    "      rm-nil))",
                    "  (quote (hopper:first-half:emit $current $head $next)))",
                ]
            ),
            block(
                [
                    "(rm-block first-half hopper:rec:first-half",
                    "  (quote (hopper:first-half:proof $source $count $target",
                    "    (unquote $length-evidence) (unquote $unfold-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $length-evidence",
                    "      (quote (hopper:length:f $source $count)))",
                    "    (rm-cons",
                    "      (rm-premise $unfold-evidence",
                    "        (quote (rel:unfold (list hopper:atom) hopper:nat",
                    "          hopper:atom hopper:first-half:advance",
                    "          hopper:first-half:emit hopper:nat:n0",
                    "          $source $count $target)))",
                    "      rm-nil))",
                    "  (quote (hopper:first-half:f $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block nat-add-from-suc hopper:rec:nat-add-from-suc",
                    "  (quote (hopper:nat:add-from-suc-proof",
                    "    $amount $source $target (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:iterate hopper:nat hopper:nat",
                    "        hopper:nat:succ hopper:nat:pred hopper:nat:n0",
                    "        $source $amount $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:nat:add-from-suc",
                    "    $amount $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block mul-from-suc hopper:rec:mul-from-suc",
                    "  (quote (hopper:mul-from-suc:proof",
                    "    $left $right $target (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:iterate-with hopper:nat hopper:nat hopper:nat",
                    "        hopper:nat:add-from-suc hopper:nat:pred hopper:nat:n0",
                    "        $right hopper:nat:n0 $left $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:mul-from-suc:f $left $right $target)))",
                ]
            ),
        ]
    )
    return "\n".join(
        [
            "; Proof-producing execution of four authored recursive programs.",
            "; Generic evidence retains environments, states, and every premise.",
            "",
            "(= (hopper:table1:relational-recursion:package)",
            "  (compile:rule-package hopper-table1-relational-recursion-v1",
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
        "; Exact qualification for four authored relational-recursion programs.",
        "; Every occurrence retains its proof tree and is checked at its target.",
        "",
        "!(import! &self ../../lib/ilp/prime_native_list_types.metta)",
        "!(import! &self ../../lib/ilp/prime_native_list_relator_types.metta)",
        "!(import! &self ../../lib/ilp/prime_relational_combinators_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_first_order_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_relational_recursion_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_relational_recursion_rules.metta)",
        "",
        "(= (hopper:rec:proof-checks (quote $goal) $occurrences)",
        "  (collapse",
        "    (let (occurrence $proof-data) (superpose $occurrences)",
        "      (type:check (unquote $proof-data) $goal))))",
        "",
        "(= (hopper:rec:classify $name (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (hopper:table1:relational-recursion:package)",
        "      1024 100000000 65536 (quote $goal))",
        "    (let $count (- (size-atom $occurrences) 1)",
        "      (let $checks (hopper:rec:proof-checks (quote $goal) $occurrences)",
        "        (if (== $count 0)",
        "            (hopper:rec:case $name not-derived $count $checks)",
        "            (hopper:rec:case $name derived $count $checks))))))",
        "",
    ]
    expected = ["[()]" for _ in range(6)]
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
                    f"!(hopper:rec:classify {name}",
                    f"  (quote {goal}))",
                ]
            )
            checks = " ".join("True" for _ in range(count))
            if count:
                expected.append(
                    f"[(hopper:rec:case {name} derived {count} ({checks}))]"
                )
                derived += 1
                proof_occurrences += count
            else:
                expected.append(
                    f"[(hopper:rec:case {name} not-derived 0 ())]"
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

    overlap = base.Atom(
        "f",
        (
            base.ListTerm(tuple(base.Atom(str(value)) for value in (1, 2, 3))),
            base.ListTerm(tuple(base.Atom(str(value)) for value in (1, 2, 3))),
        ),
    )
    if proof_count("firstHalf", overlap) != 1:
        raise GenerationError("firstHalf: zero/step overlap canary changed")
    overlap_name = "hopper:firsthalf:ho:zero-step-overlap"
    fixture.extend(
        [
            f"!(hopper:rec:classify {overlap_name}",
            f"  (quote {target_term('firstHalf', overlap)}))",
        ]
    )
    expected.append(
        f"[(hopper:rec:case {overlap_name} derived 1 (True))]"
    )
    return "\n".join(fixture) + "\n", "\n".join(expected) + "\n", counts


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument(
        "--types-output",
        type=Path,
        default=repo / "lib/ilp/hopper_table1_relational_recursion_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/hopper_table1_relational_recursion_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo
        / "examples/prime/hopper_table1_relational_recursion_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo
        / "examples/prime/hopper_table1_relational_recursion_ground_truth.expected",
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
        print(
            f"FAIL: Hopper relational-recursion generation: {exc}",
            file=sys.stderr,
        )
        return 1

    cases = sum(
        item["source_positive"] + item["source_negative"]
        for item in counts.values()
    )
    proofs = sum(item["proof_occurrences"] for item in counts.values())
    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        f"Hopper relational-recursion qualification: {len(TASKS)} tasks, "
        f"{cases} source examples, {proofs} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
