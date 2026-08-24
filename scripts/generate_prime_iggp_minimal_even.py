#!/usr/bin/env python3
"""Generate the exact proof-relevant Prime qualification for minimal_even."""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path
import re
import sys
from typing import Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import check_prime_iggp_manifest as corpus
from prime_iggp_generation import (
    GenerationError,
    GroundAtom,
    State,
    checked_source,
    fact_block,
    load_game_states,
    materialize_outputs,
    parse_gdl,
    parse_ground_atom,
)


GAME = "minimal_even"
GDL_PATH = "games/minimal_even.txt"
GDL_SHA256 = "f8b27304bd1770b425de9951b48c419490b60d629e0de351a460d8a7edccd238"
PROLOG_PATH = "games/minimal_even.pl"
PROLOG_SHA256 = (
    "410ab20be9cbbf00bc21dbf889cad18645997e58554a31593921133a2086c89a"
)
TYPE_PATH = "types/minimal_even.typ"
TYPE_SHA256 = "258313697b80f0714dd0e9eb283dac0748787c23e46f516488ef514765e01547"
EXPECTED_CASES = 2759
EXPECTED_DERIVED_CASES = 1073
EXPECTED_PROOF_OCCURRENCES = 1657
SUCCESSORS = tuple((value, value + 1) for value in range(10))


CANONICAL_GDL = (
    ("<=", ("legal", "player", ("choose", "?x")), ("num", "?x")),
    (
        "<=",
        ("next", ("chosen", "?x")),
        ("does", "player", ("choose", "?x")),
    ),
    ("<=", ("next", ("chosen", "?x")), ("true", ("chosen", "?x"))),
    ("<=", "terminal", ("true", ("chosen", "?x")), ("even", "?x")),
    (
        "<=",
        ("goal", "player", "10"),
        ("true", ("chosen", "?x")),
        ("even", "?x"),
    ),
    ("<=", ("num", "?x"), ("succ", "?x", "?y")),
    ("<=", ("num", "?y"), ("succ", "?x", "?y")),
    *(("succ", str(first), str(second)) for first, second in SUCCESSORS),
    ("even", "0"),
    (
        "<=",
        ("even", "?x"),
        ("even", "?y"),
        ("succ", "?y", "?z"),
        ("succ", "?z", "?x"),
    ),
)


STATIC_CLOSURE = (
    "even(0)",
    "even(10)",
    "even(2)",
    "even(4)",
    "even(6)",
    "even(8)",
    "num(0)",
    "num(1)",
    "num(10)",
    "num(2)",
    "num(3)",
    "num(4)",
    "num(5)",
    "num(6)",
    "num(7)",
    "num(8)",
    "num(9)",
    *(f"succ({first}, {second})" for first, second in SUCCESSORS),
)


def validate_rule_sources(snapshot_root: Path) -> None:
    gdl = checked_source(
        snapshot_root / GDL_PATH, GDL_SHA256, "canonical minimal_even GDL"
    ).decode("utf-8")
    if parse_gdl(gdl) != CANONICAL_GDL:
        raise GenerationError("canonical minimal_even GDL structure changed")

    prolog = checked_source(
        snapshot_root / PROLOG_PATH,
        PROLOG_SHA256,
        "excluded minimal_even Prolog translation",
    ).decode("utf-8")
    translated = re.sub(r"\s+", "", prolog)
    if "next_chosen(X):-does_chose(player,X)." not in translated:
        raise GenerationError("expected does_chose negative control vanished")

    checked_source(
        snapshot_root / TYPE_PATH,
        TYPE_SHA256,
        "minimal_even type declarations",
    )


def load_states(snapshot_root: Path, repo: Path) -> tuple[State, ...]:
    validate_rule_sources(snapshot_root)
    states = load_game_states(snapshot_root, repo, GAME, "minimal-even")
    for state in states:
        if state.statics != STATIC_CLOSURE:
            raise GenerationError(
                f"{state.episode}: source static closure changed"
            )
    return states


def leaf(atom: GroundAtom) -> str:
    if atom.args:
        raise GenerationError(f"expected leaf, got {atom}")
    if atom.head == "player":
        return "minimal-even:player"
    if atom.head.isdigit() and 0 <= int(atom.head) <= 10:
        return f"minimal-even:n{int(atom.head)}"
    raise GenerationError(f"unsupported minimal_even symbol {atom.head}")


