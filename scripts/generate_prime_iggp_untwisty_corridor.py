#!/usr/bin/env python3
"""Generate the exact Prime qualification for IGGP Untwisty Corridor."""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import sys
from typing import Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import check_prime_iggp_manifest as corpus
from prime_iggp_generation import (
    ABSENT,
    PRESENT,
    GenerationError,
    State,
    checked_source,
    fact_block,
    finite_status_view,
    load_game_states,
    materialize_outputs,
    parse_gdl,
    parse_ground_atom,
    rule_block,
    unique_finite_member,
)


GAME = "untwisty_corridor"
GDL_PATH = "games/untwisty_corridor.pl"
GDL_SHA256 = "5202b3e657a95aaa71b0cecd5903a5b14f8631793d07d8bda43e4dbdf1b65d48"
TYPE_PATH = "types/untwisty_corridor.typ"
TYPE_SHA256 = "f089d6c59d4bd778fc470862d1debf922547f1cd9f22de4e6ec9f99213b18b19"

EXPECTED_CASES = 1243
EXPECTED_DERIVED_CASES = 299
EXPECTED_PROOF_OCCURRENCES = 341

PROPOSITIONS = ("p", *(f"q{value}" for value in range(1, 9)))
ACTIONS = tuple("abcdefgh")
STEPS = tuple(str(value) for value in range(1, 9))
SUCCESSORS = tuple((str(value), str(value + 1)) for value in range(1, 8))

STATIC_CLOSURE = (
    *(f"input(robot, {action})" for action in ACTIONS),
    "role(robot)",
    *(f"successor({earlier}, {later})" for earlier, later in SUCCESSORS),
)

CANONICAL_GDL = (
    ("role", "robot"),
    *(("base", proposition) for proposition in PROPOSITIONS),
    *(("base", ("step", step)) for step in STEPS),
    *(("input", "robot", action) for action in ACTIONS),
    ("init", "q1"),
    ("init", ("step", "1")),
    *(("legal", "robot", action) for action in ACTIONS),
    *(("<=", ("next", "p"), ("does", "robot", action)) for action in ACTIONS[:-1]),
    ("<=", ("next", "p"), ("true", "p")),
    *(("<=", ("next", "q1"), ("does", "robot", action)) for action in ACTIONS),
    *(
        (
            "<=",
            ("next", f"q{value}"),
            ("does", "robot", "h"),
            ("not", ("true", "p")),
            ("true", f"q{value - 1}"),
        )
        for value in range(2, 9)
    ),
    (
        "<=",
        ("next", ("step", "?n")),
        ("true", ("step", "?m")),
        ("successor", "?m", "?n"),
    ),
    ("<=", ("goal", "robot", "100"), ("true", "q8")),
    ("<=", ("goal", "robot", "0"), ("not", ("true", "q8"))),
    ("<=", "terminal", ("true", ("step", "8"))),
    *(("successor", earlier, later) for earlier, later in SUCCESSORS),
)


@dataclass(frozen=True)
class EpisodeView:
    propositions: tuple[str, ...]
    actions: tuple[str, ...]
    step: str

    def proposition(self, proposition: str) -> str:
        return self.propositions[PROPOSITIONS.index(proposition)]

    def action(self, action: str) -> str:
        return self.actions[ACTIONS.index(action)]


def validate_rule_sources(snapshot_root: Path) -> None:
    gdl = checked_source(
        snapshot_root / GDL_PATH,
        GDL_SHA256,
        "canonical Untwisty Corridor GDL",
    ).decode("utf-8")
    if parse_gdl(gdl) != CANONICAL_GDL:
        raise GenerationError("canonical Untwisty Corridor GDL changed")
    checked_source(
        snapshot_root / TYPE_PATH,
        TYPE_SHA256,
        "Untwisty Corridor type declarations",
    )
    if (snapshot_root / "games/untwisty_corridor.txt").exists():
        raise GenerationError(
            "an unpinned Untwisty Corridor rule source appeared"
        )


