#!/usr/bin/env python3
"""Validate Prime's source-pinned Hopper Table-1 conversion ledger."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any


SCHEMA = "cetta-prime-hopper-table1-v1"
REVISION = "2283c3f947fc2eb81d5d931fb6f23f03b544a53b"
GROUPS = (
    (
        "learning-first-order",
        (
            "dropK",
            "allEven",
            "findDup",
            "length",
            "member",
            "sorted",
            "reverse",
        ),
    ),
    ("learning-higher-order", ("dropLast", "encryption")),
    (
        "additional",
        (
            "repeatN",
            "rotateN",
            "allSeqN",
            "dropLastK",
            "firstHalf",
            "lastHalf",
            "of1And2",
            "isPalindrome",
            "depth",
            "isBranch",
            "isSubTree",
            "addN",
            "mulFromSuc",
        ),
    ),
)
TASKS = tuple(task for _group, tasks in GROUPS for task in tasks)
GROUP_OF = {
    task: group for group, tasks in GROUPS for task in tasks
}
VARIANTS = ("fo", "ho", "ho-opt", "meta")
PROGRAM_VARIANTS = ("fo", "ho", "ho-opt")
PROGRAM_FILES = ("bias.pl", "bk.pl", "exs.pl")
META_FILES = ("HO.pl",)
EXPECTED_HEADER_COUNT_MISMATCHES = (
    ("findDup", "ho"),
    ("findDup", "ho-opt"),
    ("repeatN", "ho"),
    ("repeatN", "ho-opt"),
)
QUALIFICATION_SOURCE_VARIANT = "ho"
FIRST_ORDER_FIXTURE = (
    "examples/prime/hopper_table1_first_order_ground_truth.metta"
)
FIRST_ORDER_COVERAGE = (
    "authored-ho-best-program-and-all-ho-example-occurrences"
)
FIRST_ORDER_QUALIFIED_CASES = {
    "dropK": {
        "source_positive": 3,
        "source_negative": 2,
        "derived": 3,
        "not_derived": 2,
        "proof_occurrences": 3,
        "label_disagreements": 0,
    },
    "allEven": {
        "source_positive": 1,
        "source_negative": 1,
        "derived": 1,
        "not_derived": 1,
        "proof_occurrences": 1,
        "label_disagreements": 0,
    },
    "findDup": {
        "source_positive": 3,
        "source_negative": 2,
        "derived": 3,
        "not_derived": 2,
        "proof_occurrences": 3,
        "label_disagreements": 0,
    },
    "length": {
        "source_positive": 4,
        "source_negative": 3,
        "derived": 4,
        "not_derived": 3,
        "proof_occurrences": 4,
        "label_disagreements": 0,
    },
    "member": {
        "source_positive": 4,
        "source_negative": 3,
        "derived": 4,
        "not_derived": 3,
        "proof_occurrences": 4,
        "label_disagreements": 0,
    },
    "sorted": {
        "source_positive": 10,
        "source_negative": 12,
        "derived": 11,
        "not_derived": 11,
        "proof_occurrences": 11,
        "label_disagreements": 1,
    },
    "reverse": {
        "source_positive": 4,
        "source_negative": 5,
        "derived": 4,
        "not_derived": 5,
        "proof_occurrences": 4,
        "label_disagreements": 0,
    },
}
HIGHER_ORDER_FIXTURE = (
    "examples/prime/hopper_table1_higher_order_ground_truth.metta"
)
HIGHER_ORDER_COVERAGE = (
    "authored-ho-best-program-and-all-ho-example-occurrences-with-map-rel"
)
HIGHER_ORDER_QUALIFIED_CASES = {
    "dropLast": {
        "source_positive": 2,
        "source_negative": 2,
        "derived": 2,
        "not_derived": 2,
        "proof_occurrences": 2,
        "label_disagreements": 0,
    },
    "encryption": {
        "source_positive": 28,
        "source_negative": 13,
        "derived": 28,
        "not_derived": 13,
        "proof_occurrences": 28,
        "label_disagreements": 0,
    },
}
HIGHER_ORDER_VARIANT_COMPARISONS = {
    "dropLast": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-ho-opt-identical;fo-has-no-authored-best-program",
        "target": "ho-ho-opt-list;fo-dlist",
    },
    "encryption": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "fo-ho-ho-opt-identical",
        "target": "fo-ho-ho-opt-list",
    },
}
NATIVE_ENCRYPTION_LEARNING = {
    "status": "qualified",
    "coverage": "typed-path-learning-and-all-ho-examples-with-map-rel",
    "source_variant": "ho",
    "fixture": "examples/prime/hopper_encryption_native_learning.metta",
    "hypothesis_space": {
        "path_length": 4,
        "primitive_declarations": 3,
        "raw_candidates": 81,
        "typed_candidates": 2,
        "example_consistent": 1,
    },
    "cases": {
        "source_positive": 28,
        "source_negative": 13,
        "label_disagreements": 0,
    },
}
ITERATIVE_FIXTURE = (
    "examples/prime/hopper_table1_additional_iterative_ground_truth.metta"
)
ITERATIVE_COVERAGE = (
    "authored-ho-best-program-and-all-ho-example-occurrences-with-typed-iteration"
)
ITERATIVE_QUALIFIED_CASES = {
    "rotateN": {
        "source_positive": 6,
        "source_negative": 2,
        "derived": 6,
        "not_derived": 2,
        "proof_occurrences": 6,
        "label_disagreements": 0,
    },
    "dropLastK": {
        "source_positive": 5,
        "source_negative": 3,
        "derived": 5,
        "not_derived": 3,
        "proof_occurrences": 5,
        "label_disagreements": 0,
    },
    "addN": {
        "source_positive": 4,
        "source_negative": 3,
        "derived": 4,
        "not_derived": 3,
        "proof_occurrences": 4,
        "label_disagreements": 0,
    },
}
ITERATIVE_VARIANT_COMPARISONS = {
    "rotateN": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-ho-opt-identical;fo-has-no-authored-best-program",
        "target": "fo-ho-ho-opt-identical",
    },
    "dropLastK": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-ho-opt-identical;fo-has-no-authored-best-program",
        "target": "fo-ho-ho-opt-identical",
    },
    "addN": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-uses-eq;ho-opt-uses-eqs;fo-has-no-authored-best-program",
        "target": "fo-ho-ho-opt-identical",
    },
}
STRUCTURAL_LIST_FIXTURE = (
    "examples/prime/hopper_table1_structural_list_ground_truth.metta"
)
STRUCTURAL_LIST_COVERAGE = (
    "authored-ho-best-program-and-all-ho-example-occurrences-with-typed-structural-list-evidence"
)
STRUCTURAL_LIST_QUALIFIED_CASES = {
    "lastHalf": {
        "source_positive": 8,
        "source_negative": 6,
        "derived": 8,
        "not_derived": 6,
        "proof_occurrences": 8,
        "label_disagreements": 0,
    },
    "of1And2": {
        "source_positive": 4,
        "source_negative": 6,
        "derived": 4,
        "not_derived": 6,
        "proof_occurrences": 4,
        "label_disagreements": 0,
    },
    "isPalindrome": {
        "source_positive": 11,
        "source_negative": 2,
        "derived": 11,
        "not_derived": 2,
        "proof_occurrences": 11,
        "label_disagreements": 0,
    },
}
STRUCTURAL_LIST_VARIANT_COMPARISONS = {
    "lastHalf": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-ho-opt-identical;fo-authored-different",
        "target": "fo-ho-ho-opt-identical",
    },
    "of1And2": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-ho-opt-identical;fo-has-no-authored-best-program",
        "target": "fo-ho-ho-opt-identical",
    },
    "isPalindrome": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-ho-opt-identical;fo-has-no-authored-best-program",
        "target": "fo-ho-ho-opt-identical",
    },
}
RELATIONAL_RECURSION_FIXTURE = (
    "examples/prime/hopper_table1_relational_recursion_ground_truth.metta"
)
RELATIONAL_RECURSION_COVERAGE = (
    "authored-ho-best-program-and-all-ho-example-occurrences-with-proof-relevant-indexed-recursion"
)
RELATIONAL_RECURSION_QUALIFIED_CASES = {
    "repeatN": {
        "source_positive": 7,
        "source_negative": 2,
        "derived": 7,
        "not_derived": 2,
        "proof_occurrences": 7,
        "label_disagreements": 0,
    },
    "allSeqN": {
        "source_positive": 5,
        "source_negative": 3,
        "derived": 5,
        "not_derived": 3,
        "proof_occurrences": 5,
        "label_disagreements": 0,
    },
    "firstHalf": {
        "source_positive": 8,
        "source_negative": 2,
        "derived": 8,
        "not_derived": 2,
        "proof_occurrences": 8,
        "label_disagreements": 0,
    },
    "mulFromSuc": {
        "source_positive": 10,
        "source_negative": 6,
        "derived": 10,
        "not_derived": 6,
        "proof_occurrences": 10,
        "label_disagreements": 0,
    },
}
RELATIONAL_RECURSION_VARIANT_COMPARISONS = {
    "repeatN": {
        "examples": "fo-ho-ho-opt-semantic-identical;physical-source-format-differs",
        "program": "ho-ho-opt-identical;fo-has-no-authored-best-program",
        "target": "fo-ho-ho-opt-identical",
    },
    "allSeqN": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-ho-opt-identical;fo-authored-different",
        "target": "fo-ho-ho-opt-identical",
    },
    "firstHalf": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-ho-opt-identical;fo-has-no-authored-best-program",
        "target": "fo-ho-ho-opt-identical",
    },
    "mulFromSuc": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "fo-ho-ho-opt-identical",
        "target": "fo-ho-ho-opt-identical",
    },
}
TREE_RELATIONS_FIXTURE = (
    "examples/prime/hopper_table1_tree_relations_ground_truth.metta"
)
TREE_RELATIONS_COVERAGE = (
    "authored-ho-best-program-and-all-ho-example-occurrences-with-"
    "proof-relevant-tree-relations-over-checked-raw-boundary"
)
TREE_RELATIONS_QUALIFIED_CASES = {
    "depth": {
        "source_positive": 7,
        "source_negative": 4,
        "derived": 7,
        "not_derived": 4,
        "proof_occurrences": 7,
        "label_disagreements": 0,
    },
    "isBranch": {
        "source_positive": 42,
        "source_negative": 5,
        "derived": 42,
        "not_derived": 5,
        "proof_occurrences": 50,
        "label_disagreements": 0,
    },
    "isSubTree": {
        "source_positive": 7,
        "source_negative": 2,
        "derived": 7,
        "not_derived": 2,
        "proof_occurrences": 7,
        "label_disagreements": 0,
    },
}
TREE_RELATIONS_VARIANT_COMPARISONS = {
    "depth": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-ho-opt-identical;fo-has-no-authored-best-program",
        "target": "fo-ho-ho-opt-identical",
    },
    "isBranch": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-ho-opt-identical;fo-has-no-authored-best-program",
        "target": "fo-ho-ho-opt-identical",
    },
    "isSubTree": {
        "examples": "fo-ho-ho-opt-identical",
        "program": "ho-uses-eq;ho-opt-uses-eqs;fo-authored-different",
        "target": "fo-ho-ho-opt-identical",
    },
}
SORTED_SOURCE_DISAGREEMENT = {
    "source_variant": "ho",
    "source_polarity": "negative",
    "source_term": "f([0,0,0,0])",
    "derived_proof_occurrences": 1,
}
SORTED_SOURCE_ERRATUM = {
    "kind": "published-best-program-confusion-matrix-disagrees-with-examples",
    "reported": {"tp": 10, "fn": 0, "tn": 12, "fp": 0},
    "computed": {"tp": 10, "fn": 0, "tn": 11, "fp": 1},
}
HEX256 = re.compile(r"[0-9a-f]{64}\Z")
HEAD_RE = re.compile(
    r"^head_pred\(\s*(?P<name>[a-zA-Z0-9_]+)\s*,\s*"
    r"(?P<arity>[0-9]+)\s*\)\.$"
)
TUPLE_RE = re.compile(
    r"^(?P<kind>type|direction)\(\s*"
    r"(?P<name>[a-zA-Z0-9_]+)\s*,\s*"
    r"\((?P<items>.*)\)\s*\)\.$"
)
METRICS_RE = re.compile(
    r"^Precision:(?P<precision>[0-9.]+),\s*"
    r"Recall:(?P<recall>[0-9.]+),\s*"
    r"TP:(?P<tp>[0-9]+),\s*FN:(?P<fn>[0-9]+),\s*"
    r"TN:(?P<tn>[0-9]+),\s*FP:(?P<fp>[0-9]+)$"
)
HIGHER_ORDER_RE = re.compile(
    r"^higher_order_predicate\(\s*"
    r"(?P<name>[a-zA-Z0-9_]+)\s*,\s*"
    r"\[(?P<arities>[0-9, ]*)\]\s*,\s*"
    r"(?P<ordinary>[0-9]+)\s*,\s*"
    r"(?P<instances>[0-9]+)\s*\)\.$"
)


class ManifestError(RuntimeError):
    """The ledger or its pinned source does not satisfy its contract."""


def qualification_contract(task: str) -> dict[str, Any] | None:
    groups = (
        (
            "first-order",
            FIRST_ORDER_FIXTURE,
            FIRST_ORDER_COVERAGE,
            FIRST_ORDER_QUALIFIED_CASES,
            None,
        ),
        (
            "higher-order",
            HIGHER_ORDER_FIXTURE,
            HIGHER_ORDER_COVERAGE,
            HIGHER_ORDER_QUALIFIED_CASES,
            HIGHER_ORDER_VARIANT_COMPARISONS,
        ),
        (
            "iterative",
            ITERATIVE_FIXTURE,
            ITERATIVE_COVERAGE,
            ITERATIVE_QUALIFIED_CASES,
            ITERATIVE_VARIANT_COMPARISONS,
        ),
        (
            "structural-list",
            STRUCTURAL_LIST_FIXTURE,
            STRUCTURAL_LIST_COVERAGE,
            STRUCTURAL_LIST_QUALIFIED_CASES,
            STRUCTURAL_LIST_VARIANT_COMPARISONS,
        ),
        (
            "relational-recursion",
            RELATIONAL_RECURSION_FIXTURE,
            RELATIONAL_RECURSION_COVERAGE,
            RELATIONAL_RECURSION_QUALIFIED_CASES,
            RELATIONAL_RECURSION_VARIANT_COMPARISONS,
        ),
        (
            "tree-relations",
            TREE_RELATIONS_FIXTURE,
            TREE_RELATIONS_COVERAGE,
            TREE_RELATIONS_QUALIFIED_CASES,
            TREE_RELATIONS_VARIANT_COMPARISONS,
        ),
    )
    for label, fixture, coverage, cases, comparisons in groups:
        if task in cases:
            return {
                "label": label,
                "fixture": fixture,
                "coverage": coverage,
                "cases": cases[task],
                "variant_comparison": (
                    None if comparisons is None else comparisons[task]
                ),
            }
    return None


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def tuple_items(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def parse_best_program(lines: list[str]) -> dict[str, Any] | None:
    start: int | None = None
    label = ""
    for index, raw_line in enumerate(lines):
        stripped = raw_line.lstrip("%").strip()
        if stripped.startswith("BEST PROG"):
            start = index
            label = stripped
            break
    if start is None:
        return None

    clauses: list[str] = []
    metrics: dict[str, Any] | None = None
    for raw_line in lines[start + 1 :]:
        if not raw_line.lstrip().startswith("%"):
            break
        stripped = raw_line.lstrip().lstrip("%").strip()
        metric_match = METRICS_RE.fullmatch(stripped)
        if metric_match:
            metrics = {
                "precision": metric_match.group("precision"),
                "recall": metric_match.group("recall"),
                "tp": int(metric_match.group("tp")),
                "fn": int(metric_match.group("fn")),
                "tn": int(metric_match.group("tn")),
                "fp": int(metric_match.group("fp")),
            }
            break
        if stripped and ":-" in stripped and stripped.endswith("."):
            clauses.append(stripped)

    if not clauses:
        raise ManifestError("malformed BEST PROG header")
    return {"label": label, "clauses": clauses, "metrics": metrics}


def parse_target(path: Path) -> dict[str, Any]:
    heads: dict[str, int] = {}
    types: dict[str, list[str]] = {}
    directions: dict[str, list[str]] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        head = HEAD_RE.fullmatch(line)
        if head:
            heads[head.group("name")] = int(head.group("arity"))
        tuple_declaration = TUPLE_RE.fullmatch(line)
        if tuple_declaration:
            destination = (
                types
                if tuple_declaration.group("kind") == "type"
                else directions
            )
            destination[tuple_declaration.group("name")] = tuple_items(
                tuple_declaration.group("items")
            )

    if len(heads) != 1:
        raise ManifestError(
            f"{path}: expected one head_pred declaration, found {heads}"
        )
    predicate, arity = next(iter(heads.items()))
    if predicate not in types or predicate not in directions:
        raise ManifestError(
            f"{path}: target {predicate} lacks type or direction"
        )
    if len(types[predicate]) != arity:
        raise ManifestError(f"{path}: target type arity does not agree")
    if len(directions[predicate]) != arity:
        raise ManifestError(
            f"{path}: target direction arity does not agree"
        )
    return {
        "predicate": predicate,
        "arity": arity,
        "types": types[predicate],
        "directions": directions[predicate],
    }


def parse_examples(path: Path) -> dict[str, int]:
    source = path.read_text(encoding="utf-8")
    return {
        "positive": len(re.findall(r"^\s*pos\(", source, re.MULTILINE)),
        "negative": len(re.findall(r"^\s*neg\(", source, re.MULTILINE)),
    }


def parse_higher_order(path: Path) -> list[dict[str, Any]]:
    declarations: list[dict[str, Any]] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        match = HIGHER_ORDER_RE.fullmatch(line)
        if match:
            declarations.append(
                {
                    "name": match.group("name"),
                    "argument_arities": [
                        int(item)
                        for item in tuple_items(match.group("arities"))
                    ],
                    "ordinary_arguments": int(match.group("ordinary")),
                    "instances": int(match.group("instances")),
                }
            )
    return declarations


def source_files(path: Path, names: tuple[str, ...]) -> dict[str, str]:
    files: dict[str, str] = {}
    for name in names:
        source = path / name
        if not source.is_file():
            raise ManifestError(f"{path}: missing {name}")
        files[name] = sha256_file(source)
    return files


def program_variant(path: Path) -> dict[str, Any]:
    lines = (path / "bias.pl").read_text(encoding="utf-8").splitlines()
    return {
        "path": path.name,
        "files": source_files(path, PROGRAM_FILES),
        "target": parse_target(path / "bias.pl"),
        "examples": parse_examples(path / "exs.pl"),
        "higher_order": parse_higher_order(path / "bk.pl"),
        "best_program": parse_best_program(lines),
    }


def meta_variant(path: Path) -> dict[str, Any]:
    return {
        "path": path.name,
        "files": source_files(path, META_FILES),
    }


def locate_ho_opt(task_root: Path) -> Path:
    candidates = [
        child
        for child in task_root.iterdir()
        if child.is_dir() and " ".join(child.name.split()) == "HO OPT"
    ]
    if len(candidates) != 1:
        raise ManifestError(
            f"{task_root}: expected exactly one logical HO OPT directory"
        )
    return candidates[0]


def snapshot_entry(snapshot_root: Path, task: str) -> dict[str, Any]:
    task_root = snapshot_root / "examples" / task
    if not task_root.is_dir():
        raise ManifestError(f"{task}: source task is missing")
    return {
        "name": task,
        "group": GROUP_OF[task],
        "variants": {
            "fo": program_variant(task_root / "FO"),
            "ho": program_variant(task_root / "HO"),
            "ho-opt": program_variant(locate_ho_opt(task_root)),
            "meta": meta_variant(task_root / "Meta"),
        },
    }


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read manifest: {exc}") from exc
    if not isinstance(data, dict):
        raise ManifestError("manifest root must be an object")
    return data


def validate_digest_map(
    task: str, variant: str, files: Any, expected: tuple[str, ...]
) -> None:
    if not isinstance(files, dict) or tuple(files) != expected:
        raise ManifestError(
            f"{task}/{variant}: unexpected source-file inventory"
        )
    for name, digest in files.items():
        if not isinstance(digest, str) or not HEX256.fullmatch(digest):
            raise ManifestError(
                f"{task}/{variant}/{name}: invalid SHA-256"
            )


def validate_program_variant(
    task: str, variant: str, data: Any
) -> None:
    if not isinstance(data, dict):
        raise ManifestError(f"{task}/{variant}: variant must be an object")
    path = data.get("path")
    if (
        not isinstance(path, str)
        or not path
        or Path(path).is_absolute()
        or len(Path(path).parts) != 1
    ):
        raise ManifestError(f"{task}/{variant}: invalid variant path")
    validate_digest_map(task, variant, data.get("files"), PROGRAM_FILES)
    target = data.get("target")
    if not isinstance(target, dict):
        raise ManifestError(f"{task}/{variant}: missing target")
    arity = target.get("arity")
    if not isinstance(arity, int) or arity < 1:
        raise ManifestError(f"{task}/{variant}: invalid target arity")
    if len(target.get("types", [])) != arity:
        raise ManifestError(f"{task}/{variant}: target types mismatch")
    if len(target.get("directions", [])) != arity:
        raise ManifestError(f"{task}/{variant}: target directions mismatch")
    examples = data.get("examples")
    if (
        not isinstance(examples, dict)
        or not isinstance(examples.get("positive"), int)
        or not isinstance(examples.get("negative"), int)
        or examples["positive"] < 1
        or examples["negative"] < 1
    ):
        raise ManifestError(f"{task}/{variant}: invalid example counts")
    if not isinstance(data.get("higher_order"), list):
        raise ManifestError(
            f"{task}/{variant}: higher-order inventory must be an array"
        )
    best = data.get("best_program")
    if variant in ("ho", "ho-opt") and not isinstance(best, dict):
        raise ManifestError(
            f"{task}/{variant}: missing authored best-program header"
        )
    if best is not None:
        if (
            not isinstance(best, dict)
            or not isinstance(best.get("label"), str)
            or not isinstance(best.get("clauses"), list)
            or not best["clauses"]
        ):
            raise ManifestError(
                f"{task}/{variant}: malformed best-program record"
            )
        metrics = best.get("metrics")
        if variant in ("ho", "ho-opt") and not isinstance(metrics, dict):
            raise ManifestError(
                f"{task}/{variant}: best program lacks evaluation metrics"
            )
        if metrics is not None and not isinstance(metrics, dict):
            raise ManifestError(
                f"{task}/{variant}: malformed best-program metrics"
            )


def validate_manifest(
    data: dict[str, Any], repo: Path
) -> tuple[int, int]:
    if data.get("schema") != SCHEMA:
        raise ManifestError("unexpected manifest schema")
    source = data.get("source")
    if not isinstance(source, dict) or source.get("revision") != REVISION:
        raise ManifestError("manifest does not pin the expected revision")
    entries = data.get("tasks")
    if not isinstance(entries, list):
        raise ManifestError("tasks must be an array")
    names = [
        entry.get("name") for entry in entries if isinstance(entry, dict)
    ]
    if tuple(names) != TASKS:
        raise ManifestError(
            "tasks must be the exact ordered 22-task Table-1 corpus"
        )

    qualified = 0
    header_count_mismatches: list[tuple[str, str]] = []
    for entry in entries:
        task = entry["name"]
        if entry.get("group") != GROUP_OF[task]:
            raise ManifestError(f"{task}: incorrect Table-1 group")
        variants = entry.get("variants")
        if not isinstance(variants, dict) or tuple(variants) != VARIANTS:
            raise ManifestError(f"{task}: expected all four source variants")
        for variant in PROGRAM_VARIANTS:
            validate_program_variant(task, variant, variants[variant])
            best = variants[variant].get("best_program")
            if best is not None and best.get("metrics") is not None:
                metrics = best["metrics"]
                examples = variants[variant]["examples"]
                if (
                    metrics["tp"] + metrics["fn"]
                    != examples["positive"]
                    or metrics["tn"] + metrics["fp"]
                    != examples["negative"]
                ):
                    header_count_mismatches.append((task, variant))
        meta = variants["meta"]
        if not isinstance(meta, dict):
            raise ManifestError(f"{task}/meta: variant must be an object")
        path = meta.get("path")
        if (
            not isinstance(path, str)
            or Path(path).is_absolute()
            or len(Path(path).parts) != 1
        ):
            raise ManifestError(f"{task}/meta: invalid variant path")
        validate_digest_map(task, "meta", meta.get("files"), META_FILES)

        conversion = entry.get("conversion")
        if not isinstance(conversion, dict):
            raise ManifestError(f"{task}: missing conversion state")
        status = conversion.get("status")
        if status in {
            "qualified",
            "qualified-with-recorded-source-erratum",
        }:
            qualified += 1
            fixture_text = conversion.get("fixture")
            if (
                not isinstance(fixture_text, str)
                or Path(fixture_text).is_absolute()
                or not fixture_text.startswith("examples/prime/")
            ):
                raise ManifestError(f"{task}: invalid qualification fixture")
            fixture = repo / fixture_text
            if not fixture.is_file() or not fixture.with_suffix(
                ".expected"
            ).is_file():
                raise ManifestError(
                    f"{task}: qualification fixture or oracle is missing"
                )
            if conversion.get("coverage") in (None, "none"):
                raise ManifestError(f"{task}: qualified without coverage")
            contract = qualification_contract(task)
            if contract is not None:
                label = contract["label"]
                if conversion.get("fixture") != contract["fixture"]:
                    raise ManifestError(
                        f"{task}: {label} qualification fixture drift"
                    )
                if conversion.get("coverage") != contract["coverage"]:
                    raise ManifestError(
                        f"{task}: {label} qualification coverage drift"
                    )
                if (
                    conversion.get("source_variant")
                    != QUALIFICATION_SOURCE_VARIANT
                ):
                    raise ManifestError(
                        f"{task}: {label} source variant drift"
                    )
                expected_cases = contract["cases"]
                if conversion.get("cases") != expected_cases:
                    raise ManifestError(
                        f"{task}: {label} qualification counts drift"
                    )
                comparison = contract["variant_comparison"]
                if (
                    comparison is not None
                    and conversion.get("variant_comparison") != comparison
                ):
                    raise ManifestError(
                        f"{task}: source variant comparison drift"
                    )
                source_examples = variants[
                    QUALIFICATION_SOURCE_VARIANT
                ]["examples"]
                if (
                    expected_cases["source_positive"]
                    != source_examples["positive"]
                    or expected_cases["source_negative"]
                    != source_examples["negative"]
                ):
                    raise ManifestError(
                        f"{task}: qualification counts do not cover source"
                    )
            if task == "encryption":
                native_learning = conversion.get("native_learning")
                if native_learning != NATIVE_ENCRYPTION_LEARNING:
                    raise ManifestError(
                        "encryption: native learning contract drift"
                    )
                native_fixture = repo / native_learning["fixture"]
                if not native_fixture.is_file() or not native_fixture.with_suffix(
                    ".expected"
                ).is_file():
                    raise ManifestError(
                        "encryption: native learning fixture or oracle is missing"
                    )
            if task == "sorted":
                if status != "qualified-with-recorded-source-erratum":
                    raise ManifestError(
                        "sorted: source erratum must remain explicit in status"
                    )
                if conversion.get("source_erratum") != SORTED_SOURCE_ERRATUM:
                    raise ManifestError(
                        "sorted: source erratum inventory drift"
                    )
        elif status == "pending":
            reason = conversion.get("reason")
            if conversion.get("coverage") != "none":
                raise ManifestError(f"{task}: pending task claims coverage")
            if not isinstance(reason, str) or not reason.strip():
                raise ManifestError(
                    f"{task}: pending conversion has no reason"
                )
        else:
            raise ManifestError(f"{task}: unknown conversion status")
        if qualification_contract(task) is not None and status not in {
            "qualified",
            "qualified-with-recorded-source-erratum",
        }:
            raise ManifestError(
                f"{task}: qualified conversion was removed"
            )
        if task == "sorted":
            if (
                conversion.get("source_disagreement")
                != SORTED_SOURCE_DISAGREEMENT
            ):
                raise ManifestError(
                    "sorted: source disagreement inventory drift"
                )
    if tuple(header_count_mismatches) != EXPECTED_HEADER_COUNT_MISMATCHES:
        raise ManifestError(
            "BEST PROG header/example-count anomaly inventory drift"
        )
    return qualified, len(entries) - qualified


def verify_snapshot(data: dict[str, Any], snapshot_root: Path) -> None:
    for manifest_entry in data["tasks"]:
        observed = snapshot_entry(snapshot_root, manifest_entry["name"])
        for key in ("name", "group", "variants"):
            if observed[key] != manifest_entry[key]:
                raise ManifestError(
                    f"{manifest_entry['name']}: {key} source drift"
                )


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--manifest",
        type=Path,
        default=repo / "benchmarks/prime/ilp/hopper_table1_manifest.json",
    )
    parser.add_argument(
        "--snapshot-root",
        type=Path,
        help="Hopper checkout root containing examples/<task>/<variant>",
    )
    args = parser.parse_args()

    try:
        data = load_manifest(args.manifest)
        qualified, pending = validate_manifest(data, repo)
        if args.snapshot_root is not None:
            verify_snapshot(data, args.snapshot_root)
    except ManifestError as exc:
        print(f"FAIL: Hopper Table-1 manifest: {exc}", file=sys.stderr)
        return 1

    source_suffix = (
        " and the pinned source snapshot"
        if args.snapshot_root is not None
        else ""
    )
    print(
        "PASS: Hopper Table-1 manifest accounts for all "
        f"{qualified + pending} tasks ({qualified} qualified, "
        f"{pending} pending), four variants each, "
        f"{len(EXPECTED_HEADER_COUNT_MISMATCHES)} pinned "
        f"header/example count mismatches{source_suffix}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
