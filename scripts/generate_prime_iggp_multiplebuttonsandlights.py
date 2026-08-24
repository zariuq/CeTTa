#!/usr/bin/env python3
"""Generate the exact Prime qualification for IGGP multiplebuttonsandlights."""

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


GAME = "multiplebuttonsandlights"
GDL_PATH = "games/multiplebuttonsandlights.txt"
GDL_SHA256 = "7325c39696e8477c210d41411dfae0453ee932b64fea5191bddc8328484ee3b4"
TYPE_PATH = "types/multiplebuttonsandlights.typ"
TYPE_SHA256 = "ff488a6d296d2d8b742e8a58cf071a1445790c09662b2fe0d6c817622dfae37c"
EXPECTED_CASES = 23022
EXPECTED_DERIVED_CASES = 6763
EXPECTED_PROOF_OCCURRENCES = 7108

INDICES = tuple(str(value) for value in range(1, 10))
STEPS = tuple(str(value) for value in range(1, 8))
SIGNALS = ("p", "q", "r")
MOVES = ("a", "b", "c")
SUCCESSORS = tuple((str(value), str(value + 1)) for value in range(1, 7))

CANONICAL_GDL = (
    ("role", "robot"),
    *(('<=', ('base', (signal, '?x')), ('index', '?x')) for signal in SIGNALS),
    *(('base', ('step', step)) for step in STEPS),
    *(('<=', ('input', 'robot', (move, '?x')), ('index', '?x')) for move in MOVES),
    *(('index', index) for index in INDICES),
    ("init", ("step", "1")),
    *(('<=', ('legal', 'robot', (move, '?x')), ('index', '?x')) for move in MOVES),
    (
        "<=",
        ("next", ("p", "?x")),
        ("does", "robot", ("a", "?x")),
        ("not", ("true", ("p", "?x"))),
    ),
    (
        "<=",
        ("next", ("p", "?x")),
        ("does", "robot", ("b", "?x")),
        ("true", ("q", "?x")),
    ),
    (
        "<=",
        ("next", ("p", "?x")),
        ("true", ("p", "?x")),
        ("not", ("does", "robot", ("a", "?x"))),
        ("not", ("does", "robot", ("b", "?x"))),
    ),
    (
        "<=",
        ("next", ("q", "?x")),
        ("does", "robot", ("b", "?x")),
        ("true", ("p", "?x")),
    ),
    (
        "<=",
        ("next", ("q", "?x")),
        ("does", "robot", ("c", "?x")),
        ("true", ("r", "?x")),
    ),
    (
        "<=",
        ("next", ("q", "?x")),
        ("true", ("q", "?x")),
        ("not", ("does", "robot", ("b", "?x"))),
        ("not", ("does", "robot", ("c", "?x"))),
    ),
    (
        "<=",
        ("next", ("r", "?x")),
        ("does", "robot", ("c", "?x")),
        ("true", ("q", "?x")),
    ),
    (
        "<=",
        ("next", ("r", "?x")),
        ("true", ("r", "?x")),
        ("not", ("does", "robot", ("c", "?x"))),
    ),
    (
        "<=",
        ("next", ("step", "?y")),
        ("true", ("step", "?x")),
        ("successor", "?x", "?y"),
    ),
    (
        "<=",
        ("goal", "robot", "100"),
        ("true", ("p", "5")),
        ("true", ("q", "5")),
        ("true", ("r", "5")),
    ),
    ("<=", ("goal", "robot", "0"), ("not", ("true", ("p", "5")))),
    ("<=", ("goal", "robot", "0"), ("not", ("true", ("q", "5")))),
    ("<=", ("goal", "robot", "0"), ("not", ("true", ("r", "5")))),
    (
        "<=",
        "terminal",
        ("true", ("p", "5")),
        ("true", ("q", "5")),
        ("true", ("r", "5")),
    ),
    ("<=", "terminal", ("true", ("step", "7"))),
    *(('successor', first, second) for first, second in SUCCESSORS),
)

STATIC_CLOSURE = (
    *(f"index({index})" for index in INDICES),
    *(
        f"input_{move}(robot, {index})"
        for move in MOVES
        for index in INDICES
    ),
    "role(robot)",
    *(f"successor({first}, {second})" for first, second in SUCCESSORS),
)


@dataclass(frozen=True)
class EpisodeView:
    lights: tuple[tuple[str, str, str], ...]
    actions: tuple[tuple[str, str, str], ...]
    step: str

    def light(self, index: str) -> tuple[str, str, str]:
        return self.lights[INDICES.index(index)]

    def action(self, index: str) -> tuple[str, str, str]:
        return self.actions[INDICES.index(index)]


def validate_rule_sources(snapshot_root: Path) -> None:
    gdl = checked_source(
        snapshot_root / GDL_PATH,
        GDL_SHA256,
        "canonical multiplebuttonsandlights GDL",
    ).decode("utf-8")
    if parse_gdl(gdl) != CANONICAL_GDL:
        raise GenerationError("canonical multiplebuttonsandlights GDL changed")
    checked_source(
        snapshot_root / TYPE_PATH,
        TYPE_SHA256,
        "multiplebuttonsandlights type declarations",
    )
    if (snapshot_root / "games/multiplebuttonsandlights.pl").exists():
        raise GenerationError(
            "an unpinned multiplebuttonsandlights Prolog projection appeared"
        )


