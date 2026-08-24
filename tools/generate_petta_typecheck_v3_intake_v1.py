#!/usr/bin/env python3
"""Generate the checked intake artifact for a future PeTTa typecheck-v3."""

from __future__ import annotations

import argparse
import collections
from hashlib import sha256
import json
from pathlib import Path
import sys
from typing import Sequence


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import gslt2parse_schema_v1 as schema
from petta_typecheck_v2_corpus import (
    SCHEMA as ACCEPTANCE_SCHEMA,
    native_v3_intake_inventory,
    parse_census_witness_payload,
)
from generate_petta_typecheck_v2_census_v1 import (
    CensusEvent,
    GenerationError,
    catalog_from_payload,
)


INTAKE_SCHEMA = "cetta-petta-typecheck-v3-intake-v1"


def sha256_file(path: Path) -> str:
    return sha256(path.read_bytes()).hexdigest()


def relation_key(term: schema.SExpr, context: str) -> tuple[str, int]:
    if not isinstance(term, tuple) or not term:
        raise GenerationError(f"{context}: relation must be a nonempty form")
    head = term[0]
    if not isinstance(head, schema.Symbol) or not head.text:
        raise GenerationError(f"{context}: relation has no symbolic head")
    return head.text, len(term) - 1


def presentation_inventory(
    presentation: schema.Presentation,
) -> dict[str, object]:
    head_relations: set[tuple[str, int]] = set()
    consumer_rules: dict[tuple[str, int], set[str]] = collections.defaultdict(set)
    rules: list[dict[str, object]] = []
    for rule in presentation.rules:
        head_relation, head_arity = relation_key(
            rule.head, f"{presentation.name}: rule {rule.name} head"
        )
        head_relations.add((head_relation, head_arity))
        for body_index, body in enumerate(rule.body):
            relation = relation_key(
                body,
                f"{presentation.name}: rule {rule.name} body {body_index}",
            )
            consumer_rules[relation].add(rule.name)
        rules.append({
            "name": rule.name,
            "head_relation": head_relation,
            "head_arity": head_arity,
        })
    providers = [
        {
            "relation": relation,
            "arity": arity,
            "consumer_rules": sorted(consumer_rules[(relation, arity)]),
        }
        for relation, arity in sorted(set(consumer_rules) - head_relations)
    ]
    return {
        "name": presentation.name,
        "source_sha256": sha256_file(presentation.source),
        "rule_count": len(rules),
        "rules": sorted(rules, key=lambda rule: str(rule["name"])),
        "provider_requirements": providers,
    }


def census_inventory(
    events: Sequence[CensusEvent],
    witnesses: dict[str, dict[str, str]],
    interactions: list[dict[str, object]],
    authored_rules: set[str],
) -> dict[str, object]:
    if {event.name for event in events} != set(witnesses):
        raise GenerationError("generated census catalog differs from its witness ledger")
    by_axis: dict[str, list[CensusEvent]] = collections.defaultdict(list)
    for event in events:
        if event.kind == "rule" and event.mapping not in authored_rules:
            raise GenerationError(
                f"{event.name}: rule mapping {event.mapping} is not authored"
            )
        by_axis[event.axis].append(event)
    axes: dict[str, object] = {}
    for axis, axis_events in sorted(by_axis.items()):
        axes[axis] = {
            "event_count": len(axis_events),
            "rule_event_count": sum(event.kind == "rule" for event in axis_events),
            "mechanism_event_count": sum(
                event.kind == "mechanism" for event in axis_events
            ),
            "rule_mappings": sorted({
                event.mapping for event in axis_events if event.kind == "rule"
            }),
            "mechanism_mappings": sorted({
                event.mapping
                for event in axis_events
                if event.kind == "mechanism"
            }),
        }
    return {
        "event_count": len(events),
        "oracle_event_count": sum(event.scope == "oracle" for event in events),
        "mechanism_scope_event_count": sum(
            event.scope == "mechanism" for event in events
        ),
        "axes": axes,
        "axis_interactions": interactions,
    }


