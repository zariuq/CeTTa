#!/usr/bin/env python3
"""Generate the exact proof-relevant Prime qualification for IGGP SPS."""

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


GAME = "scissors_paper_stone"
GDL_PATH = "games/scissors_paper_stone.txt"
GDL_SHA256 = "fe401ac80704e5b138a48b80d6d2bd171427456245e72e555614efb96351710a"
PROLOG_PATH = "games/scissors_paper_stone.pl"
PROLOG_SHA256 = (
    "570acde717d7ca51ecee9310875299fbe9fc53d2810fb3fe0e8c933d05233a50"
)
TYPE_PATH = "types/scissors_paper_stone.typ"
TYPE_SHA256 = "eabc85a114e3021c95e54bc6dd3e57a58921c8f37a06b232c1580cdee92cb9ba"
EXPECTED_CASES = 1356
EXPECTED_DERIVED_CASES = 404
EXPECTED_PROOF_OCCURRENCES = 404

PLAYERS = ("p1", "p2")
ACTIONS = ("scissors", "paper", "stone")
SUCCESSORS = (("0", "1"), ("1", "2"), ("2", "3"))
BEATS = (
    ("scissors", "paper"),
    ("paper", "stone"),
    ("stone", "scissors"),
)

CANONICAL_GDL = (
    ("<=", ("legal", "?p", "scissors"), ("player", "?p")),
    ("<=", ("legal", "?p", "paper"), ("player", "?p")),
    ("<=", ("legal", "?p", "stone"), ("player", "?p")),
    ("<=", "terminal", ("true", ("step", "3"))),
    ("<=", ("goal", "?p", "?s"), ("true", ("score", "?p", "?s"))),
    (
        "<=",
        ("next", ("step", "?n")),
        ("true", ("step", "?m")),
        ("succ", "?m", "?n"),
    ),
    (
        "<=",
        ("next", ("score", "?p", "?n")),
        ("true", ("score", "?p", "?n")),
        ("draws", "?p"),
    ),
    (
        "<=",
        ("next", ("score", "?p", "?n")),
        ("true", ("score", "?p", "?n")),
        ("loses", "?p"),
    ),
    (
        "<=",
        ("next", ("score", "?p", "?n")),
        ("true", ("score", "?p", "?n2")),
        ("succ", "?n2", "?n"),
        ("wins", "?p"),
    ),
    (
        "<=",
        ("draws", "?p"),
        ("does", "?p", "?a"),
        ("does", "?q", "?a"),
        ("distinct", "?p", "?q"),
    ),
    (
        "<=",
        ("wins", "?p"),
        ("does", "?p", "?a1"),
        ("does", "?q", "?a2"),
        ("distinct", "?p", "?q"),
        ("beats", "?a1", "?a2"),
    ),
    (
        "<=",
        ("loses", "?p"),
        ("does", "?p", "?a1"),
        ("does", "?q", "?a2"),
        ("distinct", "?p", "?q"),
        ("beats", "?a2", "?a1"),
    ),
    *(('beats', first, second) for first, second in BEATS),
    *(('succ', first, second) for first, second in SUCCESSORS),
    *(('player', player) for player in PLAYERS),
    ("init", ("step", "0")),
    ("init", ("score", "p1", "0")),
    ("init", ("score", "p2", "0")),
)

STATIC_CLOSURE = (
    "beats(paper, stone)",
    "beats(scissors, paper)",
    "beats(stone, scissors)",
    "player(p1)",
    "player(p2)",
    "succ(0, 1)",
    "succ(1, 2)",
    "succ(2, 3)",
)