def source_atom(text: str) -> tuple[str, tuple[str, ...]]:
    atom = parse_ground_atom(text)
    if any(argument.args for argument in atom.args):
        raise GenerationError(
            f"expected flattened multiplebuttonsandlights atom, got {text!r}"
        )
    return atom.head, tuple(argument.head for argument in atom.args)


def episode_view(state: State) -> EpisodeView:
    light_members = {signal: [] for signal in SIGNALS}
    action_members = {move: [] for move in MOVES}
    steps: list[str] = []
    for text in state.background:
        head, arguments = source_atom(text)
        if head in {f"true_{signal}" for signal in SIGNALS}:
            if len(arguments) != 1:
                raise GenerationError(f"{state.episode}: malformed {head}")
            light_members[head.removeprefix("true_")].append(arguments[0])
        elif head == "true_step":
            if len(arguments) != 1:
                raise GenerationError(f"{state.episode}: malformed true_step")
            steps.append(arguments[0])
        elif head in {f"does_{move}" for move in MOVES}:
            if len(arguments) != 2 or arguments[0] != "robot":
                raise GenerationError(f"{state.episode}: malformed {head}")
            action_members[head.removeprefix("does_")].append(arguments[1])
        else:
            raise GenerationError(
                f"{state.episode}: unsupported background atom {text!r}"
            )

    light_columns = {
        signal: finite_status_view(
            light_members[signal],
            INDICES,
            f"{state.episode}: true_{signal}",
        )
        for signal in SIGNALS
    }
    action_columns = {
        move: finite_status_view(
            action_members[move],
            INDICES,
            f"{state.episode}: does_{move}",
        )
        for move in MOVES
    }
    return EpisodeView(
        lights=tuple(
            tuple(light_columns[signal][position] for signal in SIGNALS)
            for position in range(len(INDICES))
        ),
        actions=tuple(
            tuple(action_columns[move][position] for move in MOVES)
            for position in range(len(INDICES))
        ),
        step=unique_finite_member(
            steps,
            STEPS,
            f"{state.episode}: true_step",
        ),
    )


def load_states(snapshot_root: Path, repo: Path) -> tuple[State, ...]:
    validate_rule_sources(snapshot_root)
    states = load_game_states(
        snapshot_root,
        repo,
        GAME,
        "multi",
    )
    for state in states:
        if state.statics != STATIC_CLOSURE:
            raise GenerationError(f"{state.episode}: source statics changed")
        episode_view(state)
    return states


def prime_index(value: str) -> str:
    if value not in INDICES:
        raise GenerationError(f"unsupported multiplebuttonsandlights index {value}")
    return f"multi:n{value}"


def prime_atom(text: str, episode: str) -> str:
    head, arguments = source_atom(text)
    if head == "goal" and arguments in {("robot", "0"), ("robot", "100")}:
        return f"(multi:goal {episode} multi:robot multi:n{arguments[1]})"
    if head.startswith("legal_") and len(arguments) == 2:
        move = head.removeprefix("legal_")
        if move in MOVES and arguments[0] == "robot":
            return (
                f"(multi:legal {episode} multi:robot "
                f"(multi:{move} {prime_index(arguments[1])}))"
            )
    if head.startswith("next_") and len(arguments) == 1:
        constructor = head.removeprefix("next_")
        if constructor in (*SIGNALS, "step"):
            return (
                f"(multi:next {episode} "
                f"(multi:{constructor} {prime_index(arguments[0])}))"
            )
    if head == "terminal" and not arguments:
        return f"(multi:terminal {episode})"
    raise GenerationError(f"unsupported multiplebuttonsandlights atom {text!r}")


def background_counts(state: State) -> dict[str, Counter[tuple[str, ...]]]:
    result: dict[str, Counter[tuple[str, ...]]] = {}
    for text in state.background:
        head, arguments = source_atom(text)
        result.setdefault(head, Counter())[arguments] += 1
    return result


def proof_count(state: State, atom_text: str) -> int:
    head, arguments = source_atom(atom_text)
    facts = background_counts(state)
    if head.startswith("legal_") and len(arguments) == 2:
        return int(
            head.removeprefix("legal_") in MOVES
            and arguments[0] == "robot"
            and arguments[1] in INDICES
        )
    if head == "goal" and arguments == ("robot", "100"):
        return (
            facts.get("true_p", Counter())[("5",)]
            * facts.get("true_q", Counter())[("5",)]
            * facts.get("true_r", Counter())[("5",)]
        )
    if head == "goal" and arguments == ("robot", "0"):
        return sum(
            int(facts.get(f"true_{signal}", Counter())[("5",)] == 0)
            for signal in SIGNALS
        )
    if head == "terminal" and not arguments:
        return (
            facts.get("true_p", Counter())[("5",)]
            * facts.get("true_q", Counter())[("5",)]
            * facts.get("true_r", Counter())[("5",)]
            + facts.get("true_step", Counter())[("7",)]
        )
    if not head.startswith("next_") or len(arguments) != 1:
        raise GenerationError(
            f"unsupported multiplebuttonsandlights target {atom_text!r}"
        )
    constructor = head.removeprefix("next_")
    index = arguments[0]
    if constructor == "step":
        return sum(
            facts.get("true_step", Counter())[(earlier,)]
            for earlier, later in SUCCESSORS
            if later == index
        )

    true = {
        signal: facts.get(f"true_{signal}", Counter())[(index,)]
        for signal in SIGNALS
    }
    does = {
        move: facts.get(f"does_{move}", Counter())[("robot", index)]
        for move in MOVES
    }
    absent_true = {signal: int(not true[signal]) for signal in SIGNALS}
    absent_does = {move: int(not does[move]) for move in MOVES}
    if constructor == "p":
        return (
            does["a"] * absent_true["p"]
            + does["b"] * true["q"]
            + true["p"] * absent_does["a"] * absent_does["b"]
        )
    if constructor == "q":
        return (
            does["b"] * true["p"]
            + does["c"] * true["r"]
            + true["q"] * absent_does["b"] * absent_does["c"]
        )
    if constructor == "r":
        return (
            does["c"] * true["q"]
            + true["r"] * absent_does["c"]
        )
    raise GenerationError(
        f"unsupported multiplebuttonsandlights next target {atom_text!r}"
    )


