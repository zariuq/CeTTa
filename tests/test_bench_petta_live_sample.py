#!/usr/bin/env python3
"""Unit tests for the live PeTTa example sampler."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location(
    "bench_petta_live_sample", ROOT / "scripts" / "bench_petta_live_sample.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load live PeTTa sampler")
SAMPLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SAMPLE)


class LivePeTTaSampleTests(unittest.TestCase):
    def test_selection_is_stratified_and_reproducible(self) -> None:
        available = {
            name for population in SAMPLE.STRATA.values() for name in population
        }
        first = SAMPLE.select_examples(20260825, 1, available)
        second = SAMPLE.select_examples(20260825, 1, available)
        self.assertEqual(first, second)
        self.assertEqual(
            [stratum for stratum, _ in first], list(SAMPLE.STRATA)
        )

    def test_missing_stratum_fails_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "scalar-control"):
            SAMPLE.select_examples(1, 1, set())

    def test_tracked_selection_excludes_nested_and_non_metta_paths(self) -> None:
        self.assertEqual(
            SAMPLE.select_tracked_examples(
                [
                    "examples/zeta.metta",
                    "examples/nested/hidden.metta",
                    "examples/readme.txt",
                    "examples/alpha.metta",
                ]
            ),
            [
                ("tracked-corpus", "alpha.metta"),
                ("tracked-corpus", "zeta.metta"),
            ],
        )

    def test_empty_tracked_selection_fails_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "no tracked"):
            SAMPLE.select_tracked_examples(["examples/nested/hidden.metta"])

    def test_classification_requires_both_output_channels(self) -> None:
        same = (0, "answer\n", "")
        status, stdout_equal, stderr_equal = SAMPLE.classify(same, same)
        self.assertEqual(status, "MATCH")
        self.assertTrue(stdout_equal)
        self.assertTrue(stderr_equal)

        status, _, stderr_equal = SAMPLE.classify(
            same, (0, "answer\n", "warning\n")
        )
        self.assertEqual(status, "STDERR_MISMATCH")
        self.assertFalse(stderr_equal)

    def test_only_exact_optional_janus_failure_is_ignored(self) -> None:
        warning = (
            "ERROR: /opt/swipl/library/ext/swipy/janus.pl:116:\n"
            "ERROR:    /opt/swipl/library/ext/swipy/janus.pl:116: "
            "Initialization goal raised exception:\n"
            "ERROR:    open_shared_object/3: libpython3.12.so.1.0: "
            "cannot open shared object file: No such file or directory\n"
        )
        self.assertEqual(SAMPLE.semantic_stderr(warning), "")
        self.assertEqual(
            SAMPLE.semantic_stderr(warning + "ERROR: evaluator failed\n"),
            "ERROR: evaluator failed\n",
        )


if __name__ == "__main__":
    unittest.main()