def prime_atom(text: str, episode: str) -> str:
    atom = parse_ground_atom(text)
    if atom.head == "goal" and len(atom.args) == 2:
        return (
            f"(minimal-even:goal {episode} {leaf(atom.args[0])} "
            f"{leaf(atom.args[1])})"
        )
    if atom.head == "legal_choose" and len(atom.args) == 2:
        return (
            f"(minimal-even:legal {episode} {leaf(atom.args[0])} "
            f"(minimal-even:choose {leaf(atom.args[1])}))"
        )
    if atom.head == "next_chosen" and len(atom.args) == 1:
        return f"(minimal-even:next-chosen {episode} {leaf(atom.args[0])})"
    if atom.head == "terminal" and not atom.args:
        return f"(minimal-even:terminal {episode})"
    if atom.head == "true_chosen" and len(atom.args) == 1:
        return f"(minimal-even:true-chosen {episode} {leaf(atom.args[0])})"
    if atom.head == "does_choose" and len(atom.args) == 2:
        return (
            f"(minimal-even:does {episode} {leaf(atom.args[0])} "
            f"(minimal-even:choose {leaf(atom.args[1])}))"
        )
    if atom.head == "succ" and len(atom.args) == 2:
        return (
            f"(minimal-even:succ {episode} {leaf(atom.args[0])} "
            f"{leaf(atom.args[1])})"
        )
    if atom.head in {"num", "even"} and len(atom.args) == 1:
        return (
            f"(minimal-even:{atom.head} {episode} {leaf(atom.args[0])})"
        )
    raise GenerationError(f"unsupported minimal_even atom {text!r}")


def integer(atom: GroundAtom) -> int:
    if atom.args or not atom.head.isdigit():
        raise GenerationError(f"expected source integer, got {atom}")
    value = int(atom.head)
    if not 0 <= value <= 10:
        raise GenerationError(f"source integer is outside minimal_even: {value}")
    return value


def num_proof_counts() -> dict[int, int]:
    counts: Counter[int] = Counter()
    for first, second in SUCCESSORS:
        counts[first] += 1
        counts[second] += 1
    return dict(counts)


def even_proof_counts() -> dict[int, int]:
    edge_counts = Counter(SUCCESSORS)
    counts: dict[int, int] = {0: 1}
    for value in range(1, 11):
        counts[value] = sum(
            counts.get(earlier, 0)
            * edge_counts[(earlier, middle)]
            * edge_counts[(middle, value)]
            for earlier in range(value)
            for middle in range(value + 1)
        )
    return counts


NUM_PROOF_COUNTS = num_proof_counts()
EVEN_PROOF_COUNTS = even_proof_counts()


def proof_count(state: State, atom_text: str) -> int:
    atom = parse_ground_atom(atom_text)
    background = tuple(parse_ground_atom(item) for item in state.background)
    if atom.head == "legal_choose" and len(atom.args) == 2:
        return NUM_PROOF_COUNTS.get(integer(atom.args[1]), 0)
    if atom.head == "next_chosen" and len(atom.args) == 1:
        value = integer(atom.args[0])
        return sum(
            1
            for fact in background
            if (
                fact.head == "true_chosen"
                and integer(fact.args[0]) == value
            )
            or (
                fact.head == "does_choose"
                and integer(fact.args[1]) == value
            )
        )
    if atom.head == "goal" and len(atom.args) == 2:
        if integer(atom.args[1]) != 10:
            return 0
        return sum(
            EVEN_PROOF_COUNTS[integer(fact.args[0])]
            for fact in background
            if fact.head == "true_chosen"
        )
    if atom.head == "terminal" and not atom.args:
        return sum(
            EVEN_PROOF_COUNTS[integer(fact.args[0])]
            for fact in background
            if fact.head == "true_chosen"
        )
    raise GenerationError(f"unsupported target atom {atom_text!r}")


def background_proof_name(state: State, ordinal: int) -> str:
    return (
        f"minimal-even:proof:background:{state.target}:{state.split}:"
        f"state-{state.ordinal}:occurrence-{ordinal}"
    )