def validate_rule_sources(snapshot_root: Path) -> None:
    gdl = checked_source(
        snapshot_root / GDL_PATH, GDL_SHA256, "canonical SPS GDL"
    ).decode("utf-8")
    if parse_gdl(gdl) != CANONICAL_GDL:
        raise GenerationError("canonical SPS GDL structure changed")

    prolog = checked_source(
        snapshot_root / PROLOG_PATH,
        PROLOG_SHA256,
        "excluded SPS Prolog translation",
    ).decode("utf-8")
    translated = re.sub(r"\s+", "", prolog)
    if (
        "next_score(A,B):-true_score(A,B),wins(A)." not in translated
        or "next_score(A,B):-true_score(A,B),not(wins(A))." not in translated
    ):
        raise GenerationError("expected SPS translation negative control vanished")

    checked_source(
        snapshot_root / TYPE_PATH, TYPE_SHA256, "SPS type declarations"
    )


def load_states(snapshot_root: Path, repo: Path) -> tuple[State, ...]:
    validate_rule_sources(snapshot_root)
    states = load_game_states(snapshot_root, repo, GAME, "sps")
    for state in states:
        if state.statics != STATIC_CLOSURE:
            raise GenerationError(f"{state.episode}: source statics changed")
    return states


def leaf(atom: GroundAtom) -> str:
    if atom.args:
        raise GenerationError(f"expected leaf, got {atom}")
    mapping = {
        "p1": "sps:p1",
        "p2": "sps:p2",
        "scissors": "sps:scissors",
        "paper": "sps:paper",
        "stone": "sps:stone",
        **{str(value): f"sps:n{value}" for value in range(4)},
    }
    try:
        return mapping[atom.head]
    except KeyError as exc:
        raise GenerationError(f"unsupported SPS symbol {atom.head}") from exc


def prime_atom(text: str, episode: str) -> str:
    atom = parse_ground_atom(text)
    if atom.head == "goal" and len(atom.args) == 2:
        return f"(sps:goal {episode} {leaf(atom.args[0])} {leaf(atom.args[1])})"
    if atom.head == "legal" and len(atom.args) == 2:
        return f"(sps:legal {episode} {leaf(atom.args[0])} {leaf(atom.args[1])})"
    if atom.head == "next_score" and len(atom.args) == 2:
        return (
            f"(sps:next-score {episode} {leaf(atom.args[0])} "
            f"{leaf(atom.args[1])})"
        )
    if atom.head == "next_step" and len(atom.args) == 1:
        return f"(sps:next-step {episode} {leaf(atom.args[0])})"
    if atom.head == "terminal" and not atom.args:
        return f"(sps:terminal {episode})"
    if atom.head == "true_score" and len(atom.args) == 2:
        return (
            f"(sps:true-score {episode} {leaf(atom.args[0])} "
            f"{leaf(atom.args[1])})"
        )
    if atom.head == "true_step" and len(atom.args) == 1:
        return f"(sps:true-step {episode} {leaf(atom.args[0])})"
    if atom.head == "does" and len(atom.args) == 2:
        return f"(sps:does {episode} {leaf(atom.args[0])} {leaf(atom.args[1])})"
    raise GenerationError(f"unsupported SPS atom {text!r}")


def source_atom(text: str) -> tuple[str, tuple[str, ...]]:
    atom = parse_ground_atom(text)
    if any(argument.args for argument in atom.args):
        raise GenerationError(f"expected flat SPS source atom, got {text!r}")
    return atom.head, tuple(argument.head for argument in atom.args)


def source_counts(state: State) -> dict[str, Counter[tuple[str, ...]]]:
    result: dict[str, Counter[tuple[str, ...]]] = {}
    for text in (*state.statics, *state.background):
        head, arguments = source_atom(text)
        result.setdefault(head, Counter())[arguments] += 1
    return result


def outcome_counts(
    state: State, player: str
) -> dict[str, Counter[tuple[str, str, str]]]:
    facts = source_counts(state)
    does = facts.get("does", Counter())
    players = facts.get("player", Counter())
    beats = facts.get("beats", Counter())
    result = {name: Counter() for name in ("draw", "win", "loss")}
    for other in PLAYERS:
        if other == player:
            continue
        distinct_count = players[(player,)] * players[(other,)]
        for own_action in ACTIONS:
            for other_action in ACTIONS:
                common = (
                    does[(player, own_action)]
                    * does[(other, other_action)]
                    * distinct_count
                )
                if not common:
                    continue
                detail = (other, own_action, other_action)
                if own_action == other_action:
                    result["draw"][detail] += common
                result["win"][detail] += common * beats[(own_action, other_action)]
                result["loss"][detail] += common * beats[(other_action, own_action)]
    return result


