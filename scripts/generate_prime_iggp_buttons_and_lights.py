#!/usr/bin/env python3
"""Generate the exact Prime qualification for IGGP buttons_and_lights."""

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
    finite_status_view,
    load_game_states,
    materialize_outputs,
    parse_gdl,
    parse_ground_atom,
    unique_finite_member,
)


GAME = "buttons_and_lights"
GDL_PATH = "games/buttons_and_lights.txt"
GDL_SHA256 = "701742477ee9b78648fb20e5705dc2a765c5259d7b344d9ea904b83aa9ac812b"
PROLOG_PATH = "games/buttons_and_lights.pl"
PROLOG_SHA256 = (
    "2da32ceb430d3140f142b3f593cdc62fce31f44cf2521d3523e7ad64861f3a8e"
)
TYPE_PATH = "types/buttons_and_lights.typ"
TYPE_SHA256 = "96fbcca38a8e7bb6d504f79152884f5861083ed1cf108a600701378ccc9cb0fe"
EXPECTED_CASES = 992
EXPECTED_DERIVED_CASES = 284
EXPECTED_PROOF_OCCURRENCES = 314

SWITCHES = ("p", "q", "r")
ACTIONS = ("a", "b", "c")
STEPS = tuple(str(value) for value in range(1, 8))
SUCCESSORS = tuple((str(value), str(value + 1)) for value in range(1, 7))
SWITCH_RULES = (
    ("p-a-absent", "p", "a", "absent", "p"),
    ("p-b-q", "p", "b", "true", "q"),
    ("p-c-p", "p", "c", "true", "p"),
    ("q-a-q", "q", "a", "true", "q"),
    ("q-b-p", "q", "b", "true", "p"),
    ("q-c-r", "q", "c", "true", "r"),
    ("r-a-r", "r", "a", "true", "r"),
    ("r-b-r", "r", "b", "true", "r"),
    ("r-c-q", "r", "c", "true", "q"),
)

CANONICAL_GDL = (
    ("role", "robot"),
    *(('base', fluent) for fluent in (*SWITCHES, *STEPS)),
    *(('input', 'robot', action) for action in ACTIONS),
    ("init", "1"),
    *(('legal', 'robot', action) for action in ACTIONS),
    (
        "<=",
        ("next", "p"),
        ("does", "robot", "a"),
        ("not", ("true", "p")),
    ),
    ("<=", ("next", "p"), ("does", "robot", "b"), ("true", "q")),
    ("<=", ("next", "p"), ("does", "robot", "c"), ("true", "p")),
    ("<=", ("next", "q"), ("does", "robot", "a"), ("true", "q")),
    ("<=", ("next", "q"), ("does", "robot", "b"), ("true", "p")),
    ("<=", ("next", "q"), ("does", "robot", "c"), ("true", "r")),
    ("<=", ("next", "r"), ("does", "robot", "a"), ("true", "r")),
    ("<=", ("next", "r"), ("does", "robot", "b"), ("true", "r")),
    ("<=", ("next", "r"), ("does", "robot", "c"), ("true", "q")),
    (
        "<=",
        ("next", "?y"),
        ("true", "?x"),
        ("successor", "?x", "?y"),
    ),
    (
        "<=",
        ("goal", "robot", "100"),
        ("true", "p"),
        ("true", "q"),
        ("true", "r"),
    ),
    ("<=", ("goal", "robot", "0"), ("not", ("true", "p"))),
    ("<=", ("goal", "robot", "0"), ("not", ("true", "q"))),
    ("<=", ("goal", "robot", "0"), ("not", ("true", "r"))),
    ("<=", "terminal", ("true", "p"), ("true", "q"), ("true", "r")),
    ("<=", "terminal", ("true", "7")),
    *(('successor', first, second) for first, second in SUCCESSORS),
)

STATIC_CLOSURE = (
    "input(robot, a)",
    "input(robot, b)",
    "input(robot, c)",
    "role(robot)",
    *(f"successor({first}, {second})" for first, second in SUCCESSORS),
)


