#!/usr/bin/env python3
"""Fail-closed accounting for compact module qualification."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from qualify_module_values_v1 import declarations, summarize
from gslt2parse_schema_v1 import Symbol


class ModuleInventory(unittest.TestCase):
    def test_deep_declaration_count_keeps_occurrences(self):
        declaration = (Symbol("ModuleDeclarationV1"), 0, Symbol("body"))
        value = Symbol("LNil")
        for _ in range(20000):
            value = (Symbol("LCons"), declaration, value)
        self.assertEqual(declarations(value), 20000)

    def test_only_exact_declaration_forms_count(self):
        self.assertEqual(declarations((Symbol("ModuleDeclarationV1"),)), 0)
        self.assertEqual(declarations((Symbol("Other"), 1, 2)), 0)
        self.assertEqual(declarations(()), 0)
        self.assertEqual(declarations(Symbol("ModuleDeclarationV1")), 0)

    def test_all_compact_is_required(self):
        rows = [{"result": "ModuleValueV1"}] * 6
        summary = summarize(rows, [], 2, ("he", "petta", "prime"))
        self.assertTrue(summary["passed"])
        self.assertEqual(summary["compact"], 6)

    def test_every_other_outcome_fails(self):
        for outcome in ("ParserRejectedV1", "ParserLexicalRejectedV1", "ParserIncompleteV1",
                        "ParserCyclicV1", "ParserFailureV1", "ModuleRefusedV1",
                        "ModuleAmbiguousReadV1", "NoReturn"):
            with self.subTest(outcome=outcome):
                row = {"result": outcome}
                summary = summarize([{"result": "ModuleValueV1"}, row], [], 2, ("he",))
                self.assertFalse(summary["passed"])
                self.assertEqual(summary["noncompact"], [row])

    def test_missing_or_extra_observations_fail(self):
        for count in (0, 1, 3):
            self.assertFalse(summarize([{"result": "ModuleValueV1"}] * count,
                                       [], 2, ("he",))["passed"])

    def test_drift_cannot_be_qualified_by_current_acceptance(self):
        self.assertFalse(summarize([{"result": "ModuleValueV1"}],
                                  [{"frozen_sha256": "old", "current_sha256": "new"}],
                                  1, ("he",))["passed"])

    def test_empty_inventory_is_not_a_qualification(self):
        self.assertFalse(summarize([], [], 0, ("he",))["passed"])

    def test_drift_during_the_run_cannot_be_counted(self):
        self.assertFalse(summarize([{"result": "ModuleValueV1", "frozen_match": False}],
                                  [], 1, ("he",))["passed"])


if __name__ == "__main__":
    unittest.main()