def render_types(states: Iterable[State]) -> str:
    state_list = tuple(states)
    lines = [
        "; Generated exact-image Prime declarations for IGGP minimal_even.",
        "; Integers are source constants; arithmetic remains ordinary relations.",
        "; Recursive even and both num derivations remain proof-relevant.",
        "",
        "(: minimal-even:state (u 0))",
        "(: minimal-even:agent (u 0))",
        "(: minimal-even:action (u 0))",
        "(: minimal-even:int (u 0))",
        "(: minimal-even:player minimal-even:agent)",
    ]
    lines.extend(
        f"(: minimal-even:n{value} minimal-even:int)"
        for value in range(11)
    )
    lines.extend(
        [
            "(: minimal-even:choose",
            "  (-> (value : minimal-even:int) minimal-even:action))",
            "",
            "(: minimal-even:true-chosen",
            "  (-> (state : minimal-even:state) (value : minimal-even:int) (u 0)))",
            "(: minimal-even:does",
            "  (-> (state : minimal-even:state) (agent : minimal-even:agent)",
            "      (action : minimal-even:action) (u 0)))",
            "(: minimal-even:succ",
            "  (-> (state : minimal-even:state) (first : minimal-even:int)",
            "      (second : minimal-even:int) (u 0)))",
            "(: minimal-even:num",
            "  (-> (state : minimal-even:state) (value : minimal-even:int) (u 0)))",
            "(: minimal-even:even",
            "  (-> (state : minimal-even:state) (value : minimal-even:int) (u 0)))",
            "(: minimal-even:goal",
            "  (-> (state : minimal-even:state) (agent : minimal-even:agent)",
            "      (value : minimal-even:int) (u 0)))",
            "(: minimal-even:legal",
            "  (-> (state : minimal-even:state) (agent : minimal-even:agent)",
            "      (action : minimal-even:action) (u 0)))",
            "(: minimal-even:next-chosen",
            "  (-> (state : minimal-even:state) (value : minimal-even:int) (u 0)))",
            "(: minimal-even:terminal",
            "  (-> (state : minimal-even:state) (u 0)))",
            "",
        ]
    )
    lines.extend(f"(: {state.episode} minimal-even:state)" for state in state_list)
    lines.append("")

    for first, second in SUCCESSORS:
        lines.extend(
            [
                f"(: minimal-even:proof:succ-{first}-{second}",
                "  (-> (state : minimal-even:state)",
                f"      (minimal-even:succ state minimal-even:n{first}",
                f"        minimal-even:n{second})))",
            ]
        )
    lines.extend(
        [
            "",
            "(: minimal-even:proof:num-first",
            "  (-> (state : minimal-even:state)",
            "      (first : minimal-even:int) (second : minimal-even:int)",
            "      (successor-proof : (minimal-even:succ state first second))",
            "      (minimal-even:num state first)))",
            "(: minimal-even:proof:num-second",
            "  (-> (state : minimal-even:state)",
            "      (first : minimal-even:int) (second : minimal-even:int)",
            "      (successor-proof : (minimal-even:succ state first second))",
            "      (minimal-even:num state second)))",
            "(: minimal-even:proof:even-zero",
            "  (-> (state : minimal-even:state)",
            "      (minimal-even:even state minimal-even:n0)))",
            "(: minimal-even:proof:even-step",
            "  (-> (state : minimal-even:state)",
            "      (value : minimal-even:int) (earlier : minimal-even:int)",
            "      (middle : minimal-even:int)",
            "      (earlier-proof : (minimal-even:even state earlier))",
            "      (first-successor : (minimal-even:succ state earlier middle))",
            "      (second-successor : (minimal-even:succ state middle value))",
            "      (minimal-even:even state value)))",
            "",
            "(: minimal-even:proof:legal",
            "  (-> (state : minimal-even:state) (value : minimal-even:int)",
            "      (number-proof : (minimal-even:num state value))",
            "      (minimal-even:legal state minimal-even:player",
            "        (minimal-even:choose value))))",
            "(: minimal-even:proof:next-from-does",
            "  (-> (state : minimal-even:state) (value : minimal-even:int)",
            "      (does-proof : (minimal-even:does state minimal-even:player",
            "        (minimal-even:choose value)))",
            "      (minimal-even:next-chosen state value)))",
            "(: minimal-even:proof:next-from-true",
            "  (-> (state : minimal-even:state) (value : minimal-even:int)",
            "      (true-proof : (minimal-even:true-chosen state value))",
            "      (minimal-even:next-chosen state value)))",
            "(: minimal-even:proof:terminal",
            "  (-> (state : minimal-even:state) (value : minimal-even:int)",
            "      (true-proof : (minimal-even:true-chosen state value))",
            "      (even-proof : (minimal-even:even state value))",
            "      (minimal-even:terminal state)))",
            "(: minimal-even:proof:goal",
            "  (-> (state : minimal-even:state) (value : minimal-even:int)",
            "      (true-proof : (minimal-even:true-chosen state value))",
            "      (even-proof : (minimal-even:even state value))",
            "      (minimal-even:goal state minimal-even:player minimal-even:n10)))",
            "",
        ]
    )
    for state in state_list:
        for ordinal, atom in enumerate(state.background, 1):
            lines.append(
                f"(: {background_proof_name(state, ordinal)} "
                f"{prime_atom(atom, state.episode)})"
            )
    lines.append("")
    return "\n".join(lines)


