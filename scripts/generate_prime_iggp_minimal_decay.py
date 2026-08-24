#!/usr/bin/env python3
"""Generate the exact Prime qualification for IGGP minimal_decay."""

from __future__ import annotations

import argparse
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


GAME = "minimal_decay"
GDL_PATH = "games/minimal_decay.txt"
GDL_SHA256 = "3e42f2344243da41898ceb3fbb32603f417bc83aa0d939fc430b3b167bb9a174"
PROLOG_PATH = "games/minimal_decay.pl"
PROLOG_SHA256 = (
    "e492be2e79ccc2f37828e79b483d10c5ede5f5c9880470a6493bc56aaf89fada"
)
TYPE_PATH = "types/minimal_decay.typ"
TYPE_SHA256 = "75000a14b1a19a21420ed27ce6f4858a34fc5a85ebb9d002d7f081f7d7b6bdb3"
EXPECTED_CASES = 147
EXPECTED_DERIVED = 26
EXPECTED_NOT_DERIVED = 121


CANONICAL_GDL = (
    ("legal", "player", "noop"),
    ("legal", "player", "pressButton"),
    (
        "<=",
        ("next", ("value", "?x")),
        ("true", ("value", "?y")),
        ("succ", "?x", "?y"),
        ("does", "player", "noop"),
    ),
    (
        "<=",
        ("next", ("value", "5")),
        ("does", "player", "pressButton"),
    ),
    ("init", ("value", "5")),
    ("succ", "0", "1"),
    ("succ", "1", "2"),
    ("succ", "2", "3"),
    ("succ", "3", "4"),
    ("succ", "4", "5"),
)


def validate_rule_sources(snapshot_root: Path) -> None:
    gdl = checked_source(
        snapshot_root / GDL_PATH, GDL_SHA256, "canonical minimal_decay GDL"
    ).decode("utf-8")
    if parse_gdl(gdl) != CANONICAL_GDL:
        raise GenerationError("canonical minimal_decay GDL structure changed")

    prolog = checked_source(
        snapshot_root / PROLOG_PATH,
        PROLOG_SHA256,
        "excluded minimal_decay Prolog translation",
    ).decode("utf-8")
    translated_clause = re.sub(r"\s+", "", prolog)
    if "next_value(A):-true_value(B),succ(B,A),does_player(noop)." not in translated_clause:
        raise GenerationError("expected reversed-successor negative control vanished")

    checked_source(
        snapshot_root / TYPE_PATH,
        TYPE_SHA256,
        "minimal_decay type declarations",
    )


def load_states(snapshot_root: Path, repo: Path) -> tuple[State, ...]:
    validate_rule_sources(snapshot_root)
    return load_game_states(snapshot_root, repo, GAME, "decay")


def leaf(atom: GroundAtom) -> str:
    if atom.args:
        raise GenerationError(f"expected leaf, got {atom}")
    mapping = {
        "player": "decay:player",
        "noop": "decay:noop",
        "pressButton": "decay:press-button",
        **{str(value): f"decay:n{value}" for value in range(6)},
    }
    try:
        return mapping[atom.head]
    except KeyError as exc:
        raise GenerationError(f"unsupported minimal_decay symbol {atom.head}") from exc


def prime_atom(text: str, episode: str) -> str:
    atom = parse_ground_atom(text)
    if atom.head == "goal" and len(atom.args) == 2:
        return f"(decay:goal {episode} {leaf(atom.args[0])} {leaf(atom.args[1])})"
    if atom.head == "legal" and len(atom.args) == 2:
        return f"(decay:legal {episode} {leaf(atom.args[0])} {leaf(atom.args[1])})"
    if atom.head == "next_value" and len(atom.args) == 1:
        return f"(decay:next-value {episode} {leaf(atom.args[0])})"
    if atom.head == "terminal" and not atom.args:
        return f"(decay:terminal {episode})"
    if atom.head == "true_value" and len(atom.args) == 1:
        return f"(decay:true-value {episode} {leaf(atom.args[0])})"
    if atom.head == "does" and len(atom.args) == 2:
        return f"(decay:does {episode} {leaf(atom.args[0])} {leaf(atom.args[1])})"
    if atom.head == "succ" and len(atom.args) == 2:
        return f"(decay:succ {episode} {leaf(atom.args[0])} {leaf(atom.args[1])})"
    raise GenerationError(f"unsupported minimal_decay atom {text!r}")


def background_proof_name(state: State, ordinal: int) -> str:
    return (
        f"decay:proof:background:{state.target}:{state.split}:"
        f"state-{state.ordinal}:occurrence-{ordinal}"
    )


