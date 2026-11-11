"""Check cache discrimination, bounded execution and honest qualification scope."""

from argparse import Namespace
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import cetta_candidate_readiness as runner


ROOT = Path(__file__).resolve().parents[1]


class CandidateReadinessTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "runtime").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "runtime", prefix="readiness-runner-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "src").mkdir()
        (self.root / "src/value.c").write_text("original runtime\n")
        (self.root / "mk").mkdir()
        (self.root / "mk/external.mk").write_text("test-external:\n\ttrue\n")
        (self.root / "README.md").write_text("original prose\n")

    def identity_command(self, command, **kwargs):
        if command[:2] == ["git", "ls-files"]:
            return b"src/value.c\0mk/external.mk\0README.md\0"
        return b"test compiler version 1\n"

    def args(self, **overrides):
        values = dict(minutes=1, jobs=1, target=[], build="python", dry_run=False,
                      fresh=False, reuse_common_from=None, reuse_common_summary_sha256=None)
        return Namespace(**(values | overrides))

    def child(self, code):
        return [sys.executable, "-c", code]

    def invoke(self, steps, **overrides):
        with (patch.object(runner, "commands", return_value=steps),
              patch.object(runner.subprocess, "check_output", side_effect=self.identity_command),
              contextlib.redirect_stdout(io.StringIO())):
            return runner.run_candidate(self.args(**overrides), self.root)

    def latest(self):
        return json.loads((self.root / "runtime/main-readiness/candidate/latest.json").read_text())

    def test_source_bytes_invalidate_but_prose_does_not(self):
        with patch.object(runner.subprocess, "check_output", side_effect=self.identity_command):
            original = runner.source_identity(self.root)
            (self.root / "README.md").write_text("new tutorial\n")
            self.assertEqual(original, runner.source_identity(self.root))
            (self.root / "src/value.c").write_text("changed semantics\n")
            self.assertNotEqual(original, runner.source_identity(self.root))

    def test_partial_success_is_reused_but_failure_runs_again(self):
        steps = [
            ("build", self.child("pass"), False),
            ("first", self.child("print('valid first check')"), True),
            ("second", self.child("from pathlib import Path; raise SystemExit(not Path('repair').exists())"), True),
        ]
        self.assertEqual(1, self.invoke(steps))
        self.assertEqual("failed", self.latest()["status"])
        (self.root / "repair").touch()
        self.assertEqual(0, self.invoke(steps))
        summary = self.latest()
        self.assertEqual("passed", summary["status"])
        self.assertEqual([False, True, False], [s["reused"] for s in summary["steps"]])
        self.assertEqual(0, self.invoke(steps, fresh=True))
        self.assertFalse(any(s["reused"] for s in self.latest()["steps"]))

    def test_modified_log_or_binary_rejects_cached_pass(self):
        steps = [("check", self.child("print('checked')"), True)]
        self.assertEqual(0, self.invoke(steps))
        self.assertEqual(0, self.invoke(steps))
        self.assertTrue(self.latest()["steps"][0]["reused"])
        log = Path(self.latest()["steps"][0]["log"])
        log.write_text("tampered evidence\n")
        self.assertEqual(0, self.invoke(steps))
        self.assertFalse(self.latest()["steps"][0]["reused"])
        (self.root / "cetta").write_bytes(b"different executable")
        self.assertEqual(0, self.invoke(steps))
        self.assertFalse(self.latest()["steps"][0]["reused"])

    def test_runtime_change_rejects_old_cache(self):
        steps = [("check", self.child("pass"), True)]
        self.assertEqual(0, self.invoke(steps))
        (self.root / "src/value.c").write_text("fixed runtime\n")
        self.assertEqual(0, self.invoke(steps))
        self.assertFalse(self.latest()["steps"][0]["reused"])

    def test_included_recipe_change_rejects_old_cache(self):
        steps = [("test-external", ["make", "-f", "mk/external.mk", "test-external"], True)]
        self.assertEqual(0, self.invoke(steps, target=["test-external"]))
        self.assertEqual(0, self.invoke(steps, target=["test-external"]))
        self.assertTrue(self.latest()["steps"][0]["reused"])
        (self.root / "mk/external.mk").write_text("test-external:\n\tfalse\n")
        self.assertEqual(1, self.invoke(steps, target=["test-external"]))
        self.assertEqual("failed", self.latest()["status"])
        self.assertFalse(self.latest()["steps"][0]["reused"])

    def test_source_edit_during_test_never_qualifies(self):
        steps = [("check", self.child("from pathlib import Path; Path('src/value.c').write_text('race')"), True)]
        self.assertEqual(2, self.invoke(steps))
        self.assertEqual("incomplete", self.latest()["status"])
        self.assertFalse(list((self.root / "runtime/main-readiness/candidate/cache").glob("*.json")))

    def test_silent_child_timeout_kills_process_and_never_passes(self):
        pid = self.root / "child.pid"
        code = "import os, time; from pathlib import Path; Path('child.pid').write_text(str(os.getpid())); time.sleep(30)"
        started = time.monotonic()
        result = runner.run_step(self.child(code), self.root, self.root / "silent.log",
                                 dict(os.environ), started + 0.2)
        self.assertEqual("incomplete", result["status"])
        self.assertLess(time.monotonic() - started, 8)
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pid.read_text()), 0)

    def test_campaign_budget_exhaustion_does_not_start_next_check(self):
        steps = [("first", self.child("import time; time.sleep(30)"), True),
                 ("second", self.child("raise RuntimeError('must not start')"), True)]
        self.assertEqual(2, self.invoke(steps, minutes=0.003))
        summary = self.latest()
        self.assertEqual("incomplete", summary["status"])
        self.assertEqual(["first"], [s["name"] for s in summary["steps"]])

    def test_dry_run_has_no_evidence_or_qualification_prerequisites(self):
        environment = dict(os.environ)
        for name in ("METTAPEDIA_ROOT", "PETTA_ORACLE_ROOT", "CETTA_CONSOLIDATION_BASELINE_RECEIPT"):
            environment.pop(name, None)
        process = subprocess.run(
            [sys.executable, str(ROOT / "scripts/cetta_main_readiness.py"), "--dry-run"],
            cwd=self.root, env=environment, text=True, capture_output=True, timeout=10)
        self.assertEqual(0, process.returncode, process.stderr)
        self.assertIn("20-minute total budget", process.stdout)
        for forbidden in ("test-correctness-all", "test-asan", "test-tsan", "bench-heavy", "cibuildwheel"):
            self.assertNotIn(forbidden, process.stdout)
        self.assertFalse((self.root / "runtime").exists())

    def test_focused_pass_is_not_full_candidate_qualification(self):
        self.assertEqual(0, self.invoke([("test-small", self.child("pass"), True)], target=["test-small"]))
        self.assertEqual("focused", self.latest()["scope"])
        plan = runner.commands("python", 2, ["test-small"])
        self.assertEqual(["build", "test-small"], [name for name, _, _ in plan])


if __name__ == "__main__":
    unittest.main()