def relational_blocks(state: State) -> tuple[str, ...]:
    episode = state.episode
    num_first = "\n".join(
        [
            "      (rm-block num-first iggp:minimal-even:num-first",
            "        (quote",
            f"          (minimal-even:proof:num-first {episode} $first $second",
            "            (unquote $successor-proof)))",
            "        (rm-cons",
            "          (rm-premise $successor-proof",
            f"            (quote (minimal-even:succ {episode} $first $second)))",
            "          rm-nil)",
            f"        (quote (minimal-even:num {episode} $first)))",
        ]
    )
    num_second = "\n".join(
        [
            "      (rm-block num-second iggp:minimal-even:num-second",
            "        (quote",
            f"          (minimal-even:proof:num-second {episode} $first $second",
            "            (unquote $successor-proof)))",
            "        (rm-cons",
            "          (rm-premise $successor-proof",
            f"            (quote (minimal-even:succ {episode} $first $second)))",
            "          rm-nil)",
            f"        (quote (minimal-even:num {episode} $second)))",
        ]
    )
    even_step = "\n".join(
        [
            "      (rm-block even-step iggp:minimal-even:even-step",
            "        (quote",
            f"          (minimal-even:proof:even-step {episode}",
            "            $value $earlier $middle",
            "            (unquote $earlier-proof)",
            "            (unquote $first-successor)",
            "            (unquote $second-successor)))",
            "        (rm-cons",
            "          (rm-premise $earlier-proof",
            f"            (quote (minimal-even:even {episode} $earlier)))",
            "          (rm-cons",
            "            (rm-premise $first-successor",
            f"              (quote (minimal-even:succ {episode} $earlier $middle)))",
            "            (rm-cons",
            "              (rm-premise $second-successor",
            f"                (quote (minimal-even:succ {episode} $middle $value)))",
            "              rm-nil)))",
            f"        (quote (minimal-even:even {episode} $value)))",
        ]
    )
    return (num_first, num_second, even_step)


def target_blocks(state: State) -> tuple[str, ...]:
    episode = state.episode
    if state.target == "legal":
        return (
            "\n".join(
                [
                    "      (rm-block legal iggp:minimal-even:legal",
                    "        (quote",
                    f"          (minimal-even:proof:legal {episode} $value",
                    "            (unquote $number-proof)))",
                    "        (rm-cons",
                    "          (rm-premise $number-proof",
                    f"            (quote (minimal-even:num {episode} $value)))",
                    "          rm-nil)",
                    f"        (quote (minimal-even:legal {episode}",
                    "          minimal-even:player (minimal-even:choose $value))))",
                ]
            ),
        )
    if state.target == "next":
        from_does = "\n".join(
            [
                "      (rm-block next-from-does iggp:minimal-even:next-from-does",
                "        (quote",
                f"          (minimal-even:proof:next-from-does {episode} $value",
                "            (unquote $does-proof)))",
                "        (rm-cons",
                "          (rm-premise $does-proof",
                f"            (quote (minimal-even:does {episode}",
                "              minimal-even:player (minimal-even:choose $value))))",
                "          rm-nil)",
                f"        (quote (minimal-even:next-chosen {episode} $value)))",
            ]
        )
        from_true = "\n".join(
            [
                "      (rm-block next-from-true iggp:minimal-even:next-from-true",
                "        (quote",
                f"          (minimal-even:proof:next-from-true {episode} $value",
                "            (unquote $true-proof)))",
                "        (rm-cons",
                "          (rm-premise $true-proof",
                f"            (quote (minimal-even:true-chosen {episode} $value)))",
                "          rm-nil)",
                f"        (quote (minimal-even:next-chosen {episode} $value)))",
            ]
        )
        return (from_does, from_true)
    constructor = "goal" if state.target == "goal" else "terminal"
    head = (
        f"(minimal-even:goal {episode} minimal-even:player minimal-even:n10)"
        if state.target == "goal"
        else f"(minimal-even:terminal {episode})"
    )
    return (
        "\n".join(
            [
                f"      (rm-block {constructor} iggp:minimal-even:{constructor}",
                "        (quote",
                f"          (minimal-even:proof:{constructor} {episode} $value",
                "            (unquote $true-proof) (unquote $even-proof)))",
                "        (rm-cons",
                "          (rm-premise $true-proof",
                f"            (quote (minimal-even:true-chosen {episode} $value)))",
                "          (rm-cons",
                "            (rm-premise $even-proof",
                f"              (quote (minimal-even:even {episode} $value)))",
                "            rm-nil))",
                f"        (quote {head}))",
            ]
        ),
    )


