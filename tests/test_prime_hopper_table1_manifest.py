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
MODULE_PATH = REPO / "scripts/check_prime_hopper_table1_manifest.py"
SPEC = importlib.util.spec_from_file_location(
    "check_prime_hopper_table1_manifest", MODULE_PATH
)
assert SPEC is not None and SPEC.loader is not None
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)
GENERATOR_PATH = REPO / "scripts/generate_prime_hopper_first_order.py"
GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_hopper_first_order", GENERATOR_PATH
)
assert GENERATOR_SPEC is not None and GENERATOR_SPEC.loader is not None
GENERATOR = importlib.util.module_from_spec(GENERATOR_SPEC)
sys.modules[GENERATOR_SPEC.name] = GENERATOR
GENERATOR_SPEC.loader.exec_module(GENERATOR)
HIGHER_GENERATOR_PATH = (
    REPO / "scripts/generate_prime_hopper_higher_order.py"
)
HIGHER_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_hopper_higher_order", HIGHER_GENERATOR_PATH
)
assert (
    HIGHER_GENERATOR_SPEC is not None
    and HIGHER_GENERATOR_SPEC.loader is not None
)
HIGHER_GENERATOR = importlib.util.module_from_spec(HIGHER_GENERATOR_SPEC)
sys.modules[HIGHER_GENERATOR_SPEC.name] = HIGHER_GENERATOR
HIGHER_GENERATOR_SPEC.loader.exec_module(HIGHER_GENERATOR)
ITERATIVE_GENERATOR_PATH = (
    REPO / "scripts/generate_prime_hopper_additional_iterative.py"
)
ITERATIVE_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_hopper_additional_iterative", ITERATIVE_GENERATOR_PATH
)
assert (
    ITERATIVE_GENERATOR_SPEC is not None
    and ITERATIVE_GENERATOR_SPEC.loader is not None
)
ITERATIVE_GENERATOR = importlib.util.module_from_spec(
    ITERATIVE_GENERATOR_SPEC
)
sys.modules[ITERATIVE_GENERATOR_SPEC.name] = ITERATIVE_GENERATOR
ITERATIVE_GENERATOR_SPEC.loader.exec_module(ITERATIVE_GENERATOR)
STRUCTURAL_GENERATOR_PATH = (
    REPO / "scripts/generate_prime_hopper_structural_list.py"
)
STRUCTURAL_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_hopper_structural_list", STRUCTURAL_GENERATOR_PATH
)
assert (
    STRUCTURAL_GENERATOR_SPEC is not None
    and STRUCTURAL_GENERATOR_SPEC.loader is not None
)
STRUCTURAL_GENERATOR = importlib.util.module_from_spec(
    STRUCTURAL_GENERATOR_SPEC
)
sys.modules[STRUCTURAL_GENERATOR_SPEC.name] = STRUCTURAL_GENERATOR
STRUCTURAL_GENERATOR_SPEC.loader.exec_module(STRUCTURAL_GENERATOR)
RELATIONAL_GENERATOR_PATH = (
    REPO / "scripts/generate_prime_hopper_relational_recursion.py"
)
RELATIONAL_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_hopper_relational_recursion",
    RELATIONAL_GENERATOR_PATH,
)
assert (
    RELATIONAL_GENERATOR_SPEC is not None
    and RELATIONAL_GENERATOR_SPEC.loader is not None
)
RELATIONAL_GENERATOR = importlib.util.module_from_spec(
    RELATIONAL_GENERATOR_SPEC
)
sys.modules[RELATIONAL_GENERATOR_SPEC.name] = RELATIONAL_GENERATOR
RELATIONAL_GENERATOR_SPEC.loader.exec_module(RELATIONAL_GENERATOR)
TREE_GENERATOR_PATH = (
    REPO / "scripts/generate_prime_hopper_tree_relations.py"
)
TREE_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_hopper_tree_relations",
    TREE_GENERATOR_PATH,
)
assert (
    TREE_GENERATOR_SPEC is not None
    and TREE_GENERATOR_SPEC.loader is not None
)
TREE_GENERATOR = importlib.util.module_from_spec(TREE_GENERATOR_SPEC)
sys.modules[TREE_GENERATOR_SPEC.name] = TREE_GENERATOR
TREE_GENERATOR_SPEC.loader.exec_module(TREE_GENERATOR)
MANIFEST_PATH = (
    REPO / "benchmarks/prime/ilp/hopper_table1_manifest.json"
)


