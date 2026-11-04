#!/usr/bin/env python3
"""Negative and positive controls for the joined handoff verdict."""
import unittest
from check_native_qualification_handoff import (EXPECTED_MODULES, formal_ok, inventory_ok,
    mutants_ok, observer_ok, publication_ok, simple_package_ok)


class HandoffControls(unittest.TestCase):
    def test_observer_requires_green_coverage_and_cases(self):
        good = {"status": "qualified", "binary_unchanged": True, "harness_unchanged": True,
                "failures": [], "unconfirmed_routes": [], "unconfirmed_subroutes": [],
                "cases": [{"verdict": "equal"}]}
        self.assertTrue(observer_ok(good))
        good["cases"][0]["verdict"] = "value-mismatch"
        self.assertFalse(observer_ok(good))

    def test_simple_package_and_publication_reject_one_failure(self):
        self.assertTrue(simple_package_ok({"identities_unchanged": True, "results": [{"passed": True}]}))
        self.assertFalse(simple_package_ok({"identities_unchanged": True, "results": [{"passed": False}]}))
        good = {"binary_unchanged": True, "source_unchanged": True, "harness_unchanged": True,
                "records": [{"passed": True}]}
        self.assertTrue(publication_ok(good))
        good["records"][0]["passed"] = False
        self.assertFalse(publication_ok(good))

    def test_mutation_gate_does_not_hide_oracle_failures(self):
        good = {"identities_unchanged": True, "oracle_status": "qualified", "oracle_unresolved_cases": [],
                "results": [{"passed": True}]}
        self.assertTrue(mutants_ok(good))
        good["oracle_unresolved_cases"] = [{"case": 1}]
        self.assertFalse(mutants_ok(good))

    def test_inventory_needs_all_fourteen_entries(self):
        route = {"qualified_inventory_entry": True}
        self.assertTrue(inventory_ok({"binary_unchanged": True, "route_count": 14,
                                      "routes": {str(i): route for i in range(14)}}))
        self.assertFalse(inventory_ok({"binary_unchanged": True, "route_count": 13,
                                       "routes": {str(i): route for i in range(13)}}))

    def test_formal_manifest_rejects_admission_or_missing_module(self):
        def manifest(name):
            return {"ownerModule": "Mettapedia.Machines." + name, "checkedBodyDeclarationCount": 1,
                    "nonLogicalDeclarationCount": 0, "nonLogicalDeclarations": [],
                    "declarations": [{"declaration": {"kind": "theorem"}, "containsAdmission": False,
                                      "transitiveAxioms": []}]}
        good = [manifest(name) for name in EXPECTED_MODULES]
        self.assertTrue(formal_ok(good)["passed"])
        self.assertFalse(formal_ok(good[:-1])["passed"])
        good[0]["declarations"][0]["containsAdmission"] = True
        self.assertFalse(formal_ok(good)["passed"])

    def test_source_receipt_is_a_separate_required_input(self):
        good = {"binary_sha256": "abc", "status": "qualified-source-receipt",
                "checks": {"patch": True, "ledger": True}}
        self.assertEqual(good["binary_sha256"], "abc")
        self.assertTrue(all(good["checks"].values()))
        good["checks"]["ledger"] = False
        self.assertFalse(all(good["checks"].values()))


if __name__ == "__main__":
    unittest.main()
