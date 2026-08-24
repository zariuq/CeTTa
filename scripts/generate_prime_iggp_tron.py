#!/usr/bin/env python3
"""Generate the exact Prime qualification for the canonical IGGP Tron game."""

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


GAME = "tron"
GDL_PATH = "games/tron.txt"
GDL_SHA256 = "d9fbaa3972bb7e7e1a8b8d73e48e985061964bffb88c067474fb557854240a39"
PROLOG_PATH = "games/tron.pl"
PROLOG_SHA256 = "04fb21b0c844b1568e80e88ec9c5ed49fabd56a27fff22a953db3679481358cf"
TYPE_PATH = "types/tron.typ"
TYPE_SHA256 = "f5cd642b5cdd31d744c9b2c3ddde8d351cb4a14f4a6cf7c946771bfbcd01216b"

EXPECTED_CASES = 14669
EXPECTED_DERIVED_CASES = 2182
EXPECTED_PROOF_OCCURRENCES = 2274

INDICES = tuple(str(value) for value in range(1, 6))
POSITIONS = tuple((x, y) for y in INDICES for x in INDICES)
AGENTS = ("black", "white")
OBJECTS = ("x", "o")
MOVES = ("left", "right", "up", "down")
CONTROLLED_OBJECT = {"black": "x", "white": "o"}
OTHER_AGENT = {"black": "white", "white": "black"}
SUCCESSORS = tuple((str(value), str(value + 1)) for value in range(1, 5))

STATIC_CLOSURE = (
    *(f"bounds({index})" for index in INDICES),
    "controls(black, x)",
    "controls(white, o)",
    "distinct(black, white)",
    "distinct(white, black)",
    *(
        f"input({agent}, {move})"
        for agent in AGENTS
        for move in ("down", "left", "right", "up")
    ),
    "object(o)",
    "object(x)",
    "role(black)",
    "role(white)",
    *(f"succ({earlier}, {later})" for earlier, later in SUCCESSORS),
)

CANONICAL_GDL = (
    ("role", "black"),
    ("role", "white"),
    (
        "<=",
        ("base", ("at", "?x", "?y", "?obj")),
        ("bounds", "?x"),
        ("bounds", "?y"),
        ("object", "?obj"),
    ),
    (
        "<=",
        ("base", ("marked", "?x", "?y")),
        ("bounds", "?x"),
        ("bounds", "?y"),
    ),
    *(("<=", ("input", "?r", move), ("role", "?r")) for move in MOVES),
    *(("bounds", index) for index in INDICES),
    *(("succ", earlier, later) for earlier, later in SUCCESSORS),
    ("object", "x"),
    ("object", "o"),
    ("init", ("at", "1", "1", "x")),
    ("init", ("at", "2", "5", "o")),
    ("controls", "black", "x"),
    ("controls", "white", "o"),
    (
        "<=",
        ("aux_something_at", "?x", "?y"),
        ("true", ("at", "?x", "?y", "?obj")),
    ),
    (
        "<=",
        ("legal", "?r", "left"),
        ("true", ("at", "?x", "?y", "?obj")),
        ("controls", "?r", "?obj"),
        ("succ", "?z", "?x"),
        ("not", ("aux_something_at", "?z", "?y")),
    ),
    (
        "<=",
        ("legal", "?r", "right"),
        ("true", ("at", "?x", "?y", "?obj")),
        ("controls", "?r", "?obj"),
        ("succ", "?x", "?z"),
        ("not", ("aux_something_at", "?z", "?y")),
    ),
    (
        "<=",
        ("legal", "?r", "up"),
        ("true", ("at", "?x", "?y", "?obj")),
        ("controls", "?r", "?obj"),
        ("succ", "?y", "?z"),
        ("not", ("aux_something_at", "?x", "?z")),
    ),
    (
        "<=",
        ("legal", "?r", "down"),
        ("true", ("at", "?x", "?y", "?obj")),
        ("controls", "?r", "?obj"),
        ("succ", "?z", "?y"),
        ("not", ("aux_something_at", "?x", "?z")),
    ),
    (
        "<=",
        ("next", ("at", "?x", "?y", "?obj")),
        ("true", ("at", "?z", "?y", "?obj")),
        ("does", "?r", "left"),
        ("controls", "?r", "?obj"),
        ("succ", "?x", "?z"),
    ),
    (
        "<=",
        ("next", ("at", "?x", "?y", "?obj")),
        ("true", ("at", "?z", "?y", "?obj")),
        ("does", "?r", "right"),
        ("controls", "?r", "?obj"),
        ("succ", "?z", "?x"),
    ),
    (
        "<=",
        ("next", ("at", "?x", "?y", "?obj")),
        ("true", ("at", "?x", "?z", "?obj")),
        ("does", "?r", "up"),
        ("controls", "?r", "?obj"),
        ("succ", "?z", "?y"),
    ),
    (
        "<=",
        ("next", ("at", "?x", "?y", "?obj")),
        ("true", ("at", "?x", "?z", "?obj")),
        ("does", "?r", "down"),
        ("controls", "?r", "?obj"),
        ("succ", "?y", "?z"),
    ),
    (
        "<=",
        ("next", ("marked", "?x", "?y")),
        ("true", ("at", "?x", "?y", "?obj")),
    ),
    (
        "<=",
        ("next", ("marked", "?x", "?y")),
        ("true", ("marked", "?x", "?y")),
    ),
    ("distinct", "black", "white"),
    ("distinct", "white", "black"),
    (
        "<=",
        ("goal", "black", "100"),
        ("aux_dead", "white"),
        ("not", ("aux_dead", "black")),
    ),
    ("<=", ("goal", "black", "0"), ("aux_dead", "black")),
    (
        "<=",
        ("goal", "white", "100"),
        ("aux_dead", "black"),
        ("not", ("aux_dead", "white")),
    ),
    ("<=", ("goal", "white", "0"), ("aux_dead", "white")),
    (
        "<=",
        ("aux_dead", "?r"),
        ("controls", "?r", "?obj"),
        ("true", ("at", "?x", "?y", "?obj")),
        ("true", ("marked", "?x", "?y")),
    ),
    ("<=", "terminal", ("aux_dead", "?r")),
)


