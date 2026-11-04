#!/usr/bin/env python3
"""Negative controls for root-census coverage and retained-reader oracles."""
from copy import deepcopy
import unittest

from check_native_observers import Run
from check_reader_lifetimes import lifecycle_evidence, oracle, program
from native_observer_cases import MARKER


def receipt():
    return {"architecture": "i386:x86-64", "errors": [], "pending": 0, "registered_at_exit": [],
            "exit": {"code": 0, "terminal": True}, "events": [
                {"kind": "register", "returned": True, "result": 1, "registry_len": 8, "registry_cap": 8},
                {"kind": "collect", "returned": True, "success": True, "call": 2, "active_contexts": [70]},
                {"kind": "roots", "returned": True, "exact": True, "context": 70, "collections": [2], "cache": [9]},
                {"kind": "record-read", "returned": True, "before": (1 << 64) - 1,
                 "matches_cache": True, "stable_existing": True}]}


class ReaderLifetimeControls(unittest.TestCase):
    def test_completed_live_collection_qualifies(self):
        self.assertTrue(lifecycle_evidence(receipt(), True)["qualified"])

    def test_collection_after_readers_close_is_insufficient(self):
        r = receipt()
        r["events"][1]["active_contexts"] = []
        self.assertFalse(lifecycle_evidence(r, True)["qualified"])

    def test_missing_one_live_provider_is_rejected(self):
        r = receipt()
        r["events"][1]["active_contexts"].append(80)
        self.assertFalse(lifecycle_evidence(r, True)["qualified"])

    def test_inexact_callback_or_cache_is_rejected(self):
        for index, key in [(2, "exact"), (3, "matches_cache"), (3, "stable_existing")]:
            r = receipt()
            r["events"][index][key] = False
            self.assertFalse(lifecycle_evidence(r, True)["qualified"])

    def test_registration_entry_is_not_success(self):
        r = receipt()
        r["events"][0]["result"] = 0
        self.assertFalse(lifecycle_evidence(r, True)["qualified"])

    def test_early_exit_unfinished_probe_or_owner_leak_is_rejected(self):
        for key, value in [("pending", 1), ("registered_at_exit", [70]), ("errors", ["event-limit"])]:
            r = receipt()
            r[key] = value
            self.assertFalse(lifecycle_evidence(r, True)["qualified"])
        r = receipt()
        r["exit"]["terminal"] = False
        self.assertFalse(lifecycle_evidence(r, True)["qualified"])

    def test_disabled_collection_is_not_positive_collection_evidence(self):
        r = receipt()
        r["events"] = [e for e in r["events"] if e["kind"] not in ("collect", "roots")]
        self.assertTrue(lifecycle_evidence(r, False)["qualified"])
        self.assertFalse(lifecycle_evidence(r, True)["qualified"])

    def test_fixture_preserves_occurrence_multiplicity(self):
        _, contracts, expected = program("deep", 2)
        output = "".join(f"({key} {value})\n" for key, value in zip(contracts, expected))
        run = Run("exited", 0, output, "", 0, {}, [])
        self.assertEqual(oracle(run, contracts, expected), "equal")
        broken = deepcopy(run)
        broken.stdout = output.replace("(1000004 1000004) ", "", 1)
        self.assertEqual(oracle(broken, contracts, expected), "value-mismatch")

    def test_missing_packet_is_not_an_empty_answer(self):
        _, contracts, expected = program("flat", 1)
        self.assertEqual(oracle(Run("exited", 0, "", "", 0, {}, []), contracts, expected), "protocol-mismatch")

    def test_bad_fixture_inputs_are_rejected(self):
        for shape, handles in [("other", 1), ("deep", 0), ("flat", 18)]:
            with self.assertRaises(ValueError):
                program(shape, handles)

    def test_later_keys_are_not_source_literals(self):
        source, _, expected = program("deep", 1)
        self.assertNotIn("1000004", source)
        self.assertNotIn("7000020", source)
        self.assertIn("1000004", expected[0])


if __name__ == "__main__":
    unittest.main()
