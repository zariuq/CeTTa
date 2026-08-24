#!/usr/bin/env python3
"""Generate typed Prime qualification for Hopper's structural-list trio."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
from textwrap import dedent


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import check_prime_hopper_table1_manifest as corpus
import generate_prime_hopper_first_order as base
from prime_iggp_generation import GenerationError, materialize_outputs


TASKS = ("lastHalf", "of1And2", "isPalindrome")
SOURCE_VARIANT = "ho"
PROGRAMS = {
    "lastHalf": {
        "fo": (
            "f(A,B):-empty(B),empty(A).",
            "f(A,B):-tail(A,B),empty(B).",
            "f(A,B):-front(A,C),last(A,F),tail(C,E),f(E,D),app(D,F,B).",
        ),
        "ho": (
            "caselist_r_a(A,B,C):-front(B,E),caselist_a(E,D),app(D,A,C).",
            "caselist_p_a(A):-empty(A).",
            "f(A,B):-reverse(A,C),caselist_a(C,B).",
            "caselist_q_a(A,B):-any(A),empty(B).",
        ),
        "ho-opt": (
            "caselist_r_a(A,B,C):-front(B,E),caselist_a(E,D),app(D,A,C).",
            "caselist_p_a(A):-empty(A).",
            "f(A,B):-reverse(A,C),caselist_a(C,B).",
            "caselist_q_a(A,B):-any(A),empty(B).",
        ),
    },
    "of1And2": {
        "fo": None,
        "ho": (
            "f(A):-empty(A).",
            "try_q_a(A):-zero(B),suc(B,C),suc(C,A).",
            "f(A):-cons(A,C,B),try_a(C),f(B).",
            "try_p_a(A):-zero(B),suc(B,A).",
        ),
        "ho-opt": (
            "f(A):-empty(A).",
            "try_q_a(A):-zero(B),suc(B,C),suc(C,A).",
            "f(A):-cons(A,C,B),try_a(C),f(B).",
            "try_p_a(A):-zero(B),suc(B,A).",
        ),
    },
    "isPalindrome": {
        "fo": None,
        "ho": (
            "condlist_p_a(A,B):-any(A),empty(B).",
            "condlist_p_a(A,B):-last(B,A),front(B,C),condlist_a(C).",
            "f(A):-condlist_a(A).",
        ),
        "ho-opt": (
            "condlist_p_a(A,B):-any(A),empty(B).",
            "condlist_p_a(A,B):-last(B,A),front(B,C),condlist_a(C).",
            "f(A):-condlist_a(A).",
        ),
    },
}
EXPECTED_TARGETS = {
    "lastHalf": ("list", "list"),
    "of1And2": ("list",),
    "isPalindrome": ("list",),
}


def target_term(task: str, target: base.Atom) -> str:
    if task == "lastHalf":
        if len(target.args) != 2:
            raise GenerationError("lastHalf: target arity changed")
        source = base.render_list(base.list_items(target.args[0]), "hopper:atom")
        result = base.render_list(base.list_items(target.args[1]), "hopper:atom")
        return f"(hopper:last-half:f {source} {result})"
    if task == "of1And2":
        if len(target.args) != 1:
            raise GenerationError("of1And2: target arity changed")
        values = base.render_list(base.list_items(target.args[0]), "hopper:nat")
        return f"(hopper:of-one-and-two:f {values})"
    if task == "isPalindrome":
        if len(target.args) != 1:
            raise GenerationError("isPalindrome: target arity changed")
        values = base.render_list(base.list_items(target.args[0]), "hopper:atom")
        return f"(hopper:palindrome:f {values})"
    raise GenerationError(f"unsupported structural-list task {task}")


def proof_count(task: str, target: base.Atom) -> int:
    if task == "lastHalf":
        if len(target.args) != 2:
            raise GenerationError("lastHalf: target arity changed")
        source = base.list_items(target.args[0])
        expected = base.list_items(target.args[1])
        keep = len(source) // 2
        result = source[len(source) - keep :] if keep else ()
        return int(result == expected)
    if task == "of1And2":
        if len(target.args) != 1:
            raise GenerationError("of1And2: target arity changed")
        values = tuple(int(value) for value in base.list_items(target.args[0]))
        return int(all(value in (1, 2) for value in values))
    if task == "isPalindrome":
        if len(target.args) != 1:
            raise GenerationError("isPalindrome: target arity changed")
        values = base.list_items(target.args[0])
        return int(values == tuple(reversed(values)))
    raise GenerationError(f"unsupported structural-list task {task}")


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
        for variant in ("fo", "ho", "ho-opt"):
            best = variants[variant]["best_program"]
            expected = PROGRAMS[task][variant]
            observed = None if best is None else tuple(best["clauses"])
            if observed != expected:
                raise GenerationError(
                    f"{task}/{variant}: authored best program changed"
                )
        example_digests = {
            variants[variant]["files"]["exs.pl"]
            for variant in ("fo", "ho", "ho-opt")
        }
        if len(example_digests) != 1:
            raise GenerationError(f"{task}: FO/HO example corpus drift")
        path = snapshot_root / "examples" / task / selected["path"] / "exs.pl"
        parsed = base.parse_examples(path)
        observed_counts = {
            "positive": sum(polarity == "pos" for polarity, _ in parsed),
            "negative": sum(polarity == "neg" for polarity, _ in parsed),
        }
        if observed_counts != selected["examples"]:
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
    return dedent(
        """
        ; Typed structural-list specializations of three authored Hopper programs.
        ; The generic relations remain ordinary Prime values with explicit evidence.

        (: hopper:last-half:case
          (-> (source : (list hopper:atom))
              (target : (list hopper:atom)) (u 0)))
        (: hopper:last-half:case:nil
          (hopper:last-half:case
            (list:nil hopper:atom) (list:nil hopper:atom)))
        (: hopper:last-half:case:singleton
          (-> (head : hopper:atom)
              (hopper:last-half:case
                (list:cons hopper:atom head (list:nil hopper:atom))
                (list:nil hopper:atom))))
        (: hopper:last-half:case:cons
          (-> (head : hopper:atom) (next : hopper:atom)
              (rest : (list hopper:atom))
              (trimmed : (list hopper:atom))
              (recursive-result : (list hopper:atom))
              (target : (list hopper:atom))
              (front-evidence :
                (rel:list:front hopper:atom
                  (list:cons hopper:atom next rest) trimmed))
              (recursive-evidence :
                (hopper:last-half:case trimmed recursive-result))
              (snoc-evidence :
                (rel:list:snoc hopper:atom recursive-result head target))
              (hopper:last-half:case
                (list:cons hopper:atom head
                  (list:cons hopper:atom next rest)) target)))
        (: hopper:last-half:f
          (-> (source : (list hopper:atom))
              (target : (list hopper:atom)) (u 0)))
        (: hopper:last-half:proof
          (-> (source : (list hopper:atom))
              (reversed : (list hopper:atom))
              (target : (list hopper:atom))
              (reverse-evidence : (hopper:reverse:f source reversed))
              (case-evidence : (hopper:last-half:case reversed target))
              (hopper:last-half:f source target)))

        (: hopper:one (-> (value : hopper:nat) (u 0)))
        (: hopper:one:proof
          (-> (zero-evidence : (hopper:nat:zero hopper:nat:n0))
              (successor-evidence :
                (hopper:nat:succ hopper:nat:n0 hopper:nat:n1))
              (hopper:one hopper:nat:n1)))
        (: hopper:two (-> (value : hopper:nat) (u 0)))
        (: hopper:two:proof
          (-> (zero-evidence : (hopper:nat:zero hopper:nat:n0))
              (first-successor :
                (hopper:nat:succ hopper:nat:n0 hopper:nat:n1))
              (second-successor :
                (hopper:nat:succ hopper:nat:n1 hopper:nat:n2))
              (hopper:two hopper:nat:n2)))
        (: hopper:of-one-and-two:f
          (-> (values : (list hopper:nat)) (u 0)))
        (: hopper:of-one-and-two:nil
          (hopper:of-one-and-two:f (list:nil hopper:nat)))
        (: hopper:of-one-and-two:cons
          (-> (head : hopper:nat) (tail : (list hopper:nat))
              (choice-evidence :
                (rel:either hopper:nat hopper:one hopper:two head))
              (tail-evidence : (hopper:of-one-and-two:f tail))
              (hopper:of-one-and-two:f
                (list:cons hopper:nat head tail))))

        (: hopper:palindrome:f
          (-> (values : (list hopper:atom)) (u 0)))
        (: hopper:palindrome:nil
          (hopper:palindrome:f (list:nil hopper:atom)))
        (: hopper:palindrome:singleton
          (-> (head : hopper:atom)
              (hopper:palindrome:f
                (list:cons hopper:atom head (list:nil hopper:atom)))))
        (: hopper:palindrome:cons
          (-> (head : hopper:atom) (next : hopper:atom)
              (rest : (list hopper:atom))
              (middle : (list hopper:atom))
              (last-evidence :
                (rel:list:last hopper:atom
                  (list:cons hopper:atom next rest) head))
              (front-evidence :
                (rel:list:front hopper:atom
                  (list:cons hopper:atom next rest) middle))
              (recursive-evidence : (hopper:palindrome:f middle))
              (hopper:palindrome:f
                (list:cons hopper:atom head
                  (list:cons hopper:atom next rest)))))
        """
    ).lstrip()


def render_rules() -> str:
    return dedent(
        """
        ; Proof-producing structural-list Hopper realizations.
        ; Branch, front, last, reverse, and snoc evidence stays explicit.

        (= (hopper:table1:structural-list:package)
          (compile:rule-package hopper-table1-structural-list-v1
            (rm-package
              (rm-block front-singleton hopper:struct:front-singleton
                (quote (rel:list:front:singleton $element $head))
                rm-nil
                (quote (rel:list:front $element
                  (list:cons $element $head (list:nil $element))
                  (list:nil $element))))
              (rm-block front-cons hopper:struct:front-cons
                (quote (rel:list:front:cons $element $head $next $tail
                  $front-tail (unquote $tail-evidence)))
                (rm-cons
                  (rm-premise $tail-evidence
                    (quote (rel:list:front $element
                      (list:cons $element $next $tail) $front-tail)))
                  rm-nil)
                (quote (rel:list:front $element
                  (list:cons $element $head
                    (list:cons $element $next $tail))
                  (list:cons $element $head $front-tail))))
              (rm-block last-singleton hopper:struct:last-singleton
                (quote (rel:list:last:singleton $element $last))
                rm-nil
                (quote (rel:list:last $element
                  (list:cons $element $last (list:nil $element)) $last)))
              (rm-block last-cons hopper:struct:last-cons
                (quote (rel:list:last:cons $element $head $next $tail
                  $last (unquote $tail-evidence)))
                (rm-cons
                  (rm-premise $tail-evidence
                    (quote (rel:list:last $element
                      (list:cons $element $next $tail) $last)))
                  rm-nil)
                (quote (rel:list:last $element
                  (list:cons $element $head
                    (list:cons $element $next $tail)) $last)))
              (rm-block snoc-nil hopper:struct:snoc-nil
                (quote (rel:list:snoc:nil $element $last))
                rm-nil
                (quote (rel:list:snoc $element (list:nil $element) $last
                  (list:cons $element $last (list:nil $element)))))
              (rm-block snoc-cons hopper:struct:snoc-cons
                (quote (rel:list:snoc:cons $element $head $tail $last
                  $result-tail (unquote $tail-evidence)))
                (rm-cons
                  (rm-premise $tail-evidence
                    (quote (rel:list:snoc $element $tail $last $result-tail)))
                  rm-nil)
                (quote (rel:list:snoc $element
                  (list:cons $element $head $tail) $last
                  (list:cons $element $head $result-tail))))
              (rm-block fold-nil hopper:struct:fold-nil
                (quote (rel:fold:nil $element $accumulator $step $before))
                rm-nil
                (quote (rel:fold $element $accumulator $step $before
                  (list:nil $element) $before)))
              (rm-block fold-cons hopper:struct:fold-cons
                (quote (rel:fold:cons $element $accumulator $step
                  $before $head $tail $next $after
                  (unquote $step-evidence) (unquote $tail-evidence)))
                (rm-cons
                  (rm-premise $step-evidence
                    (quote ($step $before $head $next)))
                  (rm-cons
                    (rm-premise $tail-evidence
                      (quote (rel:fold $element $accumulator $step
                        $next $tail $after)))
                    rm-nil))
                (quote (rel:fold $element $accumulator $step $before
                  (list:cons $element $head $tail) $after)))
              (rm-block reverse-step hopper:struct:reverse-step
                (quote (hopper:reverse:step-proof $before $head
                  (rel:list:head-proof hopper:atom $head $before)
                  (rel:list:tail-proof hopper:atom $head $before)))
                rm-nil
                (quote (hopper:reverse:step $before $head
                  (list:cons hopper:atom $head $before))))
              (rm-block reverse hopper:struct:reverse
                (quote (hopper:reverse:proof $source $target
                  (unquote $evidence)))
                (rm-cons
                  (rm-premise $evidence
                    (quote (rel:fold hopper:atom (list hopper:atom)
                      hopper:reverse:step (list:nil hopper:atom)
                      $source $target)))
                  rm-nil)
                (quote (hopper:reverse:f $source $target)))
              (rm-block nat-zero hopper:struct:nat-zero
                (quote hopper:nat:proof:zero)
                rm-nil
                (quote (hopper:nat:zero hopper:nat:n0)))
              (rm-block nat-succ-0-1 hopper:struct:nat-succ-0-1
                (quote hopper:nat:proof:succ-0-1)
                rm-nil
                (quote (hopper:nat:succ hopper:nat:n0 hopper:nat:n1)))
              (rm-block nat-succ-1-2 hopper:struct:nat-succ-1-2
                (quote hopper:nat:proof:succ-1-2)
                rm-nil
                (quote (hopper:nat:succ hopper:nat:n1 hopper:nat:n2)))
              (rm-block either-left hopper:struct:either-left
                (quote (rel:either:left $value $left $right $subject
                  (unquote $evidence)))
                (rm-cons
                  (rm-premise $evidence (quote ($left $subject)))
                  rm-nil)
                (quote (rel:either $value $left $right $subject)))
              (rm-block either-right hopper:struct:either-right
                (quote (rel:either:right $value $left $right $subject
                  (unquote $evidence)))
                (rm-cons
                  (rm-premise $evidence (quote ($right $subject)))
                  rm-nil)
                (quote (rel:either $value $left $right $subject)))
              (rm-block last-half-nil hopper:struct:last-half-nil
                (quote hopper:last-half:case:nil)
                rm-nil
                (quote (hopper:last-half:case
                  (list:nil hopper:atom) (list:nil hopper:atom))))
              (rm-block last-half-singleton hopper:struct:last-half-singleton
                (quote (hopper:last-half:case:singleton $head))
                rm-nil
                (quote (hopper:last-half:case
                  (list:cons hopper:atom $head (list:nil hopper:atom))
                  (list:nil hopper:atom))))
              (rm-block last-half-cons hopper:struct:last-half-cons
                (quote (hopper:last-half:case:cons $head $next $rest
                  $trimmed $recursive-result $target
                  (unquote $front-evidence)
                  (unquote $recursive-evidence)
                  (unquote $snoc-evidence)))
                (rm-cons
                  (rm-premise $front-evidence
                    (quote (rel:list:front hopper:atom
                      (list:cons hopper:atom $next $rest) $trimmed)))
                  (rm-cons
                    (rm-premise $recursive-evidence
                      (quote (hopper:last-half:case
                        $trimmed $recursive-result)))
                    (rm-cons
                      (rm-premise $snoc-evidence
                        (quote (rel:list:snoc hopper:atom
                          $recursive-result $head $target)))
                      rm-nil)))
                (quote (hopper:last-half:case
                  (list:cons hopper:atom $head
                    (list:cons hopper:atom $next $rest)) $target)))
              (rm-block target-last-half hopper:struct:target-last-half
                (quote (hopper:last-half:proof $source $reversed $target
                  (unquote $reverse-evidence) (unquote $case-evidence)))
                (rm-cons
                  (rm-premise $reverse-evidence
                    (quote (hopper:reverse:f $source $reversed)))
                  (rm-cons
                    (rm-premise $case-evidence
                      (quote (hopper:last-half:case $reversed $target)))
                    rm-nil))
                (quote (hopper:last-half:f $source $target)))
              (rm-block one hopper:struct:one
                (quote (hopper:one:proof
                  (unquote $zero-evidence) (unquote $successor-evidence)))
                (rm-cons
                  (rm-premise $zero-evidence
                    (quote (hopper:nat:zero hopper:nat:n0)))
                  (rm-cons
                    (rm-premise $successor-evidence
                      (quote (hopper:nat:succ hopper:nat:n0 hopper:nat:n1)))
                    rm-nil))
                (quote (hopper:one hopper:nat:n1)))
              (rm-block two hopper:struct:two
                (quote (hopper:two:proof (unquote $zero-evidence)
                  (unquote $first-successor)
                  (unquote $second-successor)))
                (rm-cons
                  (rm-premise $zero-evidence
                    (quote (hopper:nat:zero hopper:nat:n0)))
                  (rm-cons
                    (rm-premise $first-successor
                      (quote (hopper:nat:succ hopper:nat:n0 hopper:nat:n1)))
                    (rm-cons
                      (rm-premise $second-successor
                        (quote (hopper:nat:succ hopper:nat:n1 hopper:nat:n2)))
                      rm-nil)))
                (quote (hopper:two hopper:nat:n2)))
              (rm-block of-one-and-two-nil hopper:struct:of-one-and-two-nil
                (quote hopper:of-one-and-two:nil)
                rm-nil
                (quote (hopper:of-one-and-two:f (list:nil hopper:nat))))
              (rm-block of-one-and-two-cons hopper:struct:of-one-and-two-cons
                (quote (hopper:of-one-and-two:cons $head $tail
                  (unquote $choice-evidence) (unquote $tail-evidence)))
                (rm-cons
                  (rm-premise $choice-evidence
                    (quote (rel:either hopper:nat hopper:one hopper:two $head)))
                  (rm-cons
                    (rm-premise $tail-evidence
                      (quote (hopper:of-one-and-two:f $tail)))
                    rm-nil))
                (quote (hopper:of-one-and-two:f
                  (list:cons hopper:nat $head $tail))))
              (rm-block palindrome-nil hopper:struct:palindrome-nil
                (quote hopper:palindrome:nil)
                rm-nil
                (quote (hopper:palindrome:f (list:nil hopper:atom))))
              (rm-block palindrome-singleton hopper:struct:palindrome-singleton
                (quote (hopper:palindrome:singleton $head))
                rm-nil
                (quote (hopper:palindrome:f
                  (list:cons hopper:atom $head (list:nil hopper:atom)))))
              (rm-block palindrome-cons hopper:struct:palindrome-cons
                (quote (hopper:palindrome:cons $head $next $rest $middle
                  (unquote $last-evidence) (unquote $front-evidence)
                  (unquote $recursive-evidence)))
                (rm-cons
                  (rm-premise $last-evidence
                    (quote (rel:list:last hopper:atom
                      (list:cons hopper:atom $next $rest) $head)))
                  (rm-cons
                    (rm-premise $front-evidence
                      (quote (rel:list:front hopper:atom
                        (list:cons hopper:atom $next $rest) $middle)))
                    (rm-cons
                      (rm-premise $recursive-evidence
                        (quote (hopper:palindrome:f $middle)))
                      rm-nil)))
                (quote (hopper:palindrome:f
                  (list:cons hopper:atom $head
                    (list:cons hopper:atom $next $rest)))))
            )))
        """
    ).lstrip()


def render_fixture(
    examples: dict[str, tuple[tuple[str, base.Atom], ...]], manifest: dict
) -> tuple[str, str, dict[str, dict[str, int]]]:
    entries = {entry["name"]: entry for entry in manifest["tasks"]}
    fixture = [
        "; Exact proof-relevant qualification for three structural-list tasks.",
        "; Every output occurrence is checked against its indexed target.",
        "",
        "!(import! &self ../../lib/ilp/prime_native_list_types.metta)",
        "!(import! &self ../../lib/ilp/prime_relational_combinators_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_first_order_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_structural_list_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_structural_list_rules.metta)",
        "",
        "(= (hopper:struct:proof-checks (quote $goal) $occurrences)",
        "  (collapse",
        "    (let (occurrence $proof-data) (superpose $occurrences)",
        "      (type:check (unquote $proof-data) $goal))))",
        "",
        "(= (hopper:struct:classify $name (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (hopper:table1:structural-list:package)",
        "      1024 20000000 8192 (quote $goal))",
        "    (let $count (- (size-atom $occurrences) 1)",
        "      (let $checks",
        "        (hopper:struct:proof-checks (quote $goal) $occurrences)",
        "        (if (== $count 0)",
        "            (hopper:struct:case $name not-derived $count $checks)",
        "            (hopper:struct:case $name derived $count $checks))))))",
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
            goal = target_term(task, target)
            fixture.extend(
                [
                    f"!(hopper:struct:classify {name}",
                    f"  (quote {goal}))",
                ]
            )
            checks = " ".join("True" for _ in range(count))
            if count:
                expected.append(
                    f"[(hopper:struct:case {name} derived {count} ({checks}))]"
                )
                derived += 1
                proof_occurrences += count
            else:
                expected.append(
                    f"[(hopper:struct:case {name} not-derived 0 ())]"
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
        default=repo / "lib/ilp/hopper_table1_structural_list_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/hopper_table1_structural_list_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo / "examples/prime/hopper_table1_structural_list_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo / "examples/prime/hopper_table1_structural_list_ground_truth.expected",
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
        print(f"FAIL: Hopper structural-list generation: {exc}", file=sys.stderr)
        return 1

    cases = sum(
        item["source_positive"] + item["source_negative"]
        for item in counts.values()
    )
    proofs = sum(item["proof_occurrences"] for item in counts.values())
    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        f"Hopper structural-list qualification: {len(TASKS)} tasks, "
        f"{cases} source examples, {proofs} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
