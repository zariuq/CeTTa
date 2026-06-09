#!/usr/bin/env python3
"""Negative self-tests for HE-compat catalog guardrails."""

from __future__ import annotations

import importlib.util
import json
import tempfile
from pathlib import Path
from types import ModuleType
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "scripts" / "build_he_compat_catalog.py"
SCRATCH = ROOT / "runtime" / "he_compat"


def load_generator() -> ModuleType:
    spec = importlib.util.spec_from_file_location("build_he_compat_catalog", GENERATOR)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {GENERATOR}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expect_exit(label: str, fn: Callable[[], object], needle: str) -> bool:
    try:
        fn()
    except SystemExit as exc:
        message = str(exc)
        if needle in message:
            print(f"PASS: {label}")
            return True
        print(f"FAIL: {label}: wrong error: {message}")
        return False
    print(f"FAIL: {label}: expected SystemExit")
    return False


def result_row(path: str, upstream_head: str = "[upstream]\n") -> dict[str, object]:
    return {
        "path": path,
        "classification": "diff",
        "manifest": {"lane": "test", "notes": "synthetic guard test"},
        "cetta": {"head": "[cetta]\n"},
        "upstream": {"head": upstream_head},
    }


def build_summary(module: ModuleType, rows: list[dict[str, object]]) -> object:
    SCRATCH.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="catalog_guard_", dir=SCRATCH) as tmp:
        report = Path(tmp) / "report.json"
        report.write_text(
            json.dumps(
                {
                    "summary": {"selected": len(rows)},
                    "results": rows,
                }
            ),
            encoding="utf-8",
        )
        return module.build_broad_probe_summary(report, [])


def test_rust_drift_requires_evidence(module: ModuleType) -> bool:
    original = dict(module.BROAD_RUST_DRIFT_NOTES)
    try:
        module.BROAD_RUST_DRIFT_NOTES["tests/synthetic_rust_drift.metta"] = {
            "note": "",
            "spec_refs": [],
            "rule_ids": ["not-a-real-rule"],
        }
        return expect_exit(
            "rust-drift entries require note/spec/rule evidence",
            module.validate_broad_rust_drift_notes,
            "invalid broad rust-drift notes",
        )
    finally:
        module.BROAD_RUST_DRIFT_NOTES.clear()
        module.BROAD_RUST_DRIFT_NOTES.update(original)


def test_closed_buckets_fail(module: ModuleType) -> bool:
    return expect_exit(
        "closed broad buckets fail the catalog build",
        lambda: build_summary(module, [result_row("tests/synthetic_core_gap.metta")]),
        "closed HE-compat broad buckets reappeared",
    )


def test_import_bucket_requires_import_boundary(module: ModuleType) -> bool:
    original_triage = module.triage_broad_result
    try:
        module.triage_broad_result = lambda _row: "local-library-or-import-extension"
        return expect_exit(
            "local-library bucket requires Rust import/module boundary",
            lambda: build_summary(module, [result_row("tests/synthetic_library_gap.metta")]),
            "did not stop at the module boundary",
        )
    finally:
        module.triage_broad_result = original_triage


def test_module_resolution_requires_policy_note(module: ModuleType) -> bool:
    original_triage = module.triage_broad_result
    try:
        module.triage_broad_result = lambda _row: "module-resolution-diff"
        return expect_exit(
            "module-resolution bucket requires explicit policy notes",
            lambda: build_summary(
                module,
                [
                    result_row(
                        "tests/synthetic_module_resolution_gap.metta",
                        upstream_head=(
                            "[(Error (import! &self support/foo.metta) "
                            "Illegal module name: support/foo.metta)]\n"
                        ),
                    )
                ],
            ),
            "module-resolution rows require explicit policy notes",
        )
    finally:
        module.triage_broad_result = original_triage


def test_module_inventory_requires_policy_note(module: ModuleType) -> bool:
    original_triage = module.triage_broad_result
    try:
        module.triage_broad_result = lambda _row: "module-inventory-environment-diff"
        return expect_exit(
            "module-inventory bucket requires explicit policy notes",
            lambda: build_summary(module, [result_row("tests/synthetic_print_mods_gap.metta")]),
            "module-inventory rows require explicit policy notes",
        )
    finally:
        module.triage_broad_result = original_triage


def test_track_a_rule_lanes_are_explicit(module: ModuleType) -> bool:
    original = dict(module.TRACK_A_RULE_LANES)
    try:
        module.TRACK_A_RULE_LANES.pop("minimal.eval", None)
        return expect_exit(
            "track-a rule lanes must classify every capacity explicitly",
            module.validate_track_a_rule_lanes,
            "invalid Track A rule-lane table",
        )
    finally:
        module.TRACK_A_RULE_LANES.clear()
        module.TRACK_A_RULE_LANES.update(original)


def main() -> int:
    module = load_generator()
    checks = [
        test_rust_drift_requires_evidence(module),
        test_closed_buckets_fail(module),
        test_import_bucket_requires_import_boundary(module),
        test_module_resolution_requires_policy_note(module),
        test_module_inventory_requires_policy_note(module),
        test_track_a_rule_lanes_are_explicit(module),
    ]
    if all(checks):
        print("PASS: HE-compat catalog guard self-tests")
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