@dataclass(frozen=True)
class EpisodeView:
    cells: tuple[tuple[str, str, str], ...]
    actions: tuple[tuple[str, str, str, str], ...]
    player_positions: tuple[tuple[str, str], ...]

    def cell(self, x: str, y: str) -> tuple[str, str, str]:
        return self.cells[POSITIONS.index((x, y))]

    def action(self, agent: str) -> tuple[str, str, str, str]:
        return self.actions[AGENTS.index(agent)]

    def player(self, agent: str) -> tuple[str, str, str]:
        x, y = self.player_positions[AGENTS.index(agent)]
        return x, y, self.cell(x, y)[2]


def validate_rule_sources(snapshot_root: Path) -> None:
    gdl = checked_source(
        snapshot_root / GDL_PATH,
        GDL_SHA256,
        "canonical Tron GDL",
    ).decode("utf-8")
    if parse_gdl(gdl) != CANONICAL_GDL:
        raise GenerationError("canonical Tron GDL changed")
    checked_source(
        snapshot_root / TYPE_PATH,
        TYPE_SHA256,
        "Tron type declarations",
    )
    checked_source(
        snapshot_root / PROLOG_PATH,
        PROLOG_SHA256,
        "excluded Tron Prolog projection",
    )


def source_atom(text: str) -> tuple[str, tuple[str, ...]]:
    atom = parse_ground_atom(text)
    if any(argument.args for argument in atom.args):
        raise GenerationError(f"expected flattened Tron atom, got {text!r}")
    return atom.head, tuple(argument.head for argument in atom.args)


def episode_view(state: State) -> EpisodeView:
    occupied = {obj: [] for obj in OBJECTS}
    marked: list[str] = []
    actions = {agent: [] for agent in AGENTS}
    for text in state.background:
        head, arguments = source_atom(text)
        if head == "true_at" and len(arguments) == 3:
            x, y, obj = arguments
            if obj not in OBJECTS:
                raise GenerationError(f"{state.episode}: unknown object {obj}")
            occupied[obj].append(f"{x}:{y}")
        elif head == "true_marked" and len(arguments) == 2:
            marked.append(f"{arguments[0]}:{arguments[1]}")
        elif head == "does" and len(arguments) == 2:
            agent, move = arguments
            if agent not in AGENTS:
                raise GenerationError(f"{state.episode}: unknown agent {agent}")
            actions[agent].append(move)
        else:
            raise GenerationError(
                f"{state.episode}: unsupported background atom {text!r}"
            )

    position_names = tuple(f"{x}:{y}" for x, y in POSITIONS)
    occupancy = {
        obj: finite_status_view(
            occupied[obj], position_names, f"{state.episode}: true_at/{obj}"
        )
        for obj in OBJECTS
    }
    marked_status = finite_status_view(
        marked, position_names, f"{state.episode}: true_marked"
    )
    action_status = {
        agent: finite_status_view(
            actions[agent], MOVES, f"{state.episode}: does/{agent}"
        )
        for agent in AGENTS
    }
    player_positions = tuple(
        tuple(
            unique_finite_member(
                occupied[CONTROLLED_OBJECT[agent]],
                position_names,
                f"{state.episode}: controlled position/{agent}",
            ).split(":")
        )
        for agent in AGENTS
    )
    return EpisodeView(
        cells=tuple(
            (
                occupancy["x"][position],
                occupancy["o"][position],
                marked_status[position],
            )
            for position in range(len(POSITIONS))
        ),
        actions=tuple(action_status[agent] for agent in AGENTS),
        player_positions=player_positions,
    )


def load_states(snapshot_root: Path, repo: Path) -> tuple[State, ...]:
    validate_rule_sources(snapshot_root)
    states = load_game_states(snapshot_root, repo, GAME, "tron")
    for state in states:
        if state.statics != STATIC_CLOSURE:
            raise GenerationError(f"{state.episode}: source statics changed")
        episode_view(state)
    return states


def prime_index(value: str) -> str:
    if value not in INDICES:
        raise GenerationError(f"unsupported Tron index {value}")
    return f"tron:n{value}"


def prime_atom(text: str, episode: str) -> str:
    head, arguments = source_atom(text)
    if head == "goal" and len(arguments) == 2:
        agent, score = arguments
        if agent in AGENTS and score in {"0", "100"}:
            return f"(tron:goal {episode} tron:{agent} tron:n{score})"
    if head == "legal" and len(arguments) == 2:
        agent, move = arguments
        if agent in AGENTS and move in MOVES:
            return f"(tron:legal {episode} tron:{agent} tron:{move})"
    if head == "next_at" and len(arguments) == 3:
        x, y, obj = arguments
        if obj in OBJECTS:
            return (
                f"(tron:next {episode} "
                f"(tron:at {prime_index(x)} {prime_index(y)} tron:{obj}))"
            )
    if head == "next_marked" and len(arguments) == 2:
        x, y = arguments
        return (
            f"(tron:next {episode} "
            f"(tron:marked {prime_index(x)} {prime_index(y)}))"
        )
    if head == "terminal" and not arguments:
        return f"(tron:terminal {episode})"
    raise GenerationError(f"unsupported Tron atom {text!r}")