def validate_rule_sources(snapshot_root: Path) -> None:
    gdl = checked_source(
        snapshot_root / GDL_PATH,
        GDL_SHA256,
        "canonical buttons_and_lights GDL",
    ).decode("utf-8")
    if parse_gdl(gdl) != CANONICAL_GDL:
        raise GenerationError("canonical buttons_and_lights GDL changed")

    prolog = checked_source(
        snapshot_root / PROLOG_PATH,
        PROLOG_SHA256,
        "excluded buttons_and_lights Prolog projection",
    ).decode("utf-8")
    live = "\n".join(
        line for line in prolog.splitlines() if not line.lstrip().startswith("%")
    )
    normalized = re.sub(r"\s+", "", live)
    if (
        "next(p):-does(robot,a),not(true(p))." not in normalized
        or "legal(" in normalized
        or "successor" in normalized
    ):
        raise GenerationError(
            "expected incomplete buttons_and_lights projection changed"
        )

    checked_source(
        snapshot_root / TYPE_PATH,
        TYPE_SHA256,
        "buttons_and_lights type declarations",
    )


def source_atom(text: str) -> tuple[str, tuple[str, ...]]:
    atom = parse_ground_atom(text)
    if any(argument.args for argument in atom.args):
        raise GenerationError(
            f"expected flat buttons_and_lights atom, got {text!r}"
        )
    return atom.head, tuple(argument.head for argument in atom.args)


def state_view(state: State) -> tuple[str, str, str, str]:
    true_fluents = [
        arguments[0]
        for text in state.background
        for head, arguments in (source_atom(text),)
        if head == "true" and len(arguments) == 1
    ]
    numeric = [fluent for fluent in true_fluents if fluent in STEPS]
    flags = finite_status_view(
        (fluent for fluent in true_fluents if fluent in SWITCHES),
        SWITCHES,
        f"{state.episode}: switches",
    )
    step = unique_finite_member(
        numeric,
        STEPS,
        f"{state.episode}: step",
    )
    if len(true_fluents) != len(flags) - flags.count("absent") + 1:
        raise GenerationError(
            f"{state.episode}: state contains an unsupported true fluent"
        )
    return (*flags, step)


def validate_background(state: State) -> None:
    state_view(state)
    does = [
        arguments
        for text in state.background
        for head, arguments in (source_atom(text),)
        if head == "does"
    ]
    other = [
        text
        for text in state.background
        if source_atom(text)[0] not in {"true", "does"}
    ]
    if other or len(does) > 1:
        raise GenerationError(f"{state.episode}: background shape changed")
    if any(
        arguments[0] != "robot" or arguments[1] not in ACTIONS
        for arguments in does
    ):
        raise GenerationError(f"{state.episode}: unsupported action fact")


def load_states(snapshot_root: Path, repo: Path) -> tuple[State, ...]:
    validate_rule_sources(snapshot_root)
    states = load_game_states(snapshot_root, repo, GAME, "buttons")
    for state in states:
        if state.statics != STATIC_CLOSURE:
            raise GenerationError(f"{state.episode}: source statics changed")
        validate_background(state)
    return states


def leaf(atom: GroundAtom) -> str:
    if atom.args:
        raise GenerationError(f"expected leaf, got {atom}")
    mapping = {
        "robot": "buttons:robot",
        **{action: f"buttons:{action}" for action in ACTIONS},
        **{switch: f"buttons:{switch}" for switch in SWITCHES},
        **{step: f"buttons:n{step}" for step in STEPS},
        "0": "buttons:n0",
        "100": "buttons:n100",
    }
    try:
        return mapping[atom.head]
    except KeyError as exc:
        raise GenerationError(
            f"unsupported buttons_and_lights symbol {atom.head}"
        ) from exc


