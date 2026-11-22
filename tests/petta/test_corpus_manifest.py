#!/usr/bin/env python3

from contextlib import redirect_stdout
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest import mock


sys.dont_write_bytecode = True
REPO_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPO_ROOT / "scripts" / "petta_corpus_manifest.py"
SPEC = importlib.util.spec_from_file_location(
    "petta_corpus_manifest", MODULE_PATH
)
assert SPEC is not None and SPEC.loader is not None
CORPUS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CORPUS)

COMMIT_A = "a" * 40
COMMIT_B = "b" * 40

# A stand-in for `cetta --lang petta examples/X.metta`: it prints the text of
# every `;OUT ` line of the program and exits with the `;EXIT ` code.
FAKE_CETTA = """\
import sys
exit_code = 0
for line in open(sys.argv[-1], encoding="utf-8"):
    if line.startswith(";OUT "):
        sys.stdout.write(line[5:])
    elif line.startswith(";EXIT "):
        exit_code = int(line[6:])
sys.exit(exit_code)
"""

# A stand-in for SWI-PeTTa's run.sh, for the optional report only.
FAKE_RUN_SH = """\
#!/bin/sh
sed -n 's/^;SWI //p' "$1"
"""

# Verdict lines end in a space, so these texts spell every newline out.
TILE = "(= (count) 4)\n!(test (count) 4)\n;OUT is 4, should 4. ✅ \n"
TILE_FIXED = TILE.replace("(= (count) 4)", "(= (count) 3)")
TILE_PATCH = (
    "--- a/examples/tile.metta\n"
    "+++ b/examples/tile.metta\n"
    "@@ -1,3 +1,3 @@\n"
    "-(= (count) 4)\n"
    "+(= (count) 3)\n"
    " !(test (count) 4)\n"
    " ;OUT is 4, should 4. ✅ \n"
)


def quiet(function, *arguments, **keywords):
    with redirect_stdout(io.StringIO()) as output:
        result = function(*arguments, **keywords)
    return result, output.getvalue()


class NormalizationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.root = Path("/corpus/base")

    def test_exact_leading_transport_banner_is_the_only_dropped_line(
        self,
    ) -> None:
        body = "is 1, should 1. ✅ \ntrue\n"
        self.assertEqual(
            CORPUS.normalize_oracle_stdout(
                CORPUS.MORK_READY_BANNER + body, self.root
            ),
            body,
        )
        self.assertEqual(
            CORPUS.normalize_oracle_stdout("MORK init: failed\n" + body, self.root),
            "MORK init: failed\n" + body,
        )
        self.assertEqual(
            CORPUS.normalize_oracle_stdout(
                body + CORPUS.MORK_READY_BANNER, self.root
            ),
            body + CORPUS.MORK_READY_BANNER,
        )
        self.assertEqual(
            CORPUS.normalize_oracle_stderr(
                CORPUS.PYTHON_RUNTIME_WARNING + "real error\n", self.root
            ),
            "real error\n",
        )
        self.assertEqual(
            CORPUS.normalize_oracle_stderr(
                "real error\n" + CORPUS.PYTHON_RUNTIME_WARNING, self.root
            ),
            "real error\n" + CORPUS.PYTHON_RUNTIME_WARNING,
        )

    def test_exact_leading_optional_mork_failure_is_transport_noise(
        self,
    ) -> None:
        diagnostic = (
            "ERROR: <PETTA_ROOT>/mork_ffi/morkspaces.pl:35:\n"
            "ERROR:    library/3: Unknown procedure: library_path/1\n"
            "Warning: <PETTA_ROOT>/mork_ffi/morkspaces.pl:35:\n"
            "Warning:    Goal (directive) failed: user:"
            "(library(mork_ffi,'morklib.so',_114560),"
            "use_foreign_library(_114560),"
            "current_predicate(mork/3)->writeln(\"MORK init: done\");"
            "writeln(\"MORK init: failed\"))\n"
        )
        self.assertEqual(
            CORPUS.normalize_oracle_stderr(
                diagnostic + CORPUS.PYTHON_RUNTIME_WARNING + "real\n", self.root
            ),
            "real\n",
        )
        self.assertEqual(
            CORPUS.normalize_oracle_stderr("real\n" + diagnostic, self.root),
            "real\n" + diagnostic,
        )
        altered = diagnostic.replace("library_path/1", "other/1")
        self.assertEqual(
            CORPUS.normalize_oracle_stderr(altered, self.root), altered
        )

    def test_exact_leading_optional_janus_failure_is_transport_noise(
        self,
    ) -> None:
        diagnostic = (
            "ERROR: /opt/swipl/library/ext/swipy/janus.pl:116:\n"
            "ERROR:    /opt/swipl/library/ext/swipy/janus.pl:116: "
            "Initialization goal raised exception:\n"
            "ERROR:    open_shared_object/3: libpython3.12.so.1.0: "
            "cannot open shared object file: No such file or directory\n"
        )
        self.assertEqual(
            CORPUS.normalize_oracle_stderr(diagnostic + "real error\n", self.root),
            "real error\n",
        )
        self.assertEqual(
            CORPUS.normalize_oracle_stderr("real error\n" + diagnostic, self.root),
            "real error\n" + diagnostic,
        )
        altered = diagnostic.replace("open_shared_object/3", "load/1")
        self.assertEqual(
            CORPUS.normalize_oracle_stderr(altered, self.root), altered
        )

    def test_nested_roots_are_replaced_whole(self) -> None:
        outer = Path("/corpus")
        inner = Path("/corpus/runtime/work/base")
        text = f"{inner}/examples/x.metta and {outer}/lib\n"
        self.assertEqual(
            CORPUS.normalize_stream(text, outer, (inner,)),
            "<PETTA_ROOT>/examples/x.metta and <PETTA_ROOT>/lib\n",
        )

    def test_printed_variables_are_alpha_canonicalized(self) -> None:
        left = "($_15398 $_15404 $_15398)\n(\"$not-a-variable\" $_9)\n"
        right = "($a#27 $b#27 $a#27)\n(\"$not-a-variable\" $other)\n"
        self.assertEqual(
            CORPUS.alpha_canonicalize_output(left),
            CORPUS.alpha_canonicalize_output(right),
        )

    def test_alpha_canonicalization_preserves_coreference(self) -> None:
        self.assertNotEqual(
            CORPUS.alpha_canonicalize_output("($x $x)\n"),
            CORPUS.alpha_canonicalize_output("($x $y)\n"),
        )

    def test_test_diagnostic_sides_are_canonicalized_independently(
        self,
    ) -> None:
        oracle = "is (-> $_15398 $_15404 Bool), should (-> $_2148 $_2154 Bool). ✅ \n"
        cetta = "is (-> $a#27 $b#27 Bool), should (-> $a $b Bool). ✅ \n"
        self.assertEqual(
            CORPUS.alpha_canonicalize_output(oracle),
            CORPUS.alpha_canonicalize_output(cetta),
        )

    def test_anonymous_variables_remain_distinct(self) -> None:
        self.assertEqual(
            CORPUS.alpha_canonicalize_output("($ $)\n"), "($V0 $V1)\n"
        )

    def test_specializer_diagnostic_is_not_semantic_stdout(self) -> None:
        oracle = "is finish, should finish. ✅ \nNot specialized f_Spec_[+]/3\ntrue\n"
        cetta = "is finish, should finish. ✅ \ntrue\n"
        self.assertEqual(
            CORPUS.semantic_stdout(oracle), CORPUS.semantic_stdout(cetta)
        )
        self.assertEqual(
            CORPUS.semantic_stdout("Not specialized user message\n"),
            "Not specialized user message\n",
        )

    def test_stdout_observation_contracts_distinguish_order(self) -> None:
        forward, reverse = "first\nsecond\n", "second\nfirst\n"
        self.assertFalse(CORPUS.stdout_observation_equal(
            forward, reverse, CORPUS.STDOUT_EXACT_STREAM
        ))
        self.assertTrue(CORPUS.stdout_observation_equal(
            forward, reverse, CORPUS.STDOUT_OCCURRENCE_BAG
        ))

    def test_occurrence_bag_preserves_duplicate_multiplicity(self) -> None:
        self.assertFalse(CORPUS.stdout_observation_equal(
            "same\nsame\n", "same\n", CORPUS.STDOUT_OCCURRENCE_BAG
        ))

    def test_unknown_stdout_observation_contract_fails_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "unknown stdout"):
            CORPUS.stdout_observation("answer\n", "unordered-ish")

    def test_occurrence_bag_ignores_transport_line_termination(self) -> None:
        self.assertTrue(CORPUS.stdout_observation_equal(
            "answer\n", "answer", CORPUS.STDOUT_OCCURRENCE_BAG
        ))
        self.assertFalse(CORPUS.stdout_observation_equal(
            "answer\n", "answer", CORPUS.STDOUT_EXACT_STREAM
        ))


class ProcessTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_prefix_fixture_observes_output_then_stops_live_process(
        self,
    ) -> None:
        exit_contract, stdout, stderr = CORPUS.run_supervised_prefix(
            [sys.executable, "-u", "-c",
             "import sys,time;sys.stdin.readline();print('ready');time.sleep(30)"],
            self.root, dict(os.environ), "input\n", "ready\n", 5.0,
            lambda text: text, lambda text: text,
        )
        self.assertEqual(
            (exit_contract, stdout, stderr), ("supervised-prefix", "ready\n", "")
        )

    def test_prefix_fixture_rejects_a_missing_prefix(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "expected prefix"):
            CORPUS.run_supervised_prefix(
                [sys.executable, "-c", "print('other')"],
                self.root, dict(os.environ), "", "ready\n", 5.0,
                lambda text: text, lambda text: text,
            )

    def test_complete_process_capture_is_bounded(self) -> None:
        exit_code, stdout, stderr, timed_out, limited = (
            CORPUS.run_bounded_process(
                [sys.executable, "-u", "-c",
                 "import sys; sys.stdout.write('x' * 4096)"],
                self.root, dict(os.environ), None, 5.0, max_capture_bytes=1024,
            )
        )
        self.assertNotEqual(exit_code, 0)
        self.assertEqual(len(stdout) + len(stderr), 1024)
        self.assertFalse(timed_out)
        self.assertTrue(limited)

    def test_local_git_fixture_transform_is_exact_and_ephemeral(self) -> None:
        source = self.root / "git-fixture-input.metta"
        source.write_text(
            '!(git-import! "https://github.com/patham9/faiss_ffi" "build.sh")\n'
            "!(import! &self (library lib_faiss))\n",
            encoding="utf-8",
        )
        fixture = CORPUS.CONTROLLED_CASES["git_import2.metta"]
        with CORPUS.local_git_fixture_workspace(
            REPO_ROOT, self.root, source, fixture, directory=self.root
        ) as (workspace, transformed):
            text = transformed.read_text(encoding="utf-8")
            self.assertTrue(text.startswith(fixture["source_prefix"]))
            self.assertIn('"./faiss_ffi"', text)
            self.assertIn('"../../fixture/build.sh"', text)
            self.assertIn("(library faiss_ffi lib_faiss)", text)
            self.assertNotIn("(library lib_faiss)", text)
            self.assertTrue((workspace / "faiss_ffi").is_symlink())
            self.assertTrue((workspace / "fixture" / "faiss.pl").is_file())
        self.assertFalse(workspace.exists())


class PatchTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "examples").mkdir()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write(self, relative: str, text: str) -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def test_patch_applies_at_its_line_and_after_an_offset(self) -> None:
        self.write("examples/tile.metta", TILE)
        patches = CORPUS.parse_unified_diff(TILE_PATCH)
        self.assertEqual(
            CORPUS.patched_texts(self.root, patches),
            {"examples/tile.metta": TILE_FIXED},
        )
        self.write("examples/tile.metta", "; a new first line\n" + TILE)
        self.assertEqual(
            CORPUS.patched_texts(self.root, patches)["examples/tile.metta"],
            "; a new first line\n" + TILE_FIXED,
        )

    def test_changed_context_is_stale_never_fuzzed(self) -> None:
        self.write("examples/tile.metta", TILE.replace("!(test", "!(test  "))
        patches = CORPUS.parse_unified_diff(TILE_PATCH)
        state, detail = CORPUS.overlay_state(self.root, patches)
        self.assertEqual(state, CORPUS.OVERLAY_STALE)
        self.assertIn("hunk 1 (line 1) does not match", detail)

    def test_a_base_that_contains_the_change_is_recognised(self) -> None:
        patches = CORPUS.parse_unified_diff(TILE_PATCH)
        self.write("examples/tile.metta", TILE)
        self.assertEqual(
            CORPUS.overlay_state(self.root, patches), (CORPUS.OVERLAY_APPLIES, "")
        )
        self.write("examples/tile.metta", TILE_FIXED)
        self.assertEqual(
            CORPUS.overlay_state(self.root, patches), (CORPUS.OVERLAY_IN_BASE, "")
        )

    def test_insertion_deletion_and_missing_final_newline(self) -> None:
        self.write("examples/a.metta", "one\ntwo\nthree")
        patch = (
            "--- a/examples/a.metta\n+++ b/examples/a.metta\n"
            "@@ -1,3 +1,4 @@\n one\n+one and a half\n two\n-three\n"
            "\\ No newline at end of file\n+three\n"
        )
        patches = CORPUS.parse_unified_diff(patch)
        self.assertEqual(
            CORPUS.patched_texts(self.root, patches)["examples/a.metta"],
            "one\none and a half\ntwo\nthree\n",
        )
        self.assertEqual(
            CORPUS.parse_unified_diff(CORPUS.format_file_patches(patches)),
            patches,
        )

    def test_created_and_deleted_files(self) -> None:
        self.write("examples/old.metta", "gone\n")
        patches = CORPUS.parse_unified_diff(
            "--- /dev/null\n+++ b/lib/helper.metta\n@@ -0,0 +1 @@\n+(= (h) 1)\n"
            "--- a/examples/old.metta\n+++ /dev/null\n@@ -1 +0,0 @@\n-gone\n"
        )
        self.assertEqual(
            CORPUS.patched_texts(self.root, patches),
            {"lib/helper.metta": "(= (h) 1)\n", "examples/old.metta": None},
        )
        CORPUS.write_patched(self.root, patches)
        self.assertFalse((self.root / "examples/old.metta").exists())
        self.assertEqual(
            CORPUS.overlay_state(self.root, patches), (CORPUS.OVERLAY_IN_BASE, "")
        )

    def test_blank_context_line_without_its_space_still_parses(self) -> None:
        self.write("examples/a.metta", "x\n\ny\n")
        patches = CORPUS.parse_unified_diff(
            "--- a/examples/a.metta\n+++ b/examples/a.metta\n"
            "@@ -1,3 +1,3 @@\n x\n\n-y\n+z\n"
        )
        self.assertEqual(
            CORPUS.patched_texts(self.root, patches)["examples/a.metta"],
            "x\n\nz\n",
        )

    def test_paths_must_stay_inside_the_base(self) -> None:
        for path in ("../escape.metta", "/etc/passwd", "a/../../x"):
            with self.subTest(path=path), self.assertRaisesRegex(
                CORPUS.PatchError, "inside the base"
            ):
                CORPUS.parse_unified_diff(
                    f"--- {path}\n+++ {path}\n@@ -1 +1 @@\n-a\n+b\n"
                )

    def test_malformed_patches_are_rejected(self) -> None:
        for text, message in (
            ("no diff here\n", "no file patch"),
            ("--- a/x\n+++ b/x\n", "without hunks"),
            ("--- a/x\n+++ b/x\n@@ -1,2 +1,2 @@\n a\n", "truncated"),
            ("--- a/x\n+++ b/x\n@@ -1 +1 @@\n a\n-b\n", "longer than"),
            ("--- a/x\n+++ b/x\n@@ junk @@\n", "malformed hunk header"),
        ):
            with self.subTest(text=text), self.assertRaisesRegex(
                CORPUS.PatchError, message
            ):
                CORPUS.parse_unified_diff(text)

    def test_overlay_never_writes_through_a_linked_directory(self) -> None:
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "lib.metta").write_text("a\n", encoding="utf-8")
        workspace = self.root / "workspace"
        workspace.mkdir()
        os.symlink(outside, workspace / "mork_ffi", target_is_directory=True)
        patches = CORPUS.parse_unified_diff(
            "--- a/mork_ffi/lib.metta\n+++ b/mork_ffi/lib.metta\n"
            "@@ -1 +1 @@\n-a\n+b\n"
        )
        with self.assertRaisesRegex(CORPUS.PatchError, "linked directory"):
            CORPUS.write_patched(workspace, patches)
        self.assertEqual((outside / "lib.metta").read_text(), "a\n")