def cell_view_proof_name(state: State, index: str) -> str:
    return (
        f"multi:proof:cell-view:{state.target}:{state.split}:"
        f"state-{state.ordinal}:index-{index}"
    )


def action_view_proof_name(state: State, index: str) -> str:
    return (
        f"multi:proof:action-view:{state.target}:{state.split}:"
        f"state-{state.ordinal}:index-{index}"
    )


def step_view_proof_name(state: State) -> str:
    return (
        f"multi:proof:step-view:{state.target}:{state.split}:"
        f"state-{state.ordinal}"
    )


def required_light_indices(state: State) -> tuple[str, ...]:
    if state.target in {"goal", "terminal"}:
        return ("5",)
    if state.target == "next":
        return INDICES
    return ()


def required_action_indices(state: State) -> tuple[str, ...]:
    return INDICES if state.target == "next" else ()


def needs_step_view(state: State) -> bool:
    return state.target in {"next", "terminal"}


def render_types(states: Iterable[State]) -> str:
    state_list = tuple(states)
    lines = [
        "; Generated exact-image Prime declarations for multiplebuttonsandlights.",
        "; Finite cell views construct scoped presence and absence evidence.",
        "; The views are ordinary Prime data; no closed-world engine rule is used.",
        "",
        "(: multi:state (u 0))",
        "(: multi:agent (u 0))",
        "(: multi:index (u 0))",
        "(: multi:prop (u 0))",
        "(: multi:action (u 0))",
        "(: multi:score (u 0))",
        "(: multi:robot multi:agent)",
    ]
    lines.extend(f"(: multi:n{index} multi:index)" for index in INDICES)
    lines.extend(
        [
            "(: multi:n0 multi:score)",
            "(: multi:n100 multi:score)",
        ]
    )
    lines.extend(
        f"(: multi:{signal} (-> (index : multi:index) multi:prop))"
        for signal in (*SIGNALS, "step")
    )
    lines.extend(
        f"(: multi:{move} (-> (index : multi:index) multi:action))"
        for move in MOVES
    )
    lines.extend(
        [
            "",
            "(: multi:cell-view",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (p q r : finite-view:status) (u 0)))",
            "(: multi:action-view",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (a b c : finite-view:status) (u 0)))",
            "(: multi:step-view",
            "  (-> (state : multi:state) (step : multi:index) (u 0)))",
            "(: multi:true",
            "  (-> (state : multi:state) (proposition : multi:prop) (u 0)))",
            "(: multi:not-true",
            "  (-> (state : multi:state) (proposition : multi:prop) (u 0)))",
            "(: multi:does",
            "  (-> (state : multi:state) (agent : multi:agent)",
            "      (action : multi:action) (u 0)))",
            "(: multi:not-does",
            "  (-> (state : multi:state) (agent : multi:agent)",
            "      (action : multi:action) (u 0)))",
            "(: multi:both-not-does",
            "  (-> (state : multi:state) (agent : multi:agent)",
            "      (first second : multi:action) (u 0)))",
            "(: multi:successor",
            "  (-> (earlier later : multi:index) (u 0)))",
            "(: multi:goal",
            "  (-> (state : multi:state) (agent : multi:agent)",
            "      (score : multi:score) (u 0)))",
            "(: multi:legal",
            "  (-> (state : multi:state) (agent : multi:agent)",
            "      (action : multi:action) (u 0)))",
            "(: multi:next",
            "  (-> (state : multi:state) (proposition : multi:prop) (u 0)))",
            "(: multi:terminal (-> (state : multi:state) (u 0)))",
            "",
        ]
    )
    lines.extend(f"(: {state.episode} multi:state)" for state in state_list)
    lines.extend(["", *render_projection_types(), *render_target_proof_types()])

    for state in state_list:
        view = episode_view(state)
        for index in required_light_indices(state):
            p, q, r = view.light(index)
            lines.append(
                f"(: {cell_view_proof_name(state, index)} "
                f"(multi:cell-view {state.episode} multi:n{index} "
                f"finite-view:{p} finite-view:{q} finite-view:{r}))"
            )
        for index in required_action_indices(state):
            a, b, c = view.action(index)
            lines.append(
                f"(: {action_view_proof_name(state, index)} "
                f"(multi:action-view {state.episode} multi:n{index} "
                f"finite-view:{a} finite-view:{b} finite-view:{c}))"
            )
        if needs_step_view(state):
            lines.append(
                f"(: {step_view_proof_name(state)} "
                f"(multi:step-view {state.episode} multi:n{view.step}))"
            )
    lines.append("")
    return "\n".join(lines)