def prime_atom(text: str, episode: str) -> str:
    atom = parse_ground_atom(text)
    if atom.head == "goal" and len(atom.args) == 2:
        return (
            f"(buttons:goal {episode} {leaf(atom.args[0])} "
            f"{leaf(atom.args[1])})"
        )
    if atom.head == "legal" and len(atom.args) == 2:
        return (
            f"(buttons:legal {episode} {leaf(atom.args[0])} "
            f"{leaf(atom.args[1])})"
        )
    if atom.head == "next" and len(atom.args) == 1:
        return f"(buttons:next {episode} {leaf(atom.args[0])})"
    if atom.head == "terminal" and not atom.args:
        return f"(buttons:terminal {episode})"
    if atom.head == "does" and len(atom.args) == 2:
        return (
            f"(buttons:does {episode} {leaf(atom.args[0])} "
            f"{leaf(atom.args[1])})"
        )
    if atom.head == "true" and len(atom.args) == 1:
        return f"(buttons:true {episode} {leaf(atom.args[0])})"
    raise GenerationError(f"unsupported buttons_and_lights atom {text!r}")


def background_counts(state: State) -> dict[str, Counter[tuple[str, ...]]]:
    result: dict[str, Counter[tuple[str, ...]]] = {}
    for text in state.background:
        head, arguments = source_atom(text)
        result.setdefault(head, Counter())[arguments] += 1
    return result


def proof_count(state: State, atom_text: str) -> int:
    head, arguments = source_atom(atom_text)
    facts = background_counts(state)
    true = facts.get("true", Counter())
    does = facts.get("does", Counter())
    absent = {switch: int(true[(switch,)] == 0) for switch in SWITCHES}
    if head == "legal" and len(arguments) == 2:
        return int(arguments[0] == "robot" and arguments[1] in ACTIONS)
    if head == "goal" and arguments == ("robot", "100"):
        return true[("p",)] * true[("q",)] * true[("r",)]
    if head == "goal" and arguments == ("robot", "0"):
        return sum(absent.values())
    if head == "terminal" and not arguments:
        return (
            true[("p",)] * true[("q",)] * true[("r",)]
            + true[("7",)]
        )
    if head != "next" or len(arguments) != 1:
        raise GenerationError(
            f"unsupported buttons_and_lights target {atom_text!r}"
        )
    (fluent,) = arguments
    if fluent in STEPS:
        return sum(
            true[(earlier,)]
            for earlier, later in SUCCESSORS
            if later == fluent
        )
    return sum(
        does[("robot", action)]
        * (
            absent[condition]
            if relation == "absent"
            else true[(condition,)]
        )
        for _, output, action, relation, condition in SWITCH_RULES
        if output == fluent
    )


def view_proof_name(state: State) -> str:
    return (
        f"buttons:proof:view:{state.target}:{state.split}:"
        f"state-{state.ordinal}"
    )


def does_proof_name(state: State, ordinal: int) -> str:
    return (
        f"buttons:proof:does:{state.target}:{state.split}:"
        f"state-{state.ordinal}:occurrence-{ordinal}"
    )