def flat_arguments(text: str) -> tuple[str, tuple[str, ...]]:
    atom = parse_ground_atom(text)
    if any(argument.args for argument in atom.args):
        raise GenerationError(
            f"expected flattened Untwisty Corridor atom, got {text!r}"
        )
    return atom.head, tuple(argument.head for argument in atom.args)


def episode_view(state: State) -> EpisodeView:
    propositions: list[str] = []
    actions: list[str] = []
    steps: list[str] = []
    for text in state.background:
        atom = parse_ground_atom(text)
        if (
            atom.head == "true"
            and len(atom.args) == 1
            and not atom.args[0].args
            and atom.args[0].head in PROPOSITIONS
        ):
            propositions.append(atom.args[0].head)
        elif atom.head == "true_step" and len(atom.args) == 1:
            steps.append(atom.args[0].head)
        elif (
            atom.head == "does"
            and len(atom.args) == 2
            and atom.args[0].head == "robot"
        ):
            actions.append(atom.args[1].head)
        else:
            raise GenerationError(
                f"{state.episode}: unsupported background atom {text!r}"
            )
    return EpisodeView(
        propositions=finite_status_view(
            propositions,
            PROPOSITIONS,
            f"{state.episode}: true propositions",
        ),
        actions=finite_status_view(
            actions,
            ACTIONS,
            f"{state.episode}: authored actions",
        ),
        step=unique_finite_member(
            steps,
            STEPS,
            f"{state.episode}: true step",
        ),
    )


def load_states(snapshot_root: Path, repo: Path) -> tuple[State, ...]:
    validate_rule_sources(snapshot_root)
    states = load_game_states(snapshot_root, repo, GAME, "corridor")
    for state in states:
        if state.statics != STATIC_CLOSURE:
            raise GenerationError(f"{state.episode}: source statics changed")
        episode_view(state)
    return states


def prime_atom(text: str, episode: str) -> str:
    atom = parse_ground_atom(text)
    arguments = tuple(argument.head for argument in atom.args)
    if atom.head == "goal" and arguments in {
        ("robot", "0"),
        ("robot", "100"),
    }:
        return f"(corridor:goal {episode} corridor:robot corridor:n{arguments[1]})"
    if atom.head == "legal" and len(arguments) == 2:
        if arguments[0] == "robot" and arguments[1] in ACTIONS:
            return (
                f"(corridor:legal {episode} corridor:robot "
                f"corridor:{arguments[1]})"
            )
    if (
        atom.head == "next"
        and len(atom.args) == 1
        and not atom.args[0].args
        and atom.args[0].head in PROPOSITIONS
    ):
        return f"(corridor:next {episode} corridor:{atom.args[0].head})"
    if atom.head == "next_step" and len(arguments) == 1:
        return f"(corridor:next {episode} (corridor:step corridor:n{arguments[0]}))"
    if atom.head == "terminal" and not arguments:
        return f"(corridor:terminal {episode})"
    raise GenerationError(f"unsupported Untwisty Corridor atom {text!r}")


def proof_count(state: State, atom_text: str) -> int:
    view = episode_view(state)
    atom = parse_ground_atom(atom_text)
    arguments = tuple(argument.head for argument in atom.args)

    def present_proposition(proposition: str) -> int:
        return int(view.proposition(proposition) == PRESENT)

    def present_action(action: str) -> int:
        return int(view.action(action) == PRESENT)

    if atom.head == "legal" and len(arguments) == 2:
        return int(arguments[0] == "robot" and arguments[1] in ACTIONS)
    if atom.head == "goal" and arguments == ("robot", "100"):
        return present_proposition("q8")
    if atom.head == "goal" and arguments == ("robot", "0"):
        return int(not present_proposition("q8"))
    if atom.head == "terminal" and not arguments:
        return int(view.step == "8")
    if atom.head == "next" and len(atom.args) == 1:
        proposition = atom.args[0].head
        if proposition == "p":
            return sum(present_action(action) for action in ACTIONS[:-1]) + present_proposition("p")
        if proposition == "q1":
            return sum(present_action(action) for action in ACTIONS)
        if proposition in PROPOSITIONS[2:]:
            number = int(proposition[1:])
            return (
                present_action("h")
                * int(not present_proposition("p"))
                * present_proposition(f"q{number - 1}")
            )
    if atom.head == "next_step" and len(arguments) == 1:
        target = arguments[0]
        return sum(
            int(view.step == earlier)
            for earlier, later in SUCCESSORS
            if later == target
        )
    raise GenerationError(
        f"unsupported Untwisty Corridor target {atom_text!r}"
    )