class AssertionTests(unittest.TestCase):
    def run_of(self, stdout: str, exit_code=0, source="!(test (f) 1)\n"):
        return CORPUS.Run(exit_code, stdout, "", 0.0, source)

    def test_assertion_sites_skip_comments_and_strings(self) -> None:
        sites = CORPUS.assertion_sites(
            '; !(test (commented) out)\n'
            '!(println! "!(test (in a string) out)")\n'
            '!(test (f) 1)\n'
            '(= (check $x) (test $x 1))\n'
            '!(assertEqual (f) 1)\n'
            '!(assertaPredicate (Predicate (hello world)))\n'
            '!(test-func)\n'
        )
        self.assertEqual(sites, CORPUS.AssertionSites(1, 2, 1))
        self.assertTrue(sites.self_checking)
        self.assertEqual(sites.required_verdicts, 1)
        none = CORPUS.assertion_sites('!(println! "(test x)") ; (assert y)\n')
        self.assertFalse(none.self_checking)
        nested = CORPUS.assertion_sites("!(let $x 1 (test $x 1))\n")
        self.assertEqual(nested.required_verdicts, 1)
        asserts_only = CORPUS.assertion_sites("!(assertEqual 1 1)\n")
        self.assertEqual(asserts_only.required_verdicts, 0)

    def test_a_self_check_passes_only_when_every_assertion_passes(self) -> None:
        good = "is 1, should 1. ✅ \ntrue\n"
        self.assertEqual(
            CORPUS.self_check_outcome(self.run_of(good), 5)[0], True
        )
        cases = {
            "failed verdict": (self.run_of("is 2, should 1. ❌ \n", 1), "is 2"),
            "failed assertion": (
                self.run_of("Assertion failed: (== 1 2)\n", 1,
                            "!(assertEqual 1 2)\n"),
                "Assertion failed",
            ),
            "nonzero exit": (self.run_of(good, 2), "exit 2"),
            "timeout": (self.run_of(good, CORPUS.ABNORMAL_TIMEOUT), "timed out"),
            "no verdict": (self.run_of("true\n"), "expected a verdict, 0"),
            "missing verdict": (
                self.run_of(good, 0, "!(test (f) 1)\n!(test (g) 2)\n"),
                "2 top-level test(s) expected a verdict, 1",
            ),
        }
        for label, (run, detail) in cases.items():
            with self.subTest(label):
                passed, reason = CORPUS.self_check_outcome(run, 5)
                self.assertFalse(passed)
                self.assertIn(detail, reason)
        asserts_only = self.run_of("", 0, "!(assertEqual 1 1)\n")
        self.assertTrue(CORPUS.self_check_outcome(asserts_only, 5)[0])

    def test_a_recorded_failure_must_keep_failing(self) -> None:
        entry = {"name": "x.metta", "mode": "self-check", "timeout": 5,
                 "failing": "is 2, should 1. ❌ "}
        failing = self.run_of("is 2, should 1. ❌ \n", 1)
        self.assertEqual(CORPUS.judge(entry, failing, None, 5).status, "XFAIL")
        fixed = self.run_of("is 1, should 1. ✅ \n")
        verdict = CORPUS.judge(entry, fixed, None, 5)
        self.assertEqual(verdict.status, "FAIL")
        self.assertIn("now passes", verdict.detail)
        del entry["failing"]
        self.assertEqual(CORPUS.judge(entry, fixed, None, 5).status, "PASS")
        self.assertEqual(CORPUS.judge(entry, failing, None, 5).status, "FAIL")

    def test_golden_comparison(self) -> None:
        entry = {"name": "g.metta", "mode": "golden", "timeout": 5}
        run = self.run_of("b\na\n", 0, "!(f)\n")
        self.assertEqual(CORPUS.judge(entry, run, "b\na\n", 5).status, "PASS")
        verdict = CORPUS.judge(entry, run, "a\nb\n", 5)
        self.assertEqual(verdict.status, "FAIL")
        self.assertIn("line 1", verdict.detail)
        bag = dict(entry, observation=CORPUS.STDOUT_OCCURRENCE_BAG)
        self.assertEqual(CORPUS.judge(bag, run, "a\nb\n", 5).status, "PASS")
        self.assertEqual(
            CORPUS.judge(entry, self.run_of("b\na\n", 1), "b\na\n", 5).status,
            "FAIL",
        )
        self.assertEqual(
            CORPUS.judge(dict(entry, exit=1), self.run_of("b\na\n", 1),
                         "b\na\n", 5).status,
            "PASS",
        )
        self.assertEqual(CORPUS.judge(entry, run, None, 5).status, "FAIL")

    def test_recorded_failure_cannot_hide_a_new_failure_or_abnormal_exit(self) -> None:
        failure = "is 2, should 1. ❌ "
        entry = {"name": "x.metta", "mode": "self-check", "timeout": 5,
                 "failing": failure}
        self.assertEqual(CORPUS.judge(entry, self.run_of(failure + "\n", 1), None, 5).status,
                         "XFAIL")
        for run in [self.run_of("is 3, should 1. ❌ \n", 1),
                    *(self.run_of(failure + "\n", status) for status in
                      (0, 2, -11, CORPUS.ABNORMAL_TIMEOUT, CORPUS.ABNORMAL_OUTPUT_LIMIT))]:
            with self.subTest(exit=run.exit, stdout=run.stdout):
                self.assertEqual(CORPUS.judge(entry, run, None, 5).status, "FAIL")

    def test_a_negative_case_must_fail_as_recorded(self) -> None:
        entry = {"name": "n.metta", "mode": "negative", "timeout": 5,
                 "exit": 1, "failure": "Assertion failed: (== 1 2)"}
        failing = self.run_of("Assertion failed: (== 1 2)\n", 1)
        self.assertEqual(CORPUS.judge(entry, failing, None, 5).status, "PASS")
        for run in (
            self.run_of("", 0),
            self.run_of("Assertion failed: (== 1 3)\n", 1),
            self.run_of("Assertion failed: (== 1 2)\n", 2),
        ):
            with self.subTest(stdout=run.stdout, exit=run.exit):
                self.assertEqual(CORPUS.judge(entry, run, None, 5).status, "FAIL")


