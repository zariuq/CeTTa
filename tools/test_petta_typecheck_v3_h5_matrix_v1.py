#!/usr/bin/env python3

from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from generate_petta_typecheck_v3_h5_matrix_v1 import build_matrix  # noqa: E402


class TypecheckV3H5MatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.intake = json.loads(
            (ROOT / "langdef/petta/generated/typecheck_v3_intake_v1.json").read_text(
                encoding="utf-8"
            )
        )

    def test_matrix_is_candidate_complete_and_migration_complete(self) -> None:
        matrix = build_matrix(self.intake)
        self.assertEqual(matrix["candidate_count"], 51)
        self.assertEqual(matrix["class_count"], 39)
        self.assertEqual(matrix["core_mapped_count"], 51)
        self.assertEqual(matrix["source_executable_count"], 51)
        self.assertEqual(matrix["divergence_count"], 8)
        rows = matrix["candidates"]
        self.assertEqual(len({row["id"] for row in rows}), 51)
        self.assertTrue(all(row["core_witnesses"] for row in rows))
        self.assertTrue(all(
            (row["migration"] is not None) == row["diverges_from_v2"]
            for row in rows
        ))

    def test_unknown_candidate_fails_closed(self) -> None:
        changed = json.loads(json.dumps(self.intake))
        changed["native_v3_intake"]["candidates"][0]["id"] = "unknown|strict"
        with self.assertRaisesRegex(ValueError, "target coverage differs"):
            build_matrix(changed)


if __name__ == "__main__":
    unittest.main()