def proposition_view_proof_name(state: State, proposition: str) -> str:
    return (
        f"corridor:proof:proposition-view:{state.target}:{state.split}:"
        f"state-{state.ordinal}:{proposition}"
    )


def action_view_proof_name(state: State, action: str) -> str:
    return (
        f"corridor:proof:action-view:{state.target}:{state.split}:"
        f"state-{state.ordinal}:{action}"
    )


def step_view_proof_name(state: State) -> str:
    return (
        f"corridor:proof:step-view:{state.target}:{state.split}:"
        f"state-{state.ordinal}"
    )


def required_propositions(state: State) -> tuple[str, ...]:
    if state.target == "goal":
        return ("q8",)
    if state.target == "next":
        return PROPOSITIONS
    return ()


def required_actions(state: State) -> tuple[str, ...]:
    return ACTIONS if state.target == "next" else ()


def needs_step(state: State) -> bool:
    return state.target in {"next", "terminal"}


def render_types(states: Iterable[State]) -> str:
    state_list = tuple(states)
    lines = [
        "; Generated exact-image Prime declarations for Untwisty Corridor.",
        "; Source-bound finite views carry proposition and action absence.",
        "; Distinct derivations of one endpoint remain distinct occurrences.",
        "",
        "(: corridor:state (u 0))",
        "(: corridor:agent (u 0))",
        "(: corridor:prop (u 0))",
        "(: corridor:action (u 0))",
        "(: corridor:index (u 0))",
        "(: corridor:score (u 0))",
        "(: corridor:robot corridor:agent)",
    ]
    lines.extend(
        f"(: corridor:{proposition} corridor:prop)"
        for proposition in PROPOSITIONS
    )
    lines.extend(
        f"(: corridor:{action} corridor:action)" for action in ACTIONS
    )
    lines.extend(f"(: corridor:n{step} corridor:index)" for step in STEPS)
    lines.extend(["(: corridor:n0 corridor:score)", "(: corridor:n100 corridor:score)"])
    lines.extend(
        [
            "(: corridor:step (-> (index : corridor:index) corridor:prop))",
            "(: corridor:proposition-view",
            "  (-> (state : corridor:state) (proposition : corridor:prop)",
            "      (status : finite-view:status) (u 0)))",
            "(: corridor:action-view",
            "  (-> (state : corridor:state) (action : corridor:action)",
            "      (status : finite-view:status) (u 0)))",
            "(: corridor:step-view",
            "  (-> (state : corridor:state) (step : corridor:index) (u 0)))",
            "(: corridor:true",
            "  (-> (state : corridor:state) (proposition : corridor:prop) (u 0)))",
            "(: corridor:not-true",
            "  (-> (state : corridor:state) (proposition : corridor:prop) (u 0)))",
            "(: corridor:does",
            "  (-> (state : corridor:state) (agent : corridor:agent)",
            "      (action : corridor:action) (u 0)))",
            "(: corridor:successor",
            "  (-> (earlier later : corridor:index) (u 0)))",
            "(: corridor:goal",
            "  (-> (state : corridor:state) (agent : corridor:agent)",
            "      (score : corridor:score) (u 0)))",
            "(: corridor:legal",
            "  (-> (state : corridor:state) (agent : corridor:agent)",
            "      (action : corridor:action) (u 0)))",
            "(: corridor:next",
            "  (-> (state : corridor:state) (proposition : corridor:prop) (u 0)))",
            "(: corridor:terminal (-> (state : corridor:state) (u 0)))",
            "",
        ]
    )
    lines.extend(f"(: {state.episode} corridor:state)" for state in state_list)
    lines.extend(["", *render_proof_types()])

    for state in state_list:
        view = episode_view(state)
        for proposition in required_propositions(state):
            lines.append(
                f"(: {proposition_view_proof_name(state, proposition)} "
                f"(corridor:proposition-view {state.episode} "
                f"corridor:{proposition} finite-view:{view.proposition(proposition)}))"
            )
        for action in required_actions(state):
            lines.append(
                f"(: {action_view_proof_name(state, action)} "
                f"(corridor:action-view {state.episode} corridor:{action} "
                f"finite-view:{view.action(action)}))"
            )
        if needs_step(state):
            lines.append(
                f"(: {step_view_proof_name(state)} "
                f"(corridor:step-view {state.episode} corridor:n{view.step}))"
            )
    lines.append("")
    return "\n".join(lines)


