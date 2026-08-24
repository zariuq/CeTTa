"""Shared source-faithful generation utilities for Prime IGGP fixtures."""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
from typing import Any, Iterable
from zipfile import ZipFile

import check_prime_iggp_manifest as corpus
from prime_iggp_presentation import (
    PresentationError,
    parse_gdl as parse_gdl_presentation,
)


TOKEN_RE = re.compile(r"\s*([A-Za-z_][A-Za-z0-9_]*|-?[0-9]+|[(),])")


class GenerationError(RuntimeError):
    """The pinned source cannot generate the claimed qualification."""


def parse_gdl(source: str) -> tuple[Any, ...]:
    """Parse canonical GDL while retaining the generator error boundary."""

    try:
        return parse_gdl_presentation(source)
    except PresentationError as exc:
        raise GenerationError(str(exc)) from exc


PRESENT = "present"
ABSENT = "absent"


@dataclass(frozen=True)
class GroundAtom:
    head: str
    args: tuple["GroundAtom", ...] = ()


@dataclass(frozen=True)
class State:
    target: str
    split: str
    ordinal: int
    episode: str
    atoms: tuple[str, ...]
    statics: tuple[str, ...]
    background: tuple[str, ...]
    positives: tuple[str, ...]


def checked_source(path: Path, expected_sha256: str, label: str) -> bytes:
    if not path.is_file():
        raise GenerationError(f"{label} is missing")
    source = path.read_bytes()
    observed = hashlib.sha256(source).hexdigest()
    if observed != expected_sha256:
        raise GenerationError(f"{label} SHA-256 changed")
    return source


def tokenize_ground_atom(text: str) -> tuple[str, ...]:
    tokens: list[str] = []
    position = 0
    while position < len(text):
        match = TOKEN_RE.match(text, position)
        if match is None:
            raise GenerationError(f"invalid ground atom syntax: {text!r}")
        tokens.append(match.group(1))
        position = match.end()
    return tuple(tokens)


def parse_ground_atom(text: str) -> GroundAtom:
    tokens = tokenize_ground_atom(text)
    position = 0

    def parse_term() -> GroundAtom:
        nonlocal position
        if position >= len(tokens) or tokens[position] in {"(", ")", ","}:
            raise GenerationError(f"expected atom in {text!r}")
        head = tokens[position]
        position += 1
        if position >= len(tokens) or tokens[position] != "(":
            return GroundAtom(head)
        position += 1
        arguments: list[GroundAtom] = []
        if position < len(tokens) and tokens[position] == ")":
            position += 1
            return GroundAtom(head)
        while True:
            arguments.append(parse_term())
            if position >= len(tokens):
                raise GenerationError(f"unterminated atom in {text!r}")
            token = tokens[position]
            position += 1
            if token == ")":
                break
            if token != ",":
                raise GenerationError(f"expected comma in {text!r}")
        return GroundAtom(head, tuple(arguments))

    result = parse_term()
    if position != len(tokens):
        raise GenerationError(f"trailing ground atom syntax: {text!r}")
    return result


def finite_status_view(
    observed: Iterable[str],
    universe: Iterable[str],
    label: str,
) -> tuple[str, ...]:
    """Construct the exact present/absent partition of a finite source view."""
    domain = tuple(universe)
    if len(domain) != len(set(domain)):
        raise GenerationError(f"{label}: finite universe contains duplicates")
    values = tuple(observed)
    counts = Counter(values)
    duplicates = sorted(value for value, count in counts.items() if count > 1)
    if duplicates:
        raise GenerationError(
            f"{label}: finite observation contains duplicates {duplicates}"
        )
    outside = sorted(set(values) - set(domain))
    if outside:
        raise GenerationError(
            f"{label}: finite observation is outside its universe {outside}"
        )
    return tuple(PRESENT if value in counts else ABSENT for value in domain)


def unique_finite_member(
    observed: Iterable[str],
    universe: Iterable[str],
    label: str,
) -> str:
    """Return the sole member of a complete single-valued finite field."""
    values = tuple(observed)
    finite_status_view(values, universe, label)
    if len(values) != 1:
        raise GenerationError(
            f"{label}: expected exactly one finite member, found {len(values)}"
        )
    return values[0]


def load_game_states(
    snapshot_root: Path,
    repo: Path,
    game: str,
    episode_namespace: str,
) -> tuple[State, ...]:
    manifest = corpus.load_manifest(
        repo / "benchmarks/prime/ilp/iggp_manifest.json"
    )
    corpus.validate_manifest(manifest, repo)
    corpus.verify_snapshot(manifest, snapshot_root)

    states: list[State] = []
    with ZipFile(snapshot_root / "data.zip") as archive:
        for target in corpus.TARGETS:
            for split in corpus.SPLITS:
                archive_path = f"data/{split}/{game}_{target}_{split}.dat"
                parsed = corpus.parse_task_data(
                    archive.read(archive_path), archive_path
                )
                for ordinal, state in enumerate(parsed["states"], 1):
                    states.append(
                        State(
                            target=target,
                            split=split,
                            ordinal=ordinal,
                            episode=(
                                f"{episode_namespace}:{target}:{split}:"
                                f"state-{ordinal}"
                            ),
                            atoms=parsed["atoms"],
                            statics=parsed["statics"],
                            background=state["background"],
                            positives=state["positives"],
                        )
                    )
    return tuple(states)


def write_output(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def check_output(path: Path, content: str) -> None:
    if not path.is_file():
        raise GenerationError(f"generated output is missing: {path.name}")
    if path.read_text(encoding="utf-8") != content:
        raise GenerationError(f"generated output drift: {path.name}")


def materialize_outputs(
    outputs: Iterable[tuple[Path, str]], check: bool
) -> None:
    for path, content in outputs:
        if check:
            check_output(path, content)
        else:
            write_output(path, content)


def fact_block(
    block_name: str, rule_identity: str, proof: str, goal: str
) -> str:
    return "\n".join(
        [
            f"      (rm-block {block_name} {rule_identity}",
            f"        (quote {proof})",
            "        rm-nil",
            f"        (quote {goal}))",
        ]
    )


def rule_block(
    block_name: str,
    rule_identity: str,
    proof: str,
    premises: Iterable[tuple[str, str]],
    goal: str,
) -> str:
    premise_term = "rm-nil"
    for variable, premise in reversed(tuple(premises)):
        premise_term = (
            f"(rm-cons (rm-premise {variable} (quote {premise})) "
            f"{premise_term})"
        )
    return "\n".join(
        [
            f"      (rm-block {block_name} {rule_identity}",
            f"        (quote {proof})",
            f"        {premise_term}",
            f"        (quote {goal}))",
        ]
    )
