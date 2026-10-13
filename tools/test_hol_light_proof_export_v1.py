"""Completeness and corruption controls for untrusted source graph accounting."""

from copy import deepcopy
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from hol_light_proof_export_v1 import inspect_closure


class SourceClosureAccountingTests(unittest.TestCase):
    def setUp(self):
        self.nodes = [
            {"id": 0, "rule": "REFL", "arguments": ["source term"]},
            {"id": 1, "rule": "TRANS", "arguments": [0, 0]},
            {"id": 2, "rule": "AXIOM", "arguments": ["source axiom"]},
            {"id": 3, "rule": "DEFINITION", "arguments": ["body", "c", "type"]},
        ]
        self.roots = [{"name": "SELECTED", "id": 1}]
        self.signature = {"types": [["bool", 0]], "constants": [["c", "type"]],
                          "definition_roots": [3], "axiom_roots": [2]}

    def inspect(self, nodes=None, roots=None, signature=None, requested=None):
        work = Path(__file__).resolve().parent.parent / "tmp"
        work.mkdir(exist_ok=True)
        with TemporaryDirectory(prefix="hol-light-closure-control-", dir=work) as directory:
            prefix = Path(directory) / "closure"
            for suffix, records in ((".nodes.jsonl", self.nodes if nodes is None else nodes),
                                    (".roots.jsonl", self.roots if roots is None else roots)):
                prefix.with_suffix(suffix).write_text("".join(json.dumps(record) + "\n"
                                                            for record in records))
            prefix.with_suffix(".signature.json").write_text(json.dumps(
                self.signature if signature is None else signature))
            return inspect_closure(prefix, ["SELECTED"] if requested is None else requested)

    def test_complete_shared_graph_and_declarations(self):
        accounting = self.inspect()
        self.assertEqual(accounting["nodes"], 4)
        self.assertEqual(accounting["premise_occurrences"], 2)
        self.assertEqual(accounting["axiom_nodes"], [2])
        self.assertEqual(accounting["definition_nodes"], [3])

    def test_empty_request_is_refused(self):
        with self.assertRaises(ValueError):
            self.inspect(requested=[], roots=[])

    def test_missing_requested_theorem_is_refused(self):
        with self.assertRaises(ValueError):
            self.inspect(roots=[])

    def test_unknown_theorem_label_is_refused(self):
        with self.assertRaises(ValueError):
            self.inspect(roots=[{"name": "OTHER", "id": 1}])

    def test_missing_root_node_is_refused(self):
        with self.assertRaises(ValueError):
            self.inspect(roots=[{"name": "SELECTED", "id": 99}])

    def test_missing_premise_is_refused(self):
        corrupted = deepcopy(self.nodes)
        corrupted[1]["arguments"] = [0, 99]
        with self.assertRaises(ValueError):
            self.inspect(nodes=corrupted)

    def test_duplicate_proof_identity_is_refused(self):
        with self.assertRaises(ValueError):
            self.inspect(nodes=self.nodes + [self.nodes[0]])

    def test_boolean_root_identity_is_refused(self):
        with self.assertRaises(ValueError):
            self.inspect(roots=[{"name": "SELECTED", "id": True}])

    def test_unknown_rule_is_refused(self):
        corrupted = deepcopy(self.nodes)
        corrupted[1]["rule"] = "TRUST_ME"
        with self.assertRaises(ValueError):
            self.inspect(nodes=corrupted)

    def test_unreachable_node_is_refused(self):
        with self.assertRaises(ValueError):
            self.inspect(nodes=self.nodes + [{"id": 4, "rule": "REFL", "arguments": ["unused"]}])

    def test_omitted_axiom_dependency_is_refused(self):
        corrupted = deepcopy(self.signature)
        corrupted["axiom_roots"] = []
        with self.assertRaises(ValueError):
            self.inspect(signature=corrupted)

    def test_omitted_definition_dependency_is_refused(self):
        corrupted = deepcopy(self.signature)
        corrupted["definition_roots"] = []
        with self.assertRaises(ValueError):
            self.inspect(signature=corrupted)

    def test_mismatched_dependency_kind_is_refused(self):
        corrupted = deepcopy(self.signature)
        corrupted["definition_roots"] = [2]
        with self.assertRaises(ValueError):
            self.inspect(signature=corrupted)

    def test_duplicate_declaration_name_is_refused(self):
        corrupted = deepcopy(self.signature)
        corrupted["constants"].append(["c", "other type"])
        with self.assertRaises(ValueError):
            self.inspect(signature=corrupted)


if __name__ == "__main__":
    unittest.main()