def render_types(states: Iterable[State]) -> str:
    state_list = tuple(states)
    lines = [
        "; Generated exact-image Prime declarations for IGGP buttons_and_lights.",
        "; One complete finite-state view constructs presence and absence evidence.",
        "; Absence is scoped to the pinned episode; it is not negation-as-failure.",
        "",
        "(: buttons:state (u 0))",
        "(: buttons:agent (u 0))",
        "(: buttons:action (u 0))",
        "(: buttons:fluent (u 0))",
        "(: buttons:score (u 0))",
        "(: buttons:robot buttons:agent)",
    ]
    lines.extend(f"(: buttons:{action} buttons:action)" for action in ACTIONS)
    lines.extend(f"(: buttons:{switch} buttons:fluent)" for switch in SWITCHES)
    lines.extend(f"(: buttons:n{step} buttons:fluent)" for step in STEPS)
    lines.extend(
        [
            "(: buttons:n0 buttons:score)",
            "(: buttons:n100 buttons:score)",
            "",
            "(: buttons:state-view",
            "  (-> (state : buttons:state)",
            "      (p q r : finite-view:status)",
            "      (step : buttons:fluent) (u 0)))",
            "(: buttons:true",
            "  (-> (state : buttons:state) (fluent : buttons:fluent) (u 0)))",
            "(: buttons:not-true",
            "  (-> (state : buttons:state) (fluent : buttons:fluent) (u 0)))",
            "(: buttons:does",
            "  (-> (state : buttons:state) (agent : buttons:agent)",
            "      (action : buttons:action) (u 0)))",
            "(: buttons:successor",
            "  (-> (state : buttons:state) (earlier later : buttons:fluent)",
            "      (u 0)))",
            "(: buttons:goal",
            "  (-> (state : buttons:state) (agent : buttons:agent)",
            "      (score : buttons:score) (u 0)))",
            "(: buttons:legal",
            "  (-> (state : buttons:state) (agent : buttons:agent)",
            "      (action : buttons:action) (u 0)))",
            "(: buttons:next",
            "  (-> (state : buttons:state) (fluent : buttons:fluent) (u 0)))",
            "(: buttons:terminal (-> (state : buttons:state) (u 0)))",
            "",
        ]
    )
    lines.extend(f"(: {state.episode} buttons:state)" for state in state_list)
    lines.extend(
        [
            "",
            "(: buttons:proof:true-p",
            "  (-> (state : buttons:state) (q r : finite-view:status)",
            "      (step : buttons:fluent)",
            "      (view : (buttons:state-view state finite-view:present q r step))",
            "      (buttons:true state buttons:p)))",
            "(: buttons:proof:true-q",
            "  (-> (state : buttons:state) (p r : finite-view:status)",
            "      (step : buttons:fluent)",
            "      (view : (buttons:state-view state p finite-view:present r step))",
            "      (buttons:true state buttons:q)))",
            "(: buttons:proof:true-r",
            "  (-> (state : buttons:state) (p q : finite-view:status)",
            "      (step : buttons:fluent)",
            "      (view : (buttons:state-view state p q finite-view:present step))",
            "      (buttons:true state buttons:r)))",
            "(: buttons:proof:not-true-p",
            "  (-> (state : buttons:state) (q r : finite-view:status)",
            "      (step : buttons:fluent)",
            "      (view : (buttons:state-view state finite-view:absent q r step))",
            "      (buttons:not-true state buttons:p)))",
            "(: buttons:proof:not-true-q",
            "  (-> (state : buttons:state) (p r : finite-view:status)",
            "      (step : buttons:fluent)",
            "      (view : (buttons:state-view state p finite-view:absent r step))",
            "      (buttons:not-true state buttons:q)))",
            "(: buttons:proof:not-true-r",
            "  (-> (state : buttons:state) (p q : finite-view:status)",
            "      (step : buttons:fluent)",
            "      (view : (buttons:state-view state p q finite-view:absent step))",
            "      (buttons:not-true state buttons:r)))",
            "(: buttons:proof:true-step",
            "  (-> (state : buttons:state) (p q r : finite-view:status)",
            "      (step : buttons:fluent)",
            "      (view : (buttons:state-view state p q r step))",
            "      (buttons:true state step)))",
            "",
        ]
    )
    for first, second in SUCCESSORS:
        lines.extend(
            [
                f"(: buttons:proof:successor-{first}-{second}",
                "  (-> (state : buttons:state)",
                f"      (buttons:successor state buttons:n{first}",
                f"        buttons:n{second})))",
            ]
        )
    for action in ACTIONS:
        lines.extend(
            [
                f"(: buttons:proof:legal-{action}",
                "  (-> (state : buttons:state)",
                f"      (buttons:legal state buttons:robot buttons:{action})))",
            ]
        )
    lines.extend(
        [
            "",
            "(: buttons:proof:goal-100",
            "  (-> (state : buttons:state)",
            "      (p-proof : (buttons:true state buttons:p))",
            "      (q-proof : (buttons:true state buttons:q))",
            "      (r-proof : (buttons:true state buttons:r))",
            "      (buttons:goal state buttons:robot buttons:n100)))",
        ]
    )
    for switch in SWITCHES:
        lines.extend(
            [
                f"(: buttons:proof:goal-0-{switch}",
                "  (-> (state : buttons:state)",
                f"      (absence : (buttons:not-true state buttons:{switch}))",
                "      (buttons:goal state buttons:robot buttons:n0)))",
            ]
        )
    lines.extend(
        [
            "(: buttons:proof:terminal-all-on",
            "  (-> (state : buttons:state)",
            "      (p-proof : (buttons:true state buttons:p))",
            "      (q-proof : (buttons:true state buttons:q))",
            "      (r-proof : (buttons:true state buttons:r))",
            "      (buttons:terminal state)))",
            "(: buttons:proof:terminal-step-seven",
            "  (-> (state : buttons:state)",
            "      (step-proof : (buttons:true state buttons:n7))",
            "      (buttons:terminal state)))",
            "(: buttons:proof:next-step",
            "  (-> (state : buttons:state) (earlier later : buttons:fluent)",
            "      (true-proof : (buttons:true state earlier))",
            "      (successor-proof : (buttons:successor state earlier later))",
            "      (buttons:next state later)))",
        ]
    )
    for label, output, action, relation, condition in SWITCH_RULES:
        lines.extend(
            [
                f"(: buttons:proof:next-{label}",
                "  (-> (state : buttons:state)",
                f"      (does-proof : (buttons:does state buttons:robot buttons:{action}))",
                f"      (condition-proof : (buttons:{'not-true' if relation == 'absent' else 'true'} state buttons:{condition}))",
                f"      (buttons:next state buttons:{output})))",
            ]
        )
    lines.append("")
    for state in state_list:
        p, q, r, step = state_view(state)
        lines.append(
            f"(: {view_proof_name(state)} (buttons:state-view {state.episode} "
            f"finite-view:{p} finite-view:{q} finite-view:{r} buttons:n{step}))"
        )
        for ordinal, text in enumerate(state.background, 1):
            if source_atom(text)[0] == "does":
                lines.append(
                    f"(: {does_proof_name(state, ordinal)} "
                    f"{prime_atom(text, state.episode)})"
                )
    lines.append("")
    return "\n".join(lines)