def proof_count(state: State, atom_text: str) -> int:
    view = episode_view(state)
    head, arguments = source_atom(atom_text)
    occupied = {
        (x, y, obj): int(
            view.cell(x, y)[OBJECTS.index(obj)] == PRESENT
        )
        for x, y in POSITIONS
        for obj in OBJECTS
    }
    marked = {
        (x, y): int(view.cell(x, y)[2] == PRESENT) for x, y in POSITIONS
    }
    dead = {
        agent: sum(
            occupied[x, y, CONTROLLED_OBJECT[agent]] * marked[x, y]
            for x, y in POSITIONS
        )
        for agent in AGENTS
    }
    does = {
        (agent, move): int(view.action(agent)[MOVES.index(move)] == PRESENT)
        for agent in AGENTS
        for move in MOVES
    }

    if head == "goal" and len(arguments) == 2:
        agent, score = arguments
        if score == "0":
            return dead[agent]
        if score == "100":
            return dead[OTHER_AGENT[agent]] * int(dead[agent] == 0)
    if head == "terminal" and not arguments:
        return sum(dead.values())
    if head == "legal" and len(arguments) == 2:
        agent, move = arguments
        obj = CONTROLLED_OBJECT[agent]
        result = 0
        for x, y in POSITIONS:
            if not occupied[x, y, obj]:
                continue
            destinations: tuple[tuple[str, str], ...] = ()
            if move == "left":
                destinations = tuple((z, y) for z in INDICES if (z, x) in SUCCESSORS)
            elif move == "right":
                destinations = tuple((z, y) for z in INDICES if (x, z) in SUCCESSORS)
            elif move == "up":
                destinations = tuple((x, z) for z in INDICES if (y, z) in SUCCESSORS)
            elif move == "down":
                destinations = tuple((x, z) for z in INDICES if (z, y) in SUCCESSORS)
            for target_x, target_y in destinations:
                result += int(
                    not any(
                        occupied[target_x, target_y, candidate]
                        for candidate in OBJECTS
                    )
                )
        return result
    if head == "next_at" and len(arguments) == 3:
        target_x, target_y, obj = arguments
        agent = AGENTS[OBJECTS.index(obj)]
        result = 0
        for source_x, source_y in POSITIONS:
            source = occupied[source_x, source_y, obj]
            result += source * does[agent, "left"] * int(
                source_y == target_y and (target_x, source_x) in SUCCESSORS
            )
            result += source * does[agent, "right"] * int(
                source_y == target_y and (source_x, target_x) in SUCCESSORS
            )
            result += source * does[agent, "up"] * int(
                source_x == target_x and (source_y, target_y) in SUCCESSORS
            )
            result += source * does[agent, "down"] * int(
                source_x == target_x and (target_y, source_y) in SUCCESSORS
            )
        return result
    if head == "next_marked" and len(arguments) == 2:
        x, y = arguments
        return sum(occupied[x, y, obj] for obj in OBJECTS) + marked[x, y]
    raise GenerationError(f"unsupported Tron target {atom_text!r}")


def cell_view_proof_name(state: State, x: str, y: str) -> str:
    return (
        f"tron:proof:cell-view:{state.target}:{state.split}:"
        f"state-{state.ordinal}:x-{x}:y-{y}"
    )


def action_view_proof_name(state: State, agent: str) -> str:
    return (
        f"tron:proof:action-view:{state.target}:{state.split}:"
        f"state-{state.ordinal}:{agent}"
    )


def player_view_proof_name(state: State, agent: str) -> str:
    return (
        f"tron:proof:player-view:{state.target}:{state.split}:"
        f"state-{state.ordinal}:{agent}"
    )


def needs_cells(state: State) -> bool:
    return state.target in {"legal", "next"}


def needs_actions(state: State) -> bool:
    return state.target == "next"


def needs_players(state: State) -> bool:
    return state.target in {"goal", "terminal"}


