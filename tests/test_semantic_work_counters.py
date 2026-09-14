#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from semantic_work_counters import (  # noqa: E402
    from_cetta_machine,
    parse_swi,
)
from bench_semantic_work import normalize_stdout  # noqa: E402
from petta_machine_stats import (  # noqa: E402
    extract_publication_stats,
    parse_publication_stats_line,
)


class SemanticWorkCountersTest(unittest.TestCase):
    def test_cetta_projection_ignores_internal_retry_counts(self) -> None:
        machine = {
            "clause_snapshot_candidates": 3,
            "clause_candidates_shape_pruned": 1,
            "clause_attempts_rejected_before_body": 0,
            "clause_bodies_scheduled": 2,
            "clause_result_occurrences": 2,
            "answers": 2,
            "match_decision_clause_inputs": 17,
            "unification_calls": 29,
        }
        self.assertEqual(
            from_cetta_machine(machine, 2),
            {
                "rule-candidates-considered": 3,
                "rule-candidates-rejected": 1,
                "unifications-attempted": 3,
                "unifications-succeeded": 2,
                "rule-bodies-entered": 2,
                "result-occurrences": 2,
                "answers-produced": 2,
                "ordered-occurrences-published": 2,
            },
        )

    def test_swi_parser_preserves_ordinary_stderr(self) -> None:
        text = (
            "diagnostic\n"
            "semantic-counter rule-candidates-considered 3\n"
            "semantic-counter rule-candidates-rejected 1\n"
            "semantic-counter unifications-attempted 3\n"
            "semantic-counter unifications-succeeded 2\n"
            "semantic-counter rule-bodies-entered 2\n"
            "semantic-counter result-occurrences 2\n"
            "semantic-counter answers-produced 2\n"
            "semantic-counter ordered-occurrences-published 2\n"
        )
        counters, ordinary = parse_swi(text)
        self.assertEqual(counters["rule-candidates-rejected"], 1)
        self.assertEqual(ordinary, "diagnostic\n")

    def test_invalid_candidate_partition_fails_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "candidate partition"):
            from_cetta_machine(
                {
                    "clause_snapshot_candidates": 3,
                    "clause_candidates_shape_pruned": 0,
                    "clause_attempts_rejected_before_body": 0,
                    "clause_bodies_scheduled": 2,
                    "clause_result_occurrences": 2,
                    "answers": 2,
                },
                2,
            )

    def test_occurs_check_setup_is_the_only_output_quotient(self) -> None:
        self.assertEqual(
            normalize_stdout(
                "(translatePredicate (set_prolog_flag occurs_check true))\n"
                "answer\n"
            ),
            "answer\n",
        )
        self.assertEqual(normalize_stdout("$_42\nanswer\n"), "answer\n")
        self.assertEqual(
            normalize_stdout("(translatePredicate other)\nanswer\n"),
            "(translatePredicate other)\nanswer\n",
        )

    def test_publication_records_are_separate_and_additive(self) -> None:
        records, ordinary = extract_publication_stats(
            "PETTA_PUBLICATION_STATS answers=2 ordered_occurrences=2\n"
            "diagnostic\n"
            "PETTA_PUBLICATION_STATS answers=1 ordered_occurrences=1\n"
        )
        self.assertEqual(sum(record["answers"] for record in records), 3)
        self.assertEqual(ordinary, "diagnostic\n")

    def test_publication_record_disagreement_fails_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "disagrees"):
            parse_publication_stats_line(
                "PETTA_PUBLICATION_STATS answers=2 ordered_occurrences=1"
            )


if __name__ == "__main__":
    unittest.main()