def view_blocks(state: State) -> tuple[str, ...]:
    episode = state.episode
    specifications = (
        ("true-p", "present", "$q", "$r", "p", "true"),
        ("true-q", "$p", "present", "$r", "q", "true"),
        ("true-r", "$p", "$q", "present", "r", "true"),
        ("not-true-p", "absent", "$q", "$r", "p", "not-true"),
        ("not-true-q", "$p", "absent", "$r", "q", "not-true"),
        ("not-true-r", "$p", "$q", "absent", "r", "not-true"),
    )
    blocks = []
    for label, p, q, r, fluent, relation in specifications:
        arguments = " ".join(
            value if value.startswith("$") else f"finite-view:{value}"
            for value in (p, q, r)
        )
        proof_args = " ".join(
            value for value in (p, q, r) if value.startswith("$")
        )
        if proof_args:
            proof_args += " "
        blocks.append(
            "\n".join(
                [
                    f"      (rm-block {label} iggp:buttons:{label}",
                    "        (quote",
                    f"          (buttons:proof:{label} {episode} {proof_args}$step",
                    "            (unquote $view-proof)))",
                    "        (rm-cons",
                    "          (rm-premise $view-proof",
                    f"            (quote (buttons:state-view {episode}",
                    f"              {arguments} $step)))",
                    "          rm-nil)",
                    f"        (quote (buttons:{relation} {episode} buttons:{fluent})))",
                ]
            )
        )
    blocks.append(
        "\n".join(
            [
                "      (rm-block true-step iggp:buttons:true-step",
                "        (quote",
                f"          (buttons:proof:true-step {episode} $p $q $r $step",
                "            (unquote $view-proof)))",
                "        (rm-cons",
                "          (rm-premise $view-proof",
                f"            (quote (buttons:state-view {episode}",
                "              $p $q $r $step)))",
                "          rm-nil)",
                f"        (quote (buttons:true {episode} $step)))",
            ]
        )
    )
    return tuple(blocks)