def render_types(states: Iterable[State]) -> str:
    state_list = tuple(states)
    lines = [
        "; Generated exact-image Prime declarations for canonical Tron.",
        "; Finite indexed views carry constructive occupancy and action absence.",
        "; They are ordinary Prime data and confer no authority by themselves.",
        "",
        "(: tron:state (u 0))",
        "(: tron:agent (u 0))",
        "(: tron:index (u 0))",
        "(: tron:object (u 0))",
        "(: tron:prop (u 0))",
        "(: tron:action (u 0))",
        "(: tron:score (u 0))",
        "(: tron:black tron:agent)",
        "(: tron:white tron:agent)",
        "(: tron:x tron:object)",
        "(: tron:o tron:object)",
        "(: tron:left tron:action)",
        "(: tron:right tron:action)",
        "(: tron:up tron:action)",
        "(: tron:down tron:action)",
    ]
    lines.extend(f"(: tron:n{index} tron:index)" for index in INDICES)
    lines.extend(["(: tron:n0 tron:score)", "(: tron:n100 tron:score)"])
    lines.extend(
        [
            "(: tron:at",
            "  (-> (x y : tron:index) (object : tron:object) tron:prop))",
            "(: tron:marked (-> (x y : tron:index) tron:prop))",
            "(: tron:cell-view",
            "  (-> (state : tron:state) (x y : tron:index)",
            "      (x-object o-object marked : finite-view:status) (u 0)))",
            "(: tron:action-view",
            "  (-> (state : tron:state) (agent : tron:agent)",
            "      (left right up down : finite-view:status) (u 0)))",
            "(: tron:player-view",
            "  (-> (state : tron:state) (agent : tron:agent)",
            "      (object : tron:object) (x y : tron:index)",
            "      (marked : finite-view:status) (u 0)))",
            "(: tron:true",
            "  (-> (state : tron:state) (proposition : tron:prop) (u 0)))",
            "(: tron:does",
            "  (-> (state : tron:state) (agent : tron:agent)",
            "      (action : tron:action) (u 0)))",
            "(: tron:controls",
            "  (-> (agent : tron:agent) (object : tron:object) (u 0)))",
            "(: tron:successor (-> (earlier later : tron:index) (u 0)))",
            "(: tron:something-at",
            "  (-> (state : tron:state) (x y : tron:index) (u 0)))",
            "(: tron:not-something-at",
            "  (-> (state : tron:state) (x y : tron:index) (u 0)))",
            "(: tron:dead (-> (state : tron:state) (agent : tron:agent) (u 0)))",
            "(: tron:not-dead",
            "  (-> (state : tron:state) (agent : tron:agent) (u 0)))",
            "(: tron:goal",
            "  (-> (state : tron:state) (agent : tron:agent)",
            "      (score : tron:score) (u 0)))",
            "(: tron:legal",
            "  (-> (state : tron:state) (agent : tron:agent)",
            "      (action : tron:action) (u 0)))",
            "(: tron:next",
            "  (-> (state : tron:state) (proposition : tron:prop) (u 0)))",
            "(: tron:terminal (-> (state : tron:state) (u 0)))",
            "",
        ]
    )
    lines.extend(f"(: {state.episode} tron:state)" for state in state_list)
    lines.extend(["", *render_projection_types(), *render_target_proof_types()])

    for state in state_list:
        view = episode_view(state)
        if needs_cells(state):
            for x, y in POSITIONS:
                x_status, o_status, marked_status = view.cell(x, y)
                lines.append(
                    f"(: {cell_view_proof_name(state, x, y)} "
                    f"(tron:cell-view {state.episode} tron:n{x} tron:n{y} "
                    f"finite-view:{x_status} finite-view:{o_status} "
                    f"finite-view:{marked_status}))"
                )
        if needs_actions(state):
            for agent in AGENTS:
                statuses = " ".join(
                    f"finite-view:{status}" for status in view.action(agent)
                )
                lines.append(
                    f"(: {action_view_proof_name(state, agent)} "
                    f"(tron:action-view {state.episode} tron:{agent} "
                    f"{statuses}))"
                )
        if needs_players(state):
            for agent in AGENTS:
                x, y, status = view.player(agent)
                lines.append(
                    f"(: {player_view_proof_name(state, agent)} "
                    f"(tron:player-view {state.episode} tron:{agent} "
                    f"tron:{CONTROLLED_OBJECT[agent]} tron:n{x} tron:n{y} "
                    f"finite-view:{status}))"
                )
    lines.append("")
    return "\n".join(lines)


def render_projection_types() -> list[str]:
    lines = [
        "(: tron:proof:true-at-x",
        "  (-> (state : tron:state) (x y : tron:index)",
        "      (o-object marked : finite-view:status)",
        "      (view : (tron:cell-view state x y finite-view:present o-object marked))",
        "      (tron:true state (tron:at x y tron:x))))",
        "(: tron:proof:true-at-o",
        "  (-> (state : tron:state) (x y : tron:index)",
        "      (x-object marked : finite-view:status)",
        "      (view : (tron:cell-view state x y x-object finite-view:present marked))",
        "      (tron:true state (tron:at x y tron:o))))",
        "(: tron:proof:true-marked",
        "  (-> (state : tron:state) (x y : tron:index)",
        "      (x-object o-object : finite-view:status)",
        "      (view : (tron:cell-view state x y x-object o-object finite-view:present))",
        "      (tron:true state (tron:marked x y))))",
        "(: tron:proof:something-at",
        "  (-> (state : tron:state) (x y : tron:index) (object : tron:object)",
        "      (at-proof : (tron:true state (tron:at x y object)))",
        "      (tron:something-at state x y)))",
        "(: tron:proof:not-something-at",
        "  (-> (state : tron:state) (x y : tron:index)",
        "      (marked : finite-view:status)",
        "      (view : (tron:cell-view state x y",
        "        finite-view:absent finite-view:absent marked))",
        "      (tron:not-something-at state x y)))",
        "(: tron:proof:player-at",
        "  (-> (state : tron:state) (agent : tron:agent)",
        "      (object : tron:object) (x y : tron:index)",
        "      (marked : finite-view:status)",
        "      (view : (tron:player-view state agent object x y marked))",
        "      (tron:true state (tron:at x y object))))",
        "(: tron:proof:player-marked",
        "  (-> (state : tron:state) (agent : tron:agent)",
        "      (object : tron:object) (x y : tron:index)",
        "      (view : (tron:player-view state agent object x y finite-view:present))",
        "      (tron:true state (tron:marked x y))))",
        "(: tron:proof:not-dead",
        "  (-> (state : tron:state) (agent : tron:agent)",
        "      (object : tron:object) (x y : tron:index)",
        "      (view : (tron:player-view state agent object x y finite-view:absent))",
        "      (tron:not-dead state agent)))",
    ]
    for move, pattern, binders in (
        ("left", "finite-view:present right up down", "right up down"),
        ("right", "left finite-view:present up down", "left up down"),
        ("up", "left right finite-view:present down", "left right down"),
        ("down", "left right up finite-view:present", "left right up"),
    ):
        lines.extend(
            [
                f"(: tron:proof:does-{move}",
                "  (-> (state : tron:state) (agent : tron:agent)",
                f"      ({binders} : finite-view:status)",
                f"      (view : (tron:action-view state agent {pattern}))",
                f"      (tron:does state agent tron:{move})))",
            ]
        )
    lines.extend(
        [
            "(: tron:proof:dead",
            "  (-> (state : tron:state) (agent : tron:agent)",
            "      (object : tron:object) (x y : tron:index)",
            "      (controls-proof : (tron:controls agent object))",
            "      (at-proof : (tron:true state (tron:at x y object)))",
            "      (marked-proof : (tron:true state (tron:marked x y)))",
            "      (tron:dead state agent)))",
            "",
        ]
    )
    return lines