def render_proof_types() -> list[str]:
    lines = [
        "(: corridor:proof:true-proposition",
        "  (-> (state : corridor:state) (proposition : corridor:prop)",
        "      (view : (corridor:proposition-view",
        "        state proposition finite-view:present))",
        "      (corridor:true state proposition)))",
        "(: corridor:proof:not-true-proposition",
        "  (-> (state : corridor:state) (proposition : corridor:prop)",
        "      (view : (corridor:proposition-view",
        "        state proposition finite-view:absent))",
        "      (corridor:not-true state proposition)))",
        "(: corridor:proof:does",
        "  (-> (state : corridor:state) (action : corridor:action)",
        "      (view : (corridor:action-view state action finite-view:present))",
        "      (corridor:does state corridor:robot action)))",
        "(: corridor:proof:true-step",
        "  (-> (state : corridor:state) (step : corridor:index)",
        "      (view : (corridor:step-view state step))",
        "      (corridor:true state (corridor:step step))))",
    ]
    lines.extend(
        f"(: corridor:proof:successor-{earlier}-{later} "
        f"(corridor:successor corridor:n{earlier} corridor:n{later}))"
        for earlier, later in SUCCESSORS
    )
    lines.extend(
        f"(: corridor:proof:legal-{action} "
        f"(-> (state : corridor:state) "
        f"(corridor:legal state corridor:robot corridor:{action})))"
        for action in ACTIONS
    )
    lines.extend(
        [
            "(: corridor:proof:next-p-from-action",
            "  (-> (state : corridor:state) (action : corridor:action)",
            "      (does-proof : (corridor:does state corridor:robot action))",
            "      (corridor:next state corridor:p)))",
            "(: corridor:proof:next-p-persist",
            "  (-> (state : corridor:state)",
            "      (p-proof : (corridor:true state corridor:p))",
            "      (corridor:next state corridor:p)))",
            "(: corridor:proof:next-q1",
            "  (-> (state : corridor:state) (action : corridor:action)",
            "      (does-proof : (corridor:does state corridor:robot action))",
            "      (corridor:next state corridor:q1)))",
        ]
    )
    for number in range(2, 9):
        lines.extend(
            [
                f"(: corridor:proof:next-q{number}",
                "  (-> (state : corridor:state)",
                "      (does-proof : (corridor:does state corridor:robot corridor:h))",
                "      (not-p : (corridor:not-true state corridor:p))",
                f"      (previous : (corridor:true state corridor:q{number - 1}))",
                f"      (corridor:next state corridor:q{number})))",
            ]
        )
    lines.extend(
        [
            "(: corridor:proof:next-step",
            "  (-> (state : corridor:state) (earlier later : corridor:index)",
            "      (step-proof : (corridor:true state (corridor:step earlier)))",
            "      (successor-proof : (corridor:successor earlier later))",
            "      (corridor:next state (corridor:step later))))",
            "(: corridor:proof:goal-hundred",
            "  (-> (state : corridor:state)",
            "      (q8-proof : (corridor:true state corridor:q8))",
            "      (corridor:goal state corridor:robot corridor:n100)))",
            "(: corridor:proof:goal-zero",
            "  (-> (state : corridor:state)",
            "      (not-q8 : (corridor:not-true state corridor:q8))",
            "      (corridor:goal state corridor:robot corridor:n0)))",
            "(: corridor:proof:terminal",
            "  (-> (state : corridor:state)",
            "      (step-proof : (corridor:true state (corridor:step corridor:n8)))",
            "      (corridor:terminal state)))",
            "",
        ]
    )
    return lines


