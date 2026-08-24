#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest


sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO / "scripts/check_prime_chaining_readiness_manifest.py"
SPEC = importlib.util.spec_from_file_location(
    "check_prime_chaining_readiness_manifest", MODULE_PATH
)
assert SPEC is not None and SPEC.loader is not None
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)
MANIFEST_PATH = REPO / "benchmarks/prime/ilp/chaining_readiness_manifest.json"


class ChainingReadinessManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))

    def test_manifest_exposes_five_qualified_performance_lanes(self) -> None:
        self.assertEqual(
            CHECKER.validate_manifest(self.manifest, REPO), (5, 4, 5)
        )

    def test_strongest_route_cannot_be_replaced_by_a_generic_label(self) -> None:
        altered = copy.deepcopy(self.manifest)
        altered["performance_lanes"][1]["strongest_route"] = "generic-checker"
        with self.assertRaisesRegex(CHECKER.ManifestError, "strongest route drift"):
            CHECKER.validate_manifest(altered, REPO)

    def test_prime_candidate_bag_counts_are_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        lane = next(
            item
            for item in altered["performance_lanes"]
            if item["name"] == "prime-indexed-chaining"
        )
        lane["counts"]["refuted_candidates"] = 62
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "Prime indexed chaining counts drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_open_inference_control_cannot_be_silently_qualified(self) -> None:
        altered = copy.deepcopy(self.manifest)
        item = next(
            entry
            for entry in altered["intent_coverage"]
            if entry["capability"] == "user-programmable-inference-control"
        )
        item["status"] = "qualified"
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "chaining intent coverage drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_source_grounded_lane_cannot_drop_its_pinned_identity(self) -> None:
        altered = copy.deepcopy(self.manifest)
        lane = next(
            item
            for item in altered["performance_lanes"]
            if item["name"] == "curried-chaining"
        )
        lane["source_identity"] = "experimental/curried-chaining/unknown.metta"
        with self.assertRaisesRegex(CHECKER.ManifestError, "unpinned source"):
            CHECKER.validate_manifest(altered, REPO)


if __name__ == "__main__":
    unittest.main()