def render_rules(states: Iterable[State]) -> str:
    lines = [
        "; Generated proof-producing rules for all pinned minimal_even states.",
        "; Source arithmetic stays relational: no numeral or parity checker opcode.",
        "; Both num rules and both next rules retain distinct proof occurrences.",
        "",
    ]
    for state in states:
        blocks: list[str] = []
        relational = relational_blocks(state)
        for first, second in SUCCESSORS:
            blocks.append(
                fact_block(
                    f"succ-{first}-{second}",
                    f"iggp:minimal-even:succ-{first}-{second}",
                    f"(minimal-even:proof:succ-{first}-{second} {state.episode})",
                    (
                        f"(minimal-even:succ {state.episode} "
                        f"minimal-even:n{first} minimal-even:n{second})"
                    ),
                )
            )
        blocks.extend(relational[:2])
        blocks.append(
            fact_block(
                "even-zero",
                "iggp:minimal-even:even-zero",
                f"(minimal-even:proof:even-zero {state.episode})",
                f"(minimal-even:even {state.episode} minimal-even:n0)",
            )
        )
        blocks.append(relational[2])
        for ordinal, atom in enumerate(state.background, 1):
            blocks.append(
                fact_block(
                    f"background-{ordinal}",
                    (
                        f"iggp:minimal-even:{state.target}:{state.split}:"
                        f"state-{state.ordinal}:background-{ordinal}"
                    ),
                    background_proof_name(state, ordinal),
                    prime_atom(atom, state.episode),
                )
            )
        blocks.extend(target_blocks(state))
        package_identity = (
            f"iggp-minimal-even-{state.target}-{state.split}-"
            f"state-{state.ordinal}-v1"
        )
        lines.extend(
            [
                f"(= (minimal-even:package {state.episode})",
                f"  (compile:rule-package {package_identity}",
                "    (rm-package",
                "\n".join(blocks),
                "    )))",
                "",
            ]
        )
    return "\n".join(lines)


def proof_shape_definitions() -> list[str]:
    return [
        "(= (minimal-even:num-proof-shape",
        "     (minimal-even:proof:num-first $state $first $second $proof))",
        "   num-from-first)",
        "(= (minimal-even:num-proof-shape",
        "     (minimal-even:proof:num-second $state $first $second $proof))",
        "   num-from-second)",
        "(= (minimal-even:num-proof-shape (unquote (quote $proof)))",
        "   (minimal-even:num-proof-shape $proof))",
        "(= (minimal-even:proof-shape",
        "     (minimal-even:proof:legal $state $value $number-proof))",
        "   (legal-via (minimal-even:num-proof-shape $number-proof)))",
        "(= (minimal-even:proof-shape",
        "     (minimal-even:proof:next-from-does $state $value $proof))",
        "   next-from-does)",
        "(= (minimal-even:proof-shape",
        "     (minimal-even:proof:next-from-true $state $value $proof))",
        "   next-from-true)",
        "",
        "(= (iggp:minimal-even:proof-shapes $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (minimal-even:package $episode) 32 2000000 256 (quote $goal))",
        "    (collapse",
        "      (let (occurrence $proof-data) (superpose $occurrences)",
        "        (let (quote $proof) $proof-data",
        "          (minimal-even:proof-shape $proof))))))",
        "",
    ]