def render_projection_types() -> list[str]:
    lines: list[str] = []
    for signal, other_first, other_second, pattern in (
        ("p", "q", "r", "finite-view:present q r"),
        ("q", "p", "r", "p finite-view:present r"),
        ("r", "p", "q", "p q finite-view:present"),
    ):
        lines.extend(
            [
                f"(: multi:proof:true-{signal}",
                "  (-> (state : multi:state) (index : multi:index)",
                f"      ({other_first} {other_second} : finite-view:status)",
                f"      (view : (multi:cell-view state index {pattern}))",
                f"      (multi:true state (multi:{signal} index))))",
            ]
        )
    for signal, other_first, other_second, pattern in (
        ("p", "q", "r", "finite-view:absent q r"),
        ("q", "p", "r", "p finite-view:absent r"),
        ("r", "p", "q", "p q finite-view:absent"),
    ):
        lines.extend(
            [
                f"(: multi:proof:not-true-{signal}",
                "  (-> (state : multi:state) (index : multi:index)",
                f"      ({other_first} {other_second} : finite-view:status)",
                f"      (view : (multi:cell-view state index {pattern}))",
                f"      (multi:not-true state (multi:{signal} index))))",
            ]
        )
    for move, other_first, other_second, pattern in (
        ("a", "b", "c", "finite-view:present b c"),
        ("b", "a", "c", "a finite-view:present c"),
        ("c", "a", "b", "a b finite-view:present"),
    ):
        lines.extend(
            [
                f"(: multi:proof:does-{move}",
                "  (-> (state : multi:state) (index : multi:index)",
                f"      ({other_first} {other_second} : finite-view:status)",
                f"      (view : (multi:action-view state index {pattern}))",
                f"      (multi:does state multi:robot (multi:{move} index))))",
            ]
        )
    for move, other_first, other_second, pattern in (
        ("a", "b", "c", "finite-view:absent b c"),
        ("b", "a", "c", "a finite-view:absent c"),
        ("c", "a", "b", "a b finite-view:absent"),
    ):
        lines.extend(
            [
                f"(: multi:proof:not-does-{move}",
                "  (-> (state : multi:state) (index : multi:index)",
                f"      ({other_first} {other_second} : finite-view:status)",
                f"      (view : (multi:action-view state index {pattern}))",
                f"      (multi:not-does state multi:robot (multi:{move} index))))",
            ]
        )
    lines.extend(
        [
            "(: multi:proof:true-step",
            "  (-> (state : multi:state) (step : multi:index)",
            "      (view : (multi:step-view state step))",
            "      (multi:true state (multi:step step))))",
            "",
        ]
    )
    return lines


