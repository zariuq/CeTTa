#!/usr/bin/env python3
"""Validate the source-pinned Prime chaining readiness ledger."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
from typing import Any


SCHEMA = "cetta-prime-chaining-readiness-v1"
REVISION = "bc9beb2672953e07971b3abecc1fe67651ecddc4"
LANES = {
    "curried-chaining": "native-compiled-rule-artifact",
    "hilbert-backward-via-forward": "native-no-replay-rule-program",
    "typed-synthesis": "native-compiled-rule-artifact",
    "sumo-john-carry-flower": "native-compiled-rule-artifact",
    "prime-indexed-chaining": "prime-indexed-proof-construction",
}
INTENT_STATUS = {
    "curried-backward-and-forward-modes": "qualified",
    "dependent-typed-proof-construction": "qualified",
    "typed-program-synthesis": "qualified",
    "large-authored-knowledge-base": "qualified",
    "proof-tree-type-annotations": "partial",
    "iterative-intermediate-knowledge-storage": "partial",
    "user-programmable-inference-control": "pending",
    "polyward-partially-grounded-query-control": "partial",
    "probabilistic-and-pln-chaining": "pending",
}
PRIME_COUNTS = {
    "raw_length_three_candidates": 64,
    "typed_length_three_candidates": 1,
    "refuted_candidates": 63,
    "forward_occurrences_through_length_three": 13,
    "target_occurrences": 2,
}


class ManifestError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_mapping(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ManifestError(f"{context} must be an object")
    return value


def require_list(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise ManifestError(f"{context} must be a list")
    return value


def validate_repo_file(root: Path, raw: Any, context: str) -> Path:
    if not isinstance(raw, str) or not raw or raw.startswith("/") or ".." in Path(raw).parts:
        raise ManifestError(f"{context} must be a repository-relative path")
    path = root / raw
    if not path.is_file():
        raise ManifestError(f"{context} is missing: {raw}")
    return path


def validate_manifest(
    manifest: dict[str, Any], root: Path, source_root: Path | None = None,
) -> tuple[int, int, int]:
    if manifest.get("schema") != SCHEMA:
        raise ManifestError("chaining readiness schema drift")
    source = require_mapping(manifest.get("source"), "source")
    if source.get("revision") != REVISION:
        raise ManifestError("chaining source revision drift")
    files = require_mapping(source.get("files"), "source.files")
    if not files:
        raise ManifestError("source.files must not be empty")
    for relative, expected in files.items():
        if not isinstance(relative, str) or not isinstance(expected, str) or len(expected) != 64:
            raise ManifestError("malformed source identity")
        if source_root is not None:
            path = source_root / relative
            if not path.is_file():
                raise ManifestError(f"source file is missing: {relative}")
            actual = sha256(path)
            if actual != expected:
                raise ManifestError(
                    f"source identity drift for {relative}: expected {expected}, got {actual}"
                )

    lanes = require_list(manifest.get("performance_lanes"), "performance_lanes")
    by_name: dict[str, dict[str, Any]] = {}
    for index, raw in enumerate(lanes):
        lane = require_mapping(raw, f"performance_lanes[{index}]")
        name = lane.get("name")
        if not isinstance(name, str) or name in by_name:
            raise ManifestError("duplicate or malformed performance lane")
        by_name[name] = lane
        if lane.get("status") != "qualified":
            raise ManifestError(f"performance lane {name} is not qualified")
        coverage = require_list(lane.get("coverage"), f"{name}.coverage")
        if not coverage or any(not isinstance(item, str) for item in coverage):
            raise ManifestError(f"performance lane {name} has no exact coverage")
        fixture = validate_repo_file(root, lane.get("fixture"), f"{name}.fixture")
        expected = fixture.with_suffix(".expected")
        if not expected.is_file():
            raise ManifestError(f"performance lane {name} has no golden oracle")
        if "presentation" in lane:
            validate_repo_file(root, lane.get("presentation"), f"{name}.presentation")
        identity = lane.get("source_identity")
        if identity is not None and identity not in files:
            raise ManifestError(f"performance lane {name} has an unpinned source")

    if set(by_name) != set(LANES):
        raise ManifestError("performance lane inventory drift")
    for name, route in LANES.items():
        if by_name[name].get("strongest_route") != route:
            raise ManifestError(f"strongest route drift for {name}")

    prime = by_name["prime-indexed-chaining"]
    if require_mapping(prime.get("counts"), "prime-indexed-chaining.counts") != PRIME_COUNTS:
        raise ManifestError("Prime indexed chaining counts drift")
    prime_fixture = (root / str(prime["fixture"])).read_text(encoding="utf-8")
    if "nil:" in prime_fixture or "chain-bench:" in prime_fixture:
        raise ManifestError("source provenance leaked into the Prime namespace")
    prime_expected = (root / str(prime["fixture"])).with_suffix(".expected").read_text(
        encoding="utf-8"
    )
    for receipt in (
        "(CandidatesGenerated 64)",
        "(CandidatesChecked 64)",
        "(Established 1)",
        "(Refuted 63)",
        "(CountConserved True)",
        "(TraceConserved True)",
        "(TypedProducerEqualsSafeFrontier True)",
    ):
        if receipt not in prime_expected:
            raise ManifestError(f"Prime indexed chaining oracle lost {receipt}")

    runtime_expected = (
        root / "tests/prime/nil_rule_machine_guests.generated.expected"
    ).read_text(encoding="utf-8")
    for receipt in (
        "chain-curried-r0",
        "rm-proof-app ModusPonens",
        "compile-declined rule-program-fragment unsupported-rule-shape chain-curried-r0",
    ):
        if receipt not in runtime_expected:
            raise ManifestError(f"curried source oracle lost {receipt}")

    intents = require_list(manifest.get("intent_coverage"), "intent_coverage")
    actual_intents: dict[str, str] = {}
    for index, raw in enumerate(intents):
        item = require_mapping(raw, f"intent_coverage[{index}]")
        capability = item.get("capability")
        status = item.get("status")
        evidence = item.get("evidence")
        if (
            not isinstance(capability, str)
            or capability in actual_intents
            or status not in {"qualified", "partial", "pending"}
            or not isinstance(evidence, str)
            or not evidence
        ):
            raise ManifestError("malformed intent coverage entry")
        actual_intents[capability] = status
    if actual_intents != INTENT_STATUS:
        raise ManifestError("chaining intent coverage drift")
    return len(by_name), sum(value == "qualified" for value in actual_intents.values()), sum(
        value != "qualified" for value in actual_intents.values()
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("benchmarks/prime/ilp/chaining_readiness_manifest.json"),
    )
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    lanes, qualified, open_items = validate_manifest(
        manifest, args.root, args.source_root
    )
    print(
        f"(PrimeChainingReadinessV1 lanes={lanes} "
        f"qualified-intents={qualified} open-intents={open_items})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ManifestError, json.JSONDecodeError, OSError) as error:
        print(f"check_prime_chaining_readiness_manifest: {error}", file=sys.stderr)
        raise SystemExit(1)
