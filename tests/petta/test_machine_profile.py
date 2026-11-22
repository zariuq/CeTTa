#!/usr/bin/env python3
"""The profiler preserves corpus observations and requires completed success."""

import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "bench_petta_machine_profile", ROOT / "scripts/bench_petta_machine_profile.py")
PROFILE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROFILE)


class ProfileQualificationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="profile-contract-")
        self.root = Path(self.temporary.name)
        (self.root / "examples").mkdir()
        (self.root / "examples/case.metta").write_text("!(f)\n")
        self.binary = self.root / "bin/cetta"
        self.binary.parent.mkdir()
        self.goldens = self.root / "golden"
        self.goldens.mkdir()
        (self.goldens / "case.stdout").write_text("first\nsecond\n")
        self.entry = {"name": "case.metta", "mode": "golden", "timeout": 5}
        self.patchers = [
            mock.patch.object(PROFILE.corpus, "DEFAULT_GOLDEN_DIR", self.goldens),
            mock.patch.object(PROFILE.corpus, "active_overlay", return_value=None),
            mock.patch.object(PROFILE.corpus, "CONTROLLED_CASES", {}),
        ]
        for patcher in self.patchers:
            patcher.start()

    def tearDown(self):
        for patcher in reversed(self.patchers):
            patcher.stop()
        self.temporary.cleanup()

    def run_profile(self, output="first\nsecond\n", stderr="", exit_code=0):
        stderr += "PETTA_MACHINE_STATS time_to_first_answer_ns=1 first_answer_transition=1\n"
        with mock.patch.object(PROFILE.corpus, "run_bounded_process",
                               return_value=(exit_code, output, stderr, False, False)) as runner:
            result = PROFILE.run_workload(self.binary, self.root, self.entry, 5,
                                          {"CETTA_PETTA_MACHINE_STATS": "1"}, True)
            return result, runner.call_args

    def test_completed_success_has_measured_process_and_effective_source(self):
        result, call = self.run_profile()
        self.assertTrue(result["qualified"])
        self.assertGreater(result["process_elapsed_ns"], 0)
        self.assertEqual(result["effective_source_sha256"], hashlib.sha256(b"!(f)\n").hexdigest())
        self.assertEqual(call.args[0][1:4], ["--emit-runtime-stats", "--lang", "petta"])
        self.assertEqual(call.args[1], self.root)

    def test_fifo_cannot_weaken_the_declared_order(self):
        stats = "CETTA_CONTROLLER_STATS requested=fifo active=fifo storage=shared-terms-owned-state\n"
        with self.assertRaisesRegex(RuntimeError, "stdout differs"):
            self.run_profile("second\nfirst\n", stats)
        self.entry["observation"] = PROFILE.corpus.STDOUT_OCCURRENCE_BAG
        result, _ = self.run_profile("second\nfirst\n", stats)
        self.assertTrue(result["qualified"])
        with self.assertRaises(RuntimeError):
            self.run_profile("second\nfirst\nfirst\n", stats)

    def test_known_failures_negatives_and_live_prefixes_are_not_successful_workloads(self):
        for changes, fixture in [
            ({"mode": "self-check", "failing": "is 2, should 1. ❌ "}, {}),
            ({"mode": PROFILE.corpus.MODE_NEGATIVE, "exit": 1, "failure": "bad"}, {}),
            ({}, {"mode": "prefix", "stdin": "quit\n"}),
        ]:
            entry = {**self.entry, **changes}
            with self.subTest(entry=entry), \
                 mock.patch.dict(PROFILE.corpus.CONTROLLED_CASES, {"case.metta": fixture}), \
                 mock.patch.object(PROFILE.corpus, "run_bounded_process") as runner:
                with self.assertRaisesRegex(RuntimeError, "not a completed successful workload"):
                    PROFILE.run_workload(self.binary, self.root, entry, 5, {}, False)
                runner.assert_not_called()

    def test_real_fixture_input_is_supplied_by_the_corpus_runner(self):
        with mock.patch.dict(PROFILE.corpus.CONTROLLED_CASES,
                             {"case.metta": {"stdin": "quit\n"}}):
            _, call = self.run_profile()
            self.assertEqual(call.args[3], "quit\n")

    def test_nonzero_exit_cannot_qualify_even_matching_golden(self):
        self.entry["exit"] = 1
        with self.assertRaises(RuntimeError):
            self.run_profile(exit_code=1)


if __name__ == "__main__":
    unittest.main()