def render_target_proof_types() -> list[str]:
    lines: list[str] = []
    for first, second in SUCCESSORS:
        lines.append(
            f"(: multi:proof:successor-{first}-{second} "
            f"(multi:successor multi:n{first} multi:n{second}))"
        )
    for move in MOVES:
        for index in INDICES:
            lines.extend(
                [
                    f"(: multi:proof:legal-{move}-{index}",
                    "  (-> (state : multi:state)",
                    f"      (multi:legal state multi:robot "
                    f"(multi:{move} multi:n{index}))))",
                ]
            )
    lines.extend(
        [
            "(: multi:proof:goal-100",
            "  (-> (state : multi:state)",
            "      (p-proof : (multi:true state (multi:p multi:n5)))",
            "      (q-proof : (multi:true state (multi:q multi:n5)))",
            "      (r-proof : (multi:true state (multi:r multi:n5)))",
            "      (multi:goal state multi:robot multi:n100)))",
        ]
    )
    for signal in SIGNALS:
        lines.extend(
            [
                f"(: multi:proof:goal-0-{signal}",
                "  (-> (state : multi:state)",
                f"      (absence : (multi:not-true state "
                f"(multi:{signal} multi:n5)))",
                "      (multi:goal state multi:robot multi:n0)))",
            ]
        )
    lines.extend(
        [
            "(: multi:proof:terminal-all-on",
            "  (-> (state : multi:state)",
            "      (p-proof : (multi:true state (multi:p multi:n5)))",
            "      (q-proof : (multi:true state (multi:q multi:n5)))",
            "      (r-proof : (multi:true state (multi:r multi:n5)))",
            "      (multi:terminal state)))",
            "(: multi:proof:terminal-step-seven",
            "  (-> (state : multi:state)",
            "      (step-proof : (multi:true state (multi:step multi:n7)))",
            "      (multi:terminal state)))",
            "(: multi:proof:next-p-toggle-a",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (does-proof : (multi:does state multi:robot (multi:a index)))",
            "      (absence : (multi:not-true state (multi:p index)))",
            "      (multi:next state (multi:p index))))",
            "(: multi:proof:next-p-from-q-b",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (does-proof : (multi:does state multi:robot (multi:b index)))",
            "      (q-proof : (multi:true state (multi:q index)))",
            "      (multi:next state (multi:p index))))",
            "(: multi:proof:both-not-does-a-b",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (not-a : (multi:not-does state multi:robot (multi:a index)))",
            "      (not-b : (multi:not-does state multi:robot (multi:b index)))",
            "      (multi:both-not-does state multi:robot",
            "        (multi:a index) (multi:b index))))",
            "(: multi:proof:both-not-does-b-c",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (not-b : (multi:not-does state multi:robot (multi:b index)))",
            "      (not-c : (multi:not-does state multi:robot (multi:c index)))",
            "      (multi:both-not-does state multi:robot",
            "        (multi:b index) (multi:c index))))",
            "(: multi:proof:next-p-persist",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (p-proof : (multi:true state (multi:p index)))",
            "      (absences : (multi:both-not-does state multi:robot",
            "        (multi:a index) (multi:b index)))",
            "      (multi:next state (multi:p index))))",
            "(: multi:proof:next-q-from-p-b",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (does-proof : (multi:does state multi:robot (multi:b index)))",
            "      (p-proof : (multi:true state (multi:p index)))",
            "      (multi:next state (multi:q index))))",
            "(: multi:proof:next-q-from-r-c",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (does-proof : (multi:does state multi:robot (multi:c index)))",
            "      (r-proof : (multi:true state (multi:r index)))",
            "      (multi:next state (multi:q index))))",
            "(: multi:proof:next-q-persist",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (q-proof : (multi:true state (multi:q index)))",
            "      (absences : (multi:both-not-does state multi:robot",
            "        (multi:b index) (multi:c index)))",
            "      (multi:next state (multi:q index))))",
            "(: multi:proof:next-r-from-q-c",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (does-proof : (multi:does state multi:robot (multi:c index)))",
            "      (q-proof : (multi:true state (multi:q index)))",
            "      (multi:next state (multi:r index))))",
            "(: multi:proof:next-r-persist",
            "  (-> (state : multi:state) (index : multi:index)",
            "      (r-proof : (multi:true state (multi:r index)))",
            "      (not-c : (multi:not-does state multi:robot (multi:c index)))",
            "      (multi:next state (multi:r index))))",
            "(: multi:proof:next-step",
            "  (-> (state : multi:state) (earlier later : multi:index)",
            "      (true-proof : (multi:true state (multi:step earlier)))",
            "      (successor-proof : (multi:successor earlier later))",
            "      (multi:next state (multi:step later))))",
            "",
        ]
    )
    return lines


def view_fact_blocks(state: State) -> list[str]:
    view = episode_view(state)
    blocks: list[str] = []
    for index in required_light_indices(state):
        p, q, r = view.light(index)
        blocks.append(
            fact_block(
                f"cell-view-{index}",
                f"iggp:multi:{state.target}:{state.split}:"
                f"state-{state.ordinal}:cell-view-{index}",
                cell_view_proof_name(state, index),
                f"(multi:cell-view {state.episode} multi:n{index} "
                f"finite-view:{p} finite-view:{q} finite-view:{r})",
            )
        )
    for index in required_action_indices(state):
        a, b, c = view.action(index)
        blocks.append(
            fact_block(
                f"action-view-{index}",
                f"iggp:multi:{state.target}:{state.split}:"
                f"state-{state.ordinal}:action-view-{index}",
                action_view_proof_name(state, index),
                f"(multi:action-view {state.episode} multi:n{index} "
                f"finite-view:{a} finite-view:{b} finite-view:{c})",
            )
        )
    if needs_step_view(state):
        blocks.append(
            fact_block(
                "step-view",
                f"iggp:multi:{state.target}:{state.split}:"
                f"state-{state.ordinal}:step-view",
                step_view_proof_name(state),
                f"(multi:step-view {state.episode} multi:n{view.step})",
            )
        )
    return blocks