def target_blocks(state: State) -> tuple[str, ...]:
    episode = state.episode
    if state.target == "legal":
        return tuple(
            fact_block(
                f"legal-{action}",
                f"iggp:buttons:legal-{action}",
                f"(buttons:proof:legal-{action} {episode})",
                f"(buttons:legal {episode} buttons:robot buttons:{action})",
            )
            for action in ACTIONS
        )
    if state.target == "goal":
        all_on = "\n".join(
            [
                "      (rm-block goal-100 iggp:buttons:goal-100",
                "        (quote",
                f"          (buttons:proof:goal-100 {episode}",
                "            (unquote $p-proof) (unquote $q-proof)",
                "            (unquote $r-proof)))",
                "        (rm-cons",
                "          (rm-premise $p-proof",
                f"            (quote (buttons:true {episode} buttons:p)))",
                "          (rm-cons",
                "            (rm-premise $q-proof",
                f"              (quote (buttons:true {episode} buttons:q)))",
                "            (rm-cons",
                "              (rm-premise $r-proof",
                f"                (quote (buttons:true {episode} buttons:r)))",
                "              rm-nil)))",
                f"        (quote (buttons:goal {episode}",
                "          buttons:robot buttons:n100)))",
            ]
        )
        zero = []
        for switch in SWITCHES:
            zero.append(
                "\n".join(
                    [
                        f"      (rm-block goal-0-{switch}",
                        f"        iggp:buttons:goal-0-{switch}",
                        "        (quote",
                        f"          (buttons:proof:goal-0-{switch} {episode}",
                        "            (unquote $absence)))",
                        "        (rm-cons",
                        "          (rm-premise $absence",
                        f"            (quote (buttons:not-true {episode} buttons:{switch})))",
                        "          rm-nil)",
                        f"        (quote (buttons:goal {episode}",
                        "          buttons:robot buttons:n0)))",
                    ]
                )
            )
        return (all_on, *zero)
    if state.target == "terminal":
        all_on = "\n".join(
            [
                "      (rm-block terminal-all-on iggp:buttons:terminal-all-on",
                "        (quote",
                f"          (buttons:proof:terminal-all-on {episode}",
                "            (unquote $p-proof) (unquote $q-proof)",
                "            (unquote $r-proof)))",
                "        (rm-cons",
                "          (rm-premise $p-proof",
                f"            (quote (buttons:true {episode} buttons:p)))",
                "          (rm-cons",
                "            (rm-premise $q-proof",
                f"              (quote (buttons:true {episode} buttons:q)))",
                "            (rm-cons",
                "              (rm-premise $r-proof",
                f"                (quote (buttons:true {episode} buttons:r)))",
                "              rm-nil)))",
                f"        (quote (buttons:terminal {episode})))",
            ]
        )
        step_seven = "\n".join(
            [
                "      (rm-block terminal-step-seven",
                "        iggp:buttons:terminal-step-seven",
                "        (quote",
                f"          (buttons:proof:terminal-step-seven {episode}",
                "            (unquote $step-proof)))",
                "        (rm-cons",
                "          (rm-premise $step-proof",
                f"            (quote (buttons:true {episode} buttons:n7)))",
                "          rm-nil)",
                f"        (quote (buttons:terminal {episode})))",
            ]
        )
        return all_on, step_seven

    blocks = []
    for label, output, action, relation, condition in SWITCH_RULES:
        condition_relation = "not-true" if relation == "absent" else "true"
        blocks.append(
            "\n".join(
                [
                    f"      (rm-block next-{label} iggp:buttons:next-{label}",
                    "        (quote",
                    f"          (buttons:proof:next-{label} {episode}",
                    "            (unquote $does-proof)",
                    "            (unquote $condition-proof)))",
                    "        (rm-cons",
                    "          (rm-premise $does-proof",
                    f"            (quote (buttons:does {episode}",
                    f"              buttons:robot buttons:{action})))",
                    "          (rm-cons",
                    "            (rm-premise $condition-proof",
                    f"              (quote (buttons:{condition_relation} {episode}",
                    f"                buttons:{condition})))",
                    "            rm-nil))",
                    f"        (quote (buttons:next {episode} buttons:{output})))",
                ]
            )
        )
    blocks.append(
        "\n".join(
            [
                "      (rm-block next-step iggp:buttons:next-step",
                "        (quote",
                f"          (buttons:proof:next-step {episode} $earlier $later",
                "            (unquote $true-proof)",
                "            (unquote $successor-proof)))",
                "        (rm-cons",
                "          (rm-premise $true-proof",
                f"            (quote (buttons:true {episode} $earlier)))",
                "          (rm-cons",
                "            (rm-premise $successor-proof",
                f"              (quote (buttons:successor {episode}",
                "                $earlier $later)))",
                "            rm-nil))",
                f"        (quote (buttons:next {episode} $later)))",
            ]
        )
    )
    return tuple(blocks)


