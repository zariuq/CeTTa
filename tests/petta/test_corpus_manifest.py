#!/usr/bin/env python3

from contextlib import redirect_stdout
import copy
import importlib.util
import io
import json
from pathlib import Path
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
MANIFEST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MANIFEST)


class CorpusManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.examples = self.root / "examples"
        self.examples.mkdir()
        (self.root / "run.sh").write_text("#!/bin/sh\n", encoding="utf-8")

        controlled_names = sorted(MANIFEST.CONTROLLED_CASES)
        corrected_names = sorted(MANIFEST.CORRECTED_CASES)
        hermetic_names = [
            f"case{index:03d}.metta"
            for index in range(
                MANIFEST.EXPECTED_TOTAL
                - len(controlled_names)
                - len(corrected_names)
            )
        ]
        names = sorted(hermetic_names + controlled_names + corrected_names)
        entries = []
        for name in names:
            source = self.examples / name
            source.write_text(f"; {name}\n", encoding="utf-8")
            controlled = name in MANIFEST.CONTROLLED_CASES
            fixture = MANIFEST.FIXTURE_CASES.get(name)
            entry = {
                "name": name,
                "source": f"examples/{name}",
                "source_sha256": MANIFEST.sha256_file(source),
                "git_state": "untracked",
                "class": (
                    fixture["class"]
                    if fixture is not None
                    else "hermetic"
                ),
                "command": [
                    "sh",
                    "run.sh",
                    f"examples/{name}",
                    "--silent",
                ],
                "timeout_seconds": 30.0,
                "oracle": (
                    MANIFEST.pending_oracle(
                        MANIFEST.CONTROLLED_CASES[name]["name"]
                    )
                    if controlled
                    else MANIFEST.oracle_record(
                        0,
                        name + "\n",
                        "",
                        fixture["name"] if fixture is not None else None,
                    )
                ),
            }
            if fixture is not None:
                entry["fixture"] = MANIFEST.fixture_record(
                    REPO_ROOT, fixture
                )
            entries.append(entry)

        self.manifest = {
            "schema": MANIFEST.SCHEMA,
            "petta_revision": "test-revision",
            "run_sh_sha256": MANIFEST.sha256_file(self.root / "run.sh"),
            "generator_sha256": MANIFEST.sha256_file(MODULE_PATH),
            "normalization": MANIFEST.normalization_contract(),
            "counts": {
                "total": len(entries),
                "hermetic": len(hermetic_names) + len(corrected_names),
                "controlled": len(controlled_names),
            },
            "entries": entries,
        }
        self.manifest_path = self.root / "manifest.json"
        self.write_manifest()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_manifest(self) -> None:
        self.manifest_path.write_text(
            json.dumps(self.manifest), encoding="utf-8"
        )

    def verify(self, require_complete: bool = False) -> None:
        with (
            redirect_stdout(io.StringIO()),
            mock.patch.object(
                MANIFEST, "git_revision", return_value="test-revision"
            ),
            mock.patch.object(
                MANIFEST, "tracked_examples", return_value=set()
            ),
        ):
            MANIFEST.verify_manifest(
                self.root, self.manifest_path, require_complete
            )

    def test_exact_leading_transport_banner_is_the_only_dropped_line(
        self,
    ) -> None:
        body = "is 1, should 1. ✅ \ntrue\n"
        self.assertEqual(
            MANIFEST.normalize_oracle_stdout(
                MANIFEST.MORK_READY_BANNER + body, self.root
            ),
            body,
        )
        self.assertEqual(
            MANIFEST.normalize_oracle_stdout(
                "MORK init: failed\n" + body, self.root
            ),
            "MORK init: failed\n" + body,
        )
        self.assertEqual(
            MANIFEST.normalize_oracle_stdout(
                body + MANIFEST.MORK_READY_BANNER, self.root
            ),
            body + MANIFEST.MORK_READY_BANNER,
        )
        self.assertEqual(
            MANIFEST.normalize_oracle_stderr(
                MANIFEST.PYTHON_RUNTIME_WARNING + "real error\n",
                self.root,
            ),
            "real error\n",
        )
        self.assertEqual(
            MANIFEST.normalize_oracle_stderr(
                "real error\n" + MANIFEST.PYTHON_RUNTIME_WARNING,
                self.root,
            ),
            "real error\n" + MANIFEST.PYTHON_RUNTIME_WARNING,
        )

    def test_valid_manifest_verifies(self) -> None:
        self.verify()

    def test_printed_variables_are_alpha_canonicalized(self) -> None:
        left = (
            "($_15398 $_15404 $_15398)\n"
            '("$not-a-variable" $_9)\n'
        )
        right = (
            "($a#27 $b#27 $a#27)\n"
            '("$not-a-variable" $other)\n'
        )
        self.assertEqual(
            MANIFEST.alpha_canonicalize_output(left),
            MANIFEST.alpha_canonicalize_output(right),
        )

    def test_alpha_canonicalization_preserves_coreference(self) -> None:
        shared = MANIFEST.alpha_canonicalize_output("($x $x)\n")
        distinct = MANIFEST.alpha_canonicalize_output("($x $y)\n")
        self.assertNotEqual(shared, distinct)

    def test_test_diagnostic_sides_are_canonicalized_independently(
        self,
    ) -> None:
        oracle = (
            "is (-> $_15398 $_15404 Bool), "
            "should (-> $_2148 $_2154 Bool). ✅ \n"
        )
        cetta = (
            "is (-> $a#27 $b#27 Bool), "
            "should (-> $a $b Bool). ✅ \n"
        )
        self.assertEqual(
            MANIFEST.alpha_canonicalize_output(oracle),
            MANIFEST.alpha_canonicalize_output(cetta),
        )

    def test_anonymous_variables_remain_distinct(self) -> None:
        self.assertEqual(
            MANIFEST.alpha_canonicalize_output("($ $)\n"),
            "($V0 $V1)\n",
        )

    def test_source_mutation_is_detected(self) -> None:
        source = self.examples / self.manifest["entries"][0]["name"]
        source.write_text("; changed\n", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "source digest"):
            self.verify()

    def test_required_hermetic_file_is_pinned_and_mutation_detected(
        self,
    ) -> None:
        entry = self.manifest["entries"][0]
        name = entry["name"]
        relative = "lib/required-support.metta"
        support = self.root / relative
        support.parent.mkdir()
        support.write_text("(support original)\n", encoding="utf-8")
        with mock.patch.dict(
            MANIFEST.HERMETIC_REQUIRED_FILES,
            {name: (relative,)},
        ):
            entry["required_files"] = (
                MANIFEST.hermetic_required_file_records(
                    self.root, name
                )
            )
            self.write_manifest()
            self.verify()
            support.write_text(
                "(support changed)\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(
                RuntimeError, "required hermetic files changed"
            ):
                self.verify()

    def test_missing_required_hermetic_file_is_detected(self) -> None:
        name = self.manifest["entries"][0]["name"]
        with mock.patch.dict(
            MANIFEST.HERMETIC_REQUIRED_FILES,
            {name: ("lib/missing-support.metta",)},
        ):
            with self.assertRaisesRegex(
                RuntimeError, "required hermetic file is missing"
            ):
                self.verify()

    def test_oracle_mutation_is_detected(self) -> None:
        entry = next(
            item
            for item in self.manifest["entries"]
            if item["oracle"]["status"] == "pinned"
        )
        entry["oracle"]["stdout"] += "changed\n"
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, "stdout digest"):
            self.verify()

    def test_corrected_source_fixture_mutation_is_detected(self) -> None:
        entry = next(
            item
            for item in self.manifest["entries"]
            if item["name"] in MANIFEST.CORRECTED_CASES
        )
        entry["fixture"]["files"][0]["sha256"] = "0" * 64
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, "fixture record changed"):
            self.verify()

    def test_duplicate_entry_is_detected(self) -> None:
        self.manifest["entries"][1] = copy.deepcopy(
            self.manifest["entries"][0]
        )
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, "duplicate names"):
            self.verify()

    def test_pending_controlled_fixture_fails_complete_gate(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "fixture is still pending"):
            self.verify(require_complete=True)

    def test_generator_mutation_is_detected(self) -> None:
        self.manifest["generator_sha256"] = "0" * 64
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, "generator changed"):
            self.verify()

    def test_normalization_mutation_is_detected(self) -> None:
        self.manifest["normalization"][
            "alpha_canonicalize_printed_variables"
        ] = False
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, "normalization contract"):
            self.verify()

    def test_revision_mutation_is_detected(self) -> None:
        self.manifest["petta_revision"] = "other-revision"
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, "revision changed"):
            self.verify()

    def test_command_mutation_is_detected(self) -> None:
        self.manifest["entries"][0]["command"].append("--other")
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, "command changed"):
            self.verify()

    def test_count_mutation_is_detected(self) -> None:
        self.manifest["counts"]["hermetic"] -= 1
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, "manifest counts"):
            self.verify()

    def test_exact_match_gate_rejects_every_non_match_status(self) -> None:
        MANIFEST.verify_exact_match_counts({"MATCH": 176}, 176)
        with self.assertRaisesRegex(RuntimeError, "not exact"):
            MANIFEST.verify_exact_match_counts(
                {"MATCH": 175, "OUTPUT_MISMATCH": 1}, 176
            )

    def test_prefix_fixture_observes_output_then_stops_live_process(
        self,
    ) -> None:
        exit_contract, stdout, stderr = MANIFEST.run_supervised_prefix(
            [
                sys.executable,
                "-u",
                "-c",
                (
                    "import sys,time;"
                    "sys.stdin.readline();"
                    "print('ready');"
                    "time.sleep(30)"
                ),
            ],
            self.root,
            dict(),
            "input\n",
            "ready\n",
            2.0,
            lambda text: text,
            lambda text: text,
        )
        self.assertEqual(exit_contract, "supervised-prefix")
        self.assertEqual(stdout, "ready\n")
        self.assertEqual(stderr, "")

    def test_complete_process_capture_is_bounded(self) -> None:
        exit_code, stdout, stderr, timed_out, output_limited = (
            MANIFEST.run_bounded_process(
                [
                    sys.executable,
                    "-u",
                    "-c",
                    "import sys; sys.stdout.write('x' * 4096)",
                ],
                self.root,
                dict(),
                None,
                2.0,
                max_capture_bytes=1024,
            )
        )
        self.assertNotEqual(exit_code, 0)
        self.assertEqual(len(stdout) + len(stderr), 1024)
        self.assertFalse(timed_out)
        self.assertTrue(output_limited)

    def test_local_git_fixture_transform_is_exact_and_ephemeral(
        self,
    ) -> None:
        source = self.examples / "git-fixture-input.metta"
        source.write_text(
            '!(git-import! "https://github.com/patham9/faiss_ffi" '
            '"build.sh")\n',
            encoding="utf-8",
        )
        fixture = MANIFEST.CONTROLLED_CASES["git_import2.metta"]
        with MANIFEST.local_git_fixture_workspace(
            REPO_ROOT, self.root, source, fixture
        ) as (workspace, transformed):
            text = transformed.read_text(encoding="utf-8")
            self.assertTrue(text.startswith(fixture["source_prefix"]))
            self.assertIn('"./faiss_ffi"', text)
            self.assertIn('"../../fixture/build.sh"', text)
            self.assertTrue((workspace / "faiss_ffi").is_symlink())
            self.assertTrue(
                (workspace / "fixture" / "faiss.pl").is_file()
            )


if __name__ == "__main__":
    unittest.main()