def projection_blocks(state: State) -> list[str]:
    episode = state.episode
    blocks: list[str] = []
    if required_light_indices(state):
        for label, p, q, r, signal, relation, proof_args in (
            ("true-p", "finite-view:present", "$q", "$r", "p", "true", "$q $r"),
            ("true-q", "$p", "finite-view:present", "$r", "q", "true", "$p $r"),
            ("true-r", "$p", "$q", "finite-view:present", "r", "true", "$p $q"),
            ("not-true-p", "finite-view:absent", "$q", "$r", "p", "not-true", "$q $r"),
            ("not-true-q", "$p", "finite-view:absent", "$r", "q", "not-true", "$p $r"),
            ("not-true-r", "$p", "$q", "finite-view:absent", "r", "not-true", "$p $q"),
        ):
            blocks.append(
                rule_block(
                    label,
                    f"iggp:multi:{label}",
                    f"(multi:proof:{label} {episode} $index "
                    f"{proof_args} (unquote $view-proof))",
                    (
                        (
                            "$view-proof",
                            f"(multi:cell-view {episode} $index {p} {q} {r})",
                        ),
                    ),
                    f"(multi:{relation} {episode} (multi:{signal} $index))",
                )
            )
    if required_action_indices(state):
        for label, a, b, c, move, relation, proof_args in (
            ("does-a", "finite-view:present", "$b", "$c", "a", "does", "$b $c"),
            ("does-b", "$a", "finite-view:present", "$c", "b", "does", "$a $c"),
            ("does-c", "$a", "$b", "finite-view:present", "c", "does", "$a $b"),
            ("not-does-a", "finite-view:absent", "$b", "$c", "a", "not-does", "$b $c"),
            ("not-does-b", "$a", "finite-view:absent", "$c", "b", "not-does", "$a $c"),
            ("not-does-c", "$a", "$b", "finite-view:absent", "c", "not-does", "$a $b"),
        ):
            blocks.append(
                rule_block(
                    label,
                    f"iggp:multi:{label}",
                    f"(multi:proof:{label} {episode} $index "
                    f"{proof_args} (unquote $view-proof))",
                    (
                        (
                            "$view-proof",
                            f"(multi:action-view {episode} $index {a} {b} {c})",
                        ),
                    ),
                    f"(multi:{relation} {episode} multi:robot "
                    f"(multi:{move} $index))",
                )
            )
    if needs_step_view(state):
        blocks.append(
            rule_block(
                "true-step",
                "iggp:multi:true-step",
                f"(multi:proof:true-step {episode} $step "
                "(unquote $view-proof))",
                (
                    (
                        "$view-proof",
                        f"(multi:step-view {episode} $step)",
                    ),
                ),
                f"(multi:true {episode} (multi:step $step))",
            )
        )
    return blocks


def target_blocks(state: State) -> list[str]:
    episode = state.episode
    if state.target == "legal":
        return [
            fact_block(
                f"legal-{move}-{index}",
                f"iggp:multi:legal-{move}-{index}",
                f"(multi:proof:legal-{move}-{index} {episode})",
                f"(multi:legal {episode} multi:robot "
                f"(multi:{move} multi:n{index}))",
            )
            for move in MOVES
            for index in INDICES
        ]
    if state.target == "goal":
        blocks = [
            rule_block(
                "goal-100",
                "iggp:multi:goal-100",
                f"(multi:proof:goal-100 {episode} (unquote $p-proof) "
                "(unquote $q-proof) (unquote $r-proof))",
                (
                    ("$p-proof", f"(multi:true {episode} (multi:p multi:n5))"),
                    ("$q-proof", f"(multi:true {episode} (multi:q multi:n5))"),
                    ("$r-proof", f"(multi:true {episode} (multi:r multi:n5))"),
                ),
                f"(multi:goal {episode} multi:robot multi:n100)",
            )
        ]
        for signal in SIGNALS:
            blocks.append(
                rule_block(
                    f"goal-0-{signal}",
                    f"iggp:multi:goal-0-{signal}",
                    f"(multi:proof:goal-0-{signal} {episode} "
                    "(unquote $absence))",
                    (
                        (
                            "$absence",
                            f"(multi:not-true {episode} "
                            f"(multi:{signal} multi:n5))",
                        ),
                    ),
                    f"(multi:goal {episode} multi:robot multi:n0)",
                )
            )
        return blocks
    if state.target == "terminal":
        return [
            rule_block(
                "terminal-all-on",
                "iggp:multi:terminal-all-on",
                f"(multi:proof:terminal-all-on {episode} "
                "(unquote $p-proof) (unquote $q-proof) "
                "(unquote $r-proof))",
                (
                    ("$p-proof", f"(multi:true {episode} (multi:p multi:n5))"),
                    ("$q-proof", f"(multi:true {episode} (multi:q multi:n5))"),
                    ("$r-proof", f"(multi:true {episode} (multi:r multi:n5))"),
                ),
                f"(multi:terminal {episode})",
            ),
            rule_block(
                "terminal-step-seven",
                "iggp:multi:terminal-step-seven",
                f"(multi:proof:terminal-step-seven {episode} "
                "(unquote $step-proof))",
                (
                    (
                        "$step-proof",
                        f"(multi:true {episode} (multi:step multi:n7))",
                    ),
                ),
                f"(multi:terminal {episode})",
            ),
        ]
    return next_blocks(state)


