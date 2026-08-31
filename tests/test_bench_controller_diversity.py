#!/usr/bin/env python3

from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import bench_controller_diversity as diversity  # noqa: E402
import check_search_controller as controller_check  # noqa: E402
import petta_machine_stats as machine_stats  # noqa: E402


class ControllerDiversityManifestTests(unittest.TestCase):
    def test_exact_selection_receipt_survives_aggregation(self) -> None:
        receipt = machine_stats.parse_controller_stats_line(
            "CETTA_CONTROLLER_STATS requested=fifo selection=ratio:4 "
            "active=fifo storage=shared-terms-owned-state "
            "expansions=2 max_frontier=3 "
            "max_frontier_terms_shared_bytes=4096 "
            "max_frontier_bindings_exclusive_bytes=512"
        )
        aggregate = diversity.aggregate_controller_stats([receipt])
        self.assertEqual(aggregate["active_fifo"], 1)
        self.assertEqual(
            aggregate["storage_shared_terms_owned_state"], 1
        )
        self.assertEqual(aggregate["expansions"], 2)
        self.assertEqual(aggregate["max_frontier"], 3)
        self.assertEqual(aggregate["max_frontier_terms_shared_bytes"], 4096)
        self.assertEqual(
            aggregate["max_frontier_bindings_exclusive_bytes"], 512
        )

    def test_manifest_is_complete_and_assets_exist(self) -> None:
        rows = diversity.load_manifest()
        diversity.validate_manifest(rows)
        self.assertEqual(
            {row["id"] for row in rows if row["availability"] == "runnable"},
            diversity.RUNNABLE_IDS,
        )

    def test_strategy_niches_are_not_collapsed_to_dfs_and_fifo(self) -> None:
        rows = diversity.load_manifest()
        niches = {row["niche"] for row in rows}
        self.assertTrue(
            {
                "age-protected-frontier",
                "graded-stratum",
                "gcl-scored",
                "variant-tabling",
                "iterative-deepening",
                "connection-portfolio",
                "incremental-compression-guidance",
                "finite-prefix-demand",
                "order-postprocessing",
            }.issubset(niches)
        )

    def test_planned_rows_do_not_claim_runnable_assets(self) -> None:
        rows = diversity.load_manifest()
        planned = {row["id"] for row in rows if row["availability"] == "planned"}
        self.assertEqual(
            planned,
            {"connection-search"},
        )

    def test_incremental_compression_receipt_survives_aggregation(self) -> None:
        receipt = machine_stats.parse_controller_stats_line(
            "CETTA_CONTROLLER_STATS requested=ratio selection=ratio:16 "
            "active=ratio storage=shared-terms-owned-state "
            "advisor=incremental-compression expansions=3 "
            "compression_ranking_applied=2 "
            "compression_model_observations=7"
        )
        aggregate = diversity.aggregate_controller_stats([receipt])
        self.assertEqual(aggregate["active_ratio"], 1)
        self.assertEqual(aggregate["advisor_incremental_compression"], 1)
        self.assertEqual(aggregate["compression_ranking_applied"], 2)
        self.assertEqual(aggregate["compression_model_observations"], 7)

    def test_result_exposes_collection_tournament_columns(self) -> None:
        sample = {
            "elapsed_ns": 17,
            "aggregate": {
                "heap_collections": 3,
                "heap_collection_elapsed_ns": 19,
                "heap_bytes_reclaimed": 23,
                "binding_entries_discarded": 29,
                "max_heap_live_bytes": 31,
                "max_binding_entries": 37,
                "controller_reclamation_attempts": 41,
                "controller_reclamation_applied": 2,
                "controller_reclamation_deferred": 39,
                "controller_reclamation_failures": 0,
                "controller_reclamation_elapsed_ns": 43,
                "controller_reclamation_shared_bytes_before": 47,
                "controller_reclamation_shared_bytes_after": 11,
                "controller_reclamation_shared_bytes_reclaimed": 36,
            },
        }
        result = diversity._result("gc-canary", "ratio:8", [sample])
        self.assertEqual(result["heap_collections"], 3)
        self.assertEqual(result["binding_entries_discarded"], 29)
        self.assertEqual(result["max_binding_entries"], 37)
        self.assertEqual(result["reclamation_attempts"], 41)
        self.assertEqual(result["reclamation_applied"], 2)
        self.assertEqual(result["reclamation_deferred"], 39)
        self.assertEqual(result["reclamation_shared_bytes_reclaimed"], 36)

    def test_ordinary_erasure_ignores_clocks_but_not_work(self) -> None:
        row = {"id": "erasure-canary"}
        ordinary = {
            "stdout": "answer\n",
            "aggregate": {
                "transitions": 7,
                "active_elapsed_ns": 11,
                "ttfa_ns_median": 5,
            },
        }
        inline = {
            "stdout": "answer\n",
            "aggregate": {
                "transitions": 7,
                "active_elapsed_ns": 99,
                "ttfa_ns_median": 42,
            },
        }
        diversity._qualify_ordinary_erasure(row, ordinary, inline)
        inline["aggregate"]["transitions"] = 8
        with self.assertRaisesRegex(RuntimeError, "transitions"):
            diversity._qualify_ordinary_erasure(row, ordinary, inline)

    def test_controlled_first_accepts_only_one_source_witness(self) -> None:
        row = {"id": "absorbing-once"}
        base = {
            "stdout": "(answer deep)\n",
            "aggregate": {
                "controller_expansions": 1,
                "controller_answers": 1,
            },
        }
        diversity._qualify(row, base, base, {
            "stdout": "(answer shallow)\n",
            "aggregate": {
                "controller_expansions": 1,
                "controller_answers": 1,
            },
        })
        with self.assertRaisesRegex(RuntimeError, "source witness"):
            diversity._qualify(row, base, base, {
                "stdout": "(answer invented)\n",
                "aggregate": {
                    "controller_expansions": 1,
                    "controller_answers": 1,
                },
            })
        with self.assertRaisesRegex(RuntimeError, "source witness"):
            diversity._qualify(row, base, base, {
                "stdout": "(answer deep)\n(answer shallow)\n",
                "aggregate": {
                    "controller_expansions": 1,
                    "controller_answers": 2,
                },
            })

    def test_printed_result_list_preserves_nested_occurrences(self) -> None:
        self.assertEqual(
            controller_check.metta_list_occurrences(
                '[(pair a b), (nested (pair c d)), "x,y", [z, q]]\n'
            ),
            ["(pair a b)", "(nested (pair c d))", '"x,y"', "[z, q]"],
        )
        with self.assertRaisesRegex(ValueError, "unbalanced"):
            controller_check.metta_list_occurrences("[(pair a b)] ]")


if __name__ == "__main__":
    unittest.main()
