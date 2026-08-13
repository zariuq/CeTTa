#!/usr/bin/env python3
"""Add the NIK authority and per-rule frames to an authored GSLT source.

This is a source-preserving migration tool.  It requires an explicit native
projection for every rule head (or rule-specific override) and refuses to
rewrite an already framed presentation.
"""

from __future__ import annotations

from pathlib import Path
from typing import Sequence
import argparse
import re

import gslt2parse_schema_v1 as schema


def mapping(values: Sequence[str], label: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in values:
        key, separator, target = value.partition("=")
        if not separator or not key or not target or key in result:
            raise ValueError(f"invalid or duplicate {label}: {value!r}")
        result[key] = target
    return result


def head_symbol(rule: schema.RuleDecl) -> str:
    if (
        not isinstance(rule.head, tuple)
        or not rule.head
        or not isinstance(rule.head[0], schema.Symbol)
    ):
        raise ValueError(f"rule {rule.name} has no relational head")
    return rule.head[0].text


def frame_text(
    source: str,
    presentation: schema.Presentation,
    *,
    mode: str,
    certificate_policy: str,
    fiber: str,
    outcomes: Sequence[str],
    default_outcome: str,
    native_projection: str,
    status: str,
    commitments: Sequence[str],
    head_projections: dict[str, str],
    rule_projections: dict[str, str],
    calibration_rules: set[str],
) -> str:
    if presentation.nik_frame is not None:
        raise ValueError("presentation already carries nik-authority-frame-v1")
    if len(outcomes) < 2 or len(set(outcomes)) != len(outcomes):
        raise ValueError("outcomes must contain at least two distinct symbols")
    if default_outcome not in outcomes:
        raise ValueError("default outcome must be one of the declared outcomes")

    frame = (
        "\n  (nik-authority-frame-v1\n"
        f"    (mode {mode})\n"
        f"    (certificate-policy {certificate_policy})\n"
        f"    (fiber {fiber})\n"
        f"    (outcome-algebra ({' '.join(outcomes)})\n"
        "      (exclusive)\n"
        f"      (default {default_outcome}))\n"
        f"    (native-projection {native_projection})\n"
        f"    (status {status})\n"
        f"    (commitments{' ' if commitments else ''}{' '.join(commitments)}))"
    )
    root = re.compile(
        rf"(\(gslt-presentation-v1\s+{re.escape(presentation.name)})"
    )
    source, count = root.subn(rf"\1{frame}", source, count=1)
    if count != 1:
        raise ValueError("could not locate the presentation root")

    known_rules = {rule.name for rule in presentation.rules}
    unknown_overrides = set(rule_projections).difference(known_rules)
    unknown_calibrations = calibration_rules.difference(known_rules)
    if unknown_overrides:
        raise ValueError(
            "unknown rule projection overrides: "
            + ", ".join(sorted(unknown_overrides))
        )
    if unknown_calibrations:
        raise ValueError(
            "unknown calibration rules: " + ", ".join(sorted(unknown_calibrations))
        )

    for rule in presentation.rules:
        projection = rule_projections.get(rule.name)
        if projection is None:
            projection = head_projections.get(head_symbol(rule))
        if projection is None:
            raise ValueError(f"rule {rule.name} has no native projection")
        role = "calibration" if rule.name in calibration_rules else "calculus"
        rule_frame = (
            "\n      (nik-rule-frame-v1\n"
            f"        (native-projection {projection})\n"
            f"        (role {role}))"
        )
        pattern = re.compile(
            rf"(\(rule\s+{re.escape(rule.name)})(?=\s+\(head\b)"
        )
        source, count = pattern.subn(rf"\1{rule_frame}", source, count=1)
        if count != 1:
            raise ValueError(f"could not locate rule {rule.name}")
    return source


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--certificate-policy", required=True)
    parser.add_argument("--fiber", required=True)
    parser.add_argument("--outcome", action="append", required=True)
    parser.add_argument("--default-outcome", required=True)
    parser.add_argument("--native-projection", required=True)
    parser.add_argument("--status", required=True)
    parser.add_argument("--commitment", action="append", default=[])
    parser.add_argument("--projection", action="append", default=[])
    parser.add_argument("--rule-projection", action="append", default=[])
    parser.add_argument("--calibration-rule", action="append", default=[])
    args = parser.parse_args(argv)
    try:
        presentation = schema.parse_presentation(args.source)
        framed = frame_text(
            args.source.read_text(encoding="utf-8"),
            presentation,
            mode=args.mode,
            certificate_policy=args.certificate_policy,
            fiber=args.fiber,
            outcomes=args.outcome,
            default_outcome=args.default_outcome,
            native_projection=args.native_projection,
            status=args.status,
            commitments=args.commitment,
            head_projections=mapping(args.projection, "head projection"),
            rule_projections=mapping(
                args.rule_projection, "rule projection override"
            ),
            calibration_rules=set(args.calibration_rule),
        )
        args.output.write_text(framed, encoding="utf-8")
        schema.admit([args.output])
    except (OSError, ValueError, schema.SchemaError) as error:
        parser.exit(1, f"NIK authority framing rejected: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
