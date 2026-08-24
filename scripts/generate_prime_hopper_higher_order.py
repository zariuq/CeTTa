#!/usr/bin/env python3
"""Generate proof-relevant Prime qualification for Hopper's HO task pair."""

from __future__ import annotations

import argparse
from pathlib import Path
import string
import sys
from typing import Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import check_prime_hopper_table1_manifest as corpus
import generate_prime_hopper_first_order as base
from prime_iggp_generation import GenerationError, fact_block, materialize_outputs


TASKS = ("dropLast", "encryption")
SOURCE_VARIANT = "ho"
PROGRAMS = {
    "dropLast": (
        "f(A,B):-map(map_p_a,A,B).",
        "map_p_a(A,B):-reverse(A,C),tail(C,D),reverse(D,B).",
    ),
    "encryption": (
        "f(A,B):-map_a(A,B).",
        "map_p_a(A,B):-char_to_int(A,E),my_pred(E,D),my_pred(D,C),int_to_char(C,B).",
    ),
}
EXPECTED_TARGETS = {
    "dropLast": ("list", "list"),
    "encryption": ("list", "list"),
}
ALPHABET = tuple(string.ascii_lowercase)


def list_of_lists(term: base.Term) -> tuple[tuple[str, ...], ...]:
    if not isinstance(term, base.ListTerm):
        raise GenerationError(f"expected a list of lists, found {term}")
    return tuple(base.list_items(item) for item in term.items)


def render_atom_list(values: Iterable[str]) -> str:
    return base.render_list(values, "hopper:atom")


def render_nested_list(values: Iterable[Iterable[str]]) -> str:
    element = "(list hopper:atom)"
    result = f"(list:nil {element})"
    for value in reversed(tuple(tuple(items) for items in values)):
        result = (
            f"(list:cons {element} {render_atom_list(value)} {result})"
        )
    return result


def char_name(value: str) -> str:
    if value not in ALPHABET:
        raise GenerationError(f"unsupported Hopper cipher character {value!r}")
    return f"hopper:char:{value}"


def cipher_num_name(value: int) -> str:
    if not 0 <= value < len(ALPHABET):
        raise GenerationError(f"unsupported Hopper cipher number {value}")
    return f"hopper:cipher-num:n{value}"


def render_char_list(values: Iterable[str]) -> str:
    result = "(list:nil hopper:char)"
    for value in reversed(tuple(values)):
        result = (
            f"(list:cons hopper:char {char_name(value)} {result})"
        )
    return result


def render_runtime_char_list(values: Iterable[str]) -> str:
    return "(" + " ".join(char_name(value) for value in values) + ")"


def target_term(task: str, target: base.Atom) -> str:
    if len(target.args) != 2:
        raise GenerationError(f"{task}: target arity changed")
    if task == "dropLast":
        source = render_nested_list(list_of_lists(target.args[0]))
        result = render_nested_list(list_of_lists(target.args[1]))
        return f"(hopper:drop-last:f {source} {result})"
    if task == "encryption":
        source = render_char_list(base.list_items(target.args[0]))
        result = render_char_list(base.list_items(target.args[1]))
        return f"(hopper:encryption:f {source} {result})"
    raise GenerationError(f"unsupported higher-order Hopper task {task}")