def next_blocks(state: State) -> list[str]:
    episode = state.episode
    specifications = (
        (
            "next-p-toggle-a",
            "$does $absence",
            (
                ("$does", f"(multi:does {episode} multi:robot (multi:a $index))"),
                ("$absence", f"(multi:not-true {episode} (multi:p $index))"),
            ),
            "p",
        ),
        (
            "next-p-from-q-b",
            "$does $q-proof",
            (
                ("$does", f"(multi:does {episode} multi:robot (multi:b $index))"),
                ("$q-proof", f"(multi:true {episode} (multi:q $index))"),
            ),
            "p",
        ),
        (
            "next-p-persist",
            "$p-proof $absences",
            (
                ("$p-proof", f"(multi:true {episode} (multi:p $index))"),
                (
                    "$absences",
                    f"(multi:both-not-does {episode} multi:robot "
                    "(multi:a $index) (multi:b $index))",
                ),
            ),
            "p",
        ),
        (
            "next-q-from-p-b",
            "$does $p-proof",
            (
                ("$does", f"(multi:does {episode} multi:robot (multi:b $index))"),
                ("$p-proof", f"(multi:true {episode} (multi:p $index))"),
            ),
            "q",
        ),
        (
            "next-q-from-r-c",
            "$does $r-proof",
            (
                ("$does", f"(multi:does {episode} multi:robot (multi:c $index))"),
                ("$r-proof", f"(multi:true {episode} (multi:r $index))"),
            ),
            "q",
        ),
        (
            "next-q-persist",
            "$q-proof $absences",
            (
                ("$q-proof", f"(multi:true {episode} (multi:q $index))"),
                (
                    "$absences",
                    f"(multi:both-not-does {episode} multi:robot "
                    "(multi:b $index) (multi:c $index))",
                ),
            ),
            "q",
        ),
        (
            "next-r-from-q-c",
            "$does $q-proof",
            (
                ("$does", f"(multi:does {episode} multi:robot (multi:c $index))"),
                ("$q-proof", f"(multi:true {episode} (multi:q $index))"),
            ),
            "r",
        ),
        (
            "next-r-persist",
            "$r-proof $not-c",
            (
                ("$r-proof", f"(multi:true {episode} (multi:r $index))"),
                ("$not-c", f"(multi:not-does {episode} multi:robot (multi:c $index))"),
            ),
            "r",
        ),
    )
    blocks = [
        rule_block(
            "both-not-does-a-b",
            "iggp:multi:both-not-does-a-b",
            f"(multi:proof:both-not-does-a-b {episode} $index "
            "(unquote $not-a) (unquote $not-b))",
            (
                (
                    "$not-a",
                    f"(multi:not-does {episode} multi:robot (multi:a $index))",
                ),
                (
                    "$not-b",
                    f"(multi:not-does {episode} multi:robot (multi:b $index))",
                ),
            ),
            f"(multi:both-not-does {episode} multi:robot "
            "(multi:a $index) (multi:b $index))",
        ),
        rule_block(
            "both-not-does-b-c",
            "iggp:multi:both-not-does-b-c",
            f"(multi:proof:both-not-does-b-c {episode} $index "
            "(unquote $not-b) (unquote $not-c))",
            (
                (
                    "$not-b",
                    f"(multi:not-does {episode} multi:robot (multi:b $index))",
                ),
                (
                    "$not-c",
                    f"(multi:not-does {episode} multi:robot (multi:c $index))",
                ),
            ),
            f"(multi:both-not-does {episode} multi:robot "
            "(multi:b $index) (multi:c $index))",
        ),
    ]
    blocks.extend(
        [
        rule_block(
            label,
            f"iggp:multi:{label}",
            f"(multi:proof:{label} {episode} $index "
            + " ".join(f"(unquote {argument})" for argument in proof_args.split())
            + ")",
            premises,
            f"(multi:next {episode} (multi:{signal} $index))",
        )
        for label, proof_args, premises, signal in specifications
        ]
    )
    blocks.append(
        rule_block(
            "next-step",
            "iggp:multi:next-step",
            f"(multi:proof:next-step {episode} $earlier $later "
            "(unquote $true-proof) (unquote $successor-proof))",
            (
                (
                    "$true-proof",
                    f"(multi:true {episode} (multi:step $earlier))",
                ),
                (
                    "$successor-proof",
                    "(multi:successor $earlier $later)",
                ),
            ),
            f"(multi:next {episode} (multi:step $later))",
        )
    )
    return blocks


def render_rules(states: Iterable[State]) -> str:
    lines = [
        "; Generated proof-producing rules for all multiplebuttonsandlights states.",
        "; Finite views project explicit state and action absence evidence.",
        "; Outside the exact episode image, no absence proof is manufactured.",
        "",
    ]
    for state in states:
        blocks = view_fact_blocks(state)
        if state.target == "next":
            for first, second in SUCCESSORS:
                blocks.append(
                    fact_block(
                        f"successor-{first}-{second}",
                        f"iggp:multi:successor-{first}-{second}",
                        f"multi:proof:successor-{first}-{second}",
                        f"(multi:successor multi:n{first} multi:n{second})",
                    )
                )
        blocks.extend(projection_blocks(state))
        blocks.extend(target_blocks(state))
        package_identity = (
            f"iggp-multi-{state.target}-{state.split}-"
            f"state-{state.ordinal}-v1"
        )
        lines.extend(
            [
                f"(= (multi:package {state.episode})",
                f"  (compile:rule-package {package_identity}",
                "    (rm-package",
                "\n".join(blocks),
                "    )))",
                "",
            ]
        )
    return "\n".join(lines)


