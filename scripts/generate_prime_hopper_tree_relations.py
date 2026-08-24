#!/usr/bin/env python3
"""Generate typed Prime qualification for Hopper's three rose-tree tasks."""

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


TASKS = ("depth", "isBranch", "isSubTree")
SOURCE_VARIANT = "ho"
PROGRAMS = {
    "depth": {
        "fo": None,
        "ho": (
            "fold_p_a(A,B,C):-f(B,D),max(D,A,C).",
            "f(A,B):-children(A,C),zero(E),fold_a(E,C,D),suc(D,B).",
        ),
        "ho-opt": (
            "fold_p_a(A,B,C):-f(B,D),max(D,A,C).",
            "f(A,B):-children(A,C),zero(E),fold_a(E,C,D),suc(D,B).",
        ),
    },
    "isBranch": {
        "fo": None,
        "ho": (
            "casetree_p_a(A,B):-tail(B,C),empty(C),head(B,A).",
            "any_p_a(A,B):-tail(B,C),casetree_a(A,C).",
            "casetree_q_a(A,B,C):-head(C,A),any_a(B,C).",
            "f(A,B):-casetree_a(A,B).",
        ),
        "ho-opt": (
            "casetree_p_a(A,B):-tail(B,C),empty(C),head(B,A).",
            "any_p_a(A,B):-tail(B,C),casetree_a(A,C).",
            "casetree_q_a(A,B,C):-head(C,A),any_a(B,C).",
            "f(A,B):-casetree_a(A,B).",
        ),
    },
    "isSubTree": {
        "fo": (
            "g_p_a(A,B):-children(B,A).",
            "g_p_a(A,B):-head(A,B).",
            "g_p_a(A,B):-tail(A,C),g_a(C,B).",
            "f(A,B):-children(A,C),g_p_a(C,B).",
        ),
        "ho": (
            "f(A,B):-eq(A,B).",
            "f(A,B):-children(A,C),any_a(C,B).",
            "any_p_a(A,B):-f(A,B).",
        ),
        "ho-opt": (
            "f(A,B):-eqs(A,B).",
            "f(A,B):-children(A,C),any_a(C,B).",
            "any_p_a(A,B):-f(A,B).",
        ),
    },
}
EXPECTED_TARGETS = {
    "depth": ("tree", "element"),
    "isBranch": ("tree", "list"),
    "isSubTree": ("tree", "tree"),
}
MAX_DEPTH = 3


Tree = tuple[int, tuple["Tree", ...]]


def source_tree(term: base.Term) -> Tree:
    if (
        not isinstance(term, base.Atom)
        or term.head != "t"
        or len(term.args) != 2
        or not isinstance(term.args[1], base.ListTerm)
    ):
        raise GenerationError(f"expected a source rose tree, found {term}")
    return (
        base.natural(term.args[0]),
        tuple(source_tree(child) for child in term.args[1].items),
    )


def tree_depth(tree: Tree) -> int:
    return 1 + max((tree_depth(child) for child in tree[1]), default=0)


def branch_paths(tree: Tree) -> tuple[tuple[int, ...], ...]:
    label, children = tree
    if not children:
        return ((label,),)
    return tuple(
        (label,) + suffix
        for child in children
        for suffix in branch_paths(child)
    )


def subtrees(tree: Tree) -> Iterable[Tree]:
    yield tree
    for child in tree[1]:
        yield from subtrees(child)


def render_tree(tree: Tree) -> str:
    label, children = tree
    rendered_children = "(list:nil hopper:tree)"
    for child in reversed(children):
        rendered_children = (
            f"(list:cons hopper:tree {render_tree(child)} "
            f"{rendered_children})"
        )
    return (
        f"(hopper:tree:node {base.nat_name(label)} "
        f"{rendered_children})"
    )


def render_source_tree(term: base.Term) -> str:
    if isinstance(term, base.ListTerm) and not term.items:
        return "(list:nil hopper:tree)"
    return render_tree(source_tree(term))


