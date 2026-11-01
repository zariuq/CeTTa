#!/usr/bin/env python3
"""Controls for attributing coordinate results to their caller's domain."""
from copy import deepcopy
import unittest
from check_existence_domains import VIEW, DENSE, COORDINATES, SETTLE, evidence, program


def event(symbol, call, parent=None, **fields):
    return {"symbol": symbol, "call": call, "parent": parent, "space": 7, "returned": True, **fields}


def receipt(events):
    return {"architecture": "i386:x86-64", "errors": [], "pending": 0, "active": [],
            "exit": {"code": 0, "terminal": True}, "events": events}


class ExistenceDomainControls(unittest.TestCase):
    def test_compact_positive_finishes(self):
        r = receipt([event(VIEW, 0, result=1, found=1), event(DENSE, 1, 0, result=1, count=1)])
        self.assertTrue(evidence(r)["valid"])
        self.assertEqual(evidence(r)["branches"][0]["branch"], "compact-hit")

    def test_zero_count_needs_a_completed_compact_check(self):
        r = receipt([event(VIEW, 0, result=1, found=0), event(DENSE, 1, 0, result=0, count=0),
                     event(COORDINATES, 2, 0, result=0, applicable=1)])
        self.assertFalse(evidence(r)["valid"])
        r["events"][1]["result"] = 1
        self.assertTrue(evidence(r)["valid"])

    def test_accepted_coordinate_result_matches_its_caller(self):
        r = receipt([event(VIEW, 0, result=1, found=1), event(DENSE, 1, 0, result=1, count=0),
                     event(COORDINATES, 2, 0, result=0, applicable=1)])
        self.assertFalse(evidence(r)["valid"])

    def test_coordinate_refusal_is_not_proved_absence(self):
        r = receipt([event(VIEW, 0, result=1, found=1), event(SETTLE, 1, 0, void=True),
                     event(COORDINATES, 2, 0, result=0, applicable=0)])
        self.assertTrue(evidence(r)["valid"])

    def test_missing_or_late_settlement_does_not_qualify(self):
        r = receipt([event(VIEW, 0, result=1, found=0), event(COORDINATES, 1, 0, result=0, applicable=0)])
        self.assertFalse(evidence(r)["valid"])
        r["events"].append(event(SETTLE, 2, 0, void=True))
        self.assertFalse(evidence(r)["valid"])

    def test_other_callers_evidence_cannot_be_reused(self):
        r = receipt([event(VIEW, 0, result=1, found=0), event(DENSE, 1, 99, result=1, count=0),
                     event(COORDINATES, 2, 0, result=0, applicable=1)])
        self.assertFalse(evidence(r)["valid"])
        r["events"][1]["parent"] = 0
        r["events"][1]["space"] = 8
        self.assertFalse(evidence(r)["valid"])

    def test_refusal_or_incomplete_receipt_is_not_completion(self):
        good = receipt([event(VIEW, 0, result=1, found=1), event(DENSE, 1, 0, result=1, count=1)])
        for key, value in (("pending", 1), ("active", [0]), ("errors", ["event-limit"])):
            r = deepcopy(good)
            r[key] = value
            self.assertFalse(evidence(r)["valid"])
        good["events"][0]["result"] = 0
        self.assertFalse(evidence(good)["valid"])

    def test_materialized_source_has_an_independent_fixed_oracle(self):
        for kind in ("ground", "open", "unrelated-open"):
            direct, expected = program(kind, False)
            materialized, other = program(kind, True)
            self.assertEqual(expected, other)
            self.assertNotEqual(direct, materialized)
            self.assertIn("(case $ready", materialized)
        with self.assertRaises(ValueError):
            program("unknown", False)


if __name__ == "__main__":
    unittest.main()
