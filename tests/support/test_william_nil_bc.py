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

PRIME_GENERATOR = (
    ROOT
    / "experiments"
    / "inference_guidance"
    / "generate_prime_biluk_corpus.py"
)
PRIME_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_biluk_corpus", PRIME_GENERATOR
)
assert PRIME_SPEC is not None and PRIME_SPEC.loader is not None
PRIME_MODULE = importlib.util.module_from_spec(PRIME_SPEC)
sys.modules[PRIME_SPEC.name] = PRIME_MODULE
PRIME_SPEC.loader.exec_module(PRIME_MODULE)


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

    def test_prime_generator_uses_only_the_strict_goal_prefix(self) -> None:
        class Catalog:
            @staticmethod
            def assertion_catalog(_path: Path):
                return (
                    {
                        "base": "𝜑",
                        "step": ["->", "𝜑", "𝜓"],
                        "goal": "𝜓",
                        "later": "𝜒",
                    },
                    {"base": 1, "step": 2, "goal": 3, "later": 4},
                )

        rendered = PRIME_MODULE.render(
            catalog_module=Catalog,
            corpus=DRIVER,
            goal="goal",
        )
        self.assertIn("Strict assertion prefix: 2 declarations", rendered)
        self.assertIn("(: smm:base", rendered)
        self.assertIn("(: smm:step", rendered)
        self.assertNotIn("(: smm:goal", rendered)
        self.assertNotIn("(: smm:later", rendered)
        self.assertIn("(-> (app smm:holds $ph) (app smm:holds $ps))", rendered)

    def test_generated_biluk_fixture_is_complete_and_path_independent(self) -> None:
        fixture = (
            ROOT
            / "experiments"
            / "inference_guidance"
            / "generated_biluk_prime_corpus.metta"
        ).read_text(encoding="utf-8")
        self.assertIn("Strict assertion prefix: 381 declarations", fixture)
        self.assertEqual(fixture.count("(: (proof:constructor "), 381)
        self.assertNotIn("Source path:", fixture)


if __name__ == "__main__":
    unittest.main()
