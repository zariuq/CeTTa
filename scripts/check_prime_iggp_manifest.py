#!/usr/bin/env python3
"""Validate Prime's source-pinned IGGP corpus conversion ledger."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
from typing import Any
from zipfile import ZipFile


SCHEMA = "cetta-prime-iggp-corpus-v1"
REVISION = "263db407366a284616d48fe786c0d1b7de0805e0"
ARCHIVE_SHA256 = (
    "77a48fd74950c5ab1cea3bcd76eea76657d7de8d2d02bd73193d7d30a3208e30"
)
SPLITS = ("train", "validate", "test")
TARGETS = ("goal", "legal", "next", "terminal")
GAMES = (
    "alquerque",
    "asylum",
    "battle_of_numbers",
    "breakthrough",
    "buttons_and_lights",
    "centipede",
    "checkers",
    "coins",
    "connect4team",
    "dont_touch",
    "duikoshi",
    "eight_puzzle",
    "farming",
    "firesheep",
    "fizzbuzz",
    "forager2",
    "freeforall",
    "frogs_and_toads",
    "gt_attrition",
    "gt_centipede",
    "gt_chicken",
    "gt_prisoner",
    "gt_ultimatum",
    "hexforthree",
    "horseshoe",
    "hunter",
    "knights_tour",
    "kono",
    "leafy",
    "lightboard",
    "minimal_decay",
    "minimal_even",
    "multiplebuttonsandlights",
    "nineboardtictactoe",
    "pentago",
    "pilgrimage",
    "platformjumpers",
    "rainbow",
    "scissors_paper_stone",
    "sheep_and_wolf",
    "sokoban",
    "sudoku",
    "sukoshi",
    "switches",
    "tictactoe",
    "tiger_vs_dogs",
    "tron",
    "ttcc4",
    "untwisty_corridor",
    "walkabout",
)
SELECTED_GAMES = (
    "minimal_decay",
    "minimal_even",
    "scissors_paper_stone",
    "buttons_and_lights",
    "multiplebuttonsandlights",
    "tron",
    "untwisty_corridor",
)
IGNORED_REGULAR_ENTRIES = ("data/.DS_Store",)
EXPECTED_DUPLICATE_ATOMS = {
    "data/validate/ttcc4_next_validate.dat": 449,
    "data/validate/ttcc4_legal_validate.dat": 370,
    "data/test/ttcc4_next_test.dat": 454,
    "data/test/ttcc4_legal_test.dat": 361,
    "data/train/ttcc4_legal_train.dat": 1802,
    "data/train/ttcc4_next_train.dat": 1994,
    "data/train/ttcc4_goal_train.dat": 1,
}
EXPECTED_SPLIT_SPECIFIC_ATOMS = (
    "ttcc4/goal",
    "ttcc4/legal",
    "ttcc4/next",
)
MINIMAL_DECAY_FIXTURE = (
    "examples/prime/iggp_minimal_decay_ground_truth.metta"
)
MINIMAL_DECAY_COVERAGE = (
    "canonical-gdl-and-all-pinned-train-validate-test-atom-occurrences"
)
MINIMAL_DECAY_RULE_SOURCE = {
    "path": "games/minimal_decay.txt",
    "sha256": "3e42f2344243da41898ceb3fbb32603f417bc83aa0d939fc430b3b167bb9a174",
}
MINIMAL_DECAY_EXCLUDED_TRANSLATION = {
    "path": "games/minimal_decay.pl",
    "sha256": "e492be2e79ccc2f37828e79b483d10c5ede5f5c9880470a6493bc56aaf89fada",
    "reason": (
        "Excluded as a rule oracle: its live noop clause reverses the "
        "canonical GDL successor arguments."
    ),
}
MINIMAL_EVEN_FIXTURE = (
    "examples/prime/iggp_minimal_even_ground_truth.metta"
)
MINIMAL_EVEN_COVERAGE = (
    "canonical-gdl-and-all-pinned-train-validate-test-atom-occurrences"
)
MINIMAL_EVEN_RULE_SOURCE = {
    "path": "games/minimal_even.txt",
    "sha256": "f8b27304bd1770b425de9951b48c419490b60d629e0de351a460d8a7edccd238",
}
MINIMAL_EVEN_EXCLUDED_TRANSLATION = {
    "path": "games/minimal_even.pl",
    "sha256": "410ab20be9cbbf00bc21dbf889cad18645997e58554a31593921133a2086c89a",
    "reason": (
        "Excluded as a rule oracle: its next-state rule misspells "
        "does_choose as does_chose."
    ),
}
MINIMAL_EVEN_PROOF_OCCURRENCES = {
    "goal": 42,
    "legal": 1280,
    "next": 293,
    "terminal": 42,
}
SPS_FIXTURE = (
    "examples/prime/iggp_scissors_paper_stone_ground_truth.metta"
)
SPS_COVERAGE = (
    "canonical-gdl-and-all-pinned-train-validate-test-atom-occurrences"
)
SPS_RULE_SOURCE = {
    "path": "games/scissors_paper_stone.txt",
    "sha256": "fe401ac80704e5b138a48b80d6d2bd171427456245e72e555614efb96351710a",
}
SPS_EXCLUDED_TRANSLATION = {
    "path": "games/scissors_paper_stone.pl",
    "sha256": "570acde717d7ca51ecee9310875299fbe9fc53d2810fb3fe0e8c933d05233a50",
    "reason": (
        "Excluded as a rule oracle: its live score clauses omit the "
        "canonical successor premise and replace draw-or-loss with "
        "negation of wins."
    ),
}
BUTTONS_FIXTURE = (
    "examples/prime/iggp_buttons_and_lights_ground_truth.metta"
)
BUTTONS_COVERAGE = (
    "canonical-gdl-and-all-pinned-train-validate-test-atom-occurrences"
)
BUTTONS_RULE_SOURCE = {
    "path": "games/buttons_and_lights.txt",
    "sha256": "701742477ee9b78648fb20e5705dc2a765c5259d7b344d9ea904b83aa9ac812b",
}
BUTTONS_EXCLUDED_TRANSLATION = {
    "path": "games/buttons_and_lights.pl",
    "sha256": "2da32ceb430d3140f142b3f593cdc62fce31f44cf2521d3523e7ad64861f3a8e",
    "reason": (
        "Excluded as a rule oracle: its live projection omits the canonical "
        "legal facts and numeric successor transition."
    ),
}
BUTTONS_PROOF_OCCURRENCES = {
    "goal": 61,
    "legal": 96,
    "next": 148,
    "terminal": 9,
}
MULTI_FIXTURE = (
    "examples/prime/iggp_multiplebuttonsandlights_ground_truth.metta"
)
MULTI_COVERAGE = (
    "canonical-gdl-and-all-pinned-train-validate-test-atom-occurrences"
)
MULTI_RULE_SOURCE = {
    "path": "games/multiplebuttonsandlights.txt",
    "sha256": "7325c39696e8477c210d41411dfae0453ee932b64fea5191bddc8328484ee3b4",
}
MULTI_PROOF_OCCURRENCES = {
    "goal": 546,
    "legal": 5427,
    "next": 1076,
    "terminal": 59,
}
TRON_FIXTURE = "examples/prime/iggp_tron_ground_truth.metta"
TRON_COVERAGE = (
    "canonical-gdl-and-all-pinned-train-validate-test-atom-occurrences"
)
TRON_RULE_SOURCE = {
    "path": "games/tron.txt",
    "sha256": "d9fbaa3972bb7e7e1a8b8d73e48e985061964bffb88c067474fb557854240a39",
}
TRON_EXCLUDED_TRANSLATION = {
    "path": "games/tron.pl",
    "sha256": "04fb21b0c844b1568e80e88ec9c5ed49fabd56a27fff22a953db3679481358cf",
    "reason": (
        "Excluded as a rule oracle: its live projection retains only legal "
        "and dead fragments, omits next and terminal, and misstates goal 100."
    ),
}
TRON_PROOF_OCCURRENCES = {
    "goal": 128,
    "legal": 714,
    "next": 1354,
    "terminal": 78,
}
CORRIDOR_FIXTURE = (
    "examples/prime/iggp_untwisty_corridor_ground_truth.metta"
)
CORRIDOR_COVERAGE = (
    "canonical-gdl-and-all-pinned-train-validate-test-atom-occurrences"
)
CORRIDOR_RULE_SOURCE = {
    "path": "games/untwisty_corridor.pl",
    "sha256": "5202b3e657a95aaa71b0cecd5903a5b14f8631793d07d8bda43e4dbdf1b65d48",
}
CORRIDOR_PROOF_OCCURRENCES = {
    "goal": 11,
    "legal": 88,
    "next": 241,
    "terminal": 1,
}
HEX256 = frozenset("0123456789abcdef")


class ManifestError(RuntimeError):
    """The ledger or its pinned source does not satisfy its contract."""


def is_sha256(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in HEX256 for character in value)
    )


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def identify_task_entry(name: str) -> tuple[str, str, str] | None:
    parts = name.split("/")
    if len(parts) != 3 or parts[0] != "data" or parts[1] not in SPLITS:
        return None
    split = parts[1]
    suffix = f"_{split}.dat"
    if not parts[2].endswith(suffix):
        return None
    stem = parts[2][: -len(suffix)]
    for target in TARGETS:
        marker = f"_{target}"
        if stem.endswith(marker):
            game = stem[: -len(marker)]
            return split, game, target
    return None


def parse_type_declarations(source: bytes, path: str) -> set[str]:
    try:
        text = source.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ManifestError(f"{path}: type signature is not UTF-8") from exc
    names: set[str] = set()
    for raw_line in text.splitlines():
        line = raw_line.split("%", 1)[0].strip()
        if not line or "::" not in line:
            continue
        left = line.split("::", 1)[0]
        for name in left.split(","):
            stripped = name.strip()
            if stripped:
                names.add(stripped)
    return names


def parse_task_data(source: bytes, path: str) -> dict[str, Any]:
    try:
        text = source.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ManifestError(f"{path}: task data is not UTF-8") from exc

    sections: list[tuple[str, list[str]]] = []
    current_name: str | None = None
    current_values: list[str] = []
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        if raw_line == "---":
            if current_name is not None:
                sections.append((current_name, current_values))
            current_name = None
            current_values = []
            continue
        if raw_line in ("atoms:", "statics:", "background:", "positives:"):
            if current_name is not None:
                raise ManifestError(
                    f"{path}:{line_number}: section lacks separator"
                )
            current_name = raw_line[:-1]
            continue
        if not raw_line.strip():
            continue
        if current_name is None:
            raise ManifestError(
                f"{path}:{line_number}: data outside a named section"
            )
        if not raw_line.startswith("\t"):
            raise ManifestError(
                f"{path}:{line_number}: task atom is not tab-indented"
            )
        current_values.append(raw_line.strip())
    if current_name is not None:
        sections.append((current_name, current_values))

    if len(sections) < 2 or sections[0][0] != "atoms" or sections[1][0] != "statics":
        raise ManifestError(f"{path}: task must begin with atoms and statics")
    remainder = sections[2:]
    if len(remainder) % 2 != 0:
        raise ManifestError(f"{path}: unmatched background/positives section")
    for index in range(0, len(remainder), 2):
        if remainder[index][0] != "background" or remainder[index + 1][0] != "positives":
            raise ManifestError(
                f"{path}: states must alternate background and positives"
            )

    atoms = sections[0][1]
    statics = sections[1][1]
    atom_set = set(atoms)
    positives = [
        atom
        for index in range(1, len(remainder), 2)
        for atom in remainder[index][1]
    ]
    outside = [atom for atom in positives if atom not in atom_set]
    if outside:
        raise ManifestError(
            f"{path}: positive is absent from atoms: {outside[0]}"
        )
    duplicate_non_atoms = 0
    for name, values in sections[1:]:
        duplicate_non_atoms += len(values) - len(set(values))
    if duplicate_non_atoms:
        raise ManifestError(
            f"{path}: duplicate occurrence outside the atoms inventory"
        )

    return {
        "atoms": tuple(atoms),
        "statics": tuple(statics),
        "states": tuple(
            {
                "background": tuple(remainder[index][1]),
                "positives": tuple(remainder[index + 1][1]),
            }
            for index in range(0, len(remainder), 2)
        ),
        "metrics": {
            "bytes": len(source),
            "atom_occurrences": len(atoms),
            "unique_atoms": len(atom_set),
            "static_occurrences": len(statics),
            "state_pairs": len(remainder) // 2,
            "background_occurrences": sum(
                len(remainder[index][1])
                for index in range(0, len(remainder), 2)
            ),
            "positive_occurrences": len(positives),
            "empty_positive_states": sum(
                not remainder[index][1]
                for index in range(1, len(remainder), 2)
            ),
        },
    }


def add_digest_blob(digest: Any, label: str, content: bytes) -> None:
    label_bytes = label.encode("utf-8")
    digest.update(len(label_bytes).to_bytes(8, "big"))
    digest.update(label_bytes)
    digest.update(len(content).to_bytes(8, "big"))
    digest.update(content)


def task_source_digest(
    type_path: str,
    type_source: bytes,
    data_sources: dict[str, tuple[str, bytes]],
) -> str:
    digest = hashlib.sha256(b"cetta-prime-iggp-task-source-v1\0")
    add_digest_blob(digest, type_path, type_source)
    for split in SPLITS:
        if split not in data_sources:
            raise ManifestError(f"task digest lacks {split} source")
        path, source = data_sources[split]
        add_digest_blob(digest, path, source)
    return digest.hexdigest()


def conversion_state(game: str) -> dict[str, str]:
    if game == "minimal_decay":
        reason = (
            "Selected as the smallest complete-game control; faithful Prime "
            "conversion and a source-derived oracle have not landed yet."
        )
    elif game == "minimal_even":
        reason = (
            "Selected as the first typed-arithmetic game; conversion awaits "
            "ordinary Prime natural-number and parity relations."
        )
    elif game == "scissors_paper_stone":
        reason = (
            "Selected as the first complete two-agent interaction game; "
            "conversion awaits proof-relevant win, draw, and loss joins."
        )
    elif game == "buttons_and_lights":
        reason = (
            "Selected as the first complete finite-state negation game; "
            "conversion awaits explicit episode-scoped absence evidence."
        )
    elif game == "multiplebuttonsandlights":
        reason = (
            "Selected as the indexed finite-state follow-on; conversion "
            "awaits state- and action-view absence evidence."
        )
    elif game == "tron":
        reason = (
            "Selected as the first complete finite-board game; conversion "
            "awaits indexed occupancy, constructive emptiness, and dead-player "
            "evidence."
        )
    elif game == "untwisty_corridor":
        reason = (
            "Selected as the first sequential finite-control game; conversion "
            "awaits proof-relevant action-versus-persistence paths."
        )
    else:
        reason = (
            "Source is pinned and accounted for outside the current "
            "conversion slice; it is not silently excluded."
        )
    return {"status": "pending", "coverage": "none", "reason": reason}


def observed_manifest(snapshot_root: Path) -> dict[str, Any]:
    archive_path = snapshot_root / "data.zip"
    if not archive_path.is_file():
        raise ManifestError(f"{archive_path}: data archive is missing")
    archive_sha256 = sha256_file(archive_path)
    if archive_sha256 != ARCHIVE_SHA256:
        raise ManifestError("IGGP data archive SHA-256 changed")

    sources: dict[tuple[str, str], dict[str, tuple[str, bytes]]] = {}
    parsed: dict[tuple[str, str, str], dict[str, Any]] = {}
    duplicate_atoms: dict[str, int] = {}
    ignored_regular: list[str] = []
    with ZipFile(archive_path) as archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        if len(names) != len(set(names)):
            raise ManifestError("IGGP data archive has duplicate entry names")
        files = [info for info in infos if not info.is_dir()]
        sidecar = [
            info for info in infos if info.filename.startswith("__MACOSX/")
        ]
        task_files = 0
        for info in files:
            if info.filename.startswith("__MACOSX/"):
                continue
            identity = identify_task_entry(info.filename)
            if identity is None:
                ignored_regular.append(info.filename)
                continue
            split, game, target = identity
            if game not in GAMES:
                raise ManifestError(
                    f"{info.filename}: unexpected game {game}"
                )
            source = archive.read(info)
            detail = parse_task_data(source, info.filename)
            for atom in detail["atoms"]:
                head = atom.split("(", 1)[0]
                if head != target and not head.startswith(f"{target}_"):
                    raise ManifestError(
                        f"{info.filename}: atom {head} is outside target family {target}"
                    )
            duplicates = (
                detail["metrics"]["atom_occurrences"]
                - detail["metrics"]["unique_atoms"]
            )
            if duplicates:
                duplicate_atoms[info.filename] = duplicates
            key = (game, target)
            if split in sources.setdefault(key, {}):
                raise ManifestError(
                    f"{game}/{target}: duplicate {split} source"
                )
            sources[key][split] = (info.filename, source)
            parsed[(game, target, split)] = detail
            task_files += 1

    if sorted(ignored_regular) != sorted(IGNORED_REGULAR_ENTRIES):
        raise ManifestError("unexpected regular non-task archive inventory")
    if duplicate_atoms != EXPECTED_DUPLICATE_ATOMS:
        raise ManifestError("duplicate atom occurrence inventory drift")

    task_keys = tuple((game, target) for game in GAMES for target in TARGETS)
    if set(sources) != set(task_keys):
        raise ManifestError("archive is not the exact 50-game/200-task corpus")
    for key in task_keys:
        if set(sources[key]) != set(SPLITS):
            raise ManifestError(f"{key[0]}/{key[1]}: split inventory drift")

    split_specific: list[str] = []
    for game, target in task_keys:
        atoms = {
            parsed[(game, target, split)]["atoms"] for split in SPLITS
        }
        statics = {
            parsed[(game, target, split)]["statics"] for split in SPLITS
        }
        if len(atoms) != 1:
            split_specific.append(f"{game}/{target}")
        if len(statics) != 1:
            raise ManifestError(f"{game}/{target}: statics differ across splits")
    if tuple(split_specific) != EXPECTED_SPLIT_SPECIFIC_ATOMS:
        raise ManifestError("split-specific atom inventory drift")

    tasks: list[dict[str, Any]] = []
    for game, target in task_keys:
        type_path = f"types/{game}.typ"
        type_file = snapshot_root / type_path
        if not type_file.is_file():
            raise ManifestError(f"{type_path}: type signature is missing")
        type_source = type_file.read_bytes()
        if target not in parse_type_declarations(type_source, type_path):
            raise ManifestError(
                f"{game}/{target}: target family lacks a type declaration"
            )
        split_entries: dict[str, Any] = {}
        for split in SPLITS:
            path, source = sources[(game, target)][split]
            split_entries[split] = {
                "path": path,
                "sha256": sha256_bytes(source),
                **parsed[(game, target, split)]["metrics"],
            }
        tasks.append(
            {
                "game": game,
                "target": target,
                "type_source": {
                    "path": type_path,
                    "sha256": sha256_bytes(type_source),
                },
                "splits": split_entries,
                "source_sha256": task_source_digest(
                    type_path, type_source, sources[(game, target)]
                ),
                "conversion": conversion_state(game),
            }
        )

    return {
        "schema": SCHEMA,
        "source": {
            "project": "andrewcropper/mlj19-iggp",
            "revision": REVISION,
            "archive": "data.zip",
            "archive_sha256": archive_sha256,
            "archive_entries": 1215,
            "archive_files": 1206,
            "sidecar_entries": len(sidecar),
            "task_files": task_files,
        },
        "corpus": {
            "games": list(GAMES),
            "targets": list(TARGETS),
            "splits": list(SPLITS),
            "task_count": len(task_keys),
        },
        "source_anomalies": {
            "ignored_regular_entries": list(IGNORED_REGULAR_ENTRIES),
            "duplicate_atom_occurrences": EXPECTED_DUPLICATE_ATOMS,
            "split_specific_atom_inventories": list(
                EXPECTED_SPLIT_SPECIFIC_ATOMS
            ),
        },
        "planned_slice": [
            {
                "game": "minimal_decay",
                "reason": (
                    "Smallest complete game by total task-data bytes; exercises "
                    "all four target families as a control."
                ),
            },
            {
                "game": "minimal_even",
                "reason": (
                    "Smallest complete typed-arithmetic game; exercises even, "
                    "num, succ, empty positives, and all four target families."
                ),
            },
            {
                "game": "scissors_paper_stone",
                "reason": (
                    "First complete two-agent relational game; exercises "
                    "shared-action, opponent, outcome, and score-transition "
                    "witnesses across all four target families."
                ),
            },
            {
                "game": "buttons_and_lights",
                "reason": (
                    "First complete finite-state negation game; exercises "
                    "constructive absence evidence, transition persistence, "
                    "and multiple proof routes."
                ),
            },
            {
                "game": "multiplebuttonsandlights",
                "reason": (
                    "Second complete finite-state negation game; exercises "
                    "indexed cell views, action absence, persistence, and "
                    "proof-relevant conjunction."
                ),
            },
            {
                "game": "tron",
                "reason": (
                    "First complete finite-board game; exercises indexed "
                    "occupancy, constructive emptiness, movement, persistence, "
                    "dead-player evidence, and multiplicity."
                ),
            },
            {
                "game": "untwisty_corridor",
                "reason": (
                    "First complete sequential finite-control game; exercises "
                    "constructive proposition absence, corridor progression, "
                    "and distinct action-versus-persistence derivations."
                ),
            },
        ],
        "tasks": tasks,
    }


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read manifest: {exc}") from exc
    if not isinstance(data, dict):
        raise ManifestError("manifest root must be an object")
    return data


def validate_split(game: str, target: str, split: str, data: Any) -> None:
    if not isinstance(data, dict):
        raise ManifestError(f"{game}/{target}/{split}: split must be an object")
    expected_path = f"data/{split}/{game}_{target}_{split}.dat"
    if data.get("path") != expected_path:
        raise ManifestError(f"{game}/{target}/{split}: source path drift")
    if not is_sha256(data.get("sha256")):
        raise ManifestError(f"{game}/{target}/{split}: invalid SHA-256")
    metrics = (
        "bytes",
        "atom_occurrences",
        "unique_atoms",
        "static_occurrences",
        "state_pairs",
        "background_occurrences",
        "positive_occurrences",
        "empty_positive_states",
    )
    for metric in metrics:
        value = data.get(metric)
        if not isinstance(value, int) or value < 0:
            raise ManifestError(
                f"{game}/{target}/{split}: invalid {metric}"
            )
    if data["bytes"] == 0 or data["atom_occurrences"] == 0:
        raise ManifestError(f"{game}/{target}/{split}: empty task source")
    if data["unique_atoms"] > data["atom_occurrences"]:
        raise ManifestError(
            f"{game}/{target}/{split}: impossible atom counts"
        )
    if data["empty_positive_states"] > data["state_pairs"]:
        raise ManifestError(
            f"{game}/{target}/{split}: impossible empty-state count"
        )


def validate_manifest(data: dict[str, Any], repo: Path) -> tuple[int, int]:
    if data.get("schema") != SCHEMA:
        raise ManifestError("unexpected manifest schema")
    source = data.get("source")
    if (
        not isinstance(source, dict)
        or source.get("revision") != REVISION
        or source.get("archive_sha256") != ARCHIVE_SHA256
        or source.get("archive_entries") != 1215
        or source.get("archive_files") != 1206
        or source.get("sidecar_entries") != 610
        or source.get("task_files") != 600
    ):
        raise ManifestError("manifest does not pin the expected source archive")
    corpus = data.get("corpus")
    if (
        not isinstance(corpus, dict)
        or tuple(corpus.get("games", ())) != GAMES
        or tuple(corpus.get("targets", ())) != TARGETS
        or tuple(corpus.get("splits", ())) != SPLITS
        or corpus.get("task_count") != 200
    ):
        raise ManifestError("manifest is not the exact 50-game/200-task corpus")
    anomalies = data.get("source_anomalies")
    if (
        not isinstance(anomalies, dict)
        or tuple(anomalies.get("ignored_regular_entries", ()))
        != IGNORED_REGULAR_ENTRIES
        or anomalies.get("duplicate_atom_occurrences")
        != EXPECTED_DUPLICATE_ATOMS
        or tuple(anomalies.get("split_specific_atom_inventories", ()))
        != EXPECTED_SPLIT_SPECIFIC_ATOMS
    ):
        raise ManifestError("source anomaly inventory drift")
    planned = data.get("planned_slice")
    if (
        not isinstance(planned, list)
        or tuple(
            item.get("game") for item in planned if isinstance(item, dict)
        )
        != SELECTED_GAMES
        or any(
            not isinstance(item.get("reason"), str)
            or not item["reason"].strip()
            for item in planned
            if isinstance(item, dict)
        )
    ):
        raise ManifestError("planned slice is not the exact selected-game sequence")

    entries = data.get("tasks")
    if not isinstance(entries, list):
        raise ManifestError("tasks must be an array")
    expected_keys = tuple(
        (game, target) for game in GAMES for target in TARGETS
    )
    observed_keys = tuple(
        (entry.get("game"), entry.get("target"))
        for entry in entries
        if isinstance(entry, dict)
    )
    if observed_keys != expected_keys:
        raise ManifestError("tasks must be the exact ordered 200-task corpus")

    qualified = 0
    for entry in entries:
        game = entry["game"]
        target = entry["target"]
        type_source = entry.get("type_source")
        if (
            not isinstance(type_source, dict)
            or type_source.get("path") != f"types/{game}.typ"
            or not is_sha256(type_source.get("sha256"))
        ):
            raise ManifestError(f"{game}/{target}: invalid type source")
        splits = entry.get("splits")
        if not isinstance(splits, dict) or tuple(splits) != SPLITS:
            raise ManifestError(f"{game}/{target}: split inventory drift")
        for split in SPLITS:
            validate_split(game, target, split, splits[split])
        if not is_sha256(entry.get("source_sha256")):
            raise ManifestError(f"{game}/{target}: invalid task SHA-256")

        conversion = entry.get("conversion")
        if not isinstance(conversion, dict):
            raise ManifestError(f"{game}/{target}: missing conversion state")
        status = conversion.get("status")
        if status == "qualified":
            qualified += 1
            fixture_text = conversion.get("fixture")
            if (
                not isinstance(fixture_text, str)
                or Path(fixture_text).is_absolute()
                or not fixture_text.startswith("examples/prime/")
            ):
                raise ManifestError(
                    f"{game}/{target}: invalid qualification fixture"
                )
            fixture = repo / fixture_text
            if not fixture.is_file() or not fixture.with_suffix(
                ".expected"
            ).is_file():
                raise ManifestError(
                    f"{game}/{target}: qualification fixture or oracle is missing"
                )
            if conversion.get("coverage") in (None, "none"):
                raise ManifestError(
                    f"{game}/{target}: qualified without coverage"
                )
            rule_source = conversion.get("rule_source")
            if (
                not isinstance(rule_source, dict)
                or not isinstance(rule_source.get("path"), str)
                or Path(rule_source["path"]).is_absolute()
                or not is_sha256(rule_source.get("sha256"))
            ):
                raise ManifestError(
                    f"{game}/{target}: invalid qualified rule source"
                )
            if conversion.get("label_source") != {
                "archive": "data.zip",
                "sha256": ARCHIVE_SHA256,
            }:
                raise ManifestError(
                    f"{game}/{target}: invalid qualified label source"
                )
            cases = conversion.get("cases")
            if (
                not isinstance(cases, dict)
                or tuple(cases)
                != (
                    "atom_occurrences",
                    "derived",
                    "not_derived",
                    "proof_occurrences",
                )
                or any(
                    not isinstance(value, int) or value < 0
                    for value in cases.values()
                )
                or cases["derived"] + cases["not_derived"]
                != cases["atom_occurrences"]
                or cases["proof_occurrences"] < cases["derived"]
            ):
                raise ManifestError(
                    f"{game}/{target}: invalid qualification case counts"
                )
            if game == "minimal_decay":
                expected_cases = sum(
                    splits[split]["atom_occurrences"]
                    * splits[split]["state_pairs"]
                    for split in SPLITS
                )
                expected_derived = sum(
                    splits[split]["positive_occurrences"]
                    for split in SPLITS
                )
                if (
                    fixture_text != MINIMAL_DECAY_FIXTURE
                    or conversion.get("coverage")
                    != MINIMAL_DECAY_COVERAGE
                    or rule_source != MINIMAL_DECAY_RULE_SOURCE
                    or conversion.get("excluded_translation")
                    != MINIMAL_DECAY_EXCLUDED_TRANSLATION
                    or cases
                    != {
                        "atom_occurrences": expected_cases,
                        "derived": expected_derived,
                        "not_derived": expected_cases - expected_derived,
                        "proof_occurrences": expected_derived,
                    }
                ):
                    raise ManifestError(
                        f"{game}/{target}: minimal_decay qualification drift"
                    )
            elif game == "minimal_even":
                expected_cases = sum(
                    splits[split]["atom_occurrences"]
                    * splits[split]["state_pairs"]
                    for split in SPLITS
                )
                expected_derived = sum(
                    splits[split]["positive_occurrences"]
                    for split in SPLITS
                )
                if (
                    fixture_text != MINIMAL_EVEN_FIXTURE
                    or conversion.get("coverage")
                    != MINIMAL_EVEN_COVERAGE
                    or rule_source != MINIMAL_EVEN_RULE_SOURCE
                    or conversion.get("excluded_translation")
                    != MINIMAL_EVEN_EXCLUDED_TRANSLATION
                    or cases
                    != {
                        "atom_occurrences": expected_cases,
                        "derived": expected_derived,
                        "not_derived": expected_cases - expected_derived,
                        "proof_occurrences": (
                            MINIMAL_EVEN_PROOF_OCCURRENCES[target]
                        ),
                    }
                ):
                    raise ManifestError(
                        f"{game}/{target}: minimal_even qualification drift"
                    )
            elif game == "scissors_paper_stone":
                expected_cases = sum(
                    splits[split]["atom_occurrences"]
                    * splits[split]["state_pairs"]
                    for split in SPLITS
                )
                expected_derived = sum(
                    splits[split]["positive_occurrences"]
                    for split in SPLITS
                )
                if (
                    fixture_text != SPS_FIXTURE
                    or conversion.get("coverage") != SPS_COVERAGE
                    or rule_source != SPS_RULE_SOURCE
                    or conversion.get("excluded_translation")
                    != SPS_EXCLUDED_TRANSLATION
                    or cases
                    != {
                        "atom_occurrences": expected_cases,
                        "derived": expected_derived,
                        "not_derived": expected_cases - expected_derived,
                        "proof_occurrences": expected_derived,
                    }
                ):
                    raise ManifestError(
                        f"{game}/{target}: SPS qualification drift"
                    )
            elif game == "buttons_and_lights":
                expected_cases = sum(
                    splits[split]["atom_occurrences"]
                    * splits[split]["state_pairs"]
                    for split in SPLITS
                )
                expected_derived = sum(
                    splits[split]["positive_occurrences"]
                    for split in SPLITS
                )
                if (
                    fixture_text != BUTTONS_FIXTURE
                    or conversion.get("coverage") != BUTTONS_COVERAGE
                    or rule_source != BUTTONS_RULE_SOURCE
                    or conversion.get("excluded_translation")
                    != BUTTONS_EXCLUDED_TRANSLATION
                    or cases
                    != {
                        "atom_occurrences": expected_cases,
                        "derived": expected_derived,
                        "not_derived": expected_cases - expected_derived,
                        "proof_occurrences": (
                            BUTTONS_PROOF_OCCURRENCES[target]
                        ),
                    }
                ):
                    raise ManifestError(
                        f"{game}/{target}: buttons qualification drift"
                    )
            elif game == "multiplebuttonsandlights":
                expected_cases = sum(
                    splits[split]["atom_occurrences"]
                    * splits[split]["state_pairs"]
                    for split in SPLITS
                )
                expected_derived = sum(
                    splits[split]["positive_occurrences"]
                    for split in SPLITS
                )
                if (
                    fixture_text != MULTI_FIXTURE
                    or conversion.get("coverage") != MULTI_COVERAGE
                    or rule_source != MULTI_RULE_SOURCE
                    or "excluded_translation" in conversion
                    or cases
                    != {
                        "atom_occurrences": expected_cases,
                        "derived": expected_derived,
                        "not_derived": expected_cases - expected_derived,
                        "proof_occurrences": (
                            MULTI_PROOF_OCCURRENCES[target]
                        ),
                    }
                ):
                    raise ManifestError(
                        f"{game}/{target}: multiplebuttonsandlights "
                        "qualification drift"
                    )
            elif game == "tron":
                expected_cases = sum(
                    splits[split]["atom_occurrences"]
                    * splits[split]["state_pairs"]
                    for split in SPLITS
                )
                expected_derived = sum(
                    splits[split]["positive_occurrences"]
                    for split in SPLITS
                )
                if (
                    fixture_text != TRON_FIXTURE
                    or conversion.get("coverage") != TRON_COVERAGE
                    or rule_source != TRON_RULE_SOURCE
                    or conversion.get("excluded_translation")
                    != TRON_EXCLUDED_TRANSLATION
                    or cases
                    != {
                        "atom_occurrences": expected_cases,
                        "derived": expected_derived,
                        "not_derived": expected_cases - expected_derived,
                        "proof_occurrences": TRON_PROOF_OCCURRENCES[target],
                    }
                ):
                    raise ManifestError(
                        f"{game}/{target}: Tron qualification drift"
                    )
            elif game == "untwisty_corridor":
                expected_cases = sum(
                    splits[split]["atom_occurrences"]
                    * splits[split]["state_pairs"]
                    for split in SPLITS
                )
                expected_derived = sum(
                    splits[split]["positive_occurrences"]
                    for split in SPLITS
                )
                if (
                    fixture_text != CORRIDOR_FIXTURE
                    or conversion.get("coverage") != CORRIDOR_COVERAGE
                    or rule_source != CORRIDOR_RULE_SOURCE
                    or "excluded_translation" in conversion
                    or cases
                    != {
                        "atom_occurrences": expected_cases,
                        "derived": expected_derived,
                        "not_derived": expected_cases - expected_derived,
                        "proof_occurrences": (
                            CORRIDOR_PROOF_OCCURRENCES[target]
                        ),
                    }
                ):
                    raise ManifestError(
                        f"{game}/{target}: Untwisty Corridor qualification drift"
                    )
        elif status == "pending":
            if conversion.get("coverage") != "none":
                raise ManifestError(
                    f"{game}/{target}: pending task claims coverage"
                )
            reason = conversion.get("reason")
            if not isinstance(reason, str) or not reason.strip():
                raise ManifestError(
                    f"{game}/{target}: pending conversion has no reason"
                )
        else:
            raise ManifestError(f"{game}/{target}: unknown conversion status")
    return qualified, len(entries) - qualified


def source_projection(data: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema": data["schema"],
        "source": data["source"],
        "corpus": data["corpus"],
        "source_anomalies": data["source_anomalies"],
        "tasks": [
            {
                key: entry[key]
                for key in (
                    "game",
                    "target",
                    "type_source",
                    "splits",
                    "source_sha256",
                )
            }
            for entry in data["tasks"]
        ],
    }


def verify_snapshot(data: dict[str, Any], snapshot_root: Path) -> None:
    observed = observed_manifest(snapshot_root)
    if source_projection(observed) != source_projection(data):
        raise ManifestError("pinned IGGP task source projection drift")


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--manifest",
        type=Path,
        default=repo / "benchmarks/prime/ilp/iggp_manifest.json",
    )
    parser.add_argument(
        "--snapshot-root",
        type=Path,
        help="IGGP checkout root containing data.zip and types/",
    )
    parser.add_argument(
        "--emit-observed",
        action="store_true",
        help="emit a source-observed pending manifest as canonical JSON",
    )
    args = parser.parse_args()

    try:
        if args.emit_observed:
            if args.snapshot_root is None:
                raise ManifestError("--emit-observed requires --snapshot-root")
            print(
                json.dumps(
                    observed_manifest(args.snapshot_root),
                    indent=2,
                    ensure_ascii=False,
                )
            )
            return 0
        data = load_manifest(args.manifest)
        qualified, pending = validate_manifest(data, repo)
        if args.snapshot_root is not None:
            verify_snapshot(data, args.snapshot_root)
    except ManifestError as exc:
        print(f"FAIL: IGGP manifest: {exc}", file=sys.stderr)
        return 1

    source_suffix = (
        " and the pinned source snapshot"
        if args.snapshot_root is not None
        else ""
    )
    print(
        "PASS: IGGP manifest accounts for all 50 games / 200 tasks "
        f"({qualified} qualified, {pending} pending), 600 split files, "
        f"{sum(EXPECTED_DUPLICATE_ATOMS.values())} pinned duplicate atom "
        f"occurrences{source_suffix}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
