#!/usr/bin/env python3
"""Controls for classifying reached publication faults, not only output equality."""
import unittest
from check_relation_publication_faults import SITES, classify, sites_for
from native_observer_probes import R82_SHA256, SCOPED_OWNERS_SHA256, SUCCESSOR_CANDIDATE_2_SHA256


def receipt(code=0, signals=()):
    return {"architecture": "i386:x86-64", "events": [{"site": "example"}],
            "errors": [], "exit": {"terminal": True, "code": code}, "signals": list(signals)}


class PublicationFaultControls(unittest.TestCase):
    def test_normal_refusal_can_preserve_answers(self):
        self.assertEqual(classify(receipt(), "equal"), "equal")

    def test_missing_injection_is_not_qualification(self):
        record = receipt()
        record["events"] = []
        self.assertEqual(classify(record, "equal"), "inconclusive")

    def test_multiple_injections_are_not_the_declared_fault(self):
        record = receipt()
        record["events"] *= 2
        self.assertEqual(classify(record, "equal"), "inconclusive")

    def test_a_stopped_child_is_not_terminal(self):
        record = receipt(None, ("SIGABRT",))
        record["exit"]["terminal"] = False
        self.assertEqual(classify(record, "equal"), "inconclusive")

    def test_fatal_does_not_mean_empty_or_rollback(self):
        self.assertEqual(classify(receipt(None, ("SIGABRT",)), "equal"), "fatal")
        self.assertEqual(classify(receipt(None, ("SIGSEGV",)), "equal"), "unexpected-termination")

    def test_comparison_failure_stays_a_failure(self):
        self.assertEqual(classify(receipt(), "value-mismatch"), "value-mismatch")
        self.assertEqual(classify(receipt(), "inconclusive-execution-limit"), "inconclusive-execution-limit")

    def test_probe_errors_cannot_qualify(self):
        record = receipt()
        record["errors"] = ["wrong-instruction"]
        self.assertEqual(classify(record, "equal"), "inconclusive")
        record["errors"] = []
        record["architecture"] = "different"
        self.assertEqual(classify(record, "equal"), "inconclusive")

    def test_fault_profile_separates_refusal_fatal_and_mutation(self):
        self.assertEqual(sum(site["expect"] == "equal" for site in SITES), 6)
        self.assertEqual(sum(site["expect"] == "fatal" for site in SITES), 1)
        self.assertEqual(sum(site.get("mutation", False) for site in SITES), 1)

    def test_successor_changes_only_reviewed_instruction_offsets(self):
        old = {site["name"]: site["offset"] for site in sites_for(R82_SHA256)}
        new = {site["name"]: site["offset"] for site in sites_for(SUCCESSOR_CANDIDATE_2_SHA256)}
        self.assertEqual({name for name in old if old[name] != new[name]},
                         {"moves-first", "moves-grow", "hold-array-grow", "binary-holds-grow"})
        scoped = {site["name"]: site["offset"] for site in sites_for(SCOPED_OWNERS_SHA256)}
        self.assertEqual(new, scoped)
        with self.assertRaises(ValueError):
            sites_for("unknown")


if __name__ == "__main__":
    unittest.main()