def build_intake(
    manifest_payload: object,
    witness_payload: object,
    presentation_paths: Sequence[Path],
) -> dict[str, object]:
    if not isinstance(manifest_payload, dict):
        raise GenerationError("acceptance manifest is not an object")
    if manifest_payload.get("schema") != ACCEPTANCE_SCHEMA:
        raise GenerationError("acceptance manifest has an unsupported schema")
    cases = manifest_payload.get("cases")
    if not isinstance(cases, list) or not cases:
        raise GenerationError("acceptance manifest has no cases")
    case_ids = set()
    for case in cases:
        if not isinstance(case, dict):
            raise GenerationError("acceptance manifest contains a non-object case")
        identity = case.get("id")
        if not isinstance(identity, str) or not identity or identity in case_ids:
            raise GenerationError("acceptance manifest has a duplicate or missing case id")
        case_ids.add(identity)
    try:
        witnesses, interactions = parse_census_witness_payload(
            witness_payload, case_ids
        )
        native_intake, intake_errors = native_v3_intake_inventory(cases)
        presentations = schema.admit(presentation_paths)
    except (schema.SchemaError, RuntimeError, ValueError) as error:
        raise GenerationError(str(error)) from error
    if intake_errors:
        raise GenerationError("; ".join(intake_errors))
    candidate_records: list[dict[str, object]] = []
    for case in cases:
        classification = case.get("classification")
        if not isinstance(classification, dict) or (
            classification.get("disposition") != "native-v3-candidate"
        ):
            continue
        expected = case.get("expected")
        if not isinstance(expected, dict):
            raise GenerationError(
                f"{case['id']}: native-v3 candidate has no expected v2 result"
            )
        candidate_records.append({
            "id": case["id"],
            "axis": classification["axis"],
            "class": classification["class"],
            "finding": classification["finding"],
            "phase": classification["phase"],
            "v2_expected": expected,
        })
    presentation_rows = [presentation_inventory(item) for item in presentations]
    authored_rules = {
        str(rule["name"])
        for presentation in presentation_rows
        for rule in presentation["rules"]
    }
    events = catalog_from_payload(witness_payload)
    return {
        "schema": INTAKE_SCHEMA,
        "inputs": {
            "acceptance_manifest_canonical_sha256": sha256(
                json.dumps(manifest_payload, sort_keys=True, separators=(",", ":")).encode()
            ).hexdigest(),
            "witness_ledger_canonical_sha256": sha256(
                json.dumps(witness_payload, sort_keys=True, separators=(",", ":")).encode()
            ).hexdigest(),
        },
        "presentations": presentation_rows,
        "census": census_inventory(
            events, witnesses, interactions, authored_rules
        ),
        "native_v3_intake": {
            **native_intake,
            "candidates": sorted(
                candidate_records, key=lambda candidate: str(candidate["id"])
            ),
        },
        "scope_note": (
            "This artifact identifies the authored finite-Horn rules, their "
            "required providers, and the v3 candidate intake. It is not an "
            "executable typecheck-v3 or a completeness claim for either checker."
        ),
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--witness-ledger", required=True, type=Path)
    parser.add_argument("--presentation", required=True, type=Path, action="append")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        witnesses = json.loads(args.witness_ledger.read_text(encoding="utf-8"))
        intake = build_intake(manifest, witnesses, args.presentation)
        rendered = json.dumps(intake, indent=2, sort_keys=True) + "\n"
        if args.check:
            if args.output.read_text(encoding="utf-8") != rendered:
                raise GenerationError(
                    "generated v3 intake differs from the checked-in artifact"
                )
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
    except (OSError, json.JSONDecodeError, GenerationError) as error:
        parser.exit(1, f"v3 intake generation rejected: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