def render_target_proof_types() -> list[str]:
    lines = [
        "(: tron:proof:controls-black-x (tron:controls tron:black tron:x))",
        "(: tron:proof:controls-white-o (tron:controls tron:white tron:o))",
    ]
    lines.extend(
        f"(: tron:proof:successor-{earlier}-{later} "
        f"(tron:successor tron:n{earlier} tron:n{later}))"
        for earlier, later in SUCCESSORS
    )
    legal_specs = (
        ("left", "target-x source-x", "target-x source-y"),
        ("right", "source-x target-x", "target-x source-y"),
        ("up", "source-y target-y", "source-x target-y"),
        ("down", "target-y source-y", "source-x target-y"),
    )
    for move, successor_indices, empty_indices in legal_specs:
        lines.extend(
            [
                f"(: tron:proof:legal-{move}",
                "  (-> (state : tron:state) (agent : tron:agent)",
                "      (object : tron:object)",
                "      (source-x source-y target-x target-y : tron:index)",
                "      (at-proof : (tron:true state",
                "        (tron:at source-x source-y object)))",
                "      (controls-proof : (tron:controls agent object))",
                f"      (successor-proof : (tron:successor {successor_indices}))",
                f"      (empty-proof : (tron:not-something-at state {empty_indices}))",
                f"      (tron:legal state agent tron:{move})))",
            ]
        )
    movement_specs = (
        ("left", "target-x source-x"),
        ("right", "source-x target-x"),
        ("up", "source-y target-y"),
        ("down", "target-y source-y"),
    )
    for move, successor_indices in movement_specs:
        lines.extend(
            [
                f"(: tron:proof:next-at-{move}",
                "  (-> (state : tron:state) (agent : tron:agent)",
                "      (object : tron:object)",
                "      (source-x source-y target-x target-y : tron:index)",
                "      (at-proof : (tron:true state",
                "        (tron:at source-x source-y object)))",
                f"      (does-proof : (tron:does state agent tron:{move}))",
                "      (controls-proof : (tron:controls agent object))",
                f"      (successor-proof : (tron:successor {successor_indices}))",
                "      (tron:next state (tron:at target-x target-y object))))",
            ]
        )
    lines.extend(
        [
            "(: tron:proof:next-marked-from-at",
            "  (-> (state : tron:state) (x y : tron:index)",
            "      (object : tron:object)",
            "      (at-proof : (tron:true state (tron:at x y object)))",
            "      (tron:next state (tron:marked x y))))",
            "(: tron:proof:next-marked-persist",
            "  (-> (state : tron:state) (x y : tron:index)",
            "      (marked-proof : (tron:true state (tron:marked x y)))",
            "      (tron:next state (tron:marked x y))))",
            "(: tron:proof:goal-zero",
            "  (-> (state : tron:state) (agent : tron:agent)",
            "      (dead-proof : (tron:dead state agent))",
            "      (tron:goal state agent tron:n0)))",
            "(: tron:proof:goal-hundred",
            "  (-> (state : tron:state) (agent opponent : tron:agent)",
            "      (dead-proof : (tron:dead state opponent))",
            "      (alive-proof : (tron:not-dead state agent))",
            "      (tron:goal state agent tron:n100)))",
            "(: tron:proof:terminal",
            "  (-> (state : tron:state) (agent : tron:agent)",
            "      (dead-proof : (tron:dead state agent))",
            "      (tron:terminal state)))",
            "",
        ]
    )
    return lines


def view_fact_blocks(state: State) -> list[str]:
    view = episode_view(state)
    blocks: list[str] = []
    if needs_cells(state):
        for x, y in POSITIONS:
            x_status, o_status, marked_status = view.cell(x, y)
            blocks.append(
                fact_block(
                    f"cell-view-{x}-{y}",
                    f"iggp:tron:{state.target}:{state.split}:"
                    f"state-{state.ordinal}:cell-view-{x}-{y}",
                    cell_view_proof_name(state, x, y),
                    f"(tron:cell-view {state.episode} tron:n{x} tron:n{y} "
                    f"finite-view:{x_status} finite-view:{o_status} "
                    f"finite-view:{marked_status})",
                )
            )
    if needs_actions(state):
        for agent in AGENTS:
            statuses = " ".join(
                f"finite-view:{status}" for status in view.action(agent)
            )
            blocks.append(
                fact_block(
                    f"action-view-{agent}",
                    f"iggp:tron:{state.target}:{state.split}:"
                    f"state-{state.ordinal}:action-view-{agent}",
                    action_view_proof_name(state, agent),
                    f"(tron:action-view {state.episode} tron:{agent} {statuses})",
                )
            )
    if needs_players(state):
        for agent in AGENTS:
            x, y, status = view.player(agent)
            blocks.append(
                fact_block(
                    f"player-view-{agent}",
                    f"iggp:tron:{state.target}:{state.split}:"
                    f"state-{state.ordinal}:player-view-{agent}",
                    player_view_proof_name(state, agent),
                    f"(tron:player-view {state.episode} tron:{agent} "
                    f"tron:{CONTROLLED_OBJECT[agent]} tron:n{x} tron:n{y} "
                    f"finite-view:{status})",
                )
            )
    return blocks