def view_fact_blocks(state: State) -> list[str]:
    view = episode_view(state)
    blocks: list[str] = []
    for proposition in required_propositions(state):
        blocks.append(
            fact_block(
                f"proposition-view-{proposition}",
                f"iggp:corridor:{state.target}:{state.split}:"
                f"state-{state.ordinal}:proposition-view-{proposition}",
                proposition_view_proof_name(state, proposition),
                f"(corridor:proposition-view {state.episode} "
                f"corridor:{proposition} finite-view:{view.proposition(proposition)})",
            )
        )
    for action in required_actions(state):
        blocks.append(
            fact_block(
                f"action-view-{action}",
                f"iggp:corridor:{state.target}:{state.split}:"
                f"state-{state.ordinal}:action-view-{action}",
                action_view_proof_name(state, action),
                f"(corridor:action-view {state.episode} corridor:{action} "
                f"finite-view:{view.action(action)})",
            )
        )
    if needs_step(state):
        blocks.append(
            fact_block(
                "step-view",
                f"iggp:corridor:{state.target}:{state.split}:"
                f"state-{state.ordinal}:step-view",
                step_view_proof_name(state),
                f"(corridor:step-view {state.episode} corridor:n{view.step})",
            )
        )
    return blocks


def projection_blocks(state: State) -> list[str]:
    episode = state.episode
    blocks: list[str] = []
    if required_propositions(state):
        blocks.extend(
            [
                rule_block(
                    "true-proposition",
                    "iggp:corridor:true-proposition",
                    f"(corridor:proof:true-proposition {episode} $proposition "
                    "(unquote $view-proof))",
                    (("$view-proof", f"(corridor:proposition-view {episode} $proposition finite-view:present)"),),
                    f"(corridor:true {episode} $proposition)",
                ),
                rule_block(
                    "not-true-proposition",
                    "iggp:corridor:not-true-proposition",
                    f"(corridor:proof:not-true-proposition {episode} $proposition "
                    "(unquote $view-proof))",
                    (("$view-proof", f"(corridor:proposition-view {episode} $proposition finite-view:absent)"),),
                    f"(corridor:not-true {episode} $proposition)",
                ),
            ]
        )
    if required_actions(state):
        blocks.append(
            rule_block(
                "does",
                "iggp:corridor:does",
                f"(corridor:proof:does {episode} $action "
                "(unquote $view-proof))",
                (("$view-proof", f"(corridor:action-view {episode} $action finite-view:present)"),),
                f"(corridor:does {episode} corridor:robot $action)",
            )
        )
    if needs_step(state):
        blocks.append(
            rule_block(
                "true-step",
                "iggp:corridor:true-step",
                f"(corridor:proof:true-step {episode} $step "
                "(unquote $view-proof))",
                (("$view-proof", f"(corridor:step-view {episode} $step)"),),
                f"(corridor:true {episode} (corridor:step $step))",
            )
        )
    return blocks


def target_blocks(state: State) -> list[str]:
    episode = state.episode
    if state.target == "legal":
        return [
            fact_block(
                f"legal-{action}",
                f"iggp:corridor:legal-{action}",
                f"(corridor:proof:legal-{action} {episode})",
                f"(corridor:legal {episode} corridor:robot corridor:{action})",
            )
            for action in ACTIONS
        ]
    if state.target == "goal":
        return [
            rule_block(
                "goal-hundred",
                "iggp:corridor:goal-hundred",
                f"(corridor:proof:goal-hundred {episode} "
                "(unquote $q8-proof))",
                (("$q8-proof", f"(corridor:true {episode} corridor:q8)"),),
                f"(corridor:goal {episode} corridor:robot corridor:n100)",
            ),
            rule_block(
                "goal-zero",
                "iggp:corridor:goal-zero",
                f"(corridor:proof:goal-zero {episode} "
                "(unquote $not-q8))",
                (("$not-q8", f"(corridor:not-true {episode} corridor:q8)"),),
                f"(corridor:goal {episode} corridor:robot corridor:n0)",
            ),
        ]
    if state.target == "terminal":
        return [
            rule_block(
                "terminal",
                "iggp:corridor:terminal",
                f"(corridor:proof:terminal {episode} "
                "(unquote $step-proof))",
                (("$step-proof", f"(corridor:true {episode} (corridor:step corridor:n8))"),),
                f"(corridor:terminal {episode})",
            )
        ]
    return next_blocks(state)