def proof_count(task: str, target: base.Atom) -> int:
    if len(target.args) != 2:
        raise GenerationError(f"{task}: target arity changed")
    if task == "dropLast":
        source = list_of_lists(target.args[0])
        expected = list_of_lists(target.args[1])
        if any(not inner for inner in source):
            return 0
        return int(tuple(inner[:-1] for inner in source) == expected)
    if task == "encryption":
        source = base.list_items(target.args[0])
        expected = base.list_items(target.args[1])
        shifted = tuple(
            ALPHABET[(ALPHABET.index(value) - 2) % len(ALPHABET)]
            for value in source
        )
        return int(shifted == expected)
    raise GenerationError(f"unsupported higher-order Hopper task {task}")


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
        example_digests = {
            variants[variant]["files"]["exs.pl"]
            for variant in ("fo", "ho", "ho-opt")
        }
        if len(example_digests) != 1:
            raise GenerationError(f"{task}: FO/HO example corpus drift")
        if task == "dropLast":
            if variants["fo"]["best_program"] is not None:
                raise GenerationError(
                    "dropLast/fo: an authored best program unexpectedly appeared"
                )
            if tuple(variants["fo"]["target"]["types"]) != (
                "dlist",
                "dlist",
            ):
                raise GenerationError("dropLast/fo: dlist profile changed")
        else:
            best = variants["fo"]["best_program"]
            if best is None or tuple(best["clauses"]) != PROGRAMS[task]:
                raise GenerationError(
                    "encryption/fo: authored best program changed"
                )
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
    declarations = [
        "; Typed vocabulary for Hopper's two explicitly higher-order tasks.",
        "; Both targets are ordinary proof-relevant instances of `map-rel`.",
        "",
        "(: hopper:drop-last:element",
        "  (-> (source : (list hopper:atom))",
        "      (target : (list hopper:atom)) (u 0)))",
        "(: hopper:drop-last:element-proof",
        "  (-> (source : (list hopper:atom))",
        "      (reversed : (list hopper:atom))",
        "      (trimmed : (list hopper:atom))",
        "      (target : (list hopper:atom))",
        "      (source-reversal : (hopper:reverse:f source reversed))",
        "      (tail-evidence :",
        "        (rel:list:tail hopper:atom reversed trimmed))",
        "      (target-reversal : (hopper:reverse:f trimmed target))",
        "      (hopper:drop-last:element source target)))",
        "(: hopper:drop-last:f",
        "  (-> (source : (list (list hopper:atom)))",
        "      (target : (list (list hopper:atom))) (u 0)))",
        "(: hopper:drop-last:proof",
        "  (-> (source : (list (list hopper:atom)))",
        "      (target : (list (list hopper:atom)))",
        "      (evidence :",
        "        (map-rel (list hopper:atom) (list hopper:atom)",
        "          hopper:drop-last:element source target))",
        "      (hopper:drop-last:f source target)))",
        "",
        "(: hopper:char (u 0))",
        "(: hopper:cipher-num (u 0))",
    ]
    declarations.extend(f"(: {char_name(char)} hopper:char)" for char in ALPHABET)
    declarations.extend(
        f"(: {cipher_num_name(value)} hopper:cipher-num)"
        for value in range(len(ALPHABET))
    )
    declarations.extend(
        [
            "(: hopper:cipher:char-to-num",
            "  (-> (char : hopper:char) (number : hopper:cipher-num) (u 0)))",
            "(: hopper:cipher:num-to-char",
            "  (-> (number : hopper:cipher-num) (char : hopper:char) (u 0)))",
            "(: hopper:cipher:pred",
            "  (-> (later : hopper:cipher-num)",
            "      (earlier : hopper:cipher-num) (u 0)))",
        ]
    )
    declarations.extend(
        f"(: hopper:cipher:proof:char-to-num-{char} "
        f"(hopper:cipher:char-to-num {char_name(char)} {cipher_num_name(index)}))"
        for index, char in enumerate(ALPHABET)
    )
    declarations.extend(
        f"(: hopper:cipher:proof:num-to-char-{char} "
        f"(hopper:cipher:num-to-char {cipher_num_name(index)} {char_name(char)}))"
        for index, char in enumerate(ALPHABET)
    )
    declarations.extend(
        f"(: hopper:cipher:proof:pred-{later}-{earlier} "
        f"(hopper:cipher:pred {cipher_num_name(later)} {cipher_num_name(earlier)}))"
        for later, earlier in (
            (value, (value - 1) % len(ALPHABET))
            for value in range(len(ALPHABET))
        )
    )
    declarations.extend(
        [
            "(: hopper:cipher:element",
            "  (-> (source : hopper:char) (target : hopper:char) (u 0)))",
            "(: hopper:cipher:element-proof",
            "  (-> (source : hopper:char) (source-number : hopper:cipher-num)",
            "      (middle-number : hopper:cipher-num)",
            "      (target-number : hopper:cipher-num) (target : hopper:char)",
            "      (encode :",
            "        (hopper:cipher:char-to-num source source-number))",
            "      (first-predecessor :",
            "        (hopper:cipher:pred source-number middle-number))",
            "      (second-predecessor :",
            "        (hopper:cipher:pred middle-number target-number))",
            "      (decode :",
            "        (hopper:cipher:num-to-char target-number target))",
            "      (hopper:cipher:element source target)))",
            "(: hopper:encryption:f",
            "  (-> (source : (list hopper:char))",
            "      (target : (list hopper:char)) (u 0)))",
            "(: hopper:encryption:proof",
            "  (-> (source : (list hopper:char))",
            "      (target : (list hopper:char))",
            "      (evidence :",
            "        (map-rel hopper:char hopper:char",
            "          hopper:cipher:element source target))",
            "      (hopper:encryption:f source target)))",
            "",
            "(: hopper:cipher:branch",
            "  (-> (source : hopper:char) (target : hopper:char) (u 0)))",
            "(: hopper:cipher:branch-left",
            "  (hopper:cipher:branch hopper:char:a hopper:char:y))",
            "(: hopper:cipher:branch-right",
            "  (hopper:cipher:branch hopper:char:a hopper:char:y))",
            "(: hopper:cipher:branch-f",
            "  (-> (source : (list hopper:char))",
            "      (target : (list hopper:char)) (u 0)))",
            "(: hopper:cipher:branch-proof",
            "  (-> (source : (list hopper:char))",
            "      (target : (list hopper:char))",
            "      (evidence :",
            "        (map-rel hopper:char hopper:char",
            "          hopper:cipher:branch source target))",
            "      (hopper:cipher:branch-f source target)))",
            "",
        ]
    )
    return "\n".join(declarations)


