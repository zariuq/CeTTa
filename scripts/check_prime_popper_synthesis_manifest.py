#!/usr/bin/env python3
"""Validate Prime's source-pinned Popper synthesis conversion ledger."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any


SCHEMA = "cetta-prime-popper-synthesis-v1"
REVISION = "5c608d3ec87e4975866042fe326e9a317277e021"
TASKS = (
    "synthesis-alleven",
    "synthesis-contains",
    "synthesis-dropk",
    "synthesis-droplast",
    "synthesis-filter",
    "synthesis-finddupl",
    "synthesis-length",
    "synthesis-next",
    "synthesis-reverse",
    "synthesis-sorted",
)
SOURCE_FILES = ("bias.pl", "bk.pl", "exs.pl")
RECURSIVE_ARITHMETIC_FIXTURE = (
    "examples/prime/popper_synthesis_recursive_arithmetic_ground_truth.metta"
)
RECURSIVE_ARITHMETIC_COVERAGE = (
    "authored-reference-program-and-all-source-examples-with-structural-"
    "unbounded-parity-and-proof-relevant-decrement-recursion"
)
RECURSIVE_ARITHMETIC_CASES = {
    "synthesis-alleven": {
        "source_positive": 4,
        "source_negative": 5,
        "derived": 4,
        "not_derived": 5,
        "proof_occurrences": 4,
        "label_disagreements": 0,
    },
    "synthesis-dropk": {
        "source_positive": 10,
        "source_negative": 10,
        "derived": 10,
        "not_derived": 10,
        "proof_occurrences": 10,
        "label_disagreements": 0,
    },
}
SORTED_FIXTURE = "examples/prime/popper_synthesis_sorted_ground_truth.metta"
SORTED_COVERAGE = (
    "authored-reference-program-and-all-source-examples-over-the-exact-"
    "reachable-image-of-the-extensional-geq-table"
)
SORTED_CASES = {
    "source_positive": 10,
    "source_negative": 10,
    "derived": 10,
    "not_derived": 10,
    "proof_occurrences": 10,
    "label_disagreements": 0,
}
HEX256 = re.compile(r"[0-9a-f]{64}\Z")
PREDICATE_RE = re.compile(
    r"^(?P<kind>head_pred|body_pred)\("
    r"(?P<name>[a-zA-Z0-9_]+),(?P<arity>[0-9]+)\)\.$"
)
TUPLE_RE = re.compile(
    r"^(?P<kind>type|direction)\("
    r"(?P<name>[a-zA-Z0-9_]+),\((?P<items>.*)\)\)\.$"
)


class ManifestError(RuntimeError):
    """The ledger or its pinned source does not satisfy its contract."""


def qualification_contract(name: str) -> dict[str, Any] | None:
    if name in RECURSIVE_ARITHMETIC_CASES:
        return {
            "label": "recursive-arithmetic",
            "fixture": RECURSIVE_ARITHMETIC_FIXTURE,
            "coverage": RECURSIVE_ARITHMETIC_COVERAGE,
            "cases": RECURSIVE_ARITHMETIC_CASES[name],
        }
    if name == "synthesis-sorted":
        return {
            "label": "sorted-extensional-geq",
            "fixture": SORTED_FIXTURE,
            "coverage": SORTED_COVERAGE,
            "cases": SORTED_CASES,
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


def parse_bias(path: Path) -> dict[str, Any]:
    heads: dict[str, int] = {}
    types: dict[str, list[str]] = {}
    directions: dict[str, list[str]] = {}
    recursive = False
    reference_clauses: list[str] = []

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if line == "enable_recursion.":
            recursive = True
        if line.startswith("%%") and ":-" in line:
            reference_clauses.append(line[2:].strip())
        predicate = PREDICATE_RE.fullmatch(line)
        if predicate and predicate.group("kind") == "head_pred":
            heads[predicate.group("name")] = int(
                predicate.group("arity")
            )
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
        "recursive": recursive,
        "target": {
            "predicate": predicate,
            "arity": arity,
            "types": types[predicate],
            "directions": directions[predicate],
        },
        "reference_clauses": reference_clauses,
    }


def parse_examples(path: Path) -> dict[str, int]:
    source = path.read_text(encoding="utf-8")
    return {
        "positive": len(re.findall(r"^\s*pos\(", source, re.MULTILINE)),
        "negative": len(re.findall(r"^\s*neg\(", source, re.MULTILINE)),
    }


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read manifest: {exc}") from exc
    if not isinstance(data, dict):
        raise ManifestError("manifest root must be an object")
    return data


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
    names = [entry.get("name") for entry in entries
             if isinstance(entry, dict)]
    if tuple(names) != TASKS:
        raise ManifestError(
            "tasks must be the exact ordered ten-task synthesis corpus"
        )

    qualified = 0
    for entry in entries:
        name = entry["name"]
        files = entry.get("files")
        if not isinstance(files, dict) or tuple(files) != SOURCE_FILES:
            raise ManifestError(f"{name}: expected all three source files")
        for source_name, digest in files.items():
            if not isinstance(digest, str) or not HEX256.fullmatch(digest):
                raise ManifestError(
                    f"{name}/{source_name}: invalid SHA-256"
                )

        target = entry.get("target")
        if not isinstance(target, dict):
            raise ManifestError(f"{name}: missing target contract")
        arity = target.get("arity")
        if not isinstance(arity, int) or arity < 1:
            raise ManifestError(f"{name}: invalid target arity")
        if len(target.get("types", [])) != arity:
            raise ManifestError(f"{name}: target types do not match arity")
        if len(target.get("directions", [])) != arity:
            raise ManifestError(
                f"{name}: target directions do not match arity"
            )
        examples = entry.get("examples")
        if (
            not isinstance(examples, dict)
            or not isinstance(examples.get("positive"), int)
            or not isinstance(examples.get("negative"), int)
            or examples["positive"] < 1
            or examples["negative"] < 1
        ):
            raise ManifestError(f"{name}: invalid example counts")

        conversion = entry.get("conversion")
        if not isinstance(conversion, dict):
            raise ManifestError(f"{name}: missing conversion state")
        status = conversion.get("status")
        if status == "qualified":
            qualified += 1
            fixture_text = conversion.get("fixture")
            if (
                not isinstance(fixture_text, str)
                or Path(fixture_text).is_absolute()
                or not fixture_text.startswith("examples/prime/")
            ):
                raise ManifestError(f"{name}: invalid qualification fixture")
            fixture = repo / fixture_text
            if not fixture.is_file() or not fixture.with_suffix(
                ".expected"
            ).is_file():
                raise ManifestError(
                    f"{name}: qualification fixture or oracle is missing"
                )
            if conversion.get("coverage") in (None, "none"):
                raise ManifestError(f"{name}: qualified without coverage")
            contract = qualification_contract(name)
            if contract is not None:
                label = contract["label"]
                if conversion.get("fixture") != contract["fixture"]:
                    raise ManifestError(
                        f"{name}: {label} fixture drift"
                    )
                if conversion.get("coverage") != contract["coverage"]:
                    raise ManifestError(
                        f"{name}: {label} coverage drift"
                    )
                if conversion.get("cases") != contract["cases"]:
                    raise ManifestError(
                        f"{name}: {label} counts drift"
                    )
        elif status == "pending":
            reason = conversion.get("reason")
            if conversion.get("coverage") != "none":
                raise ManifestError(f"{name}: pending task claims coverage")
            if not isinstance(reason, str) or not reason.strip():
                raise ManifestError(
                    f"{name}: pending conversion has no reason"
                )
        else:
            raise ManifestError(f"{name}: unknown conversion status")
        if qualification_contract(name) is not None and status != "qualified":
            raise ManifestError(f"{name}: qualified conversion was removed")
    return qualified, len(entries) - qualified


def verify_snapshot(data: dict[str, Any], snapshot_root: Path) -> None:
    examples_root = snapshot_root / "examples"
    for entry in data["tasks"]:
        name = entry["name"]
        task_root = examples_root / name
        for source_name in SOURCE_FILES:
            source = task_root / source_name
            if not source.is_file():
                raise ManifestError(f"{name}: missing {source_name}")
            observed = sha256_file(source)
            if observed != entry["files"][source_name]:
                raise ManifestError(f"{name}: {source_name} source drift")

        bias = parse_bias(task_root / "bias.pl")
        for field in ("recursive", "target", "reference_clauses"):
            if bias[field] != entry[field]:
                raise ManifestError(f"{name}: {field} metadata drift")
        examples = parse_examples(task_root / "exs.pl")
        if examples != entry["examples"]:
            raise ManifestError(f"{name}: example-count drift")


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--manifest",
        type=Path,
        default=repo / "benchmarks/prime/ilp/popper_synthesis_manifest.json",
    )
    parser.add_argument(
        "--snapshot-root",
        type=Path,
        help="Popper checkout root containing examples/synthesis-*",
    )
    args = parser.parse_args()

    try:
        data = load_manifest(args.manifest)
        qualified, pending = validate_manifest(data, repo)
        if args.snapshot_root is not None:
            verify_snapshot(data, args.snapshot_root)
    except ManifestError as exc:
        print(f"FAIL: Popper synthesis manifest: {exc}", file=sys.stderr)
        return 1

    source_suffix = (
        " and the pinned source snapshot"
        if args.snapshot_root is not None
        else ""
    )
    print(
        "PASS: Popper synthesis manifest accounts for all "
        f"{qualified + pending} tasks ({qualified} qualified, "
        f"{pending} pending){source_suffix}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