def static_fact_blocks(state: State) -> list[str]:
    blocks = [
        fact_block(
            "controls-black-x",
            "iggp:tron:controls-black-x",
            "tron:proof:controls-black-x",
            "(tron:controls tron:black tron:x)",
        ),
        fact_block(
            "controls-white-o",
            "iggp:tron:controls-white-o",
            "tron:proof:controls-white-o",
            "(tron:controls tron:white tron:o)",
        ),
    ]
    if state.target in {"legal", "next"}:
        for earlier, later in SUCCESSORS:
            blocks.append(
                fact_block(
                    f"successor-{earlier}-{later}",
                    f"iggp:tron:successor-{earlier}-{later}",
                    f"tron:proof:successor-{earlier}-{later}",
                    f"(tron:successor tron:n{earlier} tron:n{later})",
                )
            )
    return blocks


def cell_projection_blocks(state: State) -> list[str]:
    episode = state.episode
    return [
        rule_block(
            "true-at-x",
            "iggp:tron:true-at-x",
            f"(tron:proof:true-at-x {episode} $x $y $o $marked "
            "(unquote $view-proof))",
            (("$view-proof", f"(tron:cell-view {episode} $x $y finite-view:present $o $marked)"),),
            f"(tron:true {episode} (tron:at $x $y tron:x))",
        ),
        rule_block(
            "true-at-o",
            "iggp:tron:true-at-o",
            f"(tron:proof:true-at-o {episode} $x $y $x-object $marked "
            "(unquote $view-proof))",
            (("$view-proof", f"(tron:cell-view {episode} $x $y $x-object finite-view:present $marked)"),),
            f"(tron:true {episode} (tron:at $x $y tron:o))",
        ),
        rule_block(
            "true-marked",
            "iggp:tron:true-marked",
            f"(tron:proof:true-marked {episode} $x $y $x-object $o-object "
            "(unquote $view-proof))",
            (("$view-proof", f"(tron:cell-view {episode} $x $y $x-object $o-object finite-view:present)"),),
            f"(tron:true {episode} (tron:marked $x $y))",
        ),
        rule_block(
            "something-at",
            "iggp:tron:something-at",
            f"(tron:proof:something-at {episode} $x $y $object "
            "(unquote $at-proof))",
            (("$at-proof", f"(tron:true {episode} (tron:at $x $y $object))"),),
            f"(tron:something-at {episode} $x $y)",
        ),
        rule_block(
            "not-something-at",
            "iggp:tron:not-something-at",
            f"(tron:proof:not-something-at {episode} $x $y $marked "
            "(unquote $view-proof))",
            (("$view-proof", f"(tron:cell-view {episode} $x $y finite-view:absent finite-view:absent $marked)"),),
            f"(tron:not-something-at {episode} $x $y)",
        ),
    ]


def action_projection_blocks(state: State) -> list[str]:
    episode = state.episode
    specifications = (
        ("left", "finite-view:present $right $up $down", "$right $up $down"),
        ("right", "$left finite-view:present $up $down", "$left $up $down"),
        ("up", "$left $right finite-view:present $down", "$left $right $down"),
        ("down", "$left $right $up finite-view:present", "$left $right $up"),
    )
    return [
        rule_block(
            f"does-{move}",
            f"iggp:tron:does-{move}",
            f"(tron:proof:does-{move} {episode} $agent {proof_args} "
            "(unquote $view-proof))",
            (("$view-proof", f"(tron:action-view {episode} $agent {pattern})"),),
            f"(tron:does {episode} $agent tron:{move})",
        )
        for move, pattern, proof_args in specifications
    ]


def player_projection_blocks(state: State) -> list[str]:
    episode = state.episode
    return [
        rule_block(
            "player-at",
            "iggp:tron:player-at",
            f"(tron:proof:player-at {episode} $agent $object $x $y $marked "
            "(unquote $view-proof))",
            (("$view-proof", f"(tron:player-view {episode} $agent $object $x $y $marked)"),),
            f"(tron:true {episode} (tron:at $x $y $object))",
        ),
        rule_block(
            "player-marked",
            "iggp:tron:player-marked",
            f"(tron:proof:player-marked {episode} $agent $object $x $y "
            "(unquote $view-proof))",
            (("$view-proof", f"(tron:player-view {episode} $agent $object $x $y finite-view:present)"),),
            f"(tron:true {episode} (tron:marked $x $y))",
        ),
        rule_block(
            "not-dead",
            "iggp:tron:not-dead",
            f"(tron:proof:not-dead {episode} $agent $object $x $y "
            "(unquote $view-proof))",
            (("$view-proof", f"(tron:player-view {episode} $agent $object $x $y finite-view:absent)"),),
            f"(tron:not-dead {episode} $agent)",
        ),
        rule_block(
            "dead",
            "iggp:tron:dead",
            f"(tron:proof:dead {episode} $agent $object $x $y "
            "(unquote $controls-proof) (unquote $at-proof) "
            "(unquote $marked-proof))",
            (
                ("$controls-proof", "(tron:controls $agent $object)"),
                ("$at-proof", f"(tron:true {episode} (tron:at $x $y $object))"),
                ("$marked-proof", f"(tron:true {episode} (tron:marked $x $y))"),
            ),
            f"(tron:dead {episode} $agent)",
        ),
    ]