def score_routes(
    state: State, player: str, score: str
) -> Counter[tuple[str, str, str, str, str, str]]:
    facts = source_counts(state)
    true_score = facts.get("true_score", Counter())
    succ = facts.get("succ", Counter())
    outcomes = outcome_counts(state, player)
    routes: Counter[tuple[str, str, str, str, str, str]] = Counter()
    for detail, count in outcomes["draw"].items():
        routes[("draw", *detail, score, score)] += (
            count * true_score[(player, score)]
        )
    for detail, count in outcomes["loss"].items():
        routes[("loss", *detail, score, score)] += (
            count * true_score[(player, score)]
        )
    for detail, count in outcomes["win"].items():
        for previous in (str(value) for value in range(4)):
            routes[("win", *detail, previous, score)] += (
                count
                * true_score[(player, previous)]
                * succ[(previous, score)]
            )
    return +routes


def proof_count(state: State, atom_text: str) -> int:
    head, arguments = source_atom(atom_text)
    facts = source_counts(state)
    if head == "legal" and len(arguments) == 2:
        player, action = arguments
        return int(action in ACTIONS) * facts.get("player", Counter())[(player,)]
    if head == "terminal" and not arguments:
        return facts.get("true_step", Counter())[("3",)]
    if head == "goal" and len(arguments) == 2:
        return facts.get("true_score", Counter())[arguments]
    if head == "next_step" and len(arguments) == 1:
        (later,) = arguments
        return sum(
            facts.get("true_step", Counter())[(earlier,)] * count
            for (earlier, successor), count in facts.get("succ", Counter()).items()
            if successor == later
        )
    if head == "next_score" and len(arguments) == 2:
        return sum(score_routes(state, *arguments).values())
    raise GenerationError(f"unsupported SPS target atom {atom_text!r}")


def background_proof_name(state: State, ordinal: int) -> str:
    return (
        f"sps:proof:background:{state.target}:{state.split}:"
        f"state-{state.ordinal}:occurrence-{ordinal}"
    )