def target_term(task: str, target: base.Atom) -> str:
    if len(target.args) != 2:
        raise GenerationError(f"{task}: target arity changed")
    if task == "depth":
        return (
            f"(hopper:depth:f {render_source_tree(target.args[0])} "
            f"{base.nat_name(base.natural(target.args[1]))})"
        )
    if task == "isBranch":
        path = base.render_list(
            base.list_items(target.args[1]), "hopper:nat"
        )
        return (
            f"(hopper:is-branch:f {render_source_tree(target.args[0])} "
            f"{path})"
        )
    if task == "isSubTree":
        return (
            f"(hopper:is-subtree:f {render_source_tree(target.args[0])} "
            f"{render_source_tree(target.args[1])})"
        )
    raise GenerationError(f"unsupported tree task {task}")


def proof_count(task: str, target: base.Atom) -> int:
    if task == "depth":
        return int(
            tree_depth(source_tree(target.args[0]))
            == base.natural(target.args[1])
        )
    if task == "isBranch":
        if isinstance(target.args[0], base.ListTerm):
            return 0
        path = tuple(int(value) for value in base.list_items(target.args[1]))
        return sum(
            occurrence == path
            for occurrence in branch_paths(source_tree(target.args[0]))
        )
    if task == "isSubTree":
        sought = source_tree(target.args[1])
        return sum(
            occurrence == sought
            for occurrence in subtrees(source_tree(target.args[0]))
        )
    raise GenerationError(f"unsupported tree task {task}")


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
        selected = entry["variants"][SOURCE_VARIANT]
        if tuple(selected["target"]["types"]) != EXPECTED_TARGETS[task]:
            raise GenerationError(f"{task}: target type profile changed")
        for variant in ("fo", "ho", "ho-opt"):
            best = entry["variants"][variant]["best_program"]
            expected = PROGRAMS[task][variant]
            if expected is None:
                if best is not None:
                    raise GenerationError(
                        f"{task}/{variant}: unexpected authored best program"
                    )
            elif best is None or tuple(best["clauses"]) != expected:
                raise GenerationError(
                    f"{task}/{variant}: authored best program changed"
                )
        selected_path = (
            snapshot_root / "examples" / task / selected["path"] / "exs.pl"
        )
        parsed = base.parse_examples(selected_path)
        for variant in ("fo", "ho", "ho-opt"):
            variant_entry = entry["variants"][variant]
            variant_path = (
                snapshot_root
                / "examples"
                / task
                / variant_entry["path"]
                / "exs.pl"
            )
            if base.parse_examples(variant_path) != parsed:
                raise GenerationError(
                    f"{task}: FO/HO/HO-OPT example semantics changed"
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
    lines = [
        "; Typed source-facing rose trees and proof-relevant task relations.",
        "; `hopper:tree:node` is checked at the raw boundary.  It does not",
        "; claim generic native-family or eliminator authority.",
        "",
        "(: hopper:tree (u 0))",
        "(: hopper:tree:node",
        "  (-> (label : hopper:nat) (children : (list hopper:tree))",
        "      hopper:tree))",
        "(: hopper:tree:view",
        "  (-> (tree : hopper:tree) (label : hopper:nat)",
        "      (children : (list hopper:tree)) (u 0)))",
        "(: hopper:tree:view:node",
        "  (-> (label : hopper:nat) (children : (list hopper:tree))",
        "      (hopper:tree:view",
        "        (hopper:tree:node label children) label children)))",
        "",
        "(: hopper:nat:max",
        "  (-> (left : hopper:nat) (right : hopper:nat)",
        "      (result : hopper:nat) (u 0)))",
    ]
    for left in range(MAX_DEPTH + 1):
        for right in range(MAX_DEPTH + 1):
            result = max(left, right)
            lines.append(
                f"(: hopper:nat:proof:max-{left}-{right} "
                f"(hopper:nat:max {base.nat_name(left)} "
                f"{base.nat_name(right)} {base.nat_name(result)}))"
            )
    lines.extend(
        [
            "",
            "(: hopper:depth:step",
            "  (-> (before : hopper:nat) (child : hopper:tree)",
            "      (after : hopper:nat) (u 0)))",
            "(: hopper:depth:step-proof",
            "  (-> (before : hopper:nat) (child : hopper:tree)",
            "      (child-depth : hopper:nat) (after : hopper:nat)",
            "      (depth-evidence : (hopper:depth:f child child-depth))",
            "      (max-evidence :",
            "        (hopper:nat:max child-depth before after))",
            "      (hopper:depth:step before child after)))",
            "(: hopper:depth:f",
            "  (-> (tree : hopper:tree) (depth : hopper:nat) (u 0)))",
            "(: hopper:depth:proof",
            "  (-> (tree : hopper:tree) (root : hopper:nat)",
            "      (children : (list hopper:tree))",
            "      (maximum : hopper:nat) (depth : hopper:nat)",
            "      (view-evidence : (hopper:tree:view tree root children))",
            "      (fold-evidence :",
            "        (rel:fold hopper:tree hopper:nat hopper:depth:step",
            "          hopper:nat:n0 children maximum))",
            "      (successor-evidence : (hopper:nat:succ maximum depth))",
            "      (hopper:depth:f tree depth)))",
            "",
            "(: hopper:is-branch:f",
            "  (-> (tree : hopper:tree) (path : (list hopper:nat)) (u 0)))",
            "(: hopper:is-branch:any-child",
            "  (-> (remaining : (list hopper:tree))",
            "      (path : (list hopper:nat)) (u 0)))",
            "(: hopper:is-branch:any-child-proof",
            "  (-> (remaining : (list hopper:tree)) (child : hopper:tree)",
            "      (path : (list hopper:nat))",
            "      (path-tail : (list hopper:nat))",
            "      (child-evidence :",
            "        (rel:list:head hopper:tree remaining child))",
            "      (tail-evidence :",
            "        (rel:list:tail hopper:nat path path-tail))",
            "      (branch-evidence :",
            "        (hopper:is-branch:f child path-tail))",
            "      (hopper:is-branch:any-child remaining path)))",
            "(: hopper:is-branch:proof:leaf",
            "  (-> (root : hopper:nat) (path : (list hopper:nat))",
            "      (path-tail : (list hopper:nat))",
            "      (view-evidence :",
            "        (hopper:tree:view",
            "          (hopper:tree:node root (list:nil hopper:tree))",
            "          root (list:nil hopper:tree)))",
            "      (tail-evidence :",
            "        (rel:list:tail hopper:nat path path-tail))",
            "      (empty-evidence :",
            "        (rel:list:empty hopper:nat path-tail))",
            "      (head-evidence : (rel:list:head hopper:nat path root))",
            "      (hopper:is-branch:f",
            "        (hopper:tree:node root (list:nil hopper:tree)) path)))",
            "(: hopper:is-branch:proof:node",
            "  (-> (root : hopper:nat) (first : hopper:tree)",
            "      (rest : (list hopper:tree)) (path : (list hopper:nat))",
            "      (view-evidence :",
            "        (hopper:tree:view",
            "          (hopper:tree:node root",
            "            (list:cons hopper:tree first rest))",
            "          root (list:cons hopper:tree first rest)))",
            "      (head-evidence : (rel:list:head hopper:nat path root))",
            "      (any-evidence :",
            "        (rel:any hopper:tree (list hopper:nat)",
            "          hopper:is-branch:any-child",
            "          (list:cons hopper:tree first rest) path))",
            "      (hopper:is-branch:f",
            "        (hopper:tree:node root",
            "          (list:cons hopper:tree first rest)) path)))",
            "",
            "(: hopper:is-subtree:f",
            "  (-> (source : hopper:tree) (target : hopper:tree) (u 0)))",
            "(: hopper:is-subtree:any-child",
            "  (-> (remaining : (list hopper:tree)) (target : hopper:tree)",
            "      (u 0)))",
            "(: hopper:is-subtree:any-child-proof",
            "  (-> (remaining : (list hopper:tree)) (child : hopper:tree)",
            "      (target : hopper:tree)",
            "      (child-evidence :",
            "        (rel:list:head hopper:tree remaining child))",
            "      (subtree-evidence : (hopper:is-subtree:f child target))",
            "      (hopper:is-subtree:any-child remaining target)))",
            "(: hopper:is-subtree:proof:equal",
            "  (-> (tree : hopper:tree) (hopper:is-subtree:f tree tree)))",
            "(: hopper:is-subtree:proof:child",
            "  (-> (root : hopper:nat) (children : (list hopper:tree))",
            "      (target : hopper:tree)",
            "      (view-evidence :",
            "        (hopper:tree:view",
            "          (hopper:tree:node root children) root children))",
            "      (any-evidence :",
            "        (rel:any hopper:tree hopper:tree",
            "          hopper:is-subtree:any-child children target))",
            "      (hopper:is-subtree:f",
            "        (hopper:tree:node root children) target)))",
            "",
        ]
    )
    return "\n".join(lines)


def block(lines: Iterable[str]) -> str:
    return "\n".join(f"      {line}" for line in lines)


def render_rules() -> str:
    blocks = [
        block(
            [
                "(rm-block list-empty hopper:tree:list-empty",
                "  (quote (rel:list:empty-proof $element))",
                "  rm-nil",
                "  (quote (rel:list:empty $element (list:nil $element))))",
            ]
        ),
        block(
            [
                "(rm-block list-head hopper:tree:list-head",
                "  (quote (rel:list:head-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:head $element",
                "    (list:cons $element $head $tail) $head)))",
            ]
        ),
        block(
            [
                "(rm-block list-tail hopper:tree:list-tail",
                "  (quote (rel:list:tail-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:tail $element",
                "    (list:cons $element $head $tail) $tail)))",
            ]
        ),
        block(
            [
                "(rm-block tree-view hopper:tree:view",
                "  (quote (hopper:tree:view:node $root $children))",
                "  rm-nil",
                "  (quote (hopper:tree:view",
                "    (hopper:tree:node $root $children) $root $children)))",
            ]
        ),
    ]
    blocks.extend(
        fact_block(
            f"nat-succ-{value}-{value + 1}",
            f"hopper:tree:nat-succ-{value}-{value + 1}",
            f"hopper:nat:proof:succ-{value}-{value + 1}",
            f"(hopper:nat:succ {base.nat_name(value)} "
            f"{base.nat_name(value + 1)})",
        )
        for value in range(MAX_DEPTH)
    )
    blocks.extend(
        fact_block(
            f"nat-max-{left}-{right}",
            f"hopper:tree:nat-max-{left}-{right}",
            f"hopper:nat:proof:max-{left}-{right}",
            f"(hopper:nat:max {base.nat_name(left)} "
            f"{base.nat_name(right)} {base.nat_name(max(left, right))})",
        )
        for left in range(MAX_DEPTH + 1)
        for right in range(MAX_DEPTH + 1)
    )
    blocks.extend(
        [
            block(
                [
                    "(rm-block fold-nil hopper:tree:fold-nil",
                    "  (quote (rel:fold:nil $element $accumulator $step $before))",
                    "  rm-nil",
                    "  (quote (rel:fold $element $accumulator $step $before",
                    "    (list:nil $element) $before)))",
                ]
            ),
            block(
                [
                    "(rm-block fold-cons hopper:tree:fold-cons",
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
                    "(rm-block any-here hopper:tree:any-here",
                    "  (quote (rel:any:here $element $target $predicate",
                    "    $values $answer (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence (quote ($predicate $values $answer)))",
                    "    rm-nil)",
                    "  (quote (rel:any $element $target $predicate",
                    "    $values $answer)))",
                ]
            ),
            block(
                [
                    "(rm-block any-there hopper:tree:any-there",
                    "  (quote (rel:any:there $element $target $predicate",
                    "    $head $tail $answer (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (rel:any $element $target $predicate",
                    "        $tail $answer)))",
                    "    rm-nil)",
                    "  (quote (rel:any $element $target $predicate",
                    "    (list:cons $element $head $tail) $answer)))",
                ]
            ),
            block(
                [
                    "(rm-block depth-step hopper:tree:depth-step",
                    "  (quote (hopper:depth:step-proof",
                    "    $before $child $child-depth $after",
                    "    (unquote $depth-evidence) (unquote $max-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $depth-evidence",
                    "      (quote (hopper:depth:f $child $child-depth)))",
                    "    (rm-cons",
                    "      (rm-premise $max-evidence",
                    "        (quote (hopper:nat:max $child-depth $before $after)))",
                    "      rm-nil))",
                    "  (quote (hopper:depth:step $before $child $after)))",
                ]
            ),
            block(
                [
                    "(rm-block depth hopper:tree:depth",
                    "  (quote (hopper:depth:proof",
                    "    $tree $root $children $maximum $depth",
                    "    (unquote $view-evidence) (unquote $fold-evidence)",
                    "    (unquote $successor-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $view-evidence",
                    "      (quote (hopper:tree:view $tree $root $children)))",
                    "    (rm-cons",
                    "      (rm-premise $fold-evidence",
                    "        (quote (rel:fold hopper:tree hopper:nat",
                    "          hopper:depth:step hopper:nat:n0",
                    "          $children $maximum)))",
                    "      (rm-cons",
                    "        (rm-premise $successor-evidence",
                    "          (quote (hopper:nat:succ $maximum $depth)))",
                    "        rm-nil)))",
                    "  (quote (hopper:depth:f $tree $depth)))",
                ]
            ),
            block(
                [
                    "(rm-block branch-any-child hopper:tree:branch-any-child",
                    "  (quote (hopper:is-branch:any-child-proof",
                    "    $remaining $child $path $path-tail",
                    "    (unquote $child-evidence) (unquote $tail-evidence)",
                    "    (unquote $branch-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $child-evidence",
                    "      (quote (rel:list:head hopper:tree",
                    "        $remaining $child)))",
                    "    (rm-cons",
                    "      (rm-premise $tail-evidence",
                    "        (quote (rel:list:tail hopper:nat",
                    "          $path $path-tail)))",
                    "      (rm-cons",
                    "        (rm-premise $branch-evidence",
                    "          (quote (hopper:is-branch:f $child $path-tail)))",
                    "        rm-nil)))",
                    "  (quote (hopper:is-branch:any-child $remaining $path)))",
                ]
            ),
            block(
                [
                    "(rm-block branch-leaf hopper:tree:branch-leaf",
                    "  (quote (hopper:is-branch:proof:leaf",
                    "    $root $path $path-tail",
                    "    (unquote $view-evidence) (unquote $tail-evidence)",
                    "    (unquote $empty-evidence) (unquote $head-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $view-evidence",
                    "      (quote (hopper:tree:view",
                    "        (hopper:tree:node $root (list:nil hopper:tree))",
                    "        $root (list:nil hopper:tree))))",
                    "    (rm-cons",
                    "      (rm-premise $tail-evidence",
                    "        (quote (rel:list:tail hopper:nat",
                    "          $path $path-tail)))",
                    "      (rm-cons",
                    "        (rm-premise $empty-evidence",
                    "          (quote (rel:list:empty hopper:nat $path-tail)))",
                    "        (rm-cons",
                    "          (rm-premise $head-evidence",
                    "            (quote (rel:list:head hopper:nat $path $root)))",
                    "          rm-nil))))",
                    "  (quote (hopper:is-branch:f",
                    "    (hopper:tree:node $root (list:nil hopper:tree)) $path)))",
                ]
            ),
            block(
                [
                    "(rm-block branch-node hopper:tree:branch-node",
                    "  (quote (hopper:is-branch:proof:node",
                    "    $root $first $rest $path",
                    "    (unquote $view-evidence) (unquote $head-evidence)",
                    "    (unquote $any-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $view-evidence",
                    "      (quote (hopper:tree:view",
                    "        (hopper:tree:node $root",
                    "          (list:cons hopper:tree $first $rest))",
                    "        $root (list:cons hopper:tree $first $rest))))",
                    "    (rm-cons",
                    "      (rm-premise $head-evidence",
                    "        (quote (rel:list:head hopper:nat $path $root)))",
                    "      (rm-cons",
                    "        (rm-premise $any-evidence",
                    "          (quote (rel:any hopper:tree (list hopper:nat)",
                    "            hopper:is-branch:any-child",
                    "            (list:cons hopper:tree $first $rest) $path)))",
                    "        rm-nil)))",
                    "  (quote (hopper:is-branch:f",
                    "    (hopper:tree:node $root",
                    "      (list:cons hopper:tree $first $rest)) $path)))",
                ]
            ),
            block(
                [
                    "(rm-block subtree-equal hopper:tree:subtree-equal",
                    "  (quote (hopper:is-subtree:proof:equal $tree))",
                    "  rm-nil",
                    "  (quote (hopper:is-subtree:f $tree $tree)))",
                ]
            ),
            block(
                [
                    "(rm-block subtree-any-child hopper:tree:subtree-any-child",
                    "  (quote (hopper:is-subtree:any-child-proof",
                    "    $remaining $child $target",
                    "    (unquote $child-evidence) (unquote $subtree-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $child-evidence",
                    "      (quote (rel:list:head hopper:tree",
                    "        $remaining $child)))",
                    "    (rm-cons",
                    "      (rm-premise $subtree-evidence",
                    "        (quote (hopper:is-subtree:f $child $target)))",
                    "      rm-nil))",
                    "  (quote (hopper:is-subtree:any-child",
                    "    $remaining $target)))",
                ]
            ),
            block(
                [
                    "(rm-block subtree-child hopper:tree:subtree-child",
                    "  (quote (hopper:is-subtree:proof:child",
                    "    $root $children $target",
                    "    (unquote $view-evidence) (unquote $any-evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $view-evidence",
                    "      (quote (hopper:tree:view",
                    "        (hopper:tree:node $root $children)",
                    "        $root $children)))",
                    "    (rm-cons",
                    "      (rm-premise $any-evidence",
                    "        (quote (rel:any hopper:tree hopper:tree",
                    "          hopper:is-subtree:any-child",
                    "          $children $target)))",
                    "      rm-nil))",
                    "  (quote (hopper:is-subtree:f",
                    "    (hopper:tree:node $root $children) $target)))",
                ]
            ),
        ]
    )
    return "\n".join(
        [
            "; Proof-producing execution of the three authored rose-tree programs.",
            "; Tree data remains at the explicit checked boundary; recursive",
            "; evidence and every child occurrence remain first-class.",
            "",
            "(= (hopper:table1:tree-relations:package)",
            "  (compile:rule-package hopper-table1-tree-relations-v1",
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
        "; Exact qualification for three authored rose-tree programs.",
        "; Proof occurrences are checked; non-derivation is never refutation.",
        "",
        "!(import! &self ../../lib/ilp/prime_native_list_types.metta)",
        "!(import! &self ../../lib/ilp/prime_relational_combinators_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_first_order_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_tree_relations_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_tree_relations_rules.metta)",
        "",
        "(= (hopper:tree-rel:proof-checks (quote $goal) $occurrences)",
        "  (collapse",
        "    (let (occurrence $proof-data) (superpose $occurrences)",
        "      (type:check (unquote $proof-data) $goal))))",
        "",
        "(= (hopper:tree-rel:classify $name (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (hopper:table1:tree-relations:package)",
        "      1024 100000000 131072 (quote $goal))",
        "    (let $count (- (size-atom $occurrences) 1)",
        "      (let $checks",
        "        (hopper:tree-rel:proof-checks (quote $goal) $occurrences)",
        "        (if (== $count 0)",
        "            (hopper:tree-rel:case $name not-derived $count $checks)",
        "            (hopper:tree-rel:case $name derived $count $checks))))))",
        "",
    ]
    expected = ["[()]" for _ in range(5)]
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
            fixture.extend(
                [
                    f"!(hopper:tree-rel:classify {name}",
                    f"  (quote {target_term(task, target)}))",
                ]
            )
            checks = " ".join("True" for _ in range(count))
            if count:
                expected.append(
                    f"[(hopper:tree-rel:case {name} derived "
                    f"{count} ({checks}))]"
                )
                derived += 1
                proof_occurrences += count
            else:
                expected.append(
                    f"[(hopper:tree-rel:case {name} not-derived 0 ())]"
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
        default=repo / "lib/ilp/hopper_table1_tree_relations_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/hopper_table1_tree_relations_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo
        / "examples/prime/hopper_table1_tree_relations_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo
        / "examples/prime/hopper_table1_tree_relations_ground_truth.expected",
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
        print(f"FAIL: Hopper tree-relation generation: {exc}", file=sys.stderr)
        return 1

    cases = sum(
        item["source_positive"] + item["source_negative"]
        for item in counts.values()
    )
    proofs = sum(item["proof_occurrences"] for item in counts.values())
    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        f"Hopper tree-relation qualification: {len(TASKS)} tasks, "
        f"{cases} source examples, {proofs} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