def legal_blocks(state: State) -> list[str]:
    episode = state.episode
    specifications = (
        (
            "left",
            "$source-x $y",
            "$source-x $y $target-x $y",
            "$target-x $source-x",
            "$target-x $y",
        ),
        (
            "right",
            "$source-x $y",
            "$source-x $y $target-x $y",
            "$source-x $target-x",
            "$target-x $y",
        ),
        (
            "up",
            "$x $source-y",
            "$x $source-y $x $target-y",
            "$source-y $target-y",
            "$x $target-y",
        ),
        (
            "down",
            "$x $source-y",
            "$x $source-y $x $target-y",
            "$target-y $source-y",
            "$x $target-y",
        ),
    )
    return [
        rule_block(
            f"legal-{move}",
            f"iggp:tron:legal-{move}",
            f"(tron:proof:legal-{move} {episode} $agent $object "
            f"{proof_indices} "
            "(unquote $at-proof) (unquote $controls-proof) "
            "(unquote $successor-proof) (unquote $empty-proof))",
            (
                ("$at-proof", f"(tron:true {episode} (tron:at {at_indices} $object))"),
                ("$controls-proof", "(tron:controls $agent $object)"),
                ("$successor-proof", f"(tron:successor {successor_indices})"),
                ("$empty-proof", f"(tron:not-something-at {episode} {empty_indices})"),
            ),
            f"(tron:legal {episode} $agent tron:{move})",
        )
        for (
            move,
            at_indices,
            proof_indices,
            successor_indices,
            empty_indices,
        ) in specifications
    ]


def next_blocks(state: State) -> list[str]:
    episode = state.episode
    movement = (
        (
            "left",
            "$source-x $y",
            "$source-x $y $target-x $y",
            "$target-x $source-x",
            "$target-x $y",
        ),
        (
            "right",
            "$source-x $y",
            "$source-x $y $target-x $y",
            "$source-x $target-x",
            "$target-x $y",
        ),
        (
            "up",
            "$x $source-y",
            "$x $source-y $x $target-y",
            "$source-y $target-y",
            "$x $target-y",
        ),
        (
            "down",
            "$x $source-y",
            "$x $source-y $x $target-y",
            "$target-y $source-y",
            "$x $target-y",
        ),
    )
    blocks = [
        rule_block(
            f"next-at-{move}",
            f"iggp:tron:next-at-{move}",
            f"(tron:proof:next-at-{move} {episode} $agent $object "
            f"{proof_indices} "
            "(unquote $at-proof) (unquote $does-proof) "
            "(unquote $controls-proof) (unquote $successor-proof))",
            (
                ("$at-proof", f"(tron:true {episode} (tron:at {at_indices} $object))"),
                ("$does-proof", f"(tron:does {episode} $agent tron:{move})"),
                ("$controls-proof", "(tron:controls $agent $object)"),
                ("$successor-proof", f"(tron:successor {successor_indices})"),
            ),
            f"(tron:next {episode} (tron:at {target_indices} $object))",
        )
        for (
            move,
            at_indices,
            proof_indices,
            successor_indices,
            target_indices,
        ) in movement
    ]
    blocks.extend(
        [
            rule_block(
                "next-marked-from-at",
                "iggp:tron:next-marked-from-at",
                f"(tron:proof:next-marked-from-at {episode} $x $y $object "
                "(unquote $at-proof))",
                (("$at-proof", f"(tron:true {episode} (tron:at $x $y $object))"),),
                f"(tron:next {episode} (tron:marked $x $y))",
            ),
            rule_block(
                "next-marked-persist",
                "iggp:tron:next-marked-persist",
                f"(tron:proof:next-marked-persist {episode} $x $y "
                "(unquote $marked-proof))",
                (("$marked-proof", f"(tron:true {episode} (tron:marked $x $y))"),),
                f"(tron:next {episode} (tron:marked $x $y))",
            ),
        ]
    )
    return blocks


def goal_blocks(state: State) -> list[str]:
    episode = state.episode
    blocks: list[str] = []
    for agent in AGENTS:
        opponent = OTHER_AGENT[agent]
        blocks.extend(
            [
                rule_block(
                    f"goal-zero-{agent}",
                    f"iggp:tron:goal-zero-{agent}",
                    f"(tron:proof:goal-zero {episode} tron:{agent} "
                    "(unquote $dead-proof))",
                    (("$dead-proof", f"(tron:dead {episode} tron:{agent})"),),
                    f"(tron:goal {episode} tron:{agent} tron:n0)",
                ),
                rule_block(
                    f"goal-hundred-{agent}",
                    f"iggp:tron:goal-hundred-{agent}",
                    f"(tron:proof:goal-hundred {episode} tron:{agent} "
                    f"tron:{opponent} (unquote $dead-proof) "
                    "(unquote $alive-proof))",
                    (
                        ("$dead-proof", f"(tron:dead {episode} tron:{opponent})"),
                        ("$alive-proof", f"(tron:not-dead {episode} tron:{agent})"),
                    ),
                    f"(tron:goal {episode} tron:{agent} tron:n100)",
                ),
            ]
        )
    return blocks


def terminal_blocks(state: State) -> list[str]:
    episode = state.episode
    return [
        rule_block(
            f"terminal-{agent}",
            f"iggp:tron:terminal-{agent}",
            f"(tron:proof:terminal {episode} tron:{agent} "
            "(unquote $dead-proof))",
            (("$dead-proof", f"(tron:dead {episode} tron:{agent})"),),
            f"(tron:terminal {episode})",
        )
        for agent in AGENTS
    ]


