"""Captured streaming plans: bindings, bags, completion and dependencies."""
import os
from contextlib import closing
from pathlib import Path
import random
import sqlite3
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKER = os.environ.get("CETTA_VALUE_WORKER")
if WORKER:
    sys.path.insert(0, str(Path(os.environ.get("CETTA_CHANNEL_ROOT", ROOT)) / "tools"))
    from value_worker_client import Worker
    from space_service_client import E, S, Var, encode


@unittest.skipUnless(WORKER, "CETTA_VALUE_WORKER is required")
class QueryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cetta-kb-query-", dir=os.environ.get("TMPDIR"))
        self.addCleanup(self.temp.cleanup)
        self.path = self.temp.name
        self.worker = Worker(WORKER, ROOT / "tests/support/kb_query_probe.metta", "kbq-probe:run",
                             language="petta", import_mode="relative", timeout=30)
        self.addCleanup(self.worker.close)
        self.assertEqual(self.worker.call(E("Create", self.path))[0], S("KnowledgeReadyV1"))
        self.sequence = 0

    def commit(self, changes):
        self.sequence += 1
        result = self.worker.call(E("At", self.path, E("Commit", self.sequence, tuple(changes))))
        self.assertEqual(result[0], S("KnowledgeCommittedV1"), result)

    def query(self, plan, command):
        return self.worker.call(E("Query", self.path, plan, command))

    def pages(self, plan, size=4, fuel=8):
        result = self.query(plan, E("Pages", size, fuel))
        self.assertEqual(result[0], S("KqPages"), result)
        return result

    def rows(self, plan, size=4, fuel=8):
        return tuple(row for page in self.pages(plan, size, fuel)[1] for row in page)

    def bag(self, rows):
        return sorted(encode(row) for row in rows)

    def scan(self, relation, fields, projection, identity=None):
        return E("KqScan", E("KbRelation", relation),
                 E("KbFact", identity if identity is not None else Var(900), relation, fields), projection)

    def test_bindings_and_duplicate_occurrences_across_page_sizes(self):
        self.commit([E("KbPut", n, "pair", pair) for n, pair in enumerate([(1, 1), (1, 2), (2, 2), (1, 1)])])
        x, identity = Var(1), Var(2)
        plan = self.scan("pair", (x, x), (identity, x), identity)
        expected = ((0, 1), (2, 2), (3, 1))
        for size in (1, 2, 9):
            for fuel in (1, 3, 100):
                with self.subTest(size=size, fuel=fuel):
                    self.assertEqual(self.bag(self.rows(plan, size, fuel)), self.bag(expected))

    def test_general_match_includes_nonground_records_and_freshens_rows(self):
        x, y = Var(1), Var(2)
        self.commit([E("KbPut", "open-a", "pair", (x, x)), E("KbPut", "open-b", "pair", (x, x)),
                     E("KbPut", "closed", "pair", (3, 3))])
        plan = self.scan("pair", (3, y), (Var(9), y), Var(9))
        self.assertEqual(self.bag(self.rows(plan)), self.bag((("open-a", 3), ("open-b", 3), ("closed", 3))))
        # Variables in separate stored occurrences are fresh, while sharing
        # within an occurrence survives matching and framing.
        plan = self.scan("pair", (x, y), (x, y))
        rows = [row for row in self.rows(plan) if isinstance(row[0], Var)]
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0][0], rows[0][1])
        self.assertEqual(rows[1][0], rows[1][1])
        self.assertNotEqual(rows[0][0], rows[1][0])

    def test_projections_and_stored_rules_remain_inert(self):
        x = Var(7)
        rule = E("=", E("f", x), E("+", x, x))
        self.commit([E("KbPut", "code", "program", (rule,))])
        for value in [rule, E("+", 2, 3), E("kbq-probe:effect"), E("Error", "source", "data"), ()]:
            with self.subTest(value=value):
                plan = self.scan("program", (Var(1),), value)
                self.assertEqual(encode(self.rows(plan)), encode((value,)))
        self.assertEqual(self.worker.call(E("Effects")), 0)

    def test_work_budget_pauses_without_claiming_empty_completion(self):
        self.commit([E("KbPut", n, "edge", (n, n + 1)) for n in range(40)])
        plan = self.scan("edge", (999, Var(1)), Var(1))
        first = self.query(plan, E("First", 1, 3))
        self.assertEqual(first[:3], (S("KqFirst"), (), S("paused")))
        self.assertEqual(first[3][1:3], (3, 3))
        zero = self.query(plan, E("First", 1, 0))
        self.assertEqual(zero[2], S("paused"))
        self.assertEqual(zero[3], E("KqWork", 0, 0, 0))
        self.assertEqual(self.rows(plan, 1, 3), ())
        first = self.query(self.scan("edge", (Var(1), Var(2)), Var(1)), E("First", 1, 10))
        self.assertEqual(first[2], S("more"))
        self.assertEqual(first[3][1:3], (1, 1))

    def test_index_domain_is_exact_and_final_pattern_is_still_checked(self):
        self.commit([E("KbPut", "a", "edge", (1, 2)), E("KbPut", "b", "edge", (1, 3)),
                     E("KbPut", "c", "edge", (2, 3))])
        plan = E("KqScan", E("KbArgument", "edge", 0, 1), E("KbFact", Var(1), "edge", (1, 3)), Var(1))
        self.assertEqual(self.rows(plan), ("b",))
        result = self.pages(plan)
        self.assertLessEqual(result[2][2], 3)
        explain = self.worker.call(E("ExplainQuery", plan))
        self.assertEqual(explain[2][1], S("argument"))

    def test_join_optional_semi_anti_and_union_against_sqlite(self):
        rng = random.Random(62029)
        x, y, z, li, ri = (Var(i) for i in range(5))
        for trial in range(16):
            relation = f"edge-{trial}"
            edges = [(n, rng.randrange(5), rng.randrange(5)) for n in range(rng.randrange(1, 10))]
            self.commit([E("KbPut", (trial, n), relation, (a, b)) for n, a, b in edges])
            left = self.scan(relation, (x, y), (li, x, y), li)
            right = self.scan(relation, (y, z), (li, x, y, ri, z), ri)
            with closing(sqlite3.connect(":memory:")) as db:
                db.execute("create table edge(id integer, a integer, b integer)")
                db.executemany("insert into edge values (?, ?, ?)", edges)
                sql_inner = db.execute("select l.id,l.a,l.b,r.id,r.b from edge l join edge r on l.b=r.a").fetchall()
                expected_inner = [((trial, a), x, y, (trial, b), z) for a, x, y, b, z in sql_inner]
                sql_optional = db.execute("select l.id,l.a,l.b,r.id,r.b from edge l left join edge r on l.b=r.a").fetchall()
                expected_optional = [E("Missing", (trial, a), x, y) if b is None
                                     else ((trial, a), x, y, (trial, b), z) for a, x, y, b, z in sql_optional]
                for mode, sql in [("KqSemi", "exists"), ("KqAnti", "not exists")]:
                    expected = [((trial, n), a, b) for n, a, b in db.execute(
                        f"select l.id,l.a,l.b from edge l where {sql} (select 1 from edge r where l.b=r.a)")]
                    with self.subTest(trial=trial, mode=mode):
                        self.assertEqual(self.bag(self.rows(E(mode, left, (li, x, y), right), 2, 2)), self.bag(expected))
                with self.subTest(trial=trial, mode="inner"):
                    inner = E("KqBind", left, (li, x, y), right)
                    self.assertEqual(self.bag(self.rows(inner, 3, 2)), self.bag(expected_inner))
                with self.subTest(trial=trial, mode="optional"):
                    optional = E("KqOptional", left, (li, x, y), right, E("Missing", li, x, y))
                    self.assertEqual(self.bag(self.rows(optional, 3, 2)), self.bag(expected_optional))
                self.assertEqual(self.bag(self.rows(E("KqUnion", (inner, inner)), 4, 3)),
                                 self.bag(expected_inner * 2))

    def test_optional_does_not_confuse_an_empty_row_with_no_answer(self):
        x = Var(1)
        left = E("KqRows", (1, 1, 2))
        present = E("KqRows", ((),))
        missing = E("KqRows", ())
        self.assertEqual(self.rows(E("KqOptional", left, x, present, E("Missing", x))), ((), (), ()))
        self.assertEqual(self.rows(E("KqOptional", left, x, missing, E("Missing", x))),
                         (E("Missing", 1), E("Missing", 1), E("Missing", 2)))

    def test_existential_operations_stop_after_the_first_inner_answer(self):
        # The invalid later union branch must never be opened for existence.
        right = E("KqUnion", (E("KqRows", ("found",)), E("Invalid")))
        left = E("KqRows", (1, 2))
        self.assertEqual(self.rows(E("KqSemi", left, Var(1), right)), (1, 2))
        self.assertEqual(self.rows(E("KqAnti", left, Var(1), right)), ())

    def test_held_view_and_conservative_negative_dependencies(self):
        self.commit([E("KbPut", "old", "edge", (1, 2))])
        plan = self.scan("edge", (Var(1), Var(2)), (Var(1), Var(2)))
        changes = (E("KbDelete", "old"), E("KbPut", "new", "edge", (2, 3)))
        held = self.query(plan, E("Held", changes, 1, 1))
        self.assertEqual(held[1][0], S("KnowledgeCommittedV1"))
        self.assertEqual(tuple(row for page in held[2][1] for row in page), ((1, 2),))
        self.assertEqual(self.rows(plan), ((2, 3),))
        negative = E("KqAnti", E("KqRows", ("allowed",)), Var(8), self.scan("blocked", (Var(1),), Var(1)))
        race = self.query(negative, E("Race", (E("KbPut", "block", "blocked", (1,)),),
                                     (E("KbPut", "decision", "decision", ("allowed",)),)))
        self.assertEqual(race[3], E("StoreRefused", S("conflict")))

    def test_failures_are_explicit_and_close_scans(self):
        self.commit([E("KbPut", "a", "edge", (1, 2))])
        left = self.scan("edge", (Var(1), Var(2)), (Var(1), Var(2)))
        plan = E("KqBind", left, Var(9), E("InvalidPlan"))
        fds = Path(f"/proc/{self.worker.process.pid}/fd")
        baseline = len(list(fds.iterdir()))
        for _ in range(20):
            result = self.query(plan, E("Pages", 1, 1))
            self.assertEqual(result[0], S("KqFailedPages"), result)
            self.assertEqual(result[2], E("KbRefused", S("query-plan")))
        self.assertEqual(len(list(fds.iterdir())), baseline)
        closed = self.query(left, E("CloseThenStep"))
        self.assertEqual(closed[0], S("KqFailed"), closed)
        self.assertEqual(closed[1][0], S("StoreRefused"))

    def test_cancelling_a_nested_page_releases_both_scans(self):
        self.commit([E("KbPut", n, "edge", (n, n + 1)) for n in range(10)])
        x, y, z = Var(1), Var(2), Var(3)
        left = self.scan("edge", (x, y), (x, y))
        right = self.scan("edge", (y, z), (x, z))
        plan = E("KqBind", left, (x, y), right)
        fds = Path(f"/proc/{self.worker.process.pid}/fd")
        baseline = len(list(fds.iterdir()))
        for _ in range(30):
            first = self.query(plan, E("First", 1, 4))
            self.assertIn(first[2], (S("more"), S("paused")))
        self.assertEqual(len(list(fds.iterdir())), baseline)

    def test_malformed_explanations_and_free_cursors_refuse_without_binding(self):
        for plan, reason in [(Var(1), "query-plan"), (E("KqRows", S("bad")), "query-rows"),
                             (E("KqUnion", S("bad")), "query-plans"), ((Var(1), ()), "query-plan")]:
            with self.subTest(plan=plan):
                self.assertEqual(self.worker.call(E("ExplainQuery", plan)), E("KbRefused", S(reason)))
        answers = self.worker.call(E("CursorOperations", Var(1)))
        self.assertEqual(answers[:-1], (E("KbRefused", S("query-cursor")),) * 5)
        self.assertEqual(answers[-1], S("Variable"))
        result = self.query(E("KqRows", ()), E("FreeObservation", Var(1)))
        self.assertEqual(result, E("KqObservationCheck", E("KnowledgeRefusedV1", S("bound-observation-required")), S("Variable")))

    def test_constant_queries_require_a_live_captured_view(self):
        for kind in ("free", "owner", "closed"):
            with self.subTest(kind=kind):
                result = self.query(E("KqRows", ()), E("InvalidViewRows", S(kind), Var(1)))
                self.assertEqual(result[1][0], S("StoreRefused"), result)
                self.assertEqual(result[2], S("Variable"))

    def test_existence_queries_keep_original_open_rows(self):
        x, y = Var(1), Var(2)
        original = E("KbFact", "open", "rule", (x, y))
        self.commit([E("KbPut", "open", "rule", (x, y))])
        row = Var(3)
        left = E("KqScan", E("KbRelation", "rule"), row, row)
        pattern = E("KbFact", Var(4), "rule", (Var(5), 42))
        for mode, right in [("KqSemi", (S("hit"),)), ("KqAnti", ())]:
            with self.subTest(mode=mode):
                result = self.rows(E(mode, left, pattern, E("KqRows", right)))
                self.assertEqual(encode(result), encode((original,)))

    def test_pattern_matching_does_not_mutate_later_rows_or_input_plan(self):
        x, a = Var(1), Var(2)
        plan = E("KqBind", E("KqRows", ((x, 1), (x, 2))), (a, a), E("KqRows", (E("diag", a),)))
        result = self.query(plan, E("PagesWithPlan", 1, 2))
        self.assertEqual(result[0], S("KqWithPlan"))
        rows = tuple(row for page in result[1][1] for row in page)
        self.assertEqual(rows, (E("diag", 1), E("diag", 2)))
        self.assertEqual(encode(result[2]), encode(plan))


if __name__ == "__main__":
    unittest.main()