def proof_shape_definitions() -> list[str]:
    lines = []
    for signal in SIGNALS:
        lines.extend(
            [
                f"(= (multi:proof-shape",
                f"     (multi:proof:goal-0-{signal} $state $absence))",
                f"   (goal-zero-via multi:{signal}-at-5))",
            ]
        )
    lines.extend(
        [
            "(= (multi:absence-shape (unquote (quote $proof)))",
            "   (multi:absence-shape $proof))",
            "(= (multi:absence-shape",
            "     (multi:proof:not-true-p $state $index $q $r $view))",
            "   (state-absence (signal:p-at $index)))",
            "(= (multi:does-shape (unquote (quote $proof)))",
            "   (multi:does-shape $proof))",
            "(= (multi:does-shape",
            "     (multi:proof:does-a $state $index $b $c $view))",
            "   (action-presence (action:a-at $index)))",
            "(= (multi:proof-shape",
            "     (multi:proof:next-p-toggle-a $state $index $does $absence))",
            "   (next-p-toggle-via",
            "     (multi:does-shape $does) (multi:absence-shape $absence)))",
            "(= (multi:proof-shape",
            "     (multi:proof:terminal-step-seven $state $step))",
            "   (terminal-via step-seven))",
            "(= (iggp:multi:proof-shapes $episode (quote $goal))",
            "  (let",
            "    (compile-result proof-occurrence-bag",
            "      $occurrences $metrics $revision)",
            "    (compile:run",
            "      (multi:package $episode) 32 2000000 256 (quote $goal))",
            "    (collapse",
            "      (let (occurrence $proof-data) (superpose $occurrences)",
            "        (let (quote $proof) $proof-data",
            "          (multi:proof-shape $proof))))))",
            "",
        ]
    )
    return lines


def find_canary(
    states: tuple[State, ...],
    target: str,
    atom: str,
    count: int,
) -> State:
    for state in states:
        if (
            state.target == target
            and atom in state.atoms
            and proof_count(state, atom) == count
        ):
            return state
    raise GenerationError(f"no {target}/{atom}/{count} proof canary")


def find_toggle_canary(states: tuple[State, ...]) -> tuple[State, str]:
    for state in states:
        if state.target != "next":
            continue
        facts = background_counts(state)
        for index in INDICES:
            if (
                facts.get("does_a", Counter())[("robot", index)] == 1
                and facts.get("true_p", Counter())[(index,)] == 0
                and f"next_p({index})" in state.positives
            ):
                return state, index
    raise GenerationError("no next-p toggle/absence canary")


def render_fixture(
    states: Iterable[State],
) -> tuple[str, str, int, int, int]:
    state_list = tuple(states)
    fixture = [
        "; Exact qualification for every multiplebuttonsandlights target task.",
        ";",
        f"; Canonical GDL: {GDL_PATH}",
        f"; SHA-256: {GDL_SHA256}",
        "; No Prolog projection exists in the pinned source revision.",
        "; Every train, validation, and test atom occurrence is classified.",
        "; GDL negation consumes finite episode-view evidence, never failure.",
        "",
        "!(import! &self ../../lib/ilp/iggp_finite_view_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_multiplebuttonsandlights_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_multiplebuttonsandlights_rules.metta)",
        "!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)",
        "",
        "(= (iggp:multi:classify $name $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (multi:package $episode) 32 2000000 256 (quote $goal))",
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
        for atom_ordinal, atom in enumerate(state.atoms, 1):
            name = (
                f"multi:{state.target}:{state.split}:"
                f"state-{state.ordinal}:atom-{atom_ordinal}"
            )
            count = proof_count(state, atom)
            if (count > 0) != (atom in positives):
                raise GenerationError(
                    f"{name}: canonical GDL and source label disagree"
                )
            fixture.extend(
                [
                    f"!(iggp:multi:classify {name} {state.episode}",
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
                expected.append(
                    f"[(iggp:case {name} not-derived 0 ())]"
                )
            cases += 1

    goal_canary = find_canary(
        state_list,
        "goal",
        "goal(robot, 0)",
        3,
    )
    toggle_canary, toggle_index = find_toggle_canary(state_list)
    terminal_canary = find_canary(
        state_list,
        "terminal",
        "terminal()",
        1,
    )
    canaries = (
        (goal_canary, "goal(robot, 0)"),
        (toggle_canary, f"next_p({toggle_index})"),
        (terminal_canary, "terminal()"),
    )
    for state, atom in canaries:
        fixture.extend(
            [
                "",
                "!(iggp:multi:proof-shapes",
                f"  {state.episode}",
                f"  (quote {prime_atom(atom, state.episode)}))",
            ]
        )
    expected.extend(
        [
            "[((goal-zero-via multi:p-at-5) "
            "(goal-zero-via multi:q-at-5) "
            "(goal-zero-via multi:r-at-5))]",
            f"[((next-p-toggle-via (action-presence (action:a-at multi:n{toggle_index})) "
            f"(state-absence (signal:p-at multi:n{toggle_index}))))]",
            "[((terminal-via step-seven))]",
        ]
    )

    if (
        cases != EXPECTED_CASES
        or derived_cases != EXPECTED_DERIVED_CASES
        or proof_occurrences != EXPECTED_PROOF_OCCURRENCES
    ):
        raise GenerationError(
            "multiplebuttonsandlights totals changed: "
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
        default=repo / "lib/ilp/iggp_multiplebuttonsandlights_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/iggp_multiplebuttonsandlights_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=(
            repo
            / "examples/prime/iggp_multiplebuttonsandlights_ground_truth.metta"
        ),
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=(
            repo
            / "examples/prime/iggp_multiplebuttonsandlights_ground_truth.expected"
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
            f"FAIL: IGGP multiplebuttonsandlights generation: {exc}",
            file=sys.stderr,
        )
        return 1

    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        "IGGP multiplebuttonsandlights qualification: "
        f"{cases} atom occurrences, {derived} derived cases, "
        f"{occurrences} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