def next_blocks(state: State) -> list[str]:
    episode = state.episode
    blocks: list[str] = []
    for action in ACTIONS[:-1]:
        blocks.append(
            rule_block(
                f"next-p-from-{action}",
                f"iggp:corridor:next-p-from-{action}",
                f"(corridor:proof:next-p-from-action {episode} "
                f"corridor:{action} (unquote $does-proof))",
                (("$does-proof", f"(corridor:does {episode} corridor:robot corridor:{action})"),),
                f"(corridor:next {episode} corridor:p)",
            )
        )
    blocks.append(
        rule_block(
            "next-p-persist",
            "iggp:corridor:next-p-persist",
            f"(corridor:proof:next-p-persist {episode} "
            "(unquote $p-proof))",
            (("$p-proof", f"(corridor:true {episode} corridor:p)"),),
            f"(corridor:next {episode} corridor:p)",
        )
    )
    for action in ACTIONS:
        blocks.append(
            rule_block(
                f"next-q1-from-{action}",
                f"iggp:corridor:next-q1-from-{action}",
                f"(corridor:proof:next-q1 {episode} corridor:{action} "
                "(unquote $does-proof))",
                (("$does-proof", f"(corridor:does {episode} corridor:robot corridor:{action})"),),
                f"(corridor:next {episode} corridor:q1)",
            )
        )
    for number in range(2, 9):
        blocks.append(
            rule_block(
                f"next-q{number}",
                f"iggp:corridor:next-q{number}",
                f"(corridor:proof:next-q{number} {episode} "
                "(unquote $does-proof) (unquote $not-p) "
                "(unquote $previous))",
                (
                    ("$does-proof", f"(corridor:does {episode} corridor:robot corridor:h)"),
                    ("$not-p", f"(corridor:not-true {episode} corridor:p)"),
                    ("$previous", f"(corridor:true {episode} corridor:q{number - 1})"),
                ),
                f"(corridor:next {episode} corridor:q{number})",
            )
        )
    for earlier, later in SUCCESSORS:
        blocks.append(
            fact_block(
                f"successor-{earlier}-{later}",
                f"iggp:corridor:successor-{earlier}-{later}",
                f"corridor:proof:successor-{earlier}-{later}",
                f"(corridor:successor corridor:n{earlier} corridor:n{later})",
            )
        )
    blocks.append(
        rule_block(
            "next-step",
            "iggp:corridor:next-step",
            f"(corridor:proof:next-step {episode} $earlier $later "
            "(unquote $step-proof) (unquote $successor-proof))",
            (
                ("$step-proof", f"(corridor:true {episode} (corridor:step $earlier))"),
                ("$successor-proof", "(corridor:successor $earlier $later)"),
            ),
            f"(corridor:next {episode} (corridor:step $later))",
        )
    )
    return blocks


def render_rules(states: Iterable[State]) -> str:
    lines = [
        "; Generated proof-producing rules for every Untwisty Corridor state.",
        "; Negative GDL premises consume exact finite-view absence evidence.",
        "; Action and persistence proofs of p remain separate occurrences.",
        "",
    ]
    for state in states:
        blocks = view_fact_blocks(state)
        blocks.extend(projection_blocks(state))
        blocks.extend(target_blocks(state))
        identity = (
            f"iggp-corridor-{state.target}-{state.split}-"
            f"state-{state.ordinal}-v1"
        )
        lines.extend(
            [
                f"(= (corridor:package {state.episode})",
                f"  (compile:rule-package {identity}",
                "    (rm-package",
                "\n".join(blocks),
                "    )))",
                "",
            ]
        )
    return "\n".join(lines)