def render_fixture(
    states: Iterable[State],
) -> tuple[str, str, int, int, int]:
    state_list = tuple(states)
    fixture = [
        "; Exact proof-relevant qualification for all minimal_even target tasks.",
        ";",
        f"; Canonical GDL: {GDL_PATH}",
        f"; SHA-256: {GDL_SHA256}",
        "; The excluded Prolog translation misspells does_choose as does_chose.",
        "; Every train, validate, and test atom occurrence is classified.",
        "; Proof counts retain relational multiplicity beyond binary labels.",
        "",
        "!(import! &self ../../lib/ilp/iggp_minimal_even_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_minimal_even_rules.metta)",
        "!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)",
        "",
        "(= (iggp:minimal-even:classify $name $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (minimal-even:package $episode) 32 2000000 256 (quote $goal))",
        "    (iggp:classify-occurrences",
        "      $name (quote $goal) $occurrences)))",
        "",
        *proof_shape_definitions(),
    ]
    expected = ["[()]", "[()]", "[()]"]
    cases = 0
    derived_cases = 0
    proof_occurrences = 0
    for state in state_list:
        positives = set(state.positives)
        for atom_ordinal, atom in enumerate(state.atoms, 1):
            name = (
                f"minimal-even:{state.target}:{state.split}:"
                f"state-{state.ordinal}:atom-{atom_ordinal}"
            )
            goal = prime_atom(atom, state.episode)
            count = proof_count(state, atom)
            if (count > 0) != (atom in positives):
                raise GenerationError(
                    f"{name}: canonical GDL and source label disagree"
                )
            fixture.extend(
                [
                    f"!(iggp:minimal-even:classify {name} {state.episode}",
                    f"  (quote {goal}))",
                ]
            )
            checks = " ".join("True" for _ in range(count))
            if count:
                expected.append(
                    f"[(iggp:case {name} derived {count} ({checks}))]"
                )
                derived_cases += 1
                proof_occurrences += count
            else:
                expected.append(
                    f"[(iggp:case {name} not-derived 0 ())]"
                )
            cases += 1

    legal_canary = next(state for state in state_list if state.target == "legal")
    next_canary = next(
        state
        for state in state_list
        if state.target == "next"
        and "does_choose(player, 9)" in state.background
        and "true_chosen(9)" in state.background
    )
    fixture.extend(
        [
            "",
            "!(iggp:minimal-even:proof-shapes",
            f"  {legal_canary.episode}",
            f"  (quote (minimal-even:legal {legal_canary.episode}",
            "    minimal-even:player (minimal-even:choose minimal-even:n5))))",
            "!(iggp:minimal-even:proof-shapes",
            f"  {next_canary.episode}",
            f"  (quote (minimal-even:next-chosen {next_canary.episode}",
            "    minimal-even:n9)))",
        ]
    )
    expected.extend(
        [
            "[((legal-via num-from-first) (legal-via num-from-second))]",
            "[(next-from-does next-from-true)]",
        ]
    )

    if (
        cases != EXPECTED_CASES
        or derived_cases != EXPECTED_DERIVED_CASES
        or proof_occurrences != EXPECTED_PROOF_OCCURRENCES
    ):
        raise GenerationError(
            "minimal_even totals changed: "
            f"{cases} cases, {derived_cases} derived cases, "
            f"{proof_occurrences} proof occurrences"
        )
    return (
        "\n".join(fixture) + "\n",
        "\n".join(expected) + "\n",
        cases,
        derived_cases,
        proof_occurrences,
    )


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument(
        "--types-output",
        type=Path,
        default=repo / "lib/ilp/iggp_minimal_even_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/iggp_minimal_even_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo / "examples/prime/iggp_minimal_even_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo / "examples/prime/iggp_minimal_even_ground_truth.expected",
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    try:
        states = load_states(args.snapshot_root, repo)
        fixture, expected, cases, derived, occurrences = render_fixture(states)
        materialize_outputs(
            (
                (args.types_output, render_types(states)),
                (args.rules_output, render_rules(states)),
                (args.fixture_output, fixture),
                (args.expected_output, expected),
            ),
            args.check,
        )
    except (GenerationError, corpus.ManifestError, KeyError, OSError) as exc:
        print(f"FAIL: IGGP minimal_even generation: {exc}", file=sys.stderr)
        return 1

    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        "IGGP minimal_even qualification: "
        f"{cases} atom occurrences, {derived} derived cases, "
        f"{occurrences} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