def render_types(states: Iterable[State]) -> str:
    state_list = tuple(states)
    lines = [
        "; Generated exact-image Prime declarations for IGGP scissors-paper-stone.",
        "; Interaction outcomes retain both players, both actions, and all premises.",
        "; Canonical GDL rules remain ordinary proof-producing relations.",
        "",
        "(: sps:state (u 0))",
        "(: sps:agent (u 0))",
        "(: sps:action (u 0))",
        "(: sps:int (u 0))",
        "(: sps:p1 sps:agent)",
        "(: sps:p2 sps:agent)",
        "(: sps:scissors sps:action)",
        "(: sps:paper sps:action)",
        "(: sps:stone sps:action)",
    ]
    lines.extend(f"(: sps:n{value} sps:int)" for value in range(4))
    lines.extend(
        [
            "",
            "(: sps:true-score",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (score : sps:int) (u 0)))",
            "(: sps:true-step",
            "  (-> (state : sps:state) (step : sps:int) (u 0)))",
            "(: sps:does",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (action : sps:action) (u 0)))",
            "(: sps:player",
            "  (-> (state : sps:state) (player : sps:agent) (u 0)))",
            "(: sps:beats",
            "  (-> (state : sps:state) (winner : sps:action)",
            "      (loser : sps:action) (u 0)))",
            "(: sps:succ",
            "  (-> (state : sps:state) (earlier : sps:int)",
            "      (later : sps:int) (u 0)))",
            "(: sps:distinct",
            "  (-> (state : sps:state) (first : sps:agent)",
            "      (second : sps:agent) (u 0)))",
            "(: sps:draws",
            "  (-> (state : sps:state) (player : sps:agent) (u 0)))",
            "(: sps:wins",
            "  (-> (state : sps:state) (player : sps:agent) (u 0)))",
            "(: sps:loses",
            "  (-> (state : sps:state) (player : sps:agent) (u 0)))",
            "(: sps:goal",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (score : sps:int) (u 0)))",
            "(: sps:legal",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (action : sps:action) (u 0)))",
            "(: sps:next-score",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (score : sps:int) (u 0)))",
            "(: sps:next-step",
            "  (-> (state : sps:state) (step : sps:int) (u 0)))",
            "(: sps:terminal (-> (state : sps:state) (u 0)))",
            "",
        ]
    )
    lines.extend(f"(: {state.episode} sps:state)" for state in state_list)
    lines.extend(
        [
            "",
            "(: sps:proof:player-p1",
            "  (-> (state : sps:state) (sps:player state sps:p1)))",
            "(: sps:proof:player-p2",
            "  (-> (state : sps:state) (sps:player state sps:p2)))",
            "(: sps:proof:beats-scissors-paper",
            "  (-> (state : sps:state)",
            "      (sps:beats state sps:scissors sps:paper)))",
            "(: sps:proof:beats-paper-stone",
            "  (-> (state : sps:state)",
            "      (sps:beats state sps:paper sps:stone)))",
            "(: sps:proof:beats-stone-scissors",
            "  (-> (state : sps:state)",
            "      (sps:beats state sps:stone sps:scissors)))",
            "(: sps:proof:succ-0-1",
            "  (-> (state : sps:state) (sps:succ state sps:n0 sps:n1)))",
            "(: sps:proof:succ-1-2",
            "  (-> (state : sps:state) (sps:succ state sps:n1 sps:n2)))",
            "(: sps:proof:succ-2-3",
            "  (-> (state : sps:state) (sps:succ state sps:n2 sps:n3)))",
            "(: sps:proof:distinct-p1-p2",
            "  (-> (state : sps:state) (sps:distinct state sps:p1 sps:p2)))",
            "(: sps:proof:distinct-p2-p1",
            "  (-> (state : sps:state) (sps:distinct state sps:p2 sps:p1)))",
            "",
            "(: sps:proof:draws",
            "  (-> (state : sps:state) (player other : sps:agent)",
            "      (action : sps:action)",
            "      (player-does : (sps:does state player action))",
            "      (other-does : (sps:does state other action))",
            "      (apart : (sps:distinct state player other))",
            "      (sps:draws state player)))",
            "(: sps:proof:wins",
            "  (-> (state : sps:state) (player other : sps:agent)",
            "      (own other-action : sps:action)",
            "      (player-does : (sps:does state player own))",
            "      (other-does : (sps:does state other other-action))",
            "      (apart : (sps:distinct state player other))",
            "      (beats-proof : (sps:beats state own other-action))",
            "      (sps:wins state player)))",
            "(: sps:proof:loses",
            "  (-> (state : sps:state) (player other : sps:agent)",
            "      (own other-action : sps:action)",
            "      (player-does : (sps:does state player own))",
            "      (other-does : (sps:does state other other-action))",
            "      (apart : (sps:distinct state player other))",
            "      (beats-proof : (sps:beats state other-action own))",
            "      (sps:loses state player)))",
            "",
            "(: sps:proof:legal-scissors",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (player-proof : (sps:player state player))",
            "      (sps:legal state player sps:scissors)))",
            "(: sps:proof:legal-paper",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (player-proof : (sps:player state player))",
            "      (sps:legal state player sps:paper)))",
            "(: sps:proof:legal-stone",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (player-proof : (sps:player state player))",
            "      (sps:legal state player sps:stone)))",
            "(: sps:proof:terminal",
            "  (-> (state : sps:state)",
            "      (step-proof : (sps:true-step state sps:n3))",
            "      (sps:terminal state)))",
            "(: sps:proof:goal",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (score : sps:int)",
            "      (score-proof : (sps:true-score state player score))",
            "      (sps:goal state player score)))",
            "(: sps:proof:next-step",
            "  (-> (state : sps:state) (earlier later : sps:int)",
            "      (step-proof : (sps:true-step state earlier))",
            "      (successor-proof : (sps:succ state earlier later))",
            "      (sps:next-step state later)))",
            "(: sps:proof:next-score-draw",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (score : sps:int)",
            "      (score-proof : (sps:true-score state player score))",
            "      (draw-proof : (sps:draws state player))",
            "      (sps:next-score state player score)))",
            "(: sps:proof:next-score-loss",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (score : sps:int)",
            "      (score-proof : (sps:true-score state player score))",
            "      (loss-proof : (sps:loses state player))",
            "      (sps:next-score state player score)))",
            "(: sps:proof:next-score-win",
            "  (-> (state : sps:state) (player : sps:agent)",
            "      (previous score : sps:int)",
            "      (score-proof : (sps:true-score state player previous))",
            "      (successor-proof : (sps:succ state previous score))",
            "      (win-proof : (sps:wins state player))",
            "      (sps:next-score state player score)))",
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


def relation_blocks(state: State) -> tuple[str, ...]:
    episode = state.episode
    draws = "\n".join(
        [
            "      (rm-block draws iggp:sps:draws",
            "        (quote",
            f"          (sps:proof:draws {episode} $player $other $action",
            "            (unquote $player-does) (unquote $other-does)",
            "            (unquote $apart)))",
            "        (rm-cons",
            "          (rm-premise $player-does",
            f"            (quote (sps:does {episode} $player $action)))",
            "          (rm-cons",
            "            (rm-premise $other-does",
            f"              (quote (sps:does {episode} $other $action)))",
            "            (rm-cons",
            "              (rm-premise $apart",
            f"                (quote (sps:distinct {episode} $player $other)))",
            "              rm-nil)))",
            f"        (quote (sps:draws {episode} $player)))",
        ]
    )
    wins = "\n".join(
        [
            "      (rm-block wins iggp:sps:wins",
            "        (quote",
            f"          (sps:proof:wins {episode} $player $other",
            "            $own $other-action",
            "            (unquote $player-does) (unquote $other-does)",
            "            (unquote $apart) (unquote $beats-proof)))",
            "        (rm-cons",
            "          (rm-premise $player-does",
            f"            (quote (sps:does {episode} $player $own)))",
            "          (rm-cons",
            "            (rm-premise $other-does",
            f"              (quote (sps:does {episode} $other $other-action)))",
            "            (rm-cons",
            "              (rm-premise $apart",
            f"                (quote (sps:distinct {episode} $player $other)))",
            "              (rm-cons",
            "                (rm-premise $beats-proof",
            f"                  (quote (sps:beats {episode} $own $other-action)))",
            "                rm-nil))))",
            f"        (quote (sps:wins {episode} $player)))",
        ]
    )
    loses = "\n".join(
        [
            "      (rm-block loses iggp:sps:loses",
            "        (quote",
            f"          (sps:proof:loses {episode} $player $other",
            "            $own $other-action",
            "            (unquote $player-does) (unquote $other-does)",
            "            (unquote $apart) (unquote $beats-proof)))",
            "        (rm-cons",
            "          (rm-premise $player-does",
            f"            (quote (sps:does {episode} $player $own)))",
            "          (rm-cons",
            "            (rm-premise $other-does",
            f"              (quote (sps:does {episode} $other $other-action)))",
            "            (rm-cons",
            "              (rm-premise $apart",
            f"                (quote (sps:distinct {episode} $player $other)))",
            "              (rm-cons",
            "                (rm-premise $beats-proof",
            f"                  (quote (sps:beats {episode} $other-action $own)))",
            "                rm-nil))))",
            f"        (quote (sps:loses {episode} $player)))",
        ]
    )
    return draws, wins, loses


def target_blocks(state: State) -> tuple[str, ...]:
    episode = state.episode
    if state.target == "legal":
        blocks = []
        for action in ACTIONS:
            blocks.append(
                "\n".join(
                    [
                        f"      (rm-block legal-{action} iggp:sps:legal-{action}",
                        "        (quote",
                        f"          (sps:proof:legal-{action} {episode} $player",
                        "            (unquote $player-proof)))",
                        "        (rm-cons",
                        "          (rm-premise $player-proof",
                        f"            (quote (sps:player {episode} $player)))",
                        "          rm-nil)",
                        f"        (quote (sps:legal {episode} $player sps:{action})))",
                    ]
                )
            )
        return tuple(blocks)
    if state.target == "goal":
        return (
            "\n".join(
                [
                    "      (rm-block goal iggp:sps:goal",
                    "        (quote",
                    f"          (sps:proof:goal {episode} $player $score",
                    "            (unquote $score-proof)))",
                    "        (rm-cons",
                    "          (rm-premise $score-proof",
                    f"            (quote (sps:true-score {episode} $player $score)))",
                    "          rm-nil)",
                    f"        (quote (sps:goal {episode} $player $score)))",
                ]
            ),
        )
    if state.target == "terminal":
        return (
            "\n".join(
                [
                    "      (rm-block terminal iggp:sps:terminal",
                    "        (quote",
                    f"          (sps:proof:terminal {episode}",
                    "            (unquote $step-proof)))",
                    "        (rm-cons",
                    "          (rm-premise $step-proof",
                    f"            (quote (sps:true-step {episode} sps:n3)))",
                    "          rm-nil)",
                    f"        (quote (sps:terminal {episode})))",
                ]
            ),
        )

    next_step = "\n".join(
        [
            "      (rm-block next-step iggp:sps:next-step",
            "        (quote",
            f"          (sps:proof:next-step {episode} $earlier $later",
            "            (unquote $step-proof) (unquote $successor-proof)))",
            "        (rm-cons",
            "          (rm-premise $step-proof",
            f"            (quote (sps:true-step {episode} $earlier)))",
            "          (rm-cons",
            "            (rm-premise $successor-proof",
            f"              (quote (sps:succ {episode} $earlier $later)))",
            "            rm-nil))",
            f"        (quote (sps:next-step {episode} $later)))",
        ]
    )
    score_blocks = []
    for route, relation in (("draw", "draws"), ("loss", "loses")):
        score_blocks.append(
            "\n".join(
                [
                    f"      (rm-block next-score-{route}",
                    f"        iggp:sps:next-score-{route}",
                    "        (quote",
                    f"          (sps:proof:next-score-{route} {episode}",
                    "            $player $score",
                    f"            (unquote $score-proof) (unquote ${route}-proof)))",
                    "        (rm-cons",
                    "          (rm-premise $score-proof",
                    f"            (quote (sps:true-score {episode} $player $score)))",
                    "          (rm-cons",
                    f"            (rm-premise ${route}-proof",
                    f"              (quote (sps:{relation} {episode} $player)))",
                    "            rm-nil))",
                    f"        (quote (sps:next-score {episode} $player $score)))",
                ]
            )
        )
    score_blocks.append(
        "\n".join(
            [
                "      (rm-block next-score-win iggp:sps:next-score-win",
                "        (quote",
                f"          (sps:proof:next-score-win {episode}",
                "            $player $previous $score",
                "            (unquote $score-proof)",
                "            (unquote $successor-proof) (unquote $win-proof)))",
                "        (rm-cons",
                "          (rm-premise $score-proof",
                f"            (quote (sps:true-score {episode} $player $previous)))",
                "          (rm-cons",
                "            (rm-premise $successor-proof",
                f"              (quote (sps:succ {episode} $previous $score)))",
                "            (rm-cons",
                "              (rm-premise $win-proof",
                f"                (quote (sps:wins {episode} $player)))",
                "              rm-nil)))",
                f"        (quote (sps:next-score {episode} $player $score)))",
            ]
        )
    )
    return (next_step, *score_blocks)


def render_rules(states: Iterable[State]) -> str:
    lines = [
        "; Generated proof-producing rules for every pinned SPS state.",
        "; Win, draw, and loss remain relational joins with retained witnesses.",
        "; No game-specific evaluator or closed-world rejection is used.",
        "",
    ]
    static_facts = (
        ("player-p1", "player-p1", "(sps:player {state} sps:p1)"),
        ("player-p2", "player-p2", "(sps:player {state} sps:p2)"),
        (
            "beats-scissors-paper",
            "beats-scissors-paper",
            "(sps:beats {state} sps:scissors sps:paper)",
        ),
        (
            "beats-paper-stone",
            "beats-paper-stone",
            "(sps:beats {state} sps:paper sps:stone)",
        ),
        (
            "beats-stone-scissors",
            "beats-stone-scissors",
            "(sps:beats {state} sps:stone sps:scissors)",
        ),
        ("succ-0-1", "succ-0-1", "(sps:succ {state} sps:n0 sps:n1)"),
        ("succ-1-2", "succ-1-2", "(sps:succ {state} sps:n1 sps:n2)"),
        ("succ-2-3", "succ-2-3", "(sps:succ {state} sps:n2 sps:n3)"),
        (
            "distinct-p1-p2",
            "distinct-p1-p2",
            "(sps:distinct {state} sps:p1 sps:p2)",
        ),
        (
            "distinct-p2-p1",
            "distinct-p2-p1",
            "(sps:distinct {state} sps:p2 sps:p1)",
        ),
    )
    for state in states:
        blocks = []
        for block, proof, goal in static_facts:
            blocks.append(
                fact_block(
                    block,
                    f"iggp:sps:{proof}",
                    f"(sps:proof:{proof} {state.episode})",
                    goal.format(state=state.episode),
                )
            )
        for ordinal, atom in enumerate(state.background, 1):
            blocks.append(
                fact_block(
                    f"background-{ordinal}",
                    (
                        f"iggp:sps:{state.target}:{state.split}:"
                        f"state-{state.ordinal}:background-{ordinal}"
                    ),
                    background_proof_name(state, ordinal),
                    prime_atom(atom, state.episode),
                )
            )
        blocks.extend(relation_blocks(state))
        blocks.extend(target_blocks(state))
        package_identity = (
            f"iggp-sps-{state.target}-{state.split}-"
            f"state-{state.ordinal}-v1"
        )
        lines.extend(
            [
                f"(= (sps:package {state.episode})",
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
        "(= (sps:outcome-proof-shape (unquote (quote $proof)))",
        "   (sps:outcome-proof-shape $proof))",
        "(= (sps:outcome-proof-shape",
        "     (sps:proof:draws $state $player $other $action",
        "       $player-does $other-does $apart))",
        "   (shared-action $other $action))",
        "(= (sps:outcome-proof-shape",
        "     (sps:proof:wins $state $player $other $own $other-action",
        "       $player-does $other-does $apart $beats-proof))",
        "   (versus $other $own $other-action))",
        "(= (sps:outcome-proof-shape",
        "     (sps:proof:loses $state $player $other $own $other-action",
        "       $player-does $other-does $apart $beats-proof))",
        "   (versus $other $own $other-action))",
        "(= (sps:proof-shape",
        "     (sps:proof:next-score-draw $state $player $score",
        "       $score-proof $outcome-proof))",
        "   (score-via-draw $player $score",
        "     (sps:outcome-proof-shape $outcome-proof)))",
        "(= (sps:proof-shape",
        "     (sps:proof:next-score-loss $state $player $score",
        "       $score-proof $outcome-proof))",
        "   (score-via-loss $player $score",
        "     (sps:outcome-proof-shape $outcome-proof)))",
        "(= (sps:proof-shape",
        "     (sps:proof:next-score-win $state $player $previous $score",
        "       $score-proof $successor-proof $outcome-proof))",
        "   (score-via-win $player $previous $score",
        "     (sps:outcome-proof-shape $outcome-proof)))",
        "(= (iggp:sps:proof-shapes $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (sps:package $episode) 32 2000000 256 (quote $goal))",
        "    (collapse",
        "      (let (occurrence $proof-data) (superpose $occurrences)",
        "        (let (quote $proof) $proof-data",
        "          (sps:proof-shape $proof))))))",
        "",
    ]


def canary(
    states: tuple[State, ...], route_name: str
) -> tuple[State, str, tuple[str, str, str, str, str, str]]:
    for state in states:
        if state.target != "next":
            continue
        for atom_text in state.atoms:
            head, arguments = source_atom(atom_text)
            if head != "next_score" or arguments[0] != "p1":
                continue
            routes = score_routes(state, *arguments)
            matching = [route for route in routes if route[0] == route_name]
            if len(matching) == 1 and routes[matching[0]] == 1:
                return state, atom_text, matching[0]
    raise GenerationError(f"no unique {route_name} proof-shape canary")


def expected_shape(
    player: str, route: tuple[str, str, str, str, str, str]
) -> str:
    name, other, own, other_action, previous, score = route
    if name == "draw":
        detail = (
            f"(shared-action {leaf(GroundAtom(other))} "
            f"{leaf(GroundAtom(own))})"
        )
        return (
            f"(score-via-draw {leaf(GroundAtom(player))} "
            f"{leaf(GroundAtom(score))} {detail})"
        )
    detail = (
        f"(versus {leaf(GroundAtom(other))} {leaf(GroundAtom(own))} "
        f"{leaf(GroundAtom(other_action))})"
    )
    if name == "loss":
        return (
            f"(score-via-loss {leaf(GroundAtom(player))} "
            f"{leaf(GroundAtom(score))} {detail})"
        )
    return (
        f"(score-via-win {leaf(GroundAtom(player))} "
        f"{leaf(GroundAtom(previous))} {leaf(GroundAtom(score))} {detail})"
    )


def render_fixture(
    states: Iterable[State],
) -> tuple[str, str, int, int, int]:
    state_list = tuple(states)
    fixture = [
        "; Exact proof-relevant qualification for IGGP scissors-paper-stone.",
        ";",
        f"; Canonical GDL: {GDL_PATH}",
        f"; SHA-256: {GDL_SHA256}",
        "; The excluded Prolog projection changes the canonical scoring rules.",
        "; Every train, validate, and test atom occurrence is classified.",
        "; Intermediate opponents and actions remain visible in proof data.",
        "",
        "!(import! &self ../../lib/ilp/iggp_scissors_paper_stone_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_scissors_paper_stone_rules.metta)",
        "!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)",
        "",
        "(= (iggp:sps:classify $name $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (sps:package $episode) 32 2000000 256 (quote $goal))",
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
                f"sps:{state.target}:{state.split}:"
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
                    f"!(iggp:sps:classify {name} {state.episode}",
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

    for route_name in ("win", "draw", "loss"):
        state, atom, route = canary(state_list, route_name)
        fixture.extend(
            [
                "",
                "!(iggp:sps:proof-shapes",
                f"  {state.episode}",
                f"  (quote {prime_atom(atom, state.episode)}))",
            ]
        )
        expected.append(f"[({expected_shape('p1', route)})]")

    if (
        cases != EXPECTED_CASES
        or derived_cases != EXPECTED_DERIVED_CASES
        or proof_occurrences != EXPECTED_PROOF_OCCURRENCES
    ):
        raise GenerationError(
            "SPS totals changed: "
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
        default=repo / "lib/ilp/iggp_scissors_paper_stone_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/iggp_scissors_paper_stone_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo / "examples/prime/iggp_scissors_paper_stone_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo / "examples/prime/iggp_scissors_paper_stone_ground_truth.expected",
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
        print(f"FAIL: IGGP SPS generation: {exc}", file=sys.stderr)
        return 1

    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        "IGGP SPS qualification: "
        f"{cases} atom occurrences, {derived} derived cases, "
        f"{occurrences} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