def target_blocks(state: State) -> list[str]:
    if state.target == "goal":
        return goal_blocks(state)
    if state.target == "legal":
        return legal_blocks(state)
    if state.target == "next":
        return next_blocks(state)
    if state.target == "terminal":
        return terminal_blocks(state)
    raise GenerationError(f"unsupported Tron target family {state.target}")


def render_rules(states: Iterable[State]) -> str:
    lines = [
        "; Generated proof-producing rules for every canonical Tron state.",
        "; Constructive absence comes only from complete finite cell views.",
        "; The incomplete Prolog projection is not used as a rule oracle.",
        "",
    ]
    for state in states:
        blocks = view_fact_blocks(state)
        blocks.extend(static_fact_blocks(state))
        if needs_cells(state):
            blocks.extend(cell_projection_blocks(state))
        if needs_actions(state):
            blocks.extend(action_projection_blocks(state))
        if needs_players(state):
            blocks.extend(player_projection_blocks(state))
        blocks.extend(target_blocks(state))
        package_identity = (
            f"iggp-tron-{state.target}-{state.split}-"
            f"state-{state.ordinal}-v1"
        )
        lines.extend(
            [
                f"(= (tron:package {state.episode})",
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
        "(= (tron:proof-shape",
        "     (tron:proof:next-marked-from-at $state $x $y $object $proof))",
        "   (marked-from-current-at $object))",
        "(= (tron:proof-shape",
        "     (tron:proof:next-marked-persist $state $x $y $proof))",
        "   marked-from-existing-mark)",
        "(= (tron:proof-shape",
        "     (tron:proof:terminal $state $agent $proof))",
        "   (terminal-via $agent))",
        "(= (iggp:tron:proof-shapes $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (tron:package $episode) 32 2000000 256 (quote $goal))",
        "    (collapse",
        "      (let (occurrence $proof-data) (superpose $occurrences)",
        "        (let (quote $proof) $proof-data",
        "          (tron:proof-shape $proof))))))",
        "",
    ]


def find_canary(
    states: tuple[State, ...],
    target: str,
    atom_head: str,
    count: int,
) -> tuple[State, str]:
    for state in states:
        if state.target != target:
            continue
        for atom in state.atoms:
            if source_atom(atom)[0] == atom_head and proof_count(state, atom) == count:
                return state, atom
    raise GenerationError(f"no {target}/{atom_head}/{count} proof canary")


def render_fixture(
    states: Iterable[State],
) -> tuple[str, str, int, int, int]:
    state_list = tuple(states)
    fixture = [
        "; Exact qualification for every canonical Tron target task.",
        ";",
        f"; Canonical GDL: {GDL_PATH}",
        f"; SHA-256: {GDL_SHA256}",
        f"; Excluded Prolog projection: {PROLOG_PATH}",
        f"; SHA-256: {PROLOG_SHA256}",
        "; The projection omits most canonical rules and misstates goal 100.",
        "; Every train, validation, and test atom occurrence is classified.",
        "; GDL negation consumes finite state-view evidence, never failure.",
        "",
        "!(import! &self ../../lib/ilp/iggp_finite_view_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_tron_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_tron_rules.metta)",
        "!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)",
        "",
        "(= (iggp:tron:classify $name $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (tron:package $episode) 32 2000000 256 (quote $goal))",
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
                f"tron:{state.target}:{state.split}:"
                f"state-{state.ordinal}:atom-{atom_ordinal}"
            )
            count = proof_count(state, atom)
            if (count > 0) != (atom in positives):
                raise GenerationError(
                    f"{name}: canonical GDL and source label disagree"
                )
            fixture.extend(
                [
                    f"!(iggp:tron:classify {name} {state.episode}",
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

    marked_state, marked_atom = find_canary(
        state_list, "next", "next_marked", 2
    )
    terminal_state, terminal_atom = find_canary(
        state_list, "terminal", "terminal", 2
    )
    for state, atom in (
        (marked_state, marked_atom),
        (terminal_state, terminal_atom),
    ):
        fixture.extend(
            [
                "",
                "!(iggp:tron:proof-shapes",
                f"  {state.episode}",
                f"  (quote {prime_atom(atom, state.episode)}))",
            ]
        )
    marked_args = source_atom(marked_atom)[1]
    marked_view = episode_view(marked_state)
    marked_object = next(
        obj
        for obj in OBJECTS
        if marked_view.cell(marked_args[0], marked_args[1])[OBJECTS.index(obj)]
        == PRESENT
    )
    expected.extend(
        [
            f"[((marked-from-current-at tron:{marked_object}) "
            "marked-from-existing-mark)]",
            "[((terminal-via tron:black) (terminal-via tron:white))]",
        ]
    )

    if (
        cases != EXPECTED_CASES
        or derived_cases != EXPECTED_DERIVED_CASES
        or proof_occurrences != EXPECTED_PROOF_OCCURRENCES
    ):
        raise GenerationError(
            "Tron totals changed: "
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
        default=repo / "lib/ilp/iggp_tron_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/iggp_tron_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo / "examples/prime/iggp_tron_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo / "examples/prime/iggp_tron_ground_truth.expected",
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
        print(f"FAIL: IGGP Tron generation: {exc}", file=sys.stderr)
        return 1

    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} IGGP Tron qualification: "
        f"{cases} atom occurrences, {derived} derived cases, "
        f"{occurrences} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