def render_rules(states: Iterable[State]) -> str:
    lines = [
        "; Generated proof-producing rules for all buttons_and_lights states.",
        "; State-view projection makes finite-state absence explicit evidence.",
        "; Outside the exact state image, no absence proof is manufactured.",
        "",
    ]
    for state in states:
        p, q, r, step = state_view(state)
        blocks = [
            fact_block(
                "state-view",
                (
                    f"iggp:buttons:{state.target}:{state.split}:"
                    f"state-{state.ordinal}:view"
                ),
                view_proof_name(state),
                (
                    f"(buttons:state-view {state.episode} finite-view:{p} "
                    f"finite-view:{q} finite-view:{r} buttons:n{step})"
                ),
            )
        ]
        for first, second in SUCCESSORS:
            blocks.append(
                fact_block(
                    f"successor-{first}-{second}",
                    f"iggp:buttons:successor-{first}-{second}",
                    f"(buttons:proof:successor-{first}-{second} {state.episode})",
                    (
                        f"(buttons:successor {state.episode} "
                        f"buttons:n{first} buttons:n{second})"
                    ),
                )
            )
        for ordinal, text in enumerate(state.background, 1):
            if source_atom(text)[0] == "does":
                blocks.append(
                    fact_block(
                        f"does-{ordinal}",
                        (
                            f"iggp:buttons:{state.target}:{state.split}:"
                            f"state-{state.ordinal}:does-{ordinal}"
                        ),
                        does_proof_name(state, ordinal),
                        prime_atom(text, state.episode),
                    )
                )
        blocks.extend(view_blocks(state))
        blocks.extend(target_blocks(state))
        package_identity = (
            f"iggp-buttons-{state.target}-{state.split}-"
            f"state-{state.ordinal}-v1"
        )
        lines.extend(
            [
                f"(= (buttons:package {state.episode})",
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
        "(= (buttons:proof-shape",
        "     (buttons:proof:goal-0-p $state $absence))",
        "   (goal-zero-via buttons:p))",
        "(= (buttons:proof-shape",
        "     (buttons:proof:goal-0-q $state $absence))",
        "   (goal-zero-via buttons:q))",
        "(= (buttons:proof-shape",
        "     (buttons:proof:goal-0-r $state $absence))",
        "   (goal-zero-via buttons:r))",
        "(= (buttons:proof-shape",
        "     (buttons:proof:terminal-all-on $state $p $q $r))",
        "   (terminal-via all-on))",
        "(= (buttons:proof-shape",
        "     (buttons:proof:terminal-step-seven $state $step))",
        "   (terminal-via step-seven))",
        "(= (buttons:absence-shape (unquote (quote $proof)))",
        "   (buttons:absence-shape $proof))",
        "(= (buttons:absence-shape",
        "     (buttons:proof:not-true-p $state $q $r $step $view))",
        "   (finite-state-absence buttons:p))",
        "(= (buttons:proof-shape",
        "     (buttons:proof:next-p-a-absent $state $does $absence))",
        "   (next-p-via (buttons:absence-shape $absence)))",
        "(= (iggp:buttons:proof-shapes $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (buttons:package $episode) 32 2000000 256 (quote $goal))",
        "    (collapse",
        "      (let (occurrence $proof-data) (superpose $occurrences)",
        "        (let (quote $proof) $proof-data",
        "          (buttons:proof-shape $proof))))))",
        "",
    ]


def find_canary(
    states: tuple[State, ...], target: str, atom: str, count: int
) -> State:
    for state in states:
        if (
            state.target == target
            and atom in state.atoms
            and proof_count(state, atom) == count
        ):
            return state
    raise GenerationError(f"no {target}/{atom}/{count} proof canary")


def find_absence_next_canary(states: tuple[State, ...]) -> State:
    for state in states:
        if state.target != "next" or "next(p)" not in state.positives:
            continue
        facts = background_counts(state)
        if (
            facts.get("does", Counter())[("robot", "a")] == 1
            and facts.get("true", Counter())[("p",)] == 0
        ):
            return state
    raise GenerationError("no next-p finite-state absence canary")


def render_fixture(
    states: Iterable[State],
) -> tuple[str, str, int, int, int]:
    state_list = tuple(states)
    fixture = [
        "; Exact qualification for all IGGP buttons_and_lights target tasks.",
        ";",
        f"; Canonical GDL: {GDL_PATH}",
        f"; SHA-256: {GDL_SHA256}",
        "; The excluded Prolog projection omits numeric successor transitions.",
        "; Every train, validation, and test atom occurrence is classified.",
        "; GDL negation consumes finite-state absence evidence, never failure.",
        "",
        "!(import! &self ../../lib/ilp/iggp_finite_view_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_buttons_and_lights_types.metta)",
        "!(import! &self ../../lib/ilp/iggp_buttons_and_lights_rules.metta)",
        "!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)",
        "",
        "(= (iggp:buttons:classify $name $episode (quote $goal))",
        "  (let",
        "    (compile-result proof-occurrence-bag",
        "      $occurrences $metrics $revision)",
        "    (compile:run",
        "      (buttons:package $episode) 32 2000000 256 (quote $goal))",
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
                f"buttons:{state.target}:{state.split}:"
                f"state-{state.ordinal}:atom-{atom_ordinal}"
            )
            count = proof_count(state, atom)
            if (count > 0) != (atom in positives):
                raise GenerationError(
                    f"{name}: canonical GDL and source label disagree"
                )
            fixture.extend(
                [
                    f"!(iggp:buttons:classify {name} {state.episode}",
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
        state_list, "goal", "goal(robot, 0)", 3
    )
    terminal_canary = find_canary(
        state_list, "terminal", "terminal()", 2
    )
    absence_canary = find_absence_next_canary(state_list)
    for state, atom in (
        (goal_canary, "goal(robot, 0)"),
        (terminal_canary, "terminal()"),
        (absence_canary, "next(p)"),
    ):
        fixture.extend(
            [
                "",
                "!(iggp:buttons:proof-shapes",
                f"  {state.episode}",
                f"  (quote {prime_atom(atom, state.episode)}))",
            ]
        )
    expected.extend(
        [
            "[((goal-zero-via buttons:p) (goal-zero-via buttons:q) "
            "(goal-zero-via buttons:r))]",
            "[((terminal-via all-on) (terminal-via step-seven))]",
            "[((next-p-via (finite-state-absence buttons:p)))]",
        ]
    )

    if (
        cases != EXPECTED_CASES
        or derived_cases != EXPECTED_DERIVED_CASES
        or proof_occurrences != EXPECTED_PROOF_OCCURRENCES
    ):
        raise GenerationError(
            "buttons_and_lights totals changed: "
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
        default=repo / "lib/ilp/iggp_buttons_and_lights_types.metta",
    )
    parser.add_argument(
        "--rules-output",
        type=Path,
        default=repo / "lib/ilp/iggp_buttons_and_lights_rules.metta",
    )
    parser.add_argument(
        "--fixture-output",
        type=Path,
        default=repo / "examples/prime/iggp_buttons_and_lights_ground_truth.metta",
    )
    parser.add_argument(
        "--expected-output",
        type=Path,
        default=repo / "examples/prime/iggp_buttons_and_lights_ground_truth.expected",
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
        print(f"FAIL: IGGP buttons_and_lights generation: {exc}", file=sys.stderr)
        return 1

    print(
        "PASS: "
        f"{'verified' if args.check else 'generated'} "
        "IGGP buttons_and_lights qualification: "
        f"{cases} atom occurrences, {derived} derived cases, "
        f"{occurrences} proof occurrences"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
