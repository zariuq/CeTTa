"""Ground graph projections: finite traversal, identities, paging and reads."""
import os
import ast
from contextlib import closing
from pathlib import Path
import random
import shutil
import sqlite3
import subprocess
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
class GraphTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cetta-kb-graph-", dir=os.environ.get("TMPDIR"))
        self.addCleanup(self.temp.cleanup)
        self.path = self.temp.name
        self.worker = Worker(WORKER, ROOT / "tests/support/kb_graph_probe.metta", "kbg-probe:run",
                             language="petta", import_mode="relative", timeout=30, profile=True)
        self.addCleanup(self.worker.close)
        self.assertEqual(self.worker.call(E("Create", self.path))[0], S("KnowledgeReadyV1"))
        self.sequence = 0
        self.spec = E("KgRelation", "edge", 0, 1)

    def commit(self, changes):
        self.sequence += 1
        answer = self.worker.call(E("At", self.path, E("Commit", self.sequence, tuple(changes))))
        self.assertEqual(answer[0], S("KnowledgeCommittedV1"), answer)

    def edges(self, rows, relation="edge"):
        self.commit([E("KbPut", identity, relation, (source, target)) for identity, source, target in rows])

    def query(self, plan, command, limit=10000):
        return self.worker.call(E("Graph", self.path, plan, limit, command))

    def rows(self, plan, size=3, fuel=4):
        answer = self.query(plan, E("Pages", size, fuel))
        self.assertEqual(answer[0], S("KgPages"), answer)
        return tuple(row for page in answer[1] for row in page)

    @staticmethod
    def bag(rows):
        return sorted(encode(row) for row in rows)

    @staticmethod
    def walks(edges, start, minimum, maximum, direction="out"):
        # Exhaustive small-graph oracle; no shared traversal implementation.
        paths = [((start,), ())]
        result = []
        for depth in range(maximum + 1):
            if depth >= minimum:
                result.extend(E("KgPath", nodes, ids, depth) for nodes, ids in paths)
            expanded = []
            for nodes, ids in paths:
                for identity, source, target in edges:
                    left, right = (source, target) if direction == "out" else (target, source)
                    if encode(left) == encode(nodes[-1]):
                        expanded.append((nodes + (right,), ids + (identity,)))
            paths = expanded
        return result

    def test_neighbours_use_both_argument_indexes_and_keep_occurrences(self):
        edges = [("a", 1, 2), ("b", 1, 2), ("c", 3, 1), ("d", 1, 1)]
        self.edges(edges)
        for direction, expected in [("out", [edges[i] for i in (0, 1, 3)]),
                                    ("in", [edges[i] for i in (2, 3)])]:
            plan = E("KgNeighbours", self.spec, 1, S(direction))
            self.assertEqual(self.bag(self.rows(plan, 1, 1)), self.bag([E("KgEdge", *row) for row in expected]))
            explanation = self.worker.call(E("ExplainGraph", plan))
            self.assertEqual(explanation[4][1], S("argument"))
        first = self.query(E("KgNeighbours", self.spec, 1, S("out")), E("First", 1, 2))
        self.assertEqual(first[2], S("more"))
        self.assertEqual(first[3][2], 1)  # One indexed posting, no whole-KB load.

    def test_bounded_walks_include_cycles_without_unbounded_execution(self):
        edges = [(0, "a", "b"), (1, "b", "a"), (2, "a", "b"), (3, "b", "c")]
        self.edges(edges)
        for minimum, maximum in [(0, 0), (0, 3), (1, 2), (3, 3)]:
            for direction in ("out", "in"):
                plan = E("KgPaths", self.spec, "a", minimum, maximum, S(direction))
                self.assertEqual(self.bag(self.rows(plan, 2, 1)),
                                 self.bag(self.walks(edges, "a", minimum, maximum, direction)))

    def test_reachability_matches_recursive_sqlite_and_shortest_matches_exhaustion(self):
        rng = random.Random(61029)
        for trial in range(12):
            relation = f"graph-{trial}"
            spec = E("KgRelation", relation, 0, 1)
            edges = [((trial, i), rng.randrange(4), rng.randrange(4)) for i in range(rng.randrange(1, 8))]
            self.edges(edges, relation)
            for start in (0, 3, 99):
                with closing(sqlite3.connect(":memory:")) as oracle:
                    oracle.execute("create table edge(a integer,b integer)")
                    oracle.executemany("insert into edge values (?,?)", [(a, b) for _, a, b in edges])
                    expected = [row[0] for row in oracle.execute(
                        "with recursive reach(x) as (select b from edge where a=? union "
                        "select e.b from edge e join reach r on e.a=r.x) select x from reach", (start,))]
                with self.subTest(trial=trial, start=start, mode="reach"):
                    actual = self.rows(E("KgReach", spec, start, S("out")), 1, 3)
                    self.assertEqual(self.bag(actual), self.bag(expected))
                for target in (0, 2, 99):
                    candidates = [p for p in self.walks(edges, start, 0, 3) if p[1][-1] == target]
                    distance = min((p[3] for p in candidates), default=None)
                    expected = [p for p in candidates if p[3] == distance]
                    with self.subTest(trial=trial, start=start, target=target):
                        actual = self.rows(E("KgShortest", spec, start, target, S("all"), S("out")), 1, 2)
                        self.assertEqual(self.bag(actual), self.bag(expected))
                        one = self.rows(E("KgShortest", spec, start, target, S("one"), S("out")), 1, 2)
                        self.assertEqual(len(one), min(1, len(expected)))
                        if one:
                            self.assertIn(encode(one[0]), self.bag(expected))

    def test_identity_kinds_and_inert_code_are_not_evaluated(self):
        code = E("+", 2, 3)
        self.edges([("a", code, "1"), ("b", code, 1), ("c", 5, "unrelated")])
        expected = (E("KgEdge", "a", code, "1"), E("KgEdge", "b", code, 1))
        self.assertEqual(self.bag(self.rows(E("KgNeighbours", self.spec, code, S("out")))), self.bag(expected))
        self.assertEqual(self.bag(self.rows(E("KgReach", self.spec, code, S("out")))), self.bag(("1", 1)))
        # Callable symbol names are also ordinary identity values. Construct
        # their singleton rows as data, rather than forming a dynamic call.
        self.edges([("parse-plus", S("parse"), S("+")),
                    ("plus-parse", S("+"), S("parse")),
                    ("plus-eval", S("+"), S("eval")),
                    ("eval-parse", S("eval"), S("parse"))], S("+"))
        callable_spec = E("KgRelation", S("+"), 0, 1)
        self.assertEqual(set(self.rows(E("KgReach", callable_spec, S("parse"), S("out")))),
                         {S("parse"), S("+"), S("eval")})
        triangles = self.rows(E("KgTriangles", callable_spec, S("parse"), S("out")))
        self.assertEqual(triangles, (E("KgPath", (S("parse"), S("+"), S("eval"), S("parse")),
                                      ("parse-plus", "plus-eval", "eval-parse"), 3),))

    def test_tied_shortest_paths_and_anchored_triangles_preserve_edge_identity(self):
        edges = [("ab1", "a", "b"), ("ab2", "a", "b"), ("bd", "b", "d"),
                 ("ac", "a", "c"), ("cd", "c", "d"), ("da", "d", "a"),
                 ("aa", "a", "a"), ("ba", "b", "a")]
        self.edges(edges)
        for start, target, direction in [("a", "d", "out"), ("d", "a", "in")]:
            paths = self.walks(edges, start, 2, 2, direction)
            expected = [p for p in paths if p[1][-1] == target]
            self.assertEqual(len(expected), 3)
            actual = self.rows(E("KgShortest", self.spec, start, target, S("all"), S(direction)), 1, 1)
            self.assertEqual(self.bag(actual), self.bag(expected))
        expected_triangles = [p for p in self.walks(edges, "a", 3, 3)
                              if p[1][-1] == "a" and len(set(p[1][:-1])) == 3]
        self.assertEqual(len(expected_triangles), 3)
        self.assertEqual(self.bag(self.rows(E("KgTriangles", self.spec, "a", S("out")), 1, 1)),
                         self.bag(expected_triangles))

    def test_ground_edge_projection_does_not_instantiate_open_endpoints(self):
        self.commit([E("KbPut", "left-open", "edge", (Var(1), 2)),
                     E("KbPut", "right-open", "edge", (1, Var(2))),
                     E("KbPut", "short", "edge", (1,)),
                     E("KbPut", "good", "edge", (1, 2, E("quote", Var(3))))])
        for start, direction in [(1, "out"), (2, "in")]:
            self.assertEqual(self.rows(E("KgNeighbours", self.spec, start, S(direction))),
                             (E("KgEdge", "good", 1, 2),))
        original = self.worker.call(E("At", self.path, E("Fact", "left-open")))
        self.assertIsInstance(original[3][0], Var)

    def test_snapshot_and_path_phantoms_are_validated(self):
        self.edges([("a", 1, 2)])
        plan = E("KgReach", self.spec, 1, S("out"))
        held = self.query(plan, E("Held", (E("KbPut", "b", "edge", (2, 3)),), 1, 1))
        self.assertEqual(held[1][0], S("KnowledgeCommittedV1"))
        self.assertEqual(tuple(x for page in held[2][1] for x in page), (2,))
        self.assertEqual(set(self.rows(plan)), {2, 3})
        missing = E("KgShortest", self.spec, 1, 4, S("one"), S("out"))
        race = self.query(missing, E("Race", (E("KbPut", "c", "edge", (3, 4)),),
                                    (E("KbPut", "decision", "decision", ("unreachable",)),)))
        self.assertEqual(race[3], E("StoreRefused", S("conflict")))

    def test_pause_capacity_failure_and_cancellation_release_scans(self):
        self.edges([(n, 0, n + 1) for n in range(12)])
        plan = E("KgReach", self.spec, 0, S("out"))
        first = self.query(plan, E("First", 1, 0))
        self.assertEqual(first[1:3], ((), S("paused")))
        fds = Path(f"/proc/{self.worker.process.pid}/fd")
        before = len(list(fds.iterdir()))
        for _ in range(12):
            failure = self.query(plan, E("Pages", 1, 2), limit=3)
            self.assertEqual(failure[0], S("KgFailedPages"))
            self.assertEqual(failure[2], E("KbRefused", S("graph-state-budget")))
            self.query(plan, E("First", 1, 1))
        self.assertEqual(len(list(fds.iterdir())), before)
        closed = self.query(plan, E("CloseThenStep"))
        self.assertEqual(closed[0], S("KgFailed"))

    def test_invalid_plans_and_free_cursors_refuse(self):
        for plan in [Var(1), E("KgReach", self.spec, Var(2), S("out")),
                     E("KgPaths", self.spec, 0, 0, Var(3), S("out")),
                     E("KgPaths", self.spec, 0, 3, 2, S("out")),
                     E("KgNeighbours", E("KgRelation", "edge", -1, 1), 0, S("out")),
                     E("KgShortest", self.spec, 0, 1, S("random"), S("out"))]:
            with self.subTest(plan=plan):
                self.assertEqual(self.worker.call(E("ExplainGraph", plan))[0], S("KbRefused"))
        result = self.worker.call(E("GraphCursorOperations", Var(1)))
        self.assertEqual(result[:-1], (E("KbRefused", S("graph-cursor")),) * 5)
        self.assertEqual(result[-1], S("Variable"))

    def test_unencodable_identities_refuse_at_open_and_explanation(self):
        for part in ("start", "goal", "relation"):
            with self.subTest(part=part):
                result = self.worker.call(E("ValidateGraph", self.path, E("Identity", S(part))))
                self.assertEqual(result, E("KgValidation", E("StoreCodecRefused", "unsupported"),
                                          E("StoreCodecRefused", "unsupported")))

    def test_zero_length_paths_require_a_live_view(self):
        for kind in ("free", "owner", "integer", "closed"):
            with self.subTest(kind=kind):
                result = self.worker.call(E("ValidateGraph", self.path, E("View", S(kind), Var(1))))
                self.assertEqual(result[1][0], S("StoreRefused"), result)
                self.assertEqual(result[2], S("Variable"))

    def test_nested_cursor_holes_and_bad_counters_refuse_without_binding(self):
        for part in ("config", "scan", "work"):
            for operation in ("work", "observe", "close", "step", "page"):
                with self.subTest(part=part, operation=operation):
                    result = self.worker.call(E("ValidateGraph", self.path, E("Cursor", S(part), S(operation), Var(1))))
                    self.assertEqual(result, E("KgValidation", E("KbRefused", S("graph-cursor")), S("Variable")))

    def test_repeated_queries_do_not_grow_permanent_request_storage(self):
        self.edges([(n, n, (n + 1) % 16) for n in range(16)])
        plan = E("KgReach", self.spec, 0, S("out"))
        self.rows(plan)
        before = self.worker.last_metrics["permanent_bytes"]
        for _ in range(40):
            self.assertEqual(len(self.rows(plan, 2, 3)), 16)
            self.query(plan, E("First", 1, 2))
        self.assertEqual(self.worker.last_metrics["permanent_bytes"], before)

    def test_single_shortest_does_not_enumerate_all_tied_prefixes(self):
        pairs = [(0, 1), (0, 2)]
        for level in range(7):
            pairs.extend((left, right) for left in (1 + 2 * level, 2 + 2 * level)
                         for right in (3 + 2 * level, 4 + 2 * level))
        pairs.extend([(15, 17), (16, 17)])
        self.edges([(i, a, b) for i, (a, b) in enumerate(pairs)])
        one = self.query(E("KgShortest", self.spec, 0, 17, S("one"), S("out")), E("Pages", 1, 3), limit=40)
        self.assertEqual(one[0], S("KgPages"), one)
        paths = tuple(row for page in one[1] for row in page)
        self.assertEqual(len(paths), 1)
        self.assertEqual(paths[0][3], 9)
        all_ties = self.query(E("KgShortest", self.spec, 0, 17, S("all"), S("out")), E("Pages", 1, 3), limit=40)
        self.assertEqual(all_ties[0], S("KgFailedPages"))
        self.assertEqual(all_ties[2], E("KbRefused", S("graph-state-budget")))

    @unittest.skipUnless(shutil.which("swipl"), "SWI-Prolog is required for the tabled oracle")
    def test_reachability_matches_tabled_prolog_on_finite_generated_graphs(self):
        rng = random.Random(61031)
        for trial in range(8):
            relation = f"prolog-{trial}"
            spec = E("KgRelation", relation, 0, 1)
            pairs = [(rng.randrange(6), rng.randrange(6)) for _ in range(12)]
            self.edges([((trial, i), a, b) for i, (a, b) in enumerate(pairs)], relation)
            starts = [0, 3, 99]
            request = "graph(" + str([list(p) for p in pairs]) + "," + str(starts) + ").\n"
            result = subprocess.run([shutil.which("swipl"), "-q", "-s", str(ROOT / "tests/support/kb_graph_reference.pl"),
                                     "-g", "main", "-t", "halt"], input=request, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            expected = [ast.literal_eval(line) for line in result.stdout.splitlines()]
            self.assertEqual(len(expected), len(starts))
            for start, endpoints in zip(starts, expected):
                actual = self.rows(E("KgReach", spec, start, S("out")), 2, 2)
                self.assertEqual(self.bag(actual), self.bag(endpoints))


if __name__ == "__main__":
    unittest.main()
