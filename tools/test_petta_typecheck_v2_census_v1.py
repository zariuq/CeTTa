#!/usr/bin/env python3

from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from generate_petta_typecheck_v2_census_v1 import (  # noqa: E402
    GenerationError,
    WITNESS_SCHEMA,
    catalog_from_payload,
    render_header,
)


class CensusCatalogGenerationTests(unittest.TestCase):
    def test_live_ledger_has_one_unique_tag_per_event(self) -> None:
        payload = json.loads(
            (ROOT / "tests/petta/typecheck_v2_semantic_witnesses.json").read_text(
                encoding="utf-8"
            )
        )
        events = catalog_from_payload(payload)
        self.assertEqual(len(events), 95)
        self.assertEqual(len({event.tag for event in events}), len(events))
        rendered = render_header(events)
        self.assertIn("CETTA_PETTA_TYPECHECK_CENSUS_EVENTS", rendered)
        for event in events:
            self.assertIn(f'"{event.name}"', rendered)

    def test_tag_collision_fails_closed(self) -> None:
        descriptor = {
            "scope": "oracle",
            "kind": "mechanism",
            "mapping": "example-mechanism",
            "axis": "shape-compatibility",
        }
        payload = {
            "schema": WITNESS_SCHEMA,
            "witnesses": {
                "alpha-beta": descriptor,
                "alpha_beta": descriptor,
            },
        }
        with self.assertRaisesRegex(GenerationError, "duplicate C event tag"):
            catalog_from_payload(payload)

    def test_unknown_axis_fails_closed(self) -> None:
        payload = {
            "schema": WITNESS_SCHEMA,
            "witnesses": {
                "example": {
                    "scope": "oracle",
                    "kind": "mechanism",
                    "mapping": "example-mechanism",
                    "axis": "unclassified",
                },
            },
        }
        with self.assertRaisesRegex(GenerationError, "unknown semantic axis"):
            catalog_from_payload(payload)


if __name__ == "__main__":
    unittest.main()
