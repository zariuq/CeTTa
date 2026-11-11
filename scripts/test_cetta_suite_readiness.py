"""Check the staged suite's gate lists, aggregate expansion, order and stops."""

from argparse import Namespace
import contextlib
import io
from pathlib import Path
import tempfile
import unittest

import cetta_suite_readiness as runner


ROOT = Path(__file__).resolve().parents[1]

MAKEFILE = """\
.PHONY: test-all test-asymptotic test-examples test-comprehensive
test-all: test-a test-b test-inner test-agg test-ordered test-serial test-cfg check-z test-undeclared
test-a: ; @echo a
test-b: test-inner ; @echo b
test-inner: ; @echo inner
test-agg: test-sub1 test-sub2
test-sub1: ; @echo sub1
test-sub2: ; @echo sub2
test-ordered: test-x .WAIT test-y
test-x: ; @echo x
test-y: ; @echo y
.NOTPARALLEL: test-serial
test-serial: test-p1 test-p2
test-p1: ; @echo p1
test-p2: ; @echo p2
test-cfg: ; @$(MAKE) -s test-a
check-z: ; @echo z
test-undeclared: ; @echo undeclared
test-asymptotic: test-grow
test-grow: ; @echo grow
test-comprehensive: test-fail
test-fail: ; @exit 1
.PHONY: test-a test-b test-inner test-agg test-sub1 test-sub2 test-ordered test-x test-y
.PHONY: test-serial test-p1 test-p2 test-cfg test-grow test-fail check-z
"""


class SuiteReadinessTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "runtime").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "runtime", prefix="suite-runner-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "Makefile").write_text(MAKEFILE)
        self.state = self.root / "runtime/main-readiness/suite"

    def run_suite(self, **overrides):
        values = dict(jobs=1, stage=None, target=[], failed=False, dry_run=False)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = runner.run_suite(Namespace(**(values | overrides)), self.root)
        return status, output.getvalue()

    def recorded(self):
        return [line.split("\t")[1:4] for line in
                (self.state / "results.tsv").read_text().splitlines()]

    def test_stages_and_expansion(self):
        database = runner.make_database(self.root)
        stages = runner.stage_gates(database)
        self.assertEqual(stages["correctness"], ["test-a", "test-b", "test-inner", "test-agg",
                                                 "test-ordered", "test-serial", "test-cfg",
                                                 "check-z", "test-undeclared"])
        self.assertEqual(stages["asymptotic"], ["test-grow"])
        self.assertEqual(stages["examples"], [])
        self.assertEqual(stages["comprehensive"], ["test-fail"])
        # only the aggregate without a recipe, order or serialization expands
        self.assertEqual(runner.expand_aggregates(stages["correctness"], database),
                         ["test-a", "test-b", "test-inner", "test-sub1", "test-sub2",
                          "test-ordered", "test-serial", "test-cfg", "check-z",
                          "test-undeclared"])

    def test_order_record_and_stop(self):
        self.state.mkdir(parents=True)
        (self.state / "results.tsv").write_text(
            "t\ttest-sub2\tcorrectness\tfail\t1.0\n"
            "t\ttest-a\tcorrectness\tpass\t7.0\n"
            "t\ttest-b\tcorrectness\tpass\t3.0\n")
        status, output = self.run_suite()
        self.assertEqual(status, 1)
        self.assertIn("SUITE FAIL at the comprehensive stage", output)
        ran = [(gate, stage, result) for gate, stage, result in self.recorded()[3:]]
        # the configuration-building gate first; then last failures, then the
        # longest; the nested gate runs inside its parent
        self.assertEqual(ran, [
            ("test-cfg", "correctness", "pass"),
            ("test-sub2", "correctness", "pass"),
            ("test-a", "correctness", "pass"),
            ("test-b", "correctness", "pass"),
            ("test-sub1", "correctness", "pass"),
            ("test-ordered", "correctness", "pass"),
            ("test-serial", "correctness", "pass"),
            ("check-z", "correctness", "pass"),
            ("test-undeclared", "correctness", "pass"),
            ("test-grow", "asymptotic", "pass"),
            ("test-fail", "comprehensive", "fail"),
        ])
        # a rerun of the failures runs only them
        status, _ = self.run_suite(failed=True)
        self.assertEqual(status, 1)
        self.assertEqual(self.recorded()[-1], ["test-fail", "comprehensive", "fail"])
        self.assertEqual(len(self.recorded()), 3 + 11 + 1)

    def test_target_names_an_aggregate(self):
        status, output = self.run_suite(dry_run=True, stage="correctness", target=["test-agg"])
        self.assertEqual(status, 0)
        self.assertIn("  test-sub1\n  test-sub2\n", output)
        self.assertNotIn("test-a\n", output)


if __name__ == "__main__":
    unittest.main()
