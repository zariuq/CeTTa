#!/usr/bin/env python3
"""Focused fixture tests for WILLIAM-guided Nil backward chaining."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "experiments" / "inference_guidance" / "william_nil_bc.py"
SPEC = importlib.util.spec_from_file_location("william_nil_bc", DRIVER)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class WilliamNilBackwardChainingTests(unittest.TestCase):
    def test_parse_proof_ignores_space_setup_results(self) -> None:
        goal = ["↔", "𝜑", "𝜓"]
        stdout = "()\ntrue\ntrue\n(: (bicom bicom) (↔ 𝜑 𝜓))\n"
        self.assertEqual(MODULE.parse_proof(stdout, goal), ["bicom", "bicom"])

    def test_parse_proof_returns_none_when_search_is_empty(self) -> None:
        self.assertIsNone(MODULE.parse_proof("()\ntrue\n", ["↔", "𝜑", "𝜓"]))

    def test_parse_proof_rejects_a_wrong_goal(self) -> None:
        with self.assertRaisesRegex(MODULE.ExperimentError, "wrong theorem"):
            MODULE.parse_proof("(: proof (↔ 𝜑 𝜑))\n", ["↔", "𝜑", "𝜓"])

    def test_render_program_uses_explicit_storage_order(self) -> None:
        formulas = {"early": "𝜑", "late": "𝜓"}
        program = MODULE.render_program(
            chainer="(: Nat Type)\n",
            goal_label="goal",
            goal_formula="𝜓",
            order_name="ranked",
            selected_labels=["late", "early"],
            storage_order=["early", "late"],
            available_candidates=10,
            formulas=formulas,
            depth=2,
        )
        self.assertLess(
            program.index("!(add-atom &premises (: early $𝜑))"),
            program.index("!(add-atom &premises (: late $𝜓))"),
        )
        self.assertIn("; Selected premises: 2", program)

    def test_essential_hypothesis_goal_is_rejected(self) -> None:
        with self.assertRaisesRegex(MODULE.ExperimentError, "outer lambda"):
            MODULE.require_closed_goal("rule-goal", ["->", "premise", "result"])

    def test_proof_depth_counts_nested_rule_applications(self) -> None:
        proof = ["r4", ["r3", ["r2", ["r1", "axiom"]]]]
        self.assertEqual(MODULE.proof_depth(proof), 4)


if __name__ == "__main__":
    unittest.main()
