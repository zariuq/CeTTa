#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from petta_typecheck_v2_corpus import (  # noqa: E402
    CENSUS_PREFIX,
    WITNESS_SCHEMA,
    census_axis_inventory,
    census_static_source_inventory,
    native_v3_intake_inventory,
    parse_census_witness_payload,
    validate_axis_interactions,
    validate_census_catalog,
    validate_census_witness_catalog,
    validate_census_witness_hits,
    validate_census_witness_origins,
    split_census_records,
)


class SemanticCensusRecordTests(unittest.TestCase):
    def test_native_v3_intake_requires_an_axis_for_every_candidate(self) -> None:
        inventory, errors = native_v3_intake_inventory([
            {
                "id": "candidate-1",
                "classification": {
                    "class": "missing-axis",
                    "disposition": "native-v3-candidate",
                    "finding": "correspondence-witness",
                    "phase": "static",
                },
            },
        ])
        self.assertEqual(inventory["candidate_count"], 0)
        self.assertIn(
            "candidate-1: native-v3 candidate has no valid semantic axis",
            errors,
        )

    def test_native_v3_intake_covers_all_axes(self) -> None:
        cases = [
            {
                "id": f"candidate-{index}",
                "classification": {
                    "axis": axis,
                    "class": "axis-witness",
                    "disposition": "native-v3-candidate",
                    "finding": "correspondence-witness",
                    "phase": "static",
                },
            }
            for index, axis in enumerate(sorted({
                "shape-compatibility",
                "result-cardinality-grading",
                "source-evaluated-stage-evidence",
                "open-world-unknown-approximation",
            }))
        ]
        inventory, errors = native_v3_intake_inventory(cases)
        self.assertEqual(errors, [])
        self.assertEqual(inventory["candidate_count"], 4)
        self.assertEqual(
            set(inventory["by_axis"]),
            {
                "shape-compatibility",
                "result-cardinality-grading",
                "source-evaluated-stage-evidence",
                "open-world-unknown-approximation",
            },
        )

    def test_static_source_inventory_matches_the_live_catalog(self) -> None:
        inventory, errors = census_static_source_inventory(ROOT)
        self.assertEqual(errors, [])
        self.assertTrue(inventory)
        self.assertIn(
            "clause-slot-alias-preserved",
            inventory,
        )
        self.assertEqual(
            inventory["clause-slot-alias-preserved"],
            ["src/petta_search_machine.c"],
        )

    def test_static_source_inventory_recognizes_conditional_hits(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = pathlib.Path(temporary)
            source = repo / "src"
            source.mkdir()
            (source / "petta_typecheck_census.h").write_text(
                "#define CETTA_PETTA_TYPECHECK_CENSUS_EVENTS(X) \\\n"
                '  X(ADMINISTRATIVE_ALIAS_NONDET_PRESERVED, '
                '"administrative-alias-nondet-preserved", "oracle", '
                '"mechanism", "administrative-alias-nondeterminism-preservation", '
                '"result-cardinality-grading")\n',
                encoding="utf-8",
            )
            (source / "petta_typecheck.c").write_text(
                "CETTA_PETTA_TYPECHECK_CENSUS_HIT_IF(\n"
                "    preserves_nondeterminism(expression),\n"
                "    CETTA_PETTA_TYPECHECK_CENSUS_EVENT_"
                "ADMINISTRATIVE_ALIAS_NONDET_PRESERVED);\n",
                encoding="utf-8",
            )
            (source / "petta_search_machine.c").write_text(
                "", encoding="utf-8"
            )
            inventory, errors = census_static_source_inventory(repo)
        self.assertEqual(errors, [])
        self.assertEqual(
            inventory["administrative-alias-nondet-preserved"],
            ["src/petta_typecheck.c"],
        )

    def test_catalog_and_hit_are_removed_from_diagnostics(self) -> None:
        stderr = (
            "ordinary diagnostic\n"
            f"{CENSUS_PREFIX}\tcatalog\tactual-union-all\toracle\t"
            "rule\tconsistent-union-left\tshape-compatibility\n"
            f"{CENSUS_PREFIX}\thit\tactual-union-all\t"
            "petta_block_type_compatible_depth\n"
        )
        clean, catalog, hits = split_census_records(stderr)
        self.assertEqual(clean, "ordinary diagnostic\n")
        self.assertEqual(
            catalog,
            {
                "actual-union-all": {
                    "scope": "oracle",
                    "kind": "rule",
                    "mapping": "consistent-union-left",
                    "axis": "shape-compatibility",
                }
            },
        )
        self.assertEqual(
            hits,
            {"actual-union-all": {"petta_block_type_compatible_depth"}},
        )

    def test_malformed_record_fails_closed(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "malformed census record"):
            split_census_records(f"{CENSUS_PREFIX}\thit\n")

    def test_conflicting_catalog_fails_closed(self) -> None:
        stderr = (
            f"{CENSUS_PREFIX}\tcatalog\talias\toracle\trule\tleft\t"
            "shape-compatibility\n"
            f"{CENSUS_PREFIX}\tcatalog\talias\toracle\trule\tright\t"
            "shape-compatibility\n"
        )
        with self.assertRaisesRegex(RuntimeError, "inconsistent census"):
            split_census_records(stderr)

    def test_rule_mappings_must_name_authored_rules(self) -> None:
        catalog = {
            "good": {
                "scope": "oracle",
                "kind": "rule",
                "mapping": "consistent-union-left",
                "axis": "shape-compatibility",
            },
            "bad": {
                "scope": "oracle",
                "kind": "rule",
                "mapping": "missing-rule",
                "axis": "shape-compatibility",
            },
        }
        self.assertEqual(
            validate_census_catalog(catalog, {"consistent-union-left"}),
            ["bad: unknown langdef rule missing-rule"],
        )

    def test_named_mechanisms_are_accepted(self) -> None:
        catalog = {
            "alias": {
                "scope": "mechanism",
                "kind": "mechanism",
                "mapping": "caller-visible-substitution-preservation",
                "axis": "source-evaluated-stage-evidence",
            }
        }
        self.assertEqual(validate_census_catalog(catalog, set()), [])

    def test_unknown_semantic_axis_fails_closed(self) -> None:
        catalog = {
            "bad-axis": {
                "scope": "oracle",
                "kind": "mechanism",
                "mapping": "named-mechanism",
                "axis": "miscellaneous",
            }
        }
        self.assertEqual(
            validate_census_catalog(catalog, set()),
            ["bad-axis: unknown semantic axis miscellaneous"],
        )

    def test_witness_catalog_requires_exact_event_descriptor(self) -> None:
        catalog = {
            "event": {
                "scope": "oracle",
                "kind": "rule",
                "mapping": "consistent-union-left",
                "axis": "shape-compatibility",
            }
        }
        witness = {
            "event": {
                "case": "case-1",
                "scope": "oracle",
                "kind": "rule",
                "mapping": "different-rule",
                "axis": "shape-compatibility",
            }
        }
        self.assertEqual(
            validate_census_witness_catalog(catalog, witness),
            ["event: witness descriptor differs from the runtime catalog"],
        )

    def test_witness_must_emit_from_its_declared_case(self) -> None:
        witnesses = {
            "event": {
                "case": "case-1",
                "scope": "oracle",
                "kind": "rule",
                "mapping": "consistent-union-left",
                "axis": "shape-compatibility",
            }
        }
        self.assertEqual(
            validate_census_witness_hits({"event": ["case-2"]}, witnesses),
            ["event: declared witness case-1 did not emit the event"],
        )

    def test_witness_origin_must_emit_from_its_declared_case(self) -> None:
        witnesses = {
            "event": {
                "case": "case-1",
                "scope": "oracle",
                "kind": "rule",
                "mapping": "consistent-union-left",
                "axis": "shape-compatibility",
                "origin": "petta_block_type_compatible",
            }
        }
        self.assertEqual(
            validate_census_witness_origins(
                {"event": {"case-1": {"some_other_function"}}},
                witnesses,
            ),
            [
                "event: expected origin petta_block_type_compatible from "
                "case-1, observed some_other_function"
            ],
        )

    def test_witness_origin_must_be_exact(self) -> None:
        witnesses = {
            "event": {
                "case": "case-1",
                "scope": "oracle",
                "kind": "rule",
                "mapping": "consistent-union-left",
                "axis": "shape-compatibility",
                "origin": "petta_block_type_compatible",
            }
        }
        self.assertEqual(
            validate_census_witness_origins(
                {
                    "event": {
                        "case-1": {
                            "petta_block_type_compatible",
                            "petta_block_type_compatible_depth",
                        }
                    }
                },
                witnesses,
            ),
            [
                "event: expected origin petta_block_type_compatible from "
                "case-1, observed petta_block_type_compatible, "
                "petta_block_type_compatible_depth"
            ],
        )

    def test_axis_interaction_requires_both_declared_axes(self) -> None:
        catalog = {
            "shape": {
                "scope": "oracle",
                "kind": "rule",
                "mapping": "consistent-union-left",
                "axis": "shape-compatibility",
            },
            "grade": {
                "scope": "oracle",
                "kind": "rule",
                "mapping": "mode-fits-det-det",
                "axis": "result-cardinality-grading",
            },
        }
        errors, statuses = validate_axis_interactions(
            catalog,
            {"shape": ["case-1"]},
            [{
                "case": "case-1",
                "axes": [
                    "result-cardinality-grading",
                    "shape-compatibility",
                ],
                "events": ["shape", "grade"],
            }],
        )
        self.assertEqual(
            errors,
            ["case-1: axis interaction missed events grade",
             "case-1: axis interaction missed axes result-cardinality-grading"],
        )
        self.assertEqual(statuses[0]["verified"], False)

    def test_axis_inventory_marks_missing_axes_and_keeps_witness_origins(self) -> None:
        catalog = {
            "shape": {
                "scope": "oracle",
                "kind": "rule",
                "mapping": "consistent-union-left",
                "axis": "shape-compatibility",
            },
            "grade": {
                "scope": "oracle",
                "kind": "mechanism",
                "mapping": "mode-incompatibility",
                "axis": "result-cardinality-grading",
            },
        }
        witnesses = {
            "shape": {
                "case": "case-1",
                "origin": "shape_origin",
            },
            "grade": {
                "case": "case-2",
                "origin": "grade_origin",
            },
        }
        inventory, errors = census_axis_inventory(
            catalog, {"shape": ["case-1"]}, witnesses
        )
        self.assertEqual(
            errors,
            [
                "semantic axis open-world-unknown-approximation has no "
                "catalogued event",
                "semantic axis source-evaluated-stage-evidence has no "
                "catalogued event",
            ],
        )
        self.assertEqual(
            inventory["shape-compatibility"]["witnesses"],
            [{
                "event": "shape",
                "case": "case-1",
                "origin": "shape_origin",
            }],
        )
        self.assertEqual(
            inventory["result-cardinality-grading"]["unobserved_events"],
            ["grade"],
        )

    def test_witness_payload_requires_all_six_axis_pairs(self) -> None:
        payload = {
            "schema": WITNESS_SCHEMA,
            "witnesses": {
                "event": {
                    "case": "case-1",
                    "scope": "oracle",
                    "kind": "rule",
                    "mapping": "consistent-union-left",
                    "axis": "shape-compatibility",
                    "origin": "petta_block_type_compatible",
                }
            },
            "axis_interactions": [],
        }
        with self.assertRaisesRegex(RuntimeError, "cover each pair"):
            parse_census_witness_payload(payload, {"case-1"})

    def test_prior_witness_schema_is_rejected(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "unsupported schema"):
            parse_census_witness_payload(
                {
                    "schema": "cetta-petta-typecheck-v2-census-witnesses-v1",
                    "witnesses": {},
                    "axis_interactions": [],
                },
                {"case-1"},
            )

    def test_witness_payload_requires_origin(self) -> None:
        payload = {
            "schema": WITNESS_SCHEMA,
            "witnesses": {
                "event": {
                    "case": "case-1",
                    "scope": "oracle",
                    "kind": "rule",
                    "mapping": "consistent-union-left",
                    "axis": "shape-compatibility",
                }
            },
            "axis_interactions": [],
        }
        with self.assertRaisesRegex(RuntimeError, "event: census witness has no origin"):
            parse_census_witness_payload(payload, {"case-1"})


if __name__ == "__main__":
    unittest.main()
