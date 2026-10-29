"""The synthetic curriculum executes complete native calls and survives restart."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKER = os.environ.get("CETTA_VALUE_WORKER")
CLI = os.environ.get("CETTA_KB_CLI")
if WORKER:
    sys.path.insert(0, str(Path(os.environ.get("CETTA_CHANNEL_ROOT", ROOT)) / "tools"))
    from value_worker_client import Worker
    from space_service_client import E, S, encode


@unittest.skipUnless(WORKER, "CETTA_VALUE_WORKER is required")
class CurriculumTests(unittest.TestCase):
    def worker(self):
        return Worker(WORKER, ROOT / "examples/kb/curriculum.metta", "curriculum:run",
                      language="petta", import_mode="relative")

    def test_provenance_optional_data_and_dependencies_survive_restart(self):
        with tempfile.TemporaryDirectory(prefix="cetta-kb-example-", dir=os.environ.get("TMPDIR")) as directory:
            worker = self.worker()
            try:
                first = worker.call(directory)
            finally:
                worker.close()

            self.assertEqual(first[0], S("CurriculumResult"), first)
            self.assertEqual(first[2], E("OriginCounts", 4, 2))
            self.assertEqual({row[1] for row in first[1]}, {S("judgement1"), S("judgement2"), S("judgement3")})
            self.assertEqual({row[2] for row in first[1]}, {S(f"evidence{i}") for i in range(1, 5)})
            self.assertIn(E("NoEvidence", S("judgement4"), S("claim2")), first[3])
            expected = [E("TwoHops", S(a), S(b), S("emit"), S(middle), S(target))
                        for a, b, middle, target in [("dep3", "dep2", "check", "parse"),
                                                    ("dep4", "dep1", "parse", "lex"),
                                                    ("dep4", "dep5", "parse", "check"),
                                                    ("dep6", "dep1", "parse", "lex"),
                                                    ("dep6", "dep5", "parse", "check")]]
            self.assertEqual(sorted(map(encode, first[4])), sorted(map(encode, expected)))
            worker = self.worker()
            try:
                self.assertEqual(encode(worker.call(directory)), encode(first))
            finally:
                worker.close()

    def test_graph_curriculum_survives_restart_with_occurrence_paths(self):
        with tempfile.TemporaryDirectory(prefix="cetta-kb-graph-example-", dir=os.environ.get("TMPDIR")) as directory:
            def run():
                worker = Worker(WORKER, ROOT / "examples/kb/graph_curriculum.metta", "graph-curriculum:run",
                                language="petta", import_mode="relative")
                try:
                    return worker.call(directory)
                finally:
                    worker.close()
            answer = run()
            self.assertEqual(answer[0], S("GraphCurriculum"), answer)
            self.assertTrue(all(part[0] == S("GraphRows") for part in answer[1:]), answer)
            self.assertEqual(len(answer[1][1]), 3)
            self.assertEqual(set(answer[3][1]), {S("parse"), S("check"), S("lex")})
            expected = [E("KgPath", (S("emit"), S("parse"), S("lex")),
                          (S(first), S("dep1")), 2) for first in ("dep4", "dep6")]
            self.assertEqual(sorted(map(encode, answer[4][1])), sorted(map(encode, expected)))
            self.assertEqual(answer[6][1], ())
            self.assertEqual(encode(run()), encode(answer))

    @unittest.skipUnless(CLI, "CETTA_KB_CLI is required for the command-line boundary")
    def test_documented_cli_completes_the_result_query(self):
        for program, constructor in [("run.metta", "CurriculumResult"), ("graph_run.metta", "GraphCurriculum")]:
            with self.subTest(program=program):
                with tempfile.TemporaryDirectory(prefix="cetta-kb-cli-", dir=os.environ.get("TMPDIR")) as directory:
                    store = Path(directory) / "store"
                    store.mkdir(mode=0o700)
                    report = Path(directory) / "report.json"
                    run = subprocess.run([CLI, "--lang", "petta", "--run-contract", "--run-report", str(report),
                                          str(ROOT / "examples/kb" / program), str(store)],
                                         capture_output=True, text=True, timeout=30)
                    self.assertEqual(run.returncode, 0, run.stderr)
                    self.assertIn("(" + constructor + " ", run.stdout)
                    record = json.loads(report.read_text())
                    self.assertEqual(record["queries_unsettled"], 0, record)
                    self.assertEqual(record["queries_declared"], 3, record)
                    self.assertEqual(record["queries"][-1]["answers"], 1, record)
                    self.assertTrue(record["queries"][-1]["observation_complete"], record)


if __name__ == "__main__":
    unittest.main()
