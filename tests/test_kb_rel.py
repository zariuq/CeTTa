"""Optional matching keeps bag multiplicity and distinguishes absent partners."""
import os
from pathlib import Path
import random
import sqlite3
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKER = os.environ.get("CETTA_VALUE_WORKER")
if WORKER:
    sys.path.insert(0, str(Path(os.environ.get("CETTA_CHANNEL_ROOT", ROOT)) / "tools"))
    from value_worker_client import Worker
    from space_service_client import E, S, Var, encode


@unittest.skipUnless(WORKER, "CETTA_VALUE_WORKER is required")
class OptionalJoinTests(unittest.TestCase):
    def setUp(self):
        self.worker = Worker(WORKER, ROOT / "tests/support/kb_rel_probe.metta", "kb-rel:run",
                             language="petta", import_mode="relative")
        self.addCleanup(self.worker.close)

    def test_generated_ordered_occurrences_match_sqlite_left_join(self):
        rng = random.Random(817)
        db = sqlite3.connect(":memory:")
        self.addCleanup(db.close)
        db.executescript("CREATE TABLE l(k INTEGER,v TEXT); CREATE TABLE r(k INTEGER,v TEXT);")
        for number in range(32):
            left = tuple((rng.randrange(4), f"l{i}") for i in range(rng.randrange(8)))
            right = tuple((rng.randrange(4), f"r{i}") for i in range(rng.randrange(8)))
            db.execute("DELETE FROM l")
            db.execute("DELETE FROM r")
            db.executemany("INSERT INTO l VALUES(?,?)", left)
            db.executemany("INSERT INTO r VALUES(?,?)", right)
            rows = db.execute("SELECT l.k,l.v,r.rowid,r.k,r.v FROM l LEFT JOIN r ON l.k=r.k ORDER BY l.rowid,r.rowid")
            expected = tuple(((k, v), S("KbMissing") if identity is None else (rk, rv))
                             for k, v, identity, rk, rv in rows)
            with self.subTest(case=number):
                actual = self.worker.call(E("Left", left, right))
                self.assertEqual(encode(actual), encode(expected))

    def test_unknown_predicate_empty_rows_and_callback_cardinality(self):
        left, right = ((1, "a"), (1, "a")), ((1, "b"), (1, "b"), (1, "b"))
        self.assertEqual(self.worker.call(E("EmptyRow", left, right)), ((),) * 6)
        self.assertEqual(self.worker.call(E("Unknown", left, right)), tuple((row, S("KbMissing")) for row in left))
        fault = self.worker.call(E("Several", left, right))
        self.assertEqual(fault[0], S("RelFailure"))
        self.assertEqual(fault[2][0], S("RelCallbackAnswers"))

    def test_open_truth_and_comparison_values_are_rejected_without_binding(self):
        for command in [E("Not", Var(1)), E("And", Var(1), S("RelTrue")),
                        E("Or", Var(1), S("RelFalse")), E("OpenTruth", ((Var(1), "value"),)),
                        E("OpenBool", ((Var(1), "a"), (Var(2), "b")))]:
            with self.subTest(command=command):
                self.assertEqual(self.worker.call(command)[0], S("RelFailure"))

    def test_callable_values_remain_data_in_optional_rows_and_diagnostics(self):
        self.assertEqual(self.worker.call(S("CallableProjection")), (S("kb-rel:callable-data"),))
        for operation in ("And", "Or"):
            result = self.worker.call(E(operation, S("kb-rel:unary"), S("payload")))
            self.assertEqual(result, E("RelFailure", S("rel:" + operation.lower()),
                                      E("RelExpectedTruthPair", (S("kb-rel:unary"), S("payload")))))


if __name__ == "__main__":
    unittest.main()
