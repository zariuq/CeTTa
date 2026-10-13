"""Corruption controls for complete receiving receipts, separate from logic."""

from copy import deepcopy
import json
import unittest

from hol_light_receive_export_v1 import check_readout


class ReceivingCoverageTests(unittest.TestCase):
    def setUp(self):
        self.accounting = {
            "roots": [{"name": "first", "id": 17}, {"name": "second", "id": 25}],
            "nodes": 6, "premise_occurrences": 7,
        }
        self.outputs = [
            {"checkedRoots": [
                {"name": "first", "sourceId": 17, "cacheIndex": 4,
                 "hypotheses": 0, "objectAxioms": 1},
                {"name": "second", "sourceId": 25, "cacheIndex": 5,
                 "hypotheses": 1, "objectAxioms": 0},
            ]},
            {"status": "Checked", "events": 5, "theorems": 6,
             "retainedReceipts": 5, "sourceNodes": 6,
             "sourcePremiseOccurrences": 7, "objectAxioms": 1},
        ]

    def check(self, outputs=None):
        data = self.outputs if outputs is None else outputs
        return check_readout(self.accounting, 5,
                             "\n".join(json.dumps(value) for value in data))

    def test_complete_receiving_keeps_hypotheses_and_axioms_visible(self):
        result = self.check()
        self.assertEqual(result["checkedRoots"][1]["hypotheses"], 1)
        self.assertEqual(result["summary"]["retainedReceipts"], 5)

    def test_empty_or_summary_only_output_is_refused(self):
        for data in ([], [self.outputs[1]]):
            with self.subTest(data=data), self.assertRaises(ValueError):
                self.check(data)

    def test_every_accounting_field_must_be_complete(self):
        for field in ("events", "theorems", "retainedReceipts", "sourceNodes",
                      "sourcePremiseOccurrences"):
            data = deepcopy(self.outputs)
            data[1][field] -= 1
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.check(data)

    def test_missing_duplicate_unknown_and_reordered_roots_are_refused(self):
        roots = self.outputs[0]["checkedRoots"]
        unknown = deepcopy(roots)
        unknown[1]["sourceId"] = 999
        for changed in ([], roots[:1], [roots[0], roots[0]], roots[::-1], unknown):
            data = deepcopy(self.outputs)
            data[0]["checkedRoots"] = changed
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                self.check(data)

    def test_booleans_are_not_counts(self):
        data = deepcopy(self.outputs)
        data[1]["objectAxioms"] = True
        with self.assertRaises(ValueError):
            self.check(data)

    def test_missing_receipt_fields_and_outside_cache_are_refused(self):
        for field in ("cacheIndex", "hypotheses", "objectAxioms"):
            data = deepcopy(self.outputs)
            del data[0]["checkedRoots"][0][field]
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.check(data)
        data = deepcopy(self.outputs)
        data[0]["checkedRoots"][0]["cacheIndex"] = 6
        with self.assertRaises(ValueError):
            self.check(data)

    def test_hidden_root_axiom_accounting_is_refused(self):
        data = deepcopy(self.outputs)
        data[1]["objectAxioms"] = 0
        with self.assertRaises(ValueError):
            self.check(data)

    def test_extra_or_nonchecked_outputs_are_refused(self):
        data = deepcopy(self.outputs)
        data[1]["status"] = "Assumed"
        for changed in (data, self.outputs + [self.outputs[1]]):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                self.check(changed)


if __name__ == "__main__":
    unittest.main()
