"""Native KB facts, covering indexes and captured-view cursor contracts."""
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("CETTA_CHANNEL_ROOT", ROOT))
WORKER = os.environ.get("CETTA_VALUE_WORKER")
if WORKER:
    sys.path.insert(0, str(SDK / "tools"))
    from value_worker_client import Worker
    from space_service_client import E, S, Var, BigInt, encode


@unittest.skipUnless(WORKER, "CETTA_VALUE_WORKER is required")
class DatabaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cetta-kb-", dir=os.environ.get("TMPDIR"))
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.path = self.directory / "store"
        self.path.mkdir(mode=0o700)
        self.worker = self.new_worker()
        self.addCleanup(lambda: self.worker.close())
        self.assertEqual(self.worker.call(E("Create", str(self.path)))[0], S("KnowledgeReadyV1"))
        self.sequence = 0

    def new_worker(self):
        return Worker(WORKER, ROOT / "tests/support/kb_probe.metta", "kb-probe:run",
                      language="petta", import_mode="relative", timeout=30, profile=True)

    def call(self, command):
        return self.worker.call(E("At", str(self.path), command))

    def commit(self, changes, receipt=None):
        self.sequence += 1
        result = self.call(E("Commit", receipt or E("Receipt", self.sequence), tuple(changes)))
        self.assertEqual(result[0], S("KnowledgeCommittedV1"), result)
        return result

    def rows(self, selector):
        result = self.call(E("Select", selector))
        self.assertEqual(result[0], S("KbRows"), result)
        self.assertEqual(result[2][1], len(result[1]), result)
        return result[1]

    def same(self, left, right):
        self.assertEqual(encode(left), encode(right))

    def bag(self, rows):
        return sorted(encode(row) for row in rows)

    def test_ids_keep_equal_payload_occurrences_and_exact_value_kinds(self):
        values = ["a", S("a"), 1, True, -0.0, 0.0, BigInt(str(1 << 80)), E("term", "x"), "a\0b"]
        changes = [E("KbPut", f"id-{i}", S("value"), (v, v)) for i, v in enumerate(values)]
        changes.append(E("KbPut", "duplicate", S("value"), ("a", "a")))
        self.commit(changes)
        self.assertEqual(len(self.rows(S("KbAll"))), len(values) + 1)
        for i, value in enumerate(values):
            expected = [E("KbFact", f"id-{i}", S("value"), (value, value))]
            if i == 0:
                expected.append(E("KbFact", "duplicate", S("value"), (value, value)))
            self.assertEqual(self.bag(self.rows(E("KbArgument", S("value"), 0, value))), self.bag(expected))

    def test_updates_and_deletes_retire_old_postings(self):
        self.commit([E("KbPut", "edge", S("knows"), ("a", "b"))])
        self.commit([E("KbPut", "edge", S("likes"), ("a", "c"))])
        self.assertEqual(self.rows(E("KbRelation", S("knows"))), ())
        self.assertEqual(self.rows(E("KbArgument", S("knows"), 1, "b")), ())
        self.same(self.rows(E("KbArgument", S("likes"), 1, "c")), (E("KbFact", "edge", S("likes"), ("a", "c")),))
        self.commit([E("KbDelete", "edge")])
        self.assertEqual(self.rows(S("KbAll")), ())
        self.assertEqual(self.rows(E("KbRelation", S("likes"))), ())

    def test_last_edit_wins_and_invalid_tail_publishes_nothing(self):
        changes = [E("KbPut", "x", "r", (1,)), E("KbDelete", "x"), E("KbPut", "x", "s", (2,))]
        self.commit(changes)
        self.same(self.rows(S("KbAll")), (E("KbFact", "x", "s", (2,)),))
        self.assertEqual(self.rows(E("KbRelation", "r")), ())
        invalid = (E("KbPut", "y", "r", (3,)), E("bad-edit", "y"))
        self.same(self.call(E("Commit", "bad", invalid)), E("KbRefused", S("edit-format")))
        self.assertEqual(self.call(E("Fact", "y")), S("KbAbsent"))

    def test_query_view_and_pages_survive_updates_and_restart(self):
        facts = [E("KbPut", i, "edge", (i, i + 1)) for i in range(17)]
        self.commit(facts)
        selector = E("KbRelation", "edge")
        expected = self.rows(selector)
        for size in (1, 3, 17, 30):
            pages = self.call(E("Pages", selector, size))
            self.assertEqual(pages[0], S("KbPages"), pages)
            self.same(tuple(row for page in pages[1] for row in page), expected)
        held = self.call(E("Held", selector, (E("KbDelete", 3), E("KbPut", 100, "edge", (100, 101)))))
        self.same(held[1], held[3])
        after = self.rows(selector)
        self.worker.close()
        self.worker = self.new_worker()
        self.same(self.rows(selector), after)

    def test_negative_and_covering_reads_conflict_with_relevant_changes(self):
        first = (E("KbPut", "x", "edge", (1, 2)),)
        second = (E("KbPut", "decision", "decision", ("none",)),)
        result = self.call(E("Race", E("KbRelation", "edge"), first, second))
        self.same(result[2], E("StoreRefused", S("conflict")))
        self.assertEqual(self.call(E("Fact", "decision")), S("KbAbsent"))
        # The indexed argument stays 1; the returned other field changes.
        result = self.call(E("Race", E("KbArgument", "edge", 0, 1),
                             (E("KbPut", "x", "edge", (1, 3)),), second))
        self.assertEqual(result[1][0], S("KnowledgeCommittedV1"), result)
        self.same(result[2], E("StoreRefused", S("conflict")))

    def test_unrelated_relations_commute_and_observations_cannot_change_views(self):
        result = self.call(E("Race", E("KbRelation", "edge"),
                             (E("KbPut", "x", "other", (1, 2)),),
                             (E("KbPut", "decision", "decision", ("none",)),)))
        self.assertEqual(result[1][0], S("KnowledgeCommittedV1"), result)
        self.assertEqual(result[2][0], S("KnowledgeCommittedV1"), result)
        self.same(self.call(E("ObserveWrongView", (E("KbDelete", "x"),))),
                  E("KnowledgeRefusedV1", S("observation-view")))

    def test_inert_syntax_and_variable_sharing_survive(self):
        value = E("=", E("f", Var(1)), E("+", Var(1), Var(1)))
        self.commit([E("KbPut", "program", "syntax", (value,))])
        self.same(self.rows(E("KbRelation", "syntax")), (E("KbFact", "program", "syntax", (value,)),))
        self.same(self.call(E("Fact", "program")), E("KbFact", "program", "syntax", (value,)))

    def test_receipt_replay_does_not_repeat_index_changes(self):
        changes = (E("KbPut", "x", "edge", (1, 2)),)
        self.commit(changes, "same")
        self.commit([E("KbPut", "x", "edge", (3, 4))])
        self.same(self.call(E("Commit", "same", changes)), E("KnowledgeReplayedV1", S("KbAccepted")))
        self.assertEqual(self.rows(E("KbArgument", "edge", 0, 1)), ())
        self.same(self.call(E("Fact", "x")), E("KbFact", "x", "edge", (3, 4)))

    def test_first_page_does_not_materialize_the_matching_tail(self):
        self.commit([E("KbPut", i, "edge", (i, i + 1)) for i in range(128)])
        page = self.call(E("FirstPage", E("KbRelation", "edge"), 1))
        self.assertEqual(page[0], S("KbFirstPage"), page)
        self.assertEqual(len(page[1]), 1)
        self.assertEqual(page[2], S("KbMore"))
        self.assertEqual(page[3][1:3], (1, 1))
        self.assertLess(page[3][3], 1024)
        # An exact argument lookup visits only its match and possibly the
        # first key outside the prefix, independent of the other 127 rows.
        result = self.call(E("Select", E("KbArgument", "edge", 0, 81)))
        self.assertEqual(result[2][1], 1)
        self.assertLessEqual(result[2][2], 2)

    def test_unbound_selector_is_refused_without_selecting_every_fact(self):
        self.commit([E("KbPut", "one", "row", (1,))])
        self.same(self.worker.call(E("Explain", Var(1))), E("KbRefused", S("selector-format")))
        self.same(self.call(E("Select", Var(1))), E("KbRefused", S("selector-format")))

    def test_point_and_combined_observations_cover_the_actual_fact_namespace(self):
        first = (E("KbPut", "alice", "person", ("added",)),)
        second = (E("KbPut", "decision", "decision", ("absent",)),)
        result = self.call(E("PointRace", ("alice",), E("KbRelation", "unrelated"), first, second))
        self.assertEqual(result[1][0], S("KnowledgeCommittedV1"), result)
        self.same(result[2], E("StoreRefused", S("conflict")))
        # The relation half of a composite observation is also validated.
        result = self.call(E("PointRace", ("unrelated",), E("KbRelation", "watched"),
                             (E("KbPut", "new", "watched", (1,)),), second))
        self.same(result[2], E("StoreRefused", S("conflict")))

    def test_ground_value_equality_and_open_term_alpha_equality_are_separate(self):
        self.same(self.worker.call(E("Equal", Var(1), Var(2))), E("KbRefused", S("nonground-equality")))
        self.assertIn(self.worker.call(E("AlphaEqual", E("p", Var(1), Var(1)), E("p", Var(2), Var(2)))),
                      (S("true"), True))
        self.assertIn(self.worker.call(E("AlphaEqual", E("p", Var(1), Var(1)), E("p", Var(2), Var(3)))),
                      (S("false"), False))

    def test_callable_relation_names_are_inert_argument_index_data(self):
        self.commit([E("KbPut", "a", S("+"), (7, 6)), E("KbPut", "b", S("+"), (5, 6))])
        answer = self.call(E("Select", E("KbArgument", S("+"), 0, 7)))
        self.same(answer[1], (E("KbFact", "a", S("+"), (7, 6)),))
        # Constructing (+ position value) would alias these two selectors.
        first = self.worker.call(E("Explain", E("KbArgument", S("+"), 0, 7)))
        second = self.worker.call(E("Explain", E("KbArgument", S("+"), 1, 6)))
        self.assertNotEqual(first[2], second[2])

    def test_observation_merge_refuses_callable_data_without_invoking_it(self):
        self.same(self.worker.call(E("MalformedMerge", S("kb-probe:pure-many"), S("data"))),
                  E("KnowledgeRefusedV1", S("bound-observation-required")))

    def test_view_info_preserves_captured_revision_and_refuses_closed_views(self):
        result = self.call(E("ViewInfo"))
        self.assertEqual(result[1][0], S("StoreViewInfo"))
        self.same(result[1], result[3])
        self.assertEqual(result[2][0], S("KnowledgeCommittedV1"))
        self.assertGreater(result[2][3], result[1][2])
        self.assertEqual(result[4][0], S("StoreRefused"))

    def test_malformed_mutation_and_identity_lists_refuse_without_binding(self):
        for value in (Var(1), S("not-a-list"), "text", 7):
            with self.subTest(value=value):
                result = self.call(E("MalformedLists", value))
                self.assertEqual(result[1:6], (E("KbRefused", S("edit-format")),
                    E("KbRefused", S("edit-list")), E("KbRefused", S("identity-list")),
                    E("KnowledgeRefusedV1", S("identity-list")), E("KnowledgeRefusedV1", S("mutation-list"))))
                if isinstance(value, Var):
                    self.assertEqual(result[-1], S("Variable"))
        result = self.call(E("OpenMutation", Var(1)))
        self.assertEqual(result, E("KbInputCheck", E("KbRefused", S("edit-format")),
                                  E("KnowledgeRefusedV1", S("mutation-format")), S("Variable")))

    def test_repeated_commits_do_not_retain_one_space_per_request(self):
        self.commit([E("KbPut", "fixed", "value", (0,))])
        before = self.worker.last_metrics["permanent_bytes"]
        for value in range(1, 101):
            self.commit([E("KbPut", "fixed", "value", (value,))])
        self.assertEqual(self.worker.last_metrics["permanent_bytes"], before)

    def test_scans_reject_a_body_with_an_inconsistent_identity(self):
        self.commit([E("KbPut", "row", "relation", (1, 2))])
        key = b"kb/fact/v1/" + encode("row")
        wrong = E("KbFact", "other", "relation", (1, 2))
        self.assertEqual(self.call(E("CorruptBody", key, wrong)), S("CorruptionFixtureReady"))
        self.same(self.call(E("Select", S("KbAll"))), E("KbRefused", S("index-identity")))
        key = b"kb/relation/v1/" + encode("relation") + encode("row")
        self.assertEqual(self.call(E("CorruptBody", key, wrong)), S("CorruptionFixtureReady"))
        self.same(self.call(E("Select", E("KbRelation", "relation"))), E("KbRefused", S("index-identity")))

    def test_receipt_replay_requires_a_live_publisher_for_the_captured_domain(self):
        changes = (E("KbPut", "one", "row", (1,)),)
        self.commit(changes, "receipt")
        other = self.directory / "other"
        other.mkdir(mode=0o700)
        self.assertEqual(self.worker.call(E("Create", str(other)))[0], S("KnowledgeReadyV1"))
        self.same(self.call(E("ReplayOwner", S("path"), str(other), "receipt", changes)),
                  E("StoreRefused", S("conflict")))
        for mode in ("invalid", "closed"):
            self.same(self.call(E("ReplayOwner", S(mode), "", "receipt", changes)),
                      E("StoreRefused", S("closed-or-busy")))
        self.same(self.call(E("ReplayOwner", S("path"), str(self.path), "receipt", changes)),
                  E("KnowledgeReplayedV1", S("KbAccepted")))


if __name__ == "__main__":
    unittest.main()