def render_types(states: Iterable[State]) -> str:
    state_list = tuple(states)
    lines = [
        "; Generated exact-image Prime declarations for IGGP minimal_decay.",
        "; The canonical GDL supplies rules; data.zip supplies episode-indexed facts.",
        "; Source symbols are normalized to lowercase CeTTa spellings.",
        "",
        "(: decay:state (u 0))",
        "(: decay:agent (u 0))",
        "(: decay:action (u 0))",
        "(: decay:int (u 0))",
        "(: decay:player decay:agent)",
        "(: decay:noop decay:action)",
        "(: decay:press-button decay:action)",
    ]
    lines.extend(f"(: decay:n{value} decay:int)" for value in range(6))
    lines.extend(
        [
            "",
            "(: decay:true-value",
            "  (-> (state : decay:state) (value : decay:int) (u 0)))",
            "(: decay:does",
            "  (-> (state : decay:state) (agent : decay:agent)",
            "      (action : decay:action) (u 0)))",
            "(: decay:succ",
            "  (-> (state : decay:state) (earlier : decay:int)",
            "      (later : decay:int) (u 0)))",
            "(: decay:goal",
            "  (-> (state : decay:state) (agent : decay:agent)",
            "      (value : decay:int) (u 0)))",
            "(: decay:legal",
            "  (-> (state : decay:state) (agent : decay:agent)",
            "      (action : decay:action) (u 0)))",
            "(: decay:next-value",
            "  (-> (state : decay:state) (value : decay:int) (u 0)))",
            "(: decay:terminal",
            "  (-> (state : decay:state) (u 0)))",
            "",
        ]
    )
    for state in state_list:
        lines.append(f"(: {state.episode} decay:state)")

    lines.extend(
        [
            "",
            "(: decay:proof:succ-0-1",
            "  (-> (state : decay:state)",
            "      (decay:succ state decay:n0 decay:n1)))",
            "(: decay:proof:succ-1-2",
            "  (-> (state : decay:state)",
            "      (decay:succ state decay:n1 decay:n2)))",
            "(: decay:proof:succ-2-3",
            "  (-> (state : decay:state)",
            "      (decay:succ state decay:n2 decay:n3)))",
            "(: decay:proof:succ-3-4",
            "  (-> (state : decay:state)",
            "      (decay:succ state decay:n3 decay:n4)))",
            "(: decay:proof:succ-4-5",
            "  (-> (state : decay:state)",
            "      (decay:succ state decay:n4 decay:n5)))",
            "",
            "(: decay:proof:legal-noop",
            "  (-> (state : decay:state)",
            "      (decay:legal state decay:player decay:noop)))",
            "(: decay:proof:legal-press-button",
            "  (-> (state : decay:state)",
            "      (decay:legal state decay:player decay:press-button)))",
            "",
            "(: decay:proof:next-noop",
            "  (-> (state : decay:state)",
            "      (earlier : decay:int) (later : decay:int)",
            "      (true-proof : (decay:true-value state later))",
            "      (successor-proof : (decay:succ state earlier later))",
            "      (does-proof : (decay:does state decay:player decay:noop))",
            "      (decay:next-value state earlier)))",
            "(: decay:proof:next-press-button",
            "  (-> (state : decay:state)",
            "      (does-proof :",
            "        (decay:does state decay:player decay:press-button))",
            "      (decay:next-value state decay:n5)))",
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


def target_blocks(state: State) -> tuple[str, ...]:
    episode = state.episode
    if state.target == "legal":
        return (
            fact_block(
                "legal-noop",
                "iggp:minimal-decay:legal-noop",
                f"(decay:proof:legal-noop {episode})",
                f"(decay:legal {episode} decay:player decay:noop)",
            ),
            fact_block(
                "legal-press-button",
                "iggp:minimal-decay:legal-press-button",
                f"(decay:proof:legal-press-button {episode})",
                f"(decay:legal {episode} decay:player decay:press-button)",
            ),
        )
    if state.target == "next":
        noop = "\n".join(
            [
                "      (rm-block next-noop iggp:minimal-decay:next-noop",
                "        (quote",
                f"          (decay:proof:next-noop {episode}",
                "            $earlier $later",
                "            (unquote $true-proof)",
                "            (unquote $successor-proof)",
                "            (unquote $does-proof)))",
                "        (rm-cons",
                "          (rm-premise $true-proof",
                f"            (quote (decay:true-value {episode} $later)))",
                "          (rm-cons",
                "            (rm-premise $successor-proof",
                f"              (quote (decay:succ {episode} $earlier $later)))",
                "            (rm-cons",
                "              (rm-premise $does-proof",
                f"                (quote (decay:does {episode}",
                "                  decay:player decay:noop)))",
                "              rm-nil)))",
                f"        (quote (decay:next-value {episode} $earlier)))",
            ]
        )
        press = "\n".join(
            [
                "      (rm-block next-press-button",
                "        iggp:minimal-decay:next-press-button",
                "        (quote",
                f"          (decay:proof:next-press-button {episode}",
                "            (unquote $does-proof)))",
                "        (rm-cons",
                "          (rm-premise $does-proof",
                f"            (quote (decay:does {episode}",
                "              decay:player decay:press-button)))",
                "          rm-nil)",
                f"        (quote (decay:next-value {episode} decay:n5)))",
            ]
        )
        return (noop, press)
    return ()


def render_rules(states: Iterable[State]) -> str:
    lines = [
        "; Generated proof-producing rules for the exact minimal_decay states.",
        "; The authority-free RuleMachine produces occurrences; Prime checks each proof.",
        "; The canonical GDL successor order is earlier-to-later.",
        "",
    ]
    for state in states:
        package_identity = (
            f"iggp-minimal-decay-{state.target}-{state.split}-"
            f"state-{state.ordinal}-v1"
        )
        blocks: list[str] = []
        for earlier in range(5):
            later = earlier + 1
            blocks.append(
                fact_block(
                    f"succ-{earlier}-{later}",
                    f"iggp:minimal-decay:succ-{earlier}-{later}",
                    f"(decay:proof:succ-{earlier}-{later} {state.episode})",
                    (
                        f"(decay:succ {state.episode} "
                        f"decay:n{earlier} decay:n{later})"
                    ),
                )
            )
        for ordinal, atom in enumerate(state.background, 1):
            blocks.append(
                fact_block(
                    f"background-{ordinal}",
                    (
                        f"iggp:minimal-decay:{state.target}:{state.split}:"
                        f"state-{state.ordinal}:background-{ordinal}"
                    ),
                    background_proof_name(state, ordinal),
                    prime_atom(atom, state.episode),
                )
            )
        blocks.extend(target_blocks(state))
        lines.extend(
            [
                f"(= (decay:package {state.episode})",
                f"  (compile:rule-package {package_identity}",
                "    (rm-package",
                "\n".join(blocks),
                "    )))",
                "",
            ]
        )
    return "\n".join(lines)


def render_fixture(states: Iterable[State]) -> tuple[str, str, int, int]:
    state_list = tuple(states)
    fixture = [
        "; Exact-image qualification for all four IGGP minimal_decay targets.",
        ";",
        f"; Canonical GDL: {GDL_PATH}",
        f"; SHA-256: {GDL_SHA256}",
        "; The excluded Prolog translation reverses the successor arguments;",
        "; the canonical GDL and the independently generated data agree.",
        "; Every atom occurrence in train, validate, and test is classified.",
        "; A missing derivation is reported as not-derived, never as Refuted.",
        "",
        "!(import! &self ../../lib/ilp/iggp_minimal_decay_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_minimal_decay_rules.metta)",
        "!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)",
        "",
        "(= (iggp:decay:classify $name $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (decay:package $episode) 24 1000000 128 (quote $goal))",
        "    (iggp:classify-occurrences",
        "      $name (quote $goal) $occurrences)))",
        "",
    ]
    expected = ["[()]", "[()]", "[()]"]
    cases = 0
    derived = 0
    for state in state_list:
        positives = set(state.positives)
        for atom_ordinal, atom in enumerate(state.atoms, 1):
            name = (
                f"decay:{state.target}:{state.split}:state-{state.ordinal}:"
                f"atom-{atom_ordinal}"
            )
            goal = prime_atom(atom, state.episode)
            fixture.extend(
                [
                    f"!(iggp:decay:classify {name} {state.episode}",
                    f"  (quote {goal}))",
                ]
            )
            if atom in positives:
                expected.append(f"[(iggp:case {name} derived 1 (True))]")
                derived += 1
            else:
                expected.append(
                    f"[(iggp:case {name} not-derived 0 ())]"
                )
            cases += 1

    if (
        cases != EXPECTED_CASES
        or derived != EXPECTED_DERIVED
        or cases - derived != EXPECTED_NOT_DERIVED
    ):
        raise GenerationError(
            "minimal_decay occurrence totals changed: "
            f"{cases} cases, {derived} derived"
        )
    return (
        "\n".join(fixture) + "\n",
        "\n".join(expected) + "\n",
        cases,
        derived,
    )


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument(
        "--types-output",
        type=Path,
        default=repo / "lib/ilp/iggp_minimal_decay_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/iggp_minimal_decay_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo / "examples/prime/iggp_minimal_decay_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo / "examples/prime/iggp_minimal_decay_ground_truth.expected",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify that the checked-in outputs equal fresh source generation",
    )
    args = parser.parse_args()

    try:
        states = load_states(args.snapshot_root, repo)
        fixture, expected, cases, derived = render_fixture(states)
        outputs = (
            (args.types_output, render_types(states)),
            (args.rules_output, render_rules(states)),
            (args.fixture_output, fixture),
            (args.expected_output, expected),
        )
        materialize_outputs(outputs, args.check)
    except (GenerationError, corpus.ManifestError, KeyError, OSError) as exc:
        print(f"FAIL: IGGP minimal_decay generation: {exc}", file=sys.stderr)
        return 1

    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        "IGGP minimal_decay qualification: "
        f"{cases} exact atom occurrences, {derived} derived, "
        f"{cases - derived} not-derived"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
