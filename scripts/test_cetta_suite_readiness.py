"""Check the staged suite's gate lists, aggregate expansion, order and stops."""

from argparse import Namespace
import contextlib
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import cetta_candidate_readiness as candidate
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


class RepositorySuiteTests(unittest.TestCase):
    def test_direct_regressions_and_growth_do_not_reach_showcases(self):
        database = runner.make_database(ROOT)
        prerequisites, recipes, _, _ = database
        stages = runner.stage_gates(database)
        planned, missing = runner.selected_gates(stages, database, runner.STAGES, [])
        self.assertFalse(missing)
        self.assertTrue({
            "test-gslt-petta-direct-v1",
            "test-gslt-empty-relations-v1",
            "test-gslt-relational-premises-v1",
            "test-gslt-proof-value-boundary-v1",
            "test-gslt-invocation",
        }.issubset(planned["correctness"]))
        self.assertTrue({
            "test-megalodon-scaling",
            "test-mm0-cogslt-textual-parser-scaling-v1",
            "test-petta-open-key-lookup-growth",
        }.issubset(planned["asymptotic"]))
        # Check named test and benchmark entry points, not just the default
        # plan. Recipes may also invoke a showcase directly, with no edge.
        pending = [name for name in prerequisites if name.startswith(
            ("test-", "bench-", "benchmark-"))] + ["test", "bench"]
        reached = set()
        while pending:
            name = pending.pop()
            if name not in reached:
                reached.add(name)
                pending.extend(prerequisites.get(name, ()))
        self.assertFalse({name for name in reached if name.startswith("showcase-")})
        self.assertTrue({"showcase-petta-self-interpretation", "showcase-gslt-petta-replay",
                         "showcase-mm0-parser", "showcase-megalodon-reader",
                         "showcase-metamath-proof"}.issubset(recipes))
        pipeline_text = "\n".join(recipes.get(name, "") for name in reached)
        self.assertNotIn("showcase/petta/", pipeline_text)
        self.assertNotIn("lib/petta/petta_machine.metta", pipeline_text)
        for harness in (ROOT / "tests/support").glob("*.py"):
            self.assertNotIn("(library petta_machine)", harness.read_text(), harness.name)


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
        # last failures, then the longest; the gate making its own
        # configuration again runs with the others, since its recipe names
        # what it makes; the nested gate runs inside its parent
        self.assertEqual(ran, [
            ("test-sub2", "correctness", "pass"),
            ("test-a", "correctness", "pass"),
            ("test-b", "correctness", "pass"),
            ("test-sub1", "correctness", "pass"),
            ("test-ordered", "correctness", "pass"),
            ("test-serial", "correctness", "pass"),
            ("test-cfg", "correctness", "pass"),
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

    def test_unknown_selection_fails_before_launch(self):
        status, output = self.run_suite(stage="correctness",
                                        target=["test-a", "test-absent", "test-fail"])
        self.assertEqual(status, 2)
        self.assertIn("test-absent, test-fail", output)
        self.assertNotIn("PASS", output)
        self.assertFalse((self.state / "results.tsv").exists())

    def test_nested_gate_can_be_run_without_parent(self):
        status, _ = self.run_suite(stage="correctness", target=["test-inner"])
        self.assertEqual(status, 0)
        self.assertEqual(self.recorded(), [["test-inner", "correctness", "pass"]])

    def test_empty_stage_is_not_a_passing_run(self):
        status, output = self.run_suite(stage="examples")
        self.assertEqual(status, 2)
        self.assertIn("no gates ran", output)
        self.assertNotIn("PASS", output)
        status, output = self.run_suite(stage="correctness", failed=True)
        self.assertEqual(status, 0)
        self.assertIn("No recorded failures selected", output)
        self.assertNotIn("PASS", output)

    def test_failed_preparation_never_retries_within_the_gates(self):
        (self.root / "Makefile").write_text(MAKEFILE + """
test-a test-b: prepared
prepared:
\t@echo failed preparation
\t@exit 1
""")
        status, output = self.run_suite(stage="correctness", target=["test-a", "test-b"])
        self.assertEqual(status, 1)
        self.assertIn("no gates run", output)
        self.assertFalse((self.state / "results.tsv").exists())

    def test_recursive_file_recipe_is_prepared_not_serialized_per_gate(self):
        (self.root / "Makefile").write_text(MAKEFILE + """
test-a test-b: compiled
compiled:
	@$(MAKE) -s artifact
	@touch $@
artifact:
	@touch $@
""")
        database = runner.make_database(self.root)
        self.assertEqual(runner.made_goals(database, "test-a"), [])
        self.assertEqual(runner.made_goals(database, "test-b"), [])
        self.assertIn("compiled", runner.file_prerequisites(database, ["test-a", "test-b"]))
        status, output = self.run_suite(stage="correctness", target=["test-a", "test-b"], jobs=2)
        self.assertEqual(status, 0)
        self.assertIn("2 gates, 0 building other configurations", output)
        self.assertEqual({entry[0] for entry in self.recorded()}, {"test-a", "test-b"})

    def test_changes_to_included_makefiles_invalidate_candidate_receipts(self):
        fragment = self.root / "mk" / "guards.mk"
        fragment.parent.mkdir()
        fragment.write_text("test-all: test-a\n")
        with patch.object(candidate.subprocess, "check_output",
                          return_value=b"Makefile\0mk/guards.mk\0"):
            before = candidate.source_identity(self.root)
            fragment.write_text("test-all: test-b\n")
            self.assertNotEqual(candidate.source_identity(self.root), before)


CONFIGURED = """\
TAG ?= base
BUILD_OBJ_TAG = $(TAG)
BIN = bin-$(TAG)
GOALS = test-use
bin-%:
\t@echo $@ > $@
test-use: $(BIN)
\t@cat $(BIN)
test-other: test-use
test-forced:
\t@$(MAKE) -B -s TAG=forced test-use
test-shell:
\t@for goal in test-use; do $(MAKE) -s TAG=shell "$$goal"; done
.PHONY: test-use test-other test-forced test-shell test-made
test-all: test-made test-forced test-shell test-other
test-made:
\t@$(MAKE) -s TAG=alt $(GOALS) > /dev/null
"""


class ConfigurationPlanTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "runtime").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "runtime", prefix="suite-plan-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "Makefile").write_text(CONFIGURED)
        self.state = self.root / "runtime/main-readiness/suite"

    def test_make_words(self):
        self.assertEqual(runner.make_words(" -s TAG=alt test-use", "gate"),
                         (["TAG=alt"], ["test-use"]))
        self.assertEqual(runner.make_words(" -s --no-print-directory $@-body", "gate"),
                         ([], ["gate-body"]))
        # output redirection ends the make's words
        self.assertEqual(runner.make_words(" -s goal > log 2>&1", "gate"), ([], ["goal"]))
        # the shell's quotes are not part of the words make receives
        self.assertEqual(runner.make_words(' -s SOURCE="vendor/a b" \'goal\'', "gate"),
                         (["SOURCE=vendor/a b"], ["goal"]))
        self.assertIsNone(runner.make_words(' -s SOURCE="open goal', "gate"))
        # a dry run makes nothing, whatever its other flags
        self.assertEqual(runner.make_words(" --no-print-directory -B -n goal", "gate"), "dry")
        self.assertEqual(runner.make_words(" -Bn goal", "gate"), "dry")
        # flags that change what a make makes, or where, are not followed
        for flags in ("-B", "-C dir", "-f other.mk", "-e", "-t", "-sB"):
            self.assertIsNone(runner.make_words(f" {flags} goal", "gate"), flags)

    def test_expansion_leaves_escaped_references_to_the_child(self):
        parent = ((), ("test-made",), ())
        words = runner.expand_words(self.root, parent, ["BIN=out-$$(TAG)", "$(GOALS)"], {})
        self.assertEqual(words, ["BIN=out-$(TAG)", "test-use"])
        # a word that both expands here and escapes for the child is not guessed
        self.assertIsNone(runner.expand_words(self.root, parent, ["$(TAG)-$$(TAG)"], {}))

    def test_configuration_gates_run_beside_the_others_after_one_build(self):
        database = runner.make_database(self.root)
        movable, groups = runner.plan_configurations(
            self.root, database, ["test-made", "test-forced", "test-shell"], 2)
        # -B rebuilds whatever is made first; a goal named by the shell is unknown
        self.assertEqual(movable, ["test-made"])
        self.assertEqual(list(groups), [("alt", "")])
        [(configuration, needed)] = groups[("alt", "")]
        self.assertEqual(configuration[1], ("test-use",))
        self.assertIn("TAG=alt", configuration[0])
        self.assertEqual(needed, ["bin-alt"])
        status, output = self.run_suite(stage="correctness",
                                        target=["test-made", "test-forced", "test-other"])
        self.assertEqual(status, 0, output)
        self.assertIn("3 gates, 1 building other configurations", output)
        self.assertIn("BUILT alt (1 files)", output)
        self.assertEqual((self.root / "bin-alt").read_text(), "bin-alt\n")
        self.assertNotIn("WARN", output)

    def test_plan_is_kept_while_its_inputs_are_unchanged(self):
        self.state.mkdir(parents=True)
        database = runner.make_database(self.root)
        first = runner.plan_configurations(self.root, database, ["test-made"], 1, self.state)
        with patch.object(runner, "plan_configurations_afresh",
                          side_effect=AssertionError("planned again")):
            self.assertEqual(
                runner.plan_configurations(self.root, database, ["test-made"], 1, self.state),
                first)
        (self.root / "Makefile").write_text(CONFIGURED.replace("TAG=alt", "TAG=other"))
        database = runner.make_database(self.root)
        movable, groups = runner.plan_configurations(self.root, database, ["test-made"], 1,
                                                     self.state)
        self.assertEqual(list(groups), [("other", "")])

    def test_rebuilds_reported_are_prepared_products(self):
        self.state.mkdir(parents=True)
        stamp = self.state / "prepared.stamp"
        stamp.write_text("")
        os.utime(stamp, (1, 1))
        for name in ("src/a.alt.o", "runtime/cetta-alt", "runtime/objects/b.alt.o",
                     "runtime/gate-work/c.alt.o", "src/d.other.o"):
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text("")
        self.assertEqual(runner.rebuilt_since(self.root, stamp, {("alt", ""): []}),
                         ["./runtime/cetta-alt", "./runtime/objects/b.alt.o", "./src/a.alt.o"])

    def run_suite(self, **overrides):
        values = dict(jobs=2, stage=None, target=[], failed=False, dry_run=False)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = runner.run_suite(Namespace(**(values | overrides)), self.root)
        return status, output.getvalue()


if __name__ == "__main__":
    unittest.main()
