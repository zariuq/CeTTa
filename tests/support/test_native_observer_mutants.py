#!/usr/bin/env python3
"""Controls for the archived-mutant gate's success condition."""
import unittest
from check_native_observer_mutants import qualifies


def case(verdict="value-mismatch", ordinary="equal", repaired="equal"):
    return {"verdict": verdict, "oracle_reference": ordinary, "fixed_verdict": repaired}


class MutantGateControls(unittest.TestCase):
    def test_a_detected_difference_with_aligned_oracles_qualifies(self):
        self.assertTrue(qualifies([case("equal"), case()]))

    def test_empty_or_surviving_cohort_does_not_qualify(self):
        self.assertFalse(qualifies([]))
        self.assertFalse(qualifies([case("equal")]))

    def test_timeout_is_not_a_kill(self):
        self.assertFalse(qualifies([case("inconclusive-execution-limit")]))
        self.assertFalse(qualifies([case(), case("inconclusive-route-probe")]))

    def test_misaligned_ordinary_oracle_rejects_the_artifact(self):
        self.assertFalse(qualifies([case(ordinary="value-mismatch")]))
        self.assertFalse(qualifies([case(repaired="value-mismatch")]))

    def test_invalid_reference_or_unrecognized_status_is_rejected(self):
        for status in ("invalid-reference", "invalid-reference-oracle", "unrecognized"):
            self.assertFalse(qualifies([case(), case(status)]))


if __name__ == "__main__":
    unittest.main()
