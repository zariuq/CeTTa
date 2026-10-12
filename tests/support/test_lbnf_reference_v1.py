#!/usr/bin/env python3
"""Controls for reference receipts, diagnostic causes, and frozen fixtures."""

import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[2] / "scripts" / "qualify_lbnf_reference_v1.py"
SPEC = importlib.util.spec_from_file_location("lbnf_reference", SOURCE)
REFERENCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REFERENCE)


class ReferenceControls(unittest.TestCase):
    def test_tree_headings(self):
        for heading in ("[Linearized Tree]", "[Linearized tree]"):
            output = "[Abstract Syntax]\n\nNode (Leaf 1)\n\n" + heading + "\ntext"
            self.assertEqual(REFERENCE.abstract_tree(output), "Node (Leaf 1)")

    def test_heading_inside_token_is_data(self):
        tree = 'Node "[Linearized Tree]"'
        output = "[Abstract Syntax]\n" + tree + "\n[Linearized Tree]\ntext"
        self.assertEqual(REFERENCE.abstract_tree(output), tree)

    def test_missing_tree_is_not_acceptance(self):
        self.assertIsNone(REFERENCE.abstract_tree("Syntax error"))
        result = {"status": 0, "stdout": "no tree"}
        self.assertEqual(REFERENCE.input_expectation(Path("good01.in"), result),
                         ("accept", False))

    def test_crash_is_not_rejection(self):
        for status in (-11, "timeout", 0):
            result = {"status": status, "stdout": ""}
            self.assertEqual(REFERENCE.input_expectation(Path("bad01.in"), result),
                             ("reject", False))
        result = {"status": 1, "stdout": ""}
        self.assertEqual(REFERENCE.input_expectation(Path("bad01.in"), result),
                         ("reject", True))

    def test_unclassified_input_stays_unclassified(self):
        with patch.object(Path, "exists", return_value=False):
            result = {"status": 1, "stdout": ""}
            self.assertEqual(REFERENCE.input_expectation(Path("source.rho"), result),
                             (None, None))

    def test_diagnostic_normalization_preserves_cause(self):
        root = Path("reference-checkout").resolve()
        expected = "fail-lbnf/x.cf:3:7: Duplicate token\nAborting."
        actual = str(root / "testing") + "/fail-lbnf/x.cf:3:7: Duplicate token\n\nAborting."
        self.assertEqual(REFERENCE.normalized_diagnostic(expected, root),
                         REFERENCE.normalized_diagnostic(actual, root))
        self.assertNotEqual(REFERENCE.normalized_diagnostic(expected, root),
                            REFERENCE.normalized_diagnostic(actual.replace("Duplicate", "Nullable"), root))

    def test_frozen_additions_are_separate(self):
        a = {"path": "a.cf", "sha256": "a"}
        b = {"path": "b.module", "sha256": "b"}
        self.assertEqual(REFERENCE.frozen_fixture_additions([b, a], [a]), [b])
        self.assertEqual(REFERENCE.frozen_fixture_additions([a], [a]), [])

    def test_frozen_change_is_not_an_addition(self):
        frozen = [{"path": "a.cf", "sha256": "a"}]
        with self.assertRaisesRegex(RuntimeError, "changed or disappeared"):
            REFERENCE.frozen_fixture_additions([{"path": "a.cf", "sha256": "b"}], frozen)
        with self.assertRaisesRegex(RuntimeError, "changed or disappeared"):
            REFERENCE.frozen_fixture_additions([], frozen)

    def test_explicit_input_revision_keeps_original(self):
        original = {"path": "a.module", "sha256": "a", "bytes": 2,
                    "family": "module-input"}
        replacement = dict(original, sha256="b", bytes=3)
        revision = {"format": "LbnfInputRevisionV1", "original": original,
                    "replacement": replacement}
        self.assertEqual(REFERENCE.revised_fixture_inventory([original], [revision]),
                         [replacement])
        self.assertEqual(original["sha256"], "a")
        self.assertEqual(REFERENCE.revised_fixture_inventory([original], []), [original])

    def test_revision_cannot_change_identity_or_grammar(self):
        original = {"path": "a.module", "sha256": "a", "family": "module-input"}
        for replacement in (dict(original, sha256="b", path="b.module"),
                            dict(original, sha256="b", family="rho-input"), original):
            with self.assertRaises(RuntimeError):
                REFERENCE.revised_fixture_inventory([original], [{
                    "format": "LbnfInputRevisionV1", "original": original,
                    "replacement": replacement}])
        grammar = dict(original, path="a.cf", family="bnfc-grammar")
        with self.assertRaises(RuntimeError):
            REFERENCE.revised_fixture_inventory([grammar], [{
                "format": "LbnfInputRevisionV1", "original": grammar,
                "replacement": dict(grammar, sha256="b")}])

    def test_revision_is_unique_and_bound_to_original(self):
        original = {"path": "a.module", "sha256": "a", "family": "module-input"}
        revision = {"format": "LbnfInputRevisionV1", "original": original,
                    "replacement": dict(original, sha256="b")}
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            REFERENCE.revised_fixture_inventory([original], [revision, revision])
        with self.assertRaisesRegex(RuntimeError, "frozen fixture"):
            REFERENCE.revised_fixture_inventory([dict(original, sha256="c")], [revision])
        with self.assertRaisesRegex(RuntimeError, "unknown"):
            REFERENCE.revised_fixture_inventory([original], [dict(revision, format="Other")])


if __name__ == "__main__":
    unittest.main()