class CorpusTests(unittest.TestCase):
    """End to end on a small base, with stand-in engines."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        top = Path(self.temporary.name)
        self.base = top / "petta"
        self.examples = self.base / "examples"
        self.examples.mkdir(parents=True)
        (self.base / "lib").mkdir()
        (self.base / "lib" / "lib_x.metta").write_text("(= (x) 1)\n")
        self.overlay_dir = top / "overlay"
        self.overlay_dir.mkdir()
        self.golden_dir = top / "golden"
        self.manifest = top / "corpus" / "manifest.json"
        self.out = top / "out"
        self.cetta = top / "bin" / "cetta"
        self.cetta.parent.mkdir()
        self.cetta.write_text(f"#!{sys.executable}\n" + FAKE_CETTA)
        self.cetta.chmod(self.cetta.stat().st_mode | stat.S_IXUSR)
        self.set_head("stable-fixed", COMMIT_A)
        self.write_example("pass.metta", "!(test (x) 1)\n;OUT is 1, should 1. ✅ \n")
        self.write_example(
            "fail.metta", "!(test (y) 1)\n;OUT is 2, should 1. ❌ \n;EXIT 1\n"
        )
        self.write_example("printer.metta", "!(f)\n;OUT (f)\n;OUT done\n")
        self.write_example("tile.metta", TILE)
        (self.overlay_dir / "tile.patch").write_text(TILE_PATCH)
        (self.overlay_dir / "tile.why").write_text(
            "the count is 3, not 4; see upstream issue 1\n"
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def set_head(self, branch: str, commit: str) -> None:
        git = self.base / ".git"
        (git / "refs" / "heads").mkdir(parents=True, exist_ok=True)
        (git / "HEAD").write_text(f"ref: refs/heads/{branch}\n")
        (git / "refs" / "heads" / branch).write_text(commit + "\n")

    def write_example(self, name: str, text: str) -> None:
        (self.examples / name).write_text(text, encoding="utf-8")

    def sync(self, base: str = "stable-fixed", **keywords):
        return quiet(
            CORPUS.refresh_corpus, petta_dir=self.base,
            manifest_path=self.manifest, cetta=self.cetta,
            out_dir=self.out / "sync", timeout_override=None,
            excluded_capabilities=set(), sync=True, base_name=base,
            overlay_dir=self.overlay_dir, golden_dir=self.golden_dir,
            **keywords,
        )

    def accept(self):
        return quiet(
            CORPUS.refresh_corpus, petta_dir=self.base,
            manifest_path=self.manifest, cetta=self.cetta,
            out_dir=self.out / "accept", timeout_override=None,
            excluded_capabilities=set(), sync=False,
            overlay_dir=self.overlay_dir, golden_dir=self.golden_dir,
        )

    def compare(self, require_match: bool = True):
        return quiet(
            CORPUS.compare_manifest, self.base, self.manifest, self.cetta,
            self.out / "compare", set(), set(), None, None, True,
            require_match, self.overlay_dir, self.golden_dir,
        )

    def entries(self) -> dict:
        return {
            entry["name"]: entry
            for entry in CORPUS.load_manifest(self.manifest)["entries"]
        }

    def test_sync_classifies_records_the_base_and_writes_goldens(self) -> None:
        status, output = self.sync()
        self.assertEqual(status, 0, output)
        manifest = CORPUS.load_manifest(self.manifest)
        self.assertEqual(manifest["base"], f"stable-fixed@{COMMIT_A}")
        entries = self.entries()
        self.assertEqual(entries["pass.metta"],
                         {"name": "pass.metta", "mode": "self-check", "timeout": 60})
        self.assertEqual(entries["fail.metta"]["failing"], "is 2, should 1. ❌ ")
        self.assertEqual(entries["printer.metta"]["mode"], "golden")
        self.assertEqual(
            (self.golden_dir / "printer.stdout").read_text(), "(f)\ndone\n"
        )
        self.assertEqual(entries["tile.metta"]["overlay"], "tile.patch")
        self.assertNotIn("failing", entries["tile.metta"])
        self.assertIn("+++ b/tests/petta/golden/printer.stdout", output)
        self.assertIn("failing self-check: fail.metta", output)
        # The base itself is never written.
        self.assertEqual((self.examples / "tile.metta").read_text(), TILE)
        self.assertEqual(CORPUS.dump_manifest(manifest), self.manifest.read_text())

    def test_the_gate_rejects_recorded_failures_and_names_drift(self) -> None:
        self.sync()
        with self.assertRaisesRegex(RuntimeError, "1 example"):
            self.compare()
        summary, output = self.compare(require_match=False)
        self.assertEqual(summary["counts"], {"PASS": 3, "XFAIL": 1}, output)
        _, verify_output = quiet(
            CORPUS.verify_manifest, self.base, self.manifest, True,
            self.overlay_dir, self.golden_dir,
        )
        self.assertIn("PASS: PeTTa corpus manifest is consistent", verify_output)

        self.write_example("printer.metta", "!(f)\n;OUT (f)\n;OUT changed\n")
        self.write_example("fail.metta", "!(test (y) 1)\n;OUT is 1, should 1. ✅ \n")
        self.write_example("new.metta", "!(test (z) 1)\n")
        (self.examples / "pass.metta").unlink()
        with self.assertRaisesRegex(RuntimeError, "do not meet their oracle"):
            self.compare()
        summary, output = self.compare(require_match=False)
        failures = "\n".join(summary["failures"])
        self.assertIn("printer.metta: stdout differs from golden", failures)
        self.assertIn("fail.metta: now passes its own assertions", failures)
        self.assertIn("new.metta: in the base but not in the corpus", failures)
        self.assertIn("pass.metta: in the corpus but not in the base", failures)

    def test_accept_refreshes_and_refuses_a_moved_base(self) -> None:
        self.sync()
        self.write_example("printer.metta", "!(f)\n;OUT (f)\n;OUT changed\n")
        status, output = self.accept()
        self.assertEqual(status, 0, output)
        self.assertIn("-done", output)
        self.assertIn("+changed", output)
        self.assertEqual(self.compare(require_match=False)[0]["counts"], {"PASS": 3, "XFAIL": 1})
        self.set_head("stable-fixed", COMMIT_B)
        with self.assertRaisesRegex(RuntimeError, "petta-corpus-sync"):
            self.accept()
        _, output = self.compare(require_match=False)
        self.assertIn("NOTE: the base checkout is at bbbbbbbbbbbb", output)

    def test_sync_retires_an_overlay_the_base_now_contains(self) -> None:
        self.sync()
        self.write_example("tile.metta", TILE_FIXED)
        self.set_head("stable-fixed", COMMIT_B)
        status, output = self.sync()
        self.assertEqual(status, 0, output)
        self.assertIn("retired: tile.metta (now in base)", output)
        self.assertFalse((self.overlay_dir / "tile.patch").exists())
        self.assertFalse((self.overlay_dir / "tile.why").exists())
        self.assertNotIn("overlay", self.entries()["tile.metta"])
        self.assertEqual(
            CORPUS.load_manifest(self.manifest)["base"], f"stable-fixed@{COMMIT_B}"
        )
        self.assertEqual(self.compare(require_match=False)[0]["counts"], {"PASS": 3, "XFAIL": 1})

    def test_sync_names_an_overlay_that_no_longer_applies(self) -> None:
        self.sync()
        self.write_example("tile.metta", TILE.replace("(count) 4)", "(count) 5)", 1))
        status, output = self.sync()
        self.assertEqual(status, 1)
        self.assertIn("STALE OVERLAY: tile.patch: no longer applies", output)
        self.assertTrue((self.overlay_dir / "tile.patch").exists())
        with self.assertRaisesRegex(RuntimeError, "do not meet their oracle"):
            self.compare()

    def test_sync_refuses_a_base_named_for_another_branch(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "PETTA_CORPUS_BASE=stable-fixed"):
            self.sync(base="upstream")
        self.set_head("main", COMMIT_A)
        status, _ = self.sync(base="upstream")
        self.assertEqual(status, 0)
        self.assertEqual(
            CORPUS.load_manifest(self.manifest)["base"], f"upstream@{COMMIT_A}"
        )

    def test_a_snapshot_needs_its_commit(self) -> None:
        for path in sorted((self.base / ".git").rglob("*"), reverse=True):
            path.rmdir() if path.is_dir() else path.unlink()
        (self.base / ".git").rmdir()
        with self.assertRaisesRegex(RuntimeError, "PETTA_CORPUS_BASE_COMMIT"):
            self.sync()
        status, _ = self.sync(base_commit=COMMIT_B)
        self.assertEqual(status, 0)
        self.assertEqual(
            CORPUS.load_manifest(self.manifest)["base"], f"stable-fixed@{COMMIT_B}"
        )

    def test_negative_cases_are_kept_and_checked(self) -> None:
        self.write_example(
            "probe.metta",
            "!(assertEqual 1 2)\n;OUT Assertion failed: (== 1 2)\n;EXIT 1\n",
        )
        self.sync()
        manifest = CORPUS.load_manifest(self.manifest)
        for entry in manifest["entries"]:
            if entry["name"] == "probe.metta":
                entry.pop("failing")
                entry.update(mode="negative", exit=1,
                             failure="Assertion failed: (== 1 2)")
        self.manifest.write_text(CORPUS.dump_manifest(manifest))
        status, _ = self.accept()
        self.assertEqual(status, 0)
        self.assertEqual(self.entries()["probe.metta"]["mode"], "negative")
        self.assertEqual(self.compare(require_match=False)[0]["counts"], {"PASS": 4, "XFAIL": 1})
        self.write_example("probe.metta", "!(assertEqual 1 1)\n")
        _, output = self.compare(require_match=False)
        self.assertIn("probe.metta: expected exit 1", output)

    def test_malformed_manifests_are_rejected(self) -> None:
        self.sync()
        pristine = CORPUS.load_manifest(self.manifest)
        mutations = {
            "unknown field": lambda e: e[0].update(colour="red"),
            "mode must be": lambda e: e[0].update(mode="oracle"),
            "timeout must be": lambda e: e[0].update(timeout=0),
            "failing is": lambda e: e[2].update(failing="x"),
            "fixture must be": lambda e: e[0].update(fixture="scripted-quit-session"),
            "overlay must be": lambda e: e[3].update(overlay="other.patch"),
            "a negative case records": lambda e: e[0].update(mode="negative"),
            "not name-sorted": lambda e: e.reverse(),
            "duplicate names": lambda e: e.__setitem__(1, copy.deepcopy(e[0])),
        }
        for message, mutate in mutations.items():
            with self.subTest(message):
                manifest = copy.deepcopy(pristine)
                mutate(manifest["entries"])
                self.manifest.write_text(json.dumps(manifest))
                with self.assertRaisesRegex(RuntimeError, message):
                    CORPUS.check_corpus(
                        self.base, self.manifest, self.overlay_dir,
                        self.golden_dir, True,
                    )

    def test_orphan_files_and_missing_reasons_are_named(self) -> None:
        self.sync()
        (self.golden_dir / "stray.stdout").write_text("x\n")
        (self.overlay_dir / "pass.patch").write_text(TILE_PATCH)
        (self.overlay_dir / "tile.why").write_text("first line\nsecond line\n")
        check = CORPUS.check_corpus(
            self.base, self.manifest, self.overlay_dir, self.golden_dir, True
        )
        problems = "\n".join(check.problems)
        self.assertIn("golden stray.stdout belongs to no golden entry", problems)
        self.assertIn("overlay pass.patch has no .why", problems)
        self.assertIn("tile.why: must be exactly one non-empty line", problems)
        self.assertIn("tile.metta", check.entry_problems)

    def test_capability_exclusion_is_exact_and_fail_closed(self) -> None:
        entries = [
            {"name": "a.metta", "requires": ["git", "lib-prolog"]},
            {"name": "b.metta", "requires": ["lib-prolog"]},
            {"name": "c.metta"},
        ]
        selected, excluded = CORPUS.select_entries(entries, set(), {"git"}, None)
        self.assertEqual(excluded, ["a.metta"])
        self.assertEqual([entry["name"] for entry in selected], ["b.metta", "c.metta"])
        with self.assertRaisesRegex(RuntimeError, "unknown optional capabilities"):
            CORPUS.select_entries(entries, set(), {"typo"}, None)
        with self.assertRaisesRegex(RuntimeError, "absent from the manifest"):
            CORPUS.select_entries(entries, {"z.metta"}, set(), None)

    def test_export_writes_a_patch_for_the_stable_fixed_queue(self) -> None:
        overlays, problems = CORPUS.load_overlays(self.overlay_dir)
        self.assertEqual(problems, [])
        output = CORPUS.export_overlay(overlays["tile.metta"], self.out / "export")
        text = output.read_text()
        self.assertTrue(text.startswith("the count is 3, not 4; see upstream issue 1\n"))
        self.assertEqual(CORPUS.parse_unified_diff(text), overlays["tile.metta"].patches)
        status, output = quiet(CORPUS.main, [
            "export-overlay", "--name", "tile", "--out", str(self.out / "export2"),
            "--overlay-dir", str(self.overlay_dir), "--petta-dir", str(self.base),
        ])
        self.assertEqual(status, 0)
        self.assertIn("export2/tile.patch", output)
        self.write_example("tile.metta", TILE_FIXED)
        with redirect_stdout(io.StringIO()), mock.patch(
            "sys.stderr", new_callable=io.StringIO
        ) as stderr:
            status = CORPUS.main([
                "export-overlay", "--name", "tile.metta", "--out",
                str(self.out / "export3"), "--overlay-dir",
                str(self.overlay_dir), "--petta-dir", str(self.base),
            ])
        self.assertEqual(status, 1)
        self.assertIn("does not apply to that checkout (in-base)", stderr.getvalue())

    def test_swi_report_lists_divergences_and_never_fails(self) -> None:
        (self.base / "run.sh").write_text(FAKE_RUN_SH)
        self.write_example(
            "printer.metta", "!(f)\n;OUT (f)\n;OUT done\n;SWI (f)\n;SWI done\n"
        )
        self.sync()
        _, output = quiet(
            CORPUS.swi_report, self.base, self.manifest, self.cetta,
            self.out / "swi", set(), None, None, self.overlay_dir,
            self.golden_dir,
        )
        report = (self.out / "swi" / "report.tsv").read_text()
        self.assertIn("printer.metta\tgolden\t0\t0\t1\t", report)
        self.assertIn("pass.metta\tself-check\t0\t0\t0\tline 1", report)
        self.assertIn("3 of 4 example(s) diverge", output)
        self.assertTrue((self.out / "swi" / "diffs" / "pass.diff").is_file())


class GitHeadTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name) / "checkout"
        self.root.mkdir()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_loose_packed_detached_and_worktree_heads(self) -> None:
        git = self.root / ".git"
        (git / "refs" / "heads").mkdir(parents=True)
        (git / "HEAD").write_text("ref: refs/heads/stable-fixed\n")
        (git / "refs" / "heads" / "stable-fixed").write_text(COMMIT_A + "\n")
        self.assertEqual(CORPUS.git_head(self.root), ("stable-fixed", COMMIT_A))
        (git / "refs" / "heads" / "stable-fixed").unlink()
        (git / "packed-refs").write_text(
            "# pack-refs with: peeled\n"
            f"{COMMIT_B} refs/heads/stable-fixed\n^{COMMIT_A}\n"
        )
        self.assertEqual(CORPUS.git_head(self.root), ("stable-fixed", COMMIT_B))
        (git / "HEAD").write_text(COMMIT_A + "\n")
        self.assertEqual(CORPUS.git_head(self.root), (None, COMMIT_A))

        worktree = Path(self.temporary.name) / "worktree"
        worktree.mkdir()
        private = git / "worktrees" / "w"
        private.mkdir(parents=True)
        (private / "commondir").write_text("../..\n")
        (private / "HEAD").write_text("ref: refs/heads/main\n")
        (git / "refs" / "heads" / "main").write_text(COMMIT_B + "\n")
        (worktree / ".git").write_text(f"gitdir: {private}\n")
        self.assertEqual(CORPUS.git_head(worktree), ("main", COMMIT_B))

    def test_a_snapshot_has_no_head(self) -> None:
        self.assertEqual(CORPUS.git_head(self.root), (None, None))


if __name__ == "__main__":
    unittest.main()