class HopperTable1ManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = json.loads(
            MANIFEST_PATH.read_text(encoding="utf-8")
        )

    def test_checked_manifest_accounts_for_every_task(self) -> None:
        self.assertEqual(
            CHECKER.validate_manifest(self.manifest, REPO), (22, 0)
        )

    def test_first_order_qualification_counts_are_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        member = next(
            task for task in altered["tasks"] if task["name"] == "member"
        )
        member["conversion"]["cases"]["proof_occurrences"] = 1
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "qualification counts drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_sorted_source_disagreement_cannot_be_hidden(self) -> None:
        altered = copy.deepcopy(self.manifest)
        sorted_task = next(
            task for task in altered["tasks"] if task["name"] == "sorted"
        )
        sorted_task["conversion"].pop("source_disagreement")
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "source disagreement inventory drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_sorted_erratum_status_cannot_be_flattened(self) -> None:
        altered = copy.deepcopy(self.manifest)
        sorted_task = next(
            task for task in altered["tasks"] if task["name"] == "sorted"
        )
        sorted_task["conversion"]["status"] = "qualified"
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "source erratum must remain explicit"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_sorted_computed_confusion_matrix_is_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        sorted_task = next(
            task for task in altered["tasks"] if task["name"] == "sorted"
        )
        sorted_task["conversion"]["source_erratum"]["computed"]["fp"] = 0
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "source erratum inventory drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_higher_order_variant_comparison_is_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        drop_last = next(
            task for task in altered["tasks"] if task["name"] == "dropLast"
        )
        drop_last["conversion"]["variant_comparison"]["program"] = (
            "fo-ho-ho-opt-identical"
        )
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "source variant comparison drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_native_encryption_learning_space_is_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        encryption = next(
            task for task in altered["tasks"]
            if task["name"] == "encryption"
        )
        encryption["conversion"]["native_learning"][
            "hypothesis_space"
        ]["typed_candidates"] = 1
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "native learning contract drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_iterative_qualification_counts_are_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        rotate = next(
            task for task in altered["tasks"] if task["name"] == "rotateN"
        )
        rotate["conversion"]["cases"]["proof_occurrences"] = 5
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "qualification counts drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_iterative_variant_comparison_is_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        add_n = next(
            task for task in altered["tasks"] if task["name"] == "addN"
        )
        add_n["conversion"]["variant_comparison"]["program"] = (
            "ho-ho-opt-identical;fo-has-no-authored-best-program"
        )
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "source variant comparison drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_structural_list_qualification_counts_are_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        palindrome = next(
            task
            for task in altered["tasks"]
            if task["name"] == "isPalindrome"
        )
        palindrome["conversion"]["cases"]["proof_occurrences"] = 10
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "qualification counts drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_structural_list_variant_comparison_is_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        last_half = next(
            task for task in altered["tasks"] if task["name"] == "lastHalf"
        )
        last_half["conversion"]["variant_comparison"]["program"] = (
            "fo-ho-ho-opt-identical"
        )
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "source variant comparison drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_pending_task_cannot_hide_its_reason(self) -> None:
        altered = copy.deepcopy(self.manifest)
        pending = next(
            task for task in altered["tasks"] if task["name"] == "sorted"
        )
        pending["conversion"]["status"] = "pending"
        pending["conversion"]["coverage"] = "none"
        pending["conversion"].pop("reason", None)
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "pending conversion has no reason"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_relational_recursion_counts_are_pinned(self) -> None:
        altered = copy.deepcopy(self.manifest)
        product = next(
            task
            for task in altered["tasks"]
            if task["name"] == "mulFromSuc"
        )
        product["conversion"]["cases"]["proof_occurrences"] = 9
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "qualification counts drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_qualified_task_requires_a_real_fixture_and_oracle(self) -> None:
        altered = copy.deepcopy(self.manifest)
        altered["tasks"][0]["conversion"] = {
            "status": "qualified",
            "coverage": "authored-ho-program-and-examples",
            "fixture": "examples/prime/not-a-qualification.metta",
        }
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "fixture or oracle is missing"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_physical_double_space_does_not_change_logical_variant(self) -> None:
        find_duplicate = next(
            task
            for task in self.manifest["tasks"]
            if task["name"] == "findDup"
        )
        self.assertEqual(
            find_duplicate["variants"]["ho-opt"]["path"], "HO  OPT"
        )

    def test_header_example_count_anomalies_are_not_silently_repaired(
        self,
    ) -> None:
        altered = copy.deepcopy(self.manifest)
        find_duplicate = next(
            task for task in altered["tasks"] if task["name"] == "findDup"
        )
        find_duplicate["variants"]["ho"]["best_program"]["metrics"]["tp"] = 3
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "anomaly inventory drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_best_program_retains_clauses_and_metrics(self) -> None:
        parsed = CHECKER.parse_best_program(
            [
                "% BEST PROG 5:",
                "%f(A):-all_a(A).",
                "%all_p_a(A):-even(A).",
                "% Precision:1.00, Recall:1.00, "
                "TP:1, FN:0, TN:1, FP:0",
            ]
        )
        self.assertEqual(
            parsed,
            {
                "label": "BEST PROG 5:",
                "clauses": [
                    "f(A):-all_a(A).",
                    "all_p_a(A):-even(A).",
                ],
                "metrics": {
                    "precision": "1.00",
                    "recall": "1.00",
                    "tp": 1,
                    "fn": 0,
                    "tn": 1,
                    "fp": 0,
                },
            },
        )

    def test_higher_order_declaration_keeps_argument_arities(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "bk.pl"
            path.write_text(
                "higher_order_predicate(fold, [3], 2, 1).\n"
                "higher_order_predicate(map, [2], 2, 3).\n",
                encoding="utf-8",
            )
            self.assertEqual(
                CHECKER.parse_higher_order(path),
                [
                    {
                        "name": "fold",
                        "argument_arities": [3],
                        "ordinary_arguments": 2,
                        "instances": 1,
                    },
                    {
                        "name": "map",
                        "argument_arities": [2],
                        "ordinary_arguments": 2,
                        "instances": 3,
                    },
                ],
            )

    def test_ground_parser_preserves_nested_proper_lists(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "exs.pl"
            path.write_text(
                "pos(f([[a,b],[c]], [a,b])).\n"
                "neg(f([[a]], [b])).\n",
                encoding="utf-8",
            )
            examples = GENERATOR.parse_examples(path)
        self.assertEqual(len(examples), 2)
        outer = examples[0][1].args[0]
        self.assertIsInstance(outer, GENERATOR.ListTerm)
        self.assertEqual(len(outer.items), 2)
        self.assertIsInstance(outer.items[0], GENERATOR.ListTerm)

    def test_ground_parser_unquotes_prolog_character_atoms(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "exs.pl"
            path.write_text(
                "pos(f(['w','h'],['u','f'])).\n"
                "neg(f(['a'],['a'])).\n",
                encoding="utf-8",
            )
            examples = GENERATOR.parse_examples(path)
        self.assertEqual(
            GENERATOR.list_items(examples[0][1].args[0]), ("w", "h")
        )

    def test_proof_counts_preserve_multiplicity_and_source_conflict(self) -> None:
        repeated_member = GENERATOR.Atom(
            "f",
            (
                GENERATOR.ListTerm(
                    (
                        GENERATOR.Atom("7"),
                        GENERATOR.Atom("7"),
                        GENERATOR.Atom("7"),
                    )
                ),
                GENERATOR.Atom("7"),
            ),
        )
        sorted_zeroes = GENERATOR.Atom(
            "f",
            (
                GENERATOR.ListTerm(
                    tuple(GENERATOR.Atom("0") for _ in range(4))
                ),
            ),
        )
        self.assertEqual(GENERATOR.proof_count("member", repeated_member), 3)
        self.assertEqual(GENERATOR.proof_count("sorted", sorted_zeroes), 1)

    def test_higher_order_instances_are_typed_relations_not_kernel_opcodes(
        self,
    ) -> None:
        types = GENERATOR.render_types({})
        rules = GENERATOR.render_rules()
        self.assertIn("(: hopper:atom-list:tail", types)
        self.assertIn("(: hopper:atom-list:head", types)
        self.assertIn("hopper:atom-list:tail hopper:nat:pred", rules)
        self.assertIn("hopper:atom-list:head $values $answer", rules)
        self.assertNotIn("(rel:list:tail hopper:atom)", rules)
        self.assertNotIn("(rel:list:head hopper:atom)", rules)

    def test_higher_order_pair_uses_generic_map_rel_and_keeps_branches(
        self,
    ) -> None:
        types = HIGHER_GENERATOR.render_types()
        rules = HIGHER_GENERATOR.render_rules()
        self.assertIn("(map-rel (list hopper:atom) (list hopper:atom)", types)
        self.assertIn("(map-rel hopper:char hopper:char", types)
        self.assertIn("(rm-block map-rel-cons", rules)
        self.assertIn("hopper:cipher:branch-left", rules)
        self.assertIn("hopper:cipher:branch-right", rules)

    def test_higher_order_reference_semantics_are_exact(self) -> None:
        drop_last = GENERATOR.Atom(
            "f",
            (
                GENERATOR.ListTerm(
                    (
                        GENERATOR.ListTerm(
                            (GENERATOR.Atom("a"), GENERATOR.Atom("b"))
                        ),
                    )
                ),
                GENERATOR.ListTerm(
                    (GENERATOR.ListTerm((GENERATOR.Atom("a"),)),)
                ),
            ),
        )
        encryption = GENERATOR.Atom(
            "f",
            (
                GENERATOR.ListTerm(
                    (GENERATOR.Atom("a"), GENERATOR.Atom("b"))
                ),
                GENERATOR.ListTerm(
                    (GENERATOR.Atom("y"), GENERATOR.Atom("z"))
                ),
            ),
        )
        self.assertEqual(HIGHER_GENERATOR.proof_count("dropLast", drop_last), 1)
        self.assertEqual(
            HIGHER_GENERATOR.proof_count("encryption", encryption), 1
        )

    def test_iterative_trio_uses_generic_evidence_combinators(self) -> None:
        types = ITERATIVE_GENERATOR.render_types()
        rules = ITERATIVE_GENERATOR.render_rules()
        self.assertIn("(rel:iterate", types)
        self.assertIn("(map-rel", types)
        self.assertIn("(rel:list:snoc", types)
        self.assertIn("(rm-block iterate-step", rules)
        self.assertIn("(rm-block map-rel-cons", rules)
        self.assertIn("(rm-block snoc-cons", rules)

    def test_iterative_reference_semantics_are_exact(self) -> None:
        atom = GENERATOR.Atom
        list_term = GENERATOR.ListTerm
        rotate = atom(
            "f",
            (
                atom("2"),
                list_term((atom("1"), atom("2"), atom("3"))),
                list_term((atom("3"), atom("1"), atom("2"))),
            ),
        )
        drop_last_k = atom(
            "f",
            (
                atom("1"),
                list_term(
                    (
                        list_term((atom("a"), atom("b"))),
                        list_term((atom("c"), atom("d"))),
                    )
                ),
                list_term(
                    (list_term((atom("a"),)), list_term((atom("c"),)))
                ),
            ),
        )
        add_n = atom(
            "f",
            (
                atom("2"),
                list_term((atom("1"), atom("3"))),
                list_term((atom("3"), atom("5"))),
            ),
        )
        self.assertEqual(
            ITERATIVE_GENERATOR.proof_count("rotateN", rotate), 1
        )
        self.assertEqual(
            ITERATIVE_GENERATOR.proof_count("dropLastK", drop_last_k), 1
        )
        self.assertEqual(
            ITERATIVE_GENERATOR.proof_count("addN", add_n), 1
        )

    def test_structural_list_trio_uses_generic_relations(self) -> None:
        shared_types = (
            REPO / "lib/ilp/prime_relational_combinators_types.metta"
        ).read_text(encoding="utf-8")
        rules = STRUCTURAL_GENERATOR.render_rules()
        self.assertIn("(: rel:list:front", shared_types)
        self.assertIn("(: rel:list:last", shared_types)
        self.assertIn("(: rel:either", shared_types)
        self.assertIn("(rm-block last-half-cons", rules)
        self.assertIn("(rm-block either-left", rules)
        self.assertIn("(rm-block either-right", rules)
        self.assertIn("(rm-block palindrome-cons", rules)
        kernel = (REPO / "src/prime_native_calculus.c").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("last-half", kernel)
        self.assertNotIn("of-one-and-two", kernel)
        self.assertNotIn("palindrome", kernel)

    def test_structural_list_reference_semantics_are_exact(self) -> None:
        atom = GENERATOR.Atom
        list_term = GENERATOR.ListTerm
        last_half = atom(
            "f",
            (
                list_term(
                    tuple(atom(str(value)) for value in (1, 2, 3, 4, 5))
                ),
                list_term((atom("4"), atom("5"))),
            ),
        )
        one_and_two = atom(
            "f",
            (list_term((atom("1"), atom("2"), atom("1"))),),
        )
        palindrome = atom(
            "f",
            (list_term((atom("1"), atom("2"), atom("1"))),),
        )
        not_palindrome = atom(
            "f",
            (list_term((atom("1"), atom("2"))),),
        )
        self.assertEqual(
            STRUCTURAL_GENERATOR.proof_count("lastHalf", last_half), 1
        )
        self.assertEqual(
            STRUCTURAL_GENERATOR.proof_count("of1And2", one_and_two), 1
        )
        self.assertEqual(
            STRUCTURAL_GENERATOR.proof_count(
                "isPalindrome", palindrome
            ),
            1,
        )
        self.assertEqual(
            STRUCTURAL_GENERATOR.proof_count(
                "isPalindrome", not_palindrome
            ),
            0,
        )

    def test_relational_recursion_uses_explicit_indexed_evidence(self) -> None:
        shared_types = (
            REPO / "lib/ilp/prime_relational_combinators_types.metta"
        ).read_text(encoding="utf-8")
        task_types = RELATIONAL_GENERATOR.render_types()
        rules = RELATIONAL_GENERATOR.render_rules()
        self.assertIn("(: rel:iterate-with", shared_types)
        self.assertIn("(: rel:list:repeat", shared_types)
        self.assertIn("(: rel:unfold", shared_types)
        self.assertIn("hopper:nat:pred-clamped", task_types)
        self.assertIn("(rm-block iterate-with-step", rules)
        self.assertIn("(rm-block list-repeat-step", rules)
        self.assertIn("(rm-block unfold-step", rules)
        self.assertNotIn("closure", task_types.lower())
        kernel = (REPO / "src/prime_native_calculus.c").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("repeat-n", kernel)
        self.assertNotIn("all-seq-n", kernel)
        self.assertNotIn("first-half", kernel)
        self.assertNotIn("mul-from-suc", kernel)

    def test_relational_recursion_reference_semantics_are_exact(self) -> None:
        atom = GENERATOR.Atom
        list_term = GENERATOR.ListTerm
        repeat = atom(
            "f",
            (
                list_term((atom("1"), atom("2"))),
                atom("3"),
                list_term(
                    tuple(
                        list_term((atom("1"), atom("2")))
                        for _ in range(3)
                    )
                ),
            ),
        )
        all_sequences = atom(
            "f",
            (
                atom("3"),
                list_term(
                    (
                        list_term((atom("1"),)),
                        list_term((atom("1"), atom("2"))),
                        list_term((atom("1"), atom("2"), atom("3"))),
                    )
                ),
            ),
        )
        first_half = atom(
            "f",
            (
                list_term((atom("1"), atom("2"), atom("3"))),
                list_term((atom("1"), atom("2"))),
            ),
        )
        source_overlap = atom(
            "f",
            (
                list_term((atom("1"), atom("2"), atom("3"))),
                list_term((atom("1"), atom("2"), atom("3"))),
            ),
        )
        product = atom("f", (atom("6"), atom("6"), atom("36")))
        self.assertEqual(
            RELATIONAL_GENERATOR.proof_count("repeatN", repeat), 1
        )
        self.assertEqual(
            RELATIONAL_GENERATOR.proof_count("allSeqN", all_sequences), 1
        )
        self.assertEqual(
            RELATIONAL_GENERATOR.proof_count("firstHalf", first_half), 1
        )
        self.assertEqual(
            RELATIONAL_GENERATOR.proof_count("firstHalf", source_overlap), 1
        )
        self.assertEqual(
            RELATIONAL_GENERATOR.proof_count("mulFromSuc", product), 1
        )

    def test_tree_relation_counts_pin_duplicate_proof_occurrences(self) -> None:
        altered = copy.deepcopy(self.manifest)
        branch = next(
            task for task in altered["tasks"] if task["name"] == "isBranch"
        )
        branch["conversion"]["cases"]["proof_occurrences"] = 42
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "qualification counts drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_tree_relations_use_generic_proof_programs_at_raw_boundary(
        self,
    ) -> None:
        types = TREE_GENERATOR.render_types()
        rules = TREE_GENERATOR.render_rules()
        self.assertIn("checked at the raw boundary", types)
        self.assertIn("(rel:fold hopper:tree hopper:nat", rules)
        self.assertIn("(rel:any hopper:tree", rules)
        self.assertIn("(rm-block tree-view", rules)
        kernel = (REPO / "src/prime_native_calculus.c").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("hopper:is-branch", kernel)
        self.assertNotIn("hopper:is-subtree", kernel)

    def test_tree_reference_semantics_preserve_duplicate_paths(self) -> None:
        leaf = (2, ())
        tree = (1, (leaf, leaf))
        self.assertEqual(
            TREE_GENERATOR.branch_paths(tree), ((1, 2), (1, 2))
        )
        self.assertEqual(
            sum(
                path == (1, 2)
                for path in TREE_GENERATOR.branch_paths(tree)
            ),
            2,
        )


if __name__ == "__main__":
    unittest.main()
