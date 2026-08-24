#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO / "scripts/check_prime_popper_synthesis_manifest.py"
SPEC = importlib.util.spec_from_file_location(
    "check_prime_popper_synthesis_manifest", MODULE_PATH
)
assert SPEC is not None and SPEC.loader is not None
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)
NEXT_MODULE_PATH = REPO / "scripts/check_prime_popper_next_solution.py"
NEXT_SPEC = importlib.util.spec_from_file_location(
    "check_prime_popper_next_solution", NEXT_MODULE_PATH
)
assert NEXT_SPEC is not None and NEXT_SPEC.loader is not None
NEXT_CHECKER = importlib.util.module_from_spec(NEXT_SPEC)
NEXT_SPEC.loader.exec_module(NEXT_CHECKER)
RECURSIVE_MODULE_PATH = (
    REPO / "scripts/generate_prime_popper_recursive_arithmetic.py"
)
RECURSIVE_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_popper_recursive_arithmetic", RECURSIVE_MODULE_PATH
)
assert RECURSIVE_SPEC is not None and RECURSIVE_SPEC.loader is not None
RECURSIVE_GENERATOR = importlib.util.module_from_spec(RECURSIVE_SPEC)
sys.modules[RECURSIVE_SPEC.name] = RECURSIVE_GENERATOR
RECURSIVE_SPEC.loader.exec_module(RECURSIVE_GENERATOR)
SORTED_MODULE_PATH = REPO / "scripts/generate_prime_popper_sorted.py"
SORTED_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_popper_sorted", SORTED_MODULE_PATH
)
assert SORTED_SPEC is not None and SORTED_SPEC.loader is not None
SORTED_GENERATOR = importlib.util.module_from_spec(SORTED_SPEC)
sys.modules[SORTED_SPEC.name] = SORTED_GENERATOR
SORTED_SPEC.loader.exec_module(SORTED_GENERATOR)
MANIFEST_PATH = (
    REPO / "benchmarks/prime/ilp/popper_synthesis_manifest.json"
)


class PopperSynthesisManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))

    def test_checked_manifest_accounts_for_every_task(self) -> None:
        self.assertEqual(
            CHECKER.validate_manifest(self.manifest, REPO), (10, 0)
        )

    def test_sorted_extensional_coverage_cannot_be_weakened(self) -> None:
        altered = copy.deepcopy(self.manifest)
        sorted_task = next(
            task
            for task in altered["tasks"]
            if task["name"] == "synthesis-sorted"
        )
        sorted_task["conversion"]["coverage"] = "authored-program"
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "sorted-extensional-geq coverage drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_qualified_task_requires_a_real_fixture_and_oracle(self) -> None:
        altered = copy.deepcopy(self.manifest)
        altered["tasks"][0]["conversion"] = {
            "status": "qualified",
            "coverage": "authored-program-and-background",
            "fixture": "examples/prime/not-a-qualification.metta",
        }
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "fixture or oracle is missing"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_bias_parser_retains_target_modes_and_reference_program(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "bias.pl"
            path.write_text(
                "\n".join(
                    (
                        "%% demo",
                        "%% f(V0,V1):- edge(V0,V1).",
                        "enable_recursion.",
                        "head_pred(f,2).",
                        "body_pred(edge,2).",
                        "type(f,(node,node)).",
                        "type(edge,(node,node)).",
                        "direction(f,(in,out)).",
                        "direction(edge,(in,out)).",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            self.assertEqual(
                CHECKER.parse_bias(path),
                {
                    "recursive": True,
                    "target": {
                        "predicate": "f",
                        "arity": 2,
                        "types": ["node", "node"],
                        "directions": ["in", "out"],
                    },
                    "reference_clauses": [
                        "f(V0,V1):- edge(V0,V1)."
                    ],
                },
            )

    def test_example_parser_keeps_positive_and_negative_counts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "exs.pl"
            path.write_text(
                "pos(f(a)).\npos(f(b)).\nneg(f(c)).\n",
                encoding="utf-8",
            )
            self.assertEqual(
                CHECKER.parse_examples(path),
                {"positive": 2, "negative": 1},
            )

    def test_next_solution_ignores_elapsed_time_prefixes(self) -> None:
        score, clauses = NEXT_CHECKER.solution_lines(
            "\n".join(
                (
                    "0.8s ********** SOLUTION **********",
                    f"0.8s {NEXT_CHECKER.SCORE}",
                    "0.8s next_list(V0,V1):- tail(V0,V2),"
                    "head(V2,V1),head(V0,V3),x(V3).",
                    "0.8s next_list(V0,V1):- tail(V0,V2),"
                    "next_list(V2,V1).",
                    "0.8s ******************************",
                )
            )
        )
        self.assertEqual(score, NEXT_CHECKER.SCORE)
        self.assertEqual(len(clauses), 2)

    def test_next_solution_accepts_premise_permutation(self) -> None:
        self.assertTrue(
            NEXT_CHECKER.is_adjacent_clause(
                "next_list(A,B):- x(M),head(T,B),tail(A,T),head(A,M)."
            )
        )
        self.assertTrue(
            NEXT_CHECKER.is_scan_clause(
                "next_list(A,B):- next_list(T,B),tail(A,T)."
            )
        )

    def test_recursive_arithmetic_counts_are_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        drop_k = next(
            task
            for task in altered["tasks"]
            if task["name"] == "synthesis-dropk"
        )
        drop_k["conversion"]["cases"]["proof_occurrences"] = 9
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "recursive-arithmetic counts drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_sorted_source_counts_are_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        sorted_task = next(
            task
            for task in altered["tasks"]
            if task["name"] == "synthesis-sorted"
        )
        sorted_task["conversion"]["cases"]["proof_occurrences"] = 9
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "sorted-extensional-geq counts drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_sorted_keeps_the_authored_two_element_base_clause(self) -> None:
        facts = {(2, 1)}
        self.assertEqual(
            SORTED_GENERATOR.evaluate_reference((100, 1), facts),
            (1, ()),
        )
        self.assertEqual(
            SORTED_GENERATOR.evaluate_reference((2, 1, 3), facts),
            (0, ()),
        )

    def test_sorted_geq_evidence_is_pair_indexed(self) -> None:
        types = SORTED_GENERATOR.render_types({(2, 1)})
        rules = SORTED_GENERATOR.render_rules({(2, 1)})
        self.assertIn("popper:sorted:geq:proof:2-ge-1", types)
        self.assertNotIn("popper:sorted:geq:proof:3-ge-1", types)
        self.assertIn("(rm-block geq-2-1", rules)
        self.assertNotIn("(rm-block geq-3-1", rules)

    def test_recursive_parity_is_structural_not_a_finite_fact_table(
        self,
    ) -> None:
        types = RECURSIVE_GENERATOR.render_types()
        rules = RECURSIVE_GENERATOR.render_rules()
        self.assertIn("(: popper:nat:even:step", types)
        self.assertIn("(rm-block even-step", rules)
        self.assertIn("(popper:nat:succ (popper:nat:succ $before))", rules)
        self.assertNotIn("even-100", rules)
        self.assertEqual(
            RECURSIVE_GENERATOR.render_nat(103).count("popper:nat:succ"),
            103,
        )

    def test_drop_k_reference_semantics_keep_decrement_direction(self) -> None:
        atom = RECURSIVE_GENERATOR.ground.Atom
        list_term = RECURSIVE_GENERATOR.ground.ListTerm
        source = tuple(atom(str(value)) for value in (10, 20, 30, 40))
        exact = atom(
            "f",
            (
                list_term(source),
                atom("2"),
                list_term(source[2:]),
            ),
        )
        wrong_direction = atom(
            "f",
            (
                list_term(source),
                atom("2"),
                list_term(source[:2]),
            ),
        )
        self.assertEqual(
            RECURSIVE_GENERATOR.proof_count("synthesis-dropk", exact), 1
        )
        self.assertEqual(
            RECURSIVE_GENERATOR.proof_count(
                "synthesis-dropk", wrong_direction
            ),
            0,
        )


if __name__ == "__main__":
    unittest.main()