def block(lines: Iterable[str]) -> str:
    return "\n".join(f"      {line}" for line in lines)


def render_rules() -> str:
    blocks = [
        block(
            [
                "(rm-block list-tail hopper:ho:list-tail",
                "  (quote (rel:list:tail-proof $element $head $tail))",
                "  rm-nil",
                "  (quote (rel:list:tail $element",
                "    (list:cons $element $head $tail) $tail)))",
            ]
        ),
        block(
            [
                "(rm-block fold-nil hopper:ho:fold-nil",
                "  (quote (rel:fold:nil $element $accumulator $step $before))",
                "  rm-nil",
                "  (quote (rel:fold $element $accumulator $step $before",
                "    (list:nil $element) $before)))",
            ]
        ),
        block(
            [
                "(rm-block fold-cons hopper:ho:fold-cons",
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
                "(rm-block reverse-step hopper:ho:reverse-step",
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
                "(rm-block reverse hopper:ho:reverse",
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
                "(rm-block map-rel-nil hopper:ho:map-rel-nil",
                "  (quote (map-rel:nil $source $target $relation))",
                "  rm-nil",
                "  (quote (map-rel $source $target $relation",
                "    (list:nil $source) (list:nil $target))))",
            ]
        ),
        block(
            [
                "(rm-block map-rel-cons hopper:ho:map-rel-cons",
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
                "(rm-block drop-last-element hopper:ho:drop-last-element",
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
                "(rm-block target-drop-last hopper:ho:target-drop-last",
                "  (quote (hopper:drop-last:proof $source $target",
                "    (unquote $evidence)))",
                "  (rm-cons",
                "    (rm-premise $evidence",
                "      (quote (map-rel (list hopper:atom) (list hopper:atom)",
                "        hopper:drop-last:element $source $target)))",
                "    rm-nil)",
                "  (quote (hopper:drop-last:f $source $target)))",
            ]
        ),
    ]
    blocks.extend(
        fact_block(
            f"char-to-num-{char}",
            f"hopper:ho:char-to-num-{char}",
            f"hopper:cipher:proof:char-to-num-{char}",
            f"(hopper:cipher:char-to-num {char_name(char)} {cipher_num_name(index)})",
        )
        for index, char in enumerate(ALPHABET)
    )
    blocks.extend(
        fact_block(
            f"num-to-char-{char}",
            f"hopper:ho:num-to-char-{char}",
            f"hopper:cipher:proof:num-to-char-{char}",
            f"(hopper:cipher:num-to-char {cipher_num_name(index)} {char_name(char)})",
        )
        for index, char in enumerate(ALPHABET)
    )
    blocks.extend(
        fact_block(
            f"pred-{later}-{earlier}",
            f"hopper:ho:pred-{later}-{earlier}",
            f"hopper:cipher:proof:pred-{later}-{earlier}",
            f"(hopper:cipher:pred {cipher_num_name(later)} {cipher_num_name(earlier)})",
        )
        for later, earlier in (
            (value, (value - 1) % len(ALPHABET))
            for value in range(len(ALPHABET))
        )
    )
    blocks.extend(
        [
            block(
                [
                    "(rm-block cipher-element hopper:ho:cipher-element",
                    "  (quote (hopper:cipher:element-proof",
                    "    $source $source-number $middle-number $target-number $target",
                    "    (unquote $encode) (unquote $first-predecessor)",
                    "    (unquote $second-predecessor) (unquote $decode)))",
                    "  (rm-cons",
                    "    (rm-premise $encode",
                    "      (quote (hopper:cipher:char-to-num $source $source-number)))",
                    "    (rm-cons",
                    "      (rm-premise $first-predecessor",
                    "        (quote (hopper:cipher:pred",
                    "          $source-number $middle-number)))",
                    "      (rm-cons",
                    "        (rm-premise $second-predecessor",
                    "          (quote (hopper:cipher:pred",
                    "            $middle-number $target-number)))",
                    "        (rm-cons",
                    "          (rm-premise $decode",
                    "            (quote (hopper:cipher:num-to-char",
                    "              $target-number $target)))",
                    "          rm-nil))))",
                    "  (quote (hopper:cipher:element $source $target)))",
                ]
            ),
            block(
                [
                    "(rm-block target-encryption hopper:ho:target-encryption",
                    "  (quote (hopper:encryption:proof $source $target",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (map-rel hopper:char hopper:char",
                    "        hopper:cipher:element $source $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:encryption:f $source $target)))",
                ]
            ),
            fact_block(
                "branch-left",
                "hopper:ho:branch-left",
                "hopper:cipher:branch-left",
                "(hopper:cipher:branch hopper:char:a hopper:char:y)",
            ),
            fact_block(
                "branch-right",
                "hopper:ho:branch-right",
                "hopper:cipher:branch-right",
                "(hopper:cipher:branch hopper:char:a hopper:char:y)",
            ),
            block(
                [
                    "(rm-block target-branch hopper:ho:target-branch",
                    "  (quote (hopper:cipher:branch-proof $source $target",
                    "    (unquote $evidence)))",
                    "  (rm-cons",
                    "    (rm-premise $evidence",
                    "      (quote (map-rel hopper:char hopper:char",
                    "        hopper:cipher:branch $source $target)))",
                    "    rm-nil)",
                    "  (quote (hopper:cipher:branch-f $source $target)))",
                ]
            ),
        ]
    )
    return "\n".join(
        [
            "; Proof-producing realization of Hopper's two HO programs.",
            "; `map-rel` remains generic; task relations are ordinary typed values.",
            "",
            "(= (hopper:table1:higher-order:package)",
            "  (compile:rule-package hopper-table1-higher-order-v1",
            "    (rm-package",
            *blocks,
            "    )))",
            "",
        ]
    )


def native_encryption_path() -> str:
    sorts = "hopper:encryption:sort"
    primitives = "hopper:encryption:primitive"
    char = "hopper:encryption:char-sort"
    number = "hopper:encryption:num-sort"
    encode = (
        f"(hyp:primitive {sorts} {primitives} {char} {number} "
        "hopper:encryption:encode-symbol)"
    )
    predecessor = (
        f"(hyp:primitive {sorts} {primitives} {number} {number} "
        "hopper:encryption:predecessor-symbol)"
    )
    decode = (
        f"(hyp:primitive {sorts} {primitives} {number} {char} "
        "hopper:encryption:decode-symbol)"
    )
    tail = (
        f"(hyp:chain {sorts} {primitives} {number} {number} {char} "
        f"{predecessor} {decode})"
    )
    middle = (
        f"(hyp:chain {sorts} {primitives} {number} {number} {char} "
        f"{predecessor} {tail})"
    )
    return (
        f"(hyp:chain {sorts} {primitives} {char} {number} {char} "
        f"{encode} {middle})"
    )


def render_native_encryption(
    examples: tuple[tuple[str, base.Atom], ...]
) -> tuple[str, str]:
    positive: list[str] = []
    negative: list[str] = []
    for polarity, target in examples:
        if len(target.args) != 2:
            raise GenerationError("encryption: target arity changed")
        source = render_runtime_char_list(base.list_items(target.args[0]))
        result = render_runtime_char_list(base.list_items(target.args[1]))
        rendered = f"(rel:list-example {source} {result})"
        (positive if polarity == "pos" else negative).append(rendered)
    if (len(positive), len(negative)) != (28, 13):
        raise GenerationError("encryption: native source counts changed")

    fixture = [
        "; Native typed-path learning for Hopper Table-1 encryption.",
        "; Search constructs ordinary proof-relevant `hyp` values; List",
        "; lifting then checks the learned relation against every source case.",
        "",
        "!(import! &self ../../lib/ilp/prime_native_mil_list.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_higher_order_types.metta)",
        "",
        "(: hopper:encryption:sort (u 0))",
        "(: hopper:encryption:char-sort hopper:encryption:sort)",
        "(: hopper:encryption:num-sort hopper:encryption:sort)",
        "(: hopper:encryption:primitive",
        "  (-> hopper:encryption:sort",
        "      (-> hopper:encryption:sort (u 1))))",
        "(: hopper:encryption:encode-symbol",
        "  (hopper:encryption:primitive",
        "    hopper:encryption:char-sort hopper:encryption:num-sort))",
        "(: hopper:encryption:predecessor-symbol",
        "  (hopper:encryption:primitive",
        "    hopper:encryption:num-sort hopper:encryption:num-sort))",
        "(: hopper:encryption:decode-symbol",
        "  (hopper:encryption:primitive",
        "    hopper:encryption:num-sort hopper:encryption:char-sort))",
        "",
        "(= (hyp:carrier hopper:encryption:char-sort) hopper:char)",
        "(= (hyp:carrier hopper:encryption:num-sort) hopper:cipher-num)",
        "(= (hyp:meaning hopper:encryption:char-sort",
        "     hopper:encryption:num-sort hopper:encryption:encode-symbol)",
        "   hopper:cipher:char-to-num)",
        "(= (hyp:meaning hopper:encryption:num-sort",
        "     hopper:encryption:num-sort hopper:encryption:predecessor-symbol)",
        "   hopper:cipher:pred)",
        "(= (hyp:meaning hopper:encryption:num-sort",
        "     hopper:encryption:char-sort hopper:encryption:decode-symbol)",
        "   hopper:cipher:num-to-char)",
        "",
    ]
    fixture.extend(
        f"(= (rel:apply hopper:cipher:char-to-num {char_name(char)}) "
        f"(rel:edge {cipher_num_name(index)} "
        f"hopper:cipher:proof:char-to-num-{char}))"
        for index, char in enumerate(ALPHABET)
    )
    fixture.extend(
        f"(= (rel:apply hopper:cipher:pred {cipher_num_name(later)}) "
        f"(rel:edge {cipher_num_name(earlier)} "
        f"hopper:cipher:proof:pred-{later}-{earlier}))"
        for later, earlier in (
            (value, (value - 1) % len(ALPHABET))
            for value in range(len(ALPHABET))
        )
    )
    fixture.extend(
        f"(= (rel:apply hopper:cipher:num-to-char {cipher_num_name(index)}) "
        f"(rel:edge {char_name(char)} "
        f"hopper:cipher:proof:num-to-char-{char}))"
        for index, char in enumerate(ALPHABET)
    )
    fixture.extend(
        [
            "",
            "(= (hopper:encryption:path-shape)",
            "   (hyp:path:more",
            "     (hyp:path:more",
            "       (hyp:path:more hyp:path:one))))",
            "",
            "(= (hopper:encryption:scalar-examples)",
            "   (hyp:examples",
            "     ((hyp:example hopper:char:a hopper:char:y))",
            "     ((hyp:example hopper:char:a hopper:char:a))))",
            "",
            "(= (hopper:encryption:list-examples)",
            "   (rel:list-examples",
            "     (",
            *(f"       {item}" for item in positive),
            "     )",
            "     (",
            *(f"       {item}" for item in negative),
            "     )))",
            "",
            "(= (hopper:encryption:learn)",
            "   (hyp:learn-path",
            "     &self hopper:encryption:sort hopper:encryption:primitive",
            "     hopper:encryption:char-sort hopper:encryption:char-sort",
            "     (hopper:encryption:path-shape)",
            "     (hopper:encryption:scalar-examples)))",
            "",
            "!(list:len",
            "   (collapse",
            "     (map-rel:run hopper:cipher:char-to-num",
            "       (hopper:char:a))))",
            "",
            "!(hyp:path-benchmark",
            "   hopper-encryption-native",
            "   (native-hyp (candidate-bag exact) (proof-relevant True)",
            "     (relator list) (source hopper-table-1-encryption))",
            "   &self hopper:encryption:sort hopper:encryption:primitive",
            "   hopper:encryption:char-sort hopper:encryption:char-sort",
            "   (hopper:encryption:path-shape)",
            "   (hopper:encryption:scalar-examples))",
            "",
            "!(hopper:encryption:learn)",
            "",
            "!(let $program (hopper:encryption:learn)",
            "   (rel:list-consistent",
            "     (hyp:relation $program)",
            "     (hopper:encryption:list-examples)))",
            "",
            "!(let $program (hopper:encryption:learn)",
            "   (list:len",
            "     (collapse",
            "       (map-rel:run (hyp:relation $program)",
            "         (hopper:char:a)))))",
        ]
    )

    program = native_encryption_path()
    expected = [
        "[()]",
        "[()]",
        "[1]",
        "[(MIL:BenchV1 (Name hopper-encryption-native) "
        "(Mode (native-hyp (candidate-bag exact) (proof-relevant True) "
        "(relator list) (source hopper-table-1-encryption))) "
        "(CandidatesGenerated 81) (CandidatesChecked 81) "
        "(Established 2) (Refuted 79) (Undetermined 0) (Incomplete 0) "
        "(SafeFrontier 2) (ExampleChecked 2) (ExampleConsistent 1) "
        "(CountConserved True) (TraceConserved True) "
        "(TypedProducerEqualsSafeFrontier True))]",
        f"[(quote {program})]",
        "[True]",
        "[1]",
    ]
    return "\n".join(fixture) + "\n", "\n".join(expected) + "\n"


def render_fixture(
    examples: dict[str, tuple[tuple[str, base.Atom], ...]], manifest: dict
) -> tuple[str, str, dict[str, dict[str, int]]]:
    entries = {entry["name"]: entry for entry in manifest["tasks"]}
    fixture = [
        "; Exact proof-relevant qualification for Hopper's two HO tasks.",
        "; Every output occurrence is checked against its indexed target.",
        "",
        "!(import! &self ../../lib/ilp/prime_native_list_types.metta)",
        "!(import! &self ../../lib/ilp/prime_native_list_relator_types.metta)",
        "!(import! &self ../../lib/ilp/prime_relational_combinators_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_first_order_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_higher_order_types.metta)",
        "!(import! &self ../../lib/ilp/hopper_table1_higher_order_rules.metta)",
        "",
        "(= (hopper:ho:proof-checks (quote $goal) $occurrences)",
        "  (collapse",
        "    (let (occurrence $proof-data) (superpose $occurrences)",
        "      (type:check (unquote $proof-data) $goal))))",
        "",
        "(= (hopper:ho:classify $name (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (hopper:table1:higher-order:package)",
        "      1024 20000000 8192 (quote $goal))",
        "    (let $count (- (size-atom $occurrences) 1)",
        "      (let $checks (hopper:ho:proof-checks (quote $goal) $occurrences)",
        "        (if (== $count 0)",
        "            (hopper:ho:case $name not-derived $count $checks)",
        "            (hopper:ho:case $name derived $count $checks))))))",
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
                    f"!(hopper:ho:classify {name}",
                    f"  (quote {goal}))",
                ]
            )
            checks = " ".join("True" for _ in range(count))
            if count:
                expected.append(
                    f"[(hopper:ho:case {name} derived {count} ({checks}))]"
                )
                derived += 1
                proof_occurrences += count
            else:
                expected.append(
                    f"[(hopper:ho:case {name} not-derived 0 ())]"
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

    source = render_char_list(("a",))
    target = render_char_list(("y",))
    fixture.extend(
        [
            "",
            "; Two equal endpoints retain two distinct element proofs.",
            "!(hopper:ho:classify hopper:cipher:branch-canary",
            f"  (quote (hopper:cipher:branch-f {source} {target})))",
        ]
    )
    expected.append(
        "[(hopper:ho:case hopper:cipher:branch-canary derived 2 "
        "(True True))]"
    )
    return "\n".join(fixture) + "\n", "\n".join(expected) + "\n", counts


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument(
        "--types-output",
        type=Path,
        default=repo / "lib/ilp/hopper_table1_higher_order_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/hopper_table1_higher_order_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo / "examples/prime/hopper_table1_higher_order_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo / "examples/prime/hopper_table1_higher_order_ground_truth.expected",
    )
    parser.add_argument(
        "--native-fixture-output",
        type=Path,
        default=repo / "examples/prime/hopper_encryption_native_learning.metta",
    )
    parser.add_argument(
        "--native-expected-output",
        type=Path,
        default=repo / "examples/prime/hopper_encryption_native_learning.expected",
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    try:
        examples, manifest = load_sources(args.snapshot_root, repo)
        fixture, expected, counts = render_fixture(examples, manifest)
        native_fixture, native_expected = render_native_encryption(
            examples["encryption"]
        )
        materialize_outputs(
            (
                (args.types_output, render_types()),
                (args.rules_output, render_rules()),
                (args.fixture_output, fixture),
                (args.expected_output, expected),
                (args.native_fixture_output, native_fixture),
                (args.native_expected_output, native_expected),
            ),
            args.check,
        )
    except (GenerationError, corpus.ManifestError, KeyError, OSError) as exc:
        print(f"FAIL: Hopper higher-order generation: {exc}", file=sys.stderr)
        return 1

    cases = sum(
        item["source_positive"] + item["source_negative"]
        for item in counts.values()
    )
    proofs = sum(item["proof_occurrences"] for item in counts.values())
    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        f"Hopper higher-order qualification: {len(TASKS)} tasks, "
        f"{cases} source examples, {proofs} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
