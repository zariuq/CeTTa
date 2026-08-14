#!/usr/bin/env python3

from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from generate_petta_typecheck_v2_census_v1 import GenerationError  # noqa: E402
from generate_petta_typecheck_v3_intake_v1 import build_intake  # noqa: E402


PRESENTATIONS = [
    ROOT / "langdef/petta/generated/typecheck_v2_guard_v1.metta",
    ROOT / "langdef/petta/generated/typecheck_v2_boundary_core_v1.metta",
]


class TypecheckV3IntakeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(
            (ROOT / "tests/petta/typecheck_v2_acceptance_manifest.json").read_text(
                encoding="utf-8"
            )
        )
        cls.witnesses = json.loads(
            (ROOT / "tests/petta/typecheck_v2_semantic_witnesses.json").read_text(
                encoding="utf-8"
            )
        )

    def test_live_intake_identifies_rules_providers_and_candidates(self) -> None:
        intake = build_intake(self.manifest, self.witnesses, PRESENTATIONS)
        self.assertEqual(intake["census"]["event_count"], 95)
        self.assertEqual(intake["census"]["oracle_event_count"], 94)
        self.assertEqual(intake["native_v3_intake"]["candidate_count"], 51)
        self.assertEqual(len(intake["native_v3_intake"]["candidates"]), 51)
        self.assertTrue(all(
            {"id", "axis", "class", "finding", "phase", "v2_expected"}
            <= set(candidate)
            for candidate in intake["native_v3_intake"]["candidates"]
        ))
        self.assertEqual(len(intake["presentations"]), 2)
        guard = next(
            item for item in intake["presentations"]
            if item["name"] == "petta-typecheck-v2-guard"
        )
        boundary = next(
            item for item in intake["presentations"]
            if item["name"] == "petta-typecheck-v2-boundary-core-v1"
        )
        self.assertEqual(guard["rule_count"], 116)
        self.assertEqual(boundary["rule_count"], 60)
        self.assertEqual(
            {(item["relation"], item["arity"])
             for item in guard["provider_requirements"]},
            {
                ("EnvDeclared", 2),
                ("EnvDeclaredList", 2),
                ("EnvNonNewtype", 1),
                ("KnownExpressionEffect", 2),
                ("KnownExpressionResultType", 2),
            },
        )
        self.assertEqual(boundary["provider_requirements"], [])

    def test_unknown_rule_mapping_fails_closed(self) -> None:
        witnesses = copy.deepcopy(self.witnesses)
        for entry in witnesses["witnesses"].values():
            if entry["kind"] == "rule":
                entry["mapping"] = "not-an-authored-rule"
                break
        with self.assertRaisesRegex(GenerationError, "is not authored"):
            build_intake(self.manifest, witnesses, PRESENTATIONS)


if __name__ == "__main__":
    unittest.main()
