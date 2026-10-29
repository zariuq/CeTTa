"""Source occurrence roles must survive publication, nesting and worker capture."""
import os
import json
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
    from space_service_client import E, S, encode, decode


@unittest.skipUnless(WORKER, "CETTA_VALUE_WORKER is required")
class RuntimeOwnershipTests(unittest.TestCase):
    def setUp(self):
        self.worker = Worker(WORKER, ROOT / "tests/support/kb_runtime_ownership_probe.metta", "kb-owner:run",
                             language="petta", import_mode="relative", num_threads=2)
        self.addCleanup(self.worker.close)
        self.value = E("+", 2, 3)

    def preserved(self, result):
        self.assertEqual(result[0], S("StoreEncoded"), result)
        raw = result[1]
        self.assertEqual(encode(decode(raw.encode() if isinstance(raw, str) else raw)), encode(self.value))

    def test_transaction_publishes_equation_roles_for_same_and_later_requests(self):
        self.preserved(self.worker.call(E("Committed", self.value)))
        self.worker.call(S("Install"))
        self.preserved(self.worker.call(E("NextRequest", self.value)))

    def test_nested_transaction_roles_survive_both_directions(self):
        for mode in ("OuterToInner", "InnerToOuter"):
            with self.subTest(mode=mode):
                self.preserved(self.worker.call(E(mode, self.value)))

    def test_threaded_capture_preserves_value_roles_with_and_without_new_rules(self):
        for mode in ("Parallel", "ParallelAdded"):
            with self.subTest(mode=mode):
                answers = self.worker.call(E(mode, self.value))
                self.assertEqual(len(answers), 2)
                for answer in answers:
                    self.preserved(answer)

    def test_shared_mutation_keeps_preexisting_roles_in_sibling_workers(self):
        for mode in ("direct", "transaction"):
            with self.subTest(mode=mode):
                answers = self.worker.call(E("SiblingMutation", self.value, S(mode)))
                self.assertEqual(len(answers), 2)
                for answer in answers:
                    self.preserved(answer)

    def test_direct_worker_addition_keeps_roles_after_join(self):
        self.worker.call(S("InstallFromWorker"))
        self.preserved(self.worker.call(E("AfterWorker", self.value)))

    @unittest.skipUnless(CLI, "CETTA_KB_CLI is required for entry specialization")
    def test_worker_mutation_invalidates_the_parent_specialized_route(self):
        # Keep the calls at their original top-level entry route: wrapping
        # them in an RPC dispatcher does not warm the same specialization.
        program = """(= (inc $x) (+ $x 1))
(= (apply-twice $f $x) ($f ($f $x)))
!(apply-twice inc 10)
!(collapse (hyperpose ((add-atom &self (= (apply-twice $f $x) 999)) 0)))
!(collapse (apply-twice inc 10))
"""
        with tempfile.TemporaryDirectory(prefix="cetta-worker-route-", dir=os.environ.get("TMPDIR")) as directory:
            source = Path(directory) / "specialization.metta"
            report = Path(directory) / "report.json"
            source.write_text(program)
            for threads in (1, 2):
                with self.subTest(threads=threads):
                    result = subprocess.run([CLI, "--lang", "petta", "--num-threads", str(threads),
                                             "--run-contract", "--run-report", str(report), str(source)],
                                            capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    framed = json.loads(report.read_text())
                    self.assertEqual(framed["queries_declared"], 3)
                    self.assertEqual(framed["queries"][-1]["answers"], 1)
                    self.assertTrue(framed["queries"][-1]["observation_complete"])
                    bag = result.stdout.strip().splitlines()[-1]
                    self.assertTrue(bag.startswith("(") and bag.endswith(")"), bag)
                    self.assertEqual(sorted(int(value) for value in bag[1:-1].split()), [12, 999])

    @unittest.skipUnless(CLI, "CETTA_KB_CLI is required for typed profile checks")
    def test_new_named_space_declarations_do_not_escape_rollback(self):
        program = """(= (rollback-victim $x) $x)
!(collapse (transaction (let $added (add-atom &rollback-created (: rollback-victim (-> String String))) (empty))))
!(rollback-victim 42)
"""
        with tempfile.TemporaryDirectory(prefix="cetta-created-tx-", dir=os.environ.get("TMPDIR")) as directory:
            source = Path(directory) / "created.metta"
            source.write_text(program)
            for profile in [[], ["--profile", "typecheck-v2"]]:
                with self.subTest(profile=profile):
                    result = subprocess.run([CLI, "--lang", "petta", *profile, str(source)],
                                            capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.strip().splitlines()[-1], "42")

    @unittest.skipUnless(CLI, "CETTA_KB_CLI is required for typed profile checks")
    def test_live_callability_policy_matches_inside_and_outside_transaction(self):
        program = """(= (live-caller) (live-callee))
!(let $added (add-atom &self (= (live-callee) 99)) (live-caller))
(= (tx-caller) (tx-callee))
!(transaction (let $added (add-atom &self (= (tx-callee) 99)) (tx-caller)))
"""
        with tempfile.TemporaryDirectory(prefix="cetta-kb-plan-", dir=os.environ.get("TMPDIR")) as directory:
            source = Path(directory) / "callability.metta"
            source.write_text(program)
            for profile in [["--lang", "petta@1.1"], ["--lang", "petta", "--profile", "typecheck-v2"]]:
                with self.subTest(profile=profile):
                    result = subprocess.run([CLI, *profile, str(source)], capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.strip().splitlines(), ["99", "99"])


if __name__ == "__main__":
    unittest.main()