def proof_shape_definitions() -> list[str]:
    return [
        "(= (corridor:proof-shape",
        "     (corridor:proof:next-p-from-action $state $action $proof))",
        "   (p-via-action $action))",
        "(= (corridor:proof-shape",
        "     (corridor:proof:next-p-persist $state $proof))",
        "   p-via-persistence)",
        "(= (iggp:corridor:proof-shapes $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (corridor:package $episode) 32 2000000 256 (quote $goal))",
        "    (collapse",
        "      (let (occurrence $proof-data) (superpose $occurrences)",
        "        (let (quote $proof) $proof-data",
        "          (corridor:proof-shape $proof))))))",
        "",
    ]


def find_double_p_canary(states: tuple[State, ...]) -> State:
    for state in states:
        if state.target != "next":
            continue
        for atom in state.atoms:
            parsed = parse_ground_atom(atom)
            if (
                parsed.head == "next"
                and parsed.args[0].head == "p"
                and proof_count(state, atom) == 2
            ):
                return state
    raise GenerationError("no action-plus-persistence proof canary")


def render_fixture(
    states: Iterable[State],
) -> tuple[str, str, int, int, int]:
    state_list = tuple(states)
    fixture = [
        "; Exact qualification for every Untwisty Corridor target task.",
        ";",
        f"; Canonical GDL presentation: {GDL_PATH}",
        f"; SHA-256: {GDL_SHA256}",
        "; The .pl-suffixed file is the complete parenthesized GDL source.",
        "; No competing .txt rule source exists in the pinned revision.",
        "; Every train, validation, and test atom occurrence is classified.",
        "",
        "!(import! &self ../../lib/ilp/iggp_finite_view_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_untwisty_corridor_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_untwisty_corridor_rules.metta)",
        "!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)",
        "",
        "(= (iggp:corridor:classify $name $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (corridor:package $episode) 32 2000000 256 (quote $goal))",
        "    (iggp:classify-occurrences",
        "      $name (quote $goal) $occurrences)))",
        "",
        *proof_shape_definitions(),
    ]
    expected = ["[()]", "[()]", "[()]", "[()]"]
    cases = 0
    derived_cases = 0
    proof_occurrences = 0
    for state in state_list:
        positives = set(state.positives)
        for ordinal, atom in enumerate(state.atoms, 1):
            name = (
                f"corridor:{state.target}:{state.split}:"
                f"state-{state.ordinal}:atom-{ordinal}"
            )
            count = proof_count(state, atom)
            if (count > 0) != (atom in positives):
                raise GenerationError(
                    f"{name}: canonical GDL and source label disagree"
                )
            fixture.extend(
                [
                    f"!(iggp:corridor:classify {name} {state.episode}",
                    f"  (quote {prime_atom(atom, state.episode)}))",
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
                expected.append(f"[(iggp:case {name} not-derived 0 ())]")
            cases += 1

    canary = find_double_p_canary(state_list)
    action = next(
        action
        for action in ACTIONS[:-1]
        if episode_view(canary).action(action) == PRESENT
    )
    fixture.extend(
        [
            "",
            "!(iggp:corridor:proof-shapes",
            f"  {canary.episode}",
            f"  (quote (corridor:next {canary.episode} corridor:p)))",
        ]
    )
    expected.append(
        f"[((p-via-action corridor:{action}) p-via-persistence)]"
    )

    if (
        cases != EXPECTED_CASES
        or derived_cases != EXPECTED_DERIVED_CASES
        or proof_occurrences != EXPECTED_PROOF_OCCURRENCES
    ):
        raise GenerationError(
            "Untwisty Corridor totals changed: "
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
        default=repo / "lib/ilp/iggp_untwisty_corridor_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/iggp_untwisty_corridor_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=(
            repo / "examples/prime/iggp_untwisty_corridor_ground_truth.metta"
        ),
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=(
            repo
            / "examples/prime/iggp_untwisty_corridor_ground_truth.expected"
        ),
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
        print(
            f"FAIL: IGGP Untwisty Corridor generation: {exc}",
            file=sys.stderr,
        )
        return 1

    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        "IGGP Untwisty Corridor qualification: "
        f"{cases} atom occurrences, {derived} derived cases, "
        f"{occurrences} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
