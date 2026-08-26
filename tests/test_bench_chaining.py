#!/usr/bin/env python3
"""Falsifiers for the ordered, fail-closed chaining benchmark runner."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import re
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "bench_chaining", ROOT / "scripts" / "bench_chaining.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load chaining benchmark module")
BENCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCH)


def result(engine: str, output: bytes, sample: int = 1) -> dict[str, object]:
    return {
        "engine": engine,
        "sample": sample,
        "count": output.count(b"\n"),
        "sha256": BENCH.sha256_bytes(output),
        "normalized": output,
    }


def exact_row(output: bytes) -> dict[str, str]:
    return {
        "id": "exact-probe",
        "oracle": "exact",
        "expected_count": str(output.count(b"\n")),
        "expected_ordered_sha256": BENCH.sha256_bytes(output),
    }


class ChainingBenchmarkTests(unittest.TestCase):
    def test_two_samples_form_an_abba_order_block(self) -> None:
        specs = [("cetta-petta", "petta"), ("swi-petta", "swi")]
        actual = BENCH.balanced_sample_order(
            specs, 1
        ) + BENCH.balanced_sample_order(specs, 2)
        self.assertEqual(
            [name for name, _ in actual],
            ["cetta-petta", "swi-petta", "swi-petta", "cetta-petta"],
        )

    def test_cetta_ab_uses_only_pinned_reference_and_candidate(self) -> None:
        row = {"id": "petta-probe", "engines": "petta"}
        specs = BENCH.cetta_ab_specs(row)
        actual = BENCH.balanced_sample_order(
            specs, 1
        ) + BENCH.balanced_sample_order(specs, 2)
        self.assertEqual(
            [name for name, _ in actual],
            [
                "reference-cetta-petta",
                "cetta-petta",
                "cetta-petta",
                "reference-cetta-petta",
            ],
        )

    def test_cetta_ab_rejects_rows_without_petta_authority(self) -> None:
        with self.assertRaisesRegex(ValueError, "requires a PeTTa lane"):
            BENCH.cetta_ab_specs(
                {"id": "he-only", "engines": "he-prime"}
            )

    def test_cetta_ab_default_selects_only_petta_rows(self) -> None:
        manifest = {
            "petta": {"id": "petta", "engines": "petta"},
            "mixed": {"id": "mixed", "engines": "he-prime-petta"},
            "he-only": {"id": "he-only", "engines": "he-prime"},
        }
        self.assertEqual(
            BENCH.select_rows(manifest, None, cetta_ab=True),
            ["petta"],
        )

    def test_cetta_ab_explicit_incompatible_row_fails_before_running(self) -> None:
        manifest = {
            "petta": {"id": "petta", "engines": "petta"},
            "he-only": {"id": "he-only", "engines": "he-prime"},
        }
        with self.assertRaisesRegex(ValueError, "incompatible.*he-only"):
            BENCH.select_rows(
                manifest, ["petta", "he-only"], cetta_ab=True
            )

    def test_result_bag_preserves_nested_order_and_multiplicity(self) -> None:
        raw = b"[(proof a (x, y)), (proof b), (proof a (x, y))]\n"
        expected = b"(proof a (x, y))\n(proof b)\n(proof a (x, y))\n"
        self.assertEqual(BENCH.normalize(raw, "result-lines"), expected)

    def test_result_bag_rejects_unbalanced_output(self) -> None:
        with self.assertRaisesRegex(ValueError, "unbalanced result bag"):
            BENCH.normalize(b"[(proof a), (proof b]\n", "result-lines")

    def test_occurs_check_setup_is_mandatory(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing occurs-check setup"):
            BENCH.normalize(b"(answer a)\n", "occurs-check")

    def test_alpha_normalization_preserves_variable_aliasing(self) -> None:
        self.assertEqual(
            BENCH.alpha_normalize("(proof $x $x $y $x)"),
            "(proof $V0 $V0 $V1 $V0)",
        )

    def test_identity_alpha_normalizes_each_result_without_reordering(self) -> None:
        raw = b"(proof $x $x)\n(proof $y $z)\n"
        expected = b"(proof $V0 $V0)\n(proof $V0 $V1)\n"
        self.assertEqual(BENCH.normalize(raw, "identity-alpha"), expected)

    def test_exact_oracle_accepts_identical_ordered_bags(self) -> None:
        output = b"(proof first)\n(proof second)\n(proof first)\n"
        BENCH.qualify(
            exact_row(output),
            [result("cetta-petta", output), result("swi-petta", output)],
        )

    def test_corrupt_hash_oracle_is_killed(self) -> None:
        output = b"(proof first)\n(proof second)\n"
        row = exact_row(output)
        row["expected_ordered_sha256"] = "0" * 64
        with self.assertRaisesRegex(RuntimeError, "ordered SHA-256 oracle failed"):
            BENCH.qualify(row, [result("cetta-petta", output)])

    def test_reordered_answer_bag_is_killed(self) -> None:
        expected = b"(proof first)\n(proof second)\n"
        reordered = b"(proof second)\n(proof first)\n"
        with self.assertRaisesRegex(RuntimeError, "ordered SHA-256 oracle failed"):
            BENCH.qualify(exact_row(expected), [result("cetta-petta", reordered)])

    def test_lost_duplicate_proof_is_killed(self) -> None:
        expected = b"(proof same)\n(proof same)\n"
        with self.assertRaisesRegex(RuntimeError, "count oracle failed"):
            BENCH.qualify(
                exact_row(expected),
                [result("cetta-petta", b"(proof same)\n")],
            )

    def test_nondeterministic_samples_are_killed(self) -> None:
        expected = b"(proof first)\n(proof second)\n"
        reordered = b"(proof second)\n(proof first)\n"
        with self.assertRaisesRegex(RuntimeError, "nondeterministic output"):
            BENCH.qualify(
                exact_row(expected),
                [
                    result("cetta-petta", expected, 1),
                    result("cetta-petta", reordered, 2),
                ],
            )

    def test_zero_exit_evaluator_error_is_killed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            scratch = Path(raw)
            fake = scratch / "fake-cetta"
            fake.write_text(
                "#!/bin/sh\n"
                "printf '(proof expected)\\n'\n"
                "printf 'Error in evaluator\\n' >&2\n",
                encoding="utf-8",
            )
            fake.chmod(0o755)
            program = scratch / "probe.metta"
            program.write_text("!(probe)\n", encoding="utf-8")
            row = {
                "id": "fatal-probe",
                "oracle": "exact",
                "normalizer": "identity",
                "timeout_s": "5",
            }
            with self.assertRaisesRegex(RuntimeError, "fatal diagnostic"):
                BENCH.run_engine(
                    "cetta-he",
                    "he",
                    row,
                    program,
                    1,
                    scratch,
                    fake,
                    scratch,
                    scratch,
                )

    def test_parenthesized_evaluator_error_is_killed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            scratch = Path(raw)
            fake = scratch / "fake-cetta"
            fake.write_text(
                "#!/bin/sh\n"
                "printf '(Error (probe) evaluator rejected probe)\\n'\n",
                encoding="utf-8",
            )
            fake.chmod(0o755)
            program = scratch / "probe.metta"
            program.write_text("!(probe)\n", encoding="utf-8")
            row = {
                "id": "fatal-term-probe",
                "oracle": "exact",
                "normalizer": "identity",
                "timeout_s": "5",
            }
            with self.assertRaisesRegex(RuntimeError, "fatal diagnostic"):
                BENCH.run_engine(
                    "cetta-he",
                    "he",
                    row,
                    program,
                    1,
                    scratch,
                    fake,
                    scratch,
                    scratch,
                )

    def test_changed_nil_artifact_is_killed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            bench = Path(raw)
            base = bench / "nil_current"
            (base / "engines").mkdir(parents=True)
            artifact = base / "engines" / "obc.metta"
            artifact.write_text("original\n", encoding="utf-8")
            digest = BENCH.sha256_file(artifact)
            (base / "source_provenance.tsv").write_text(
                "artifact\tartifact_sha256\n"
                f"engines/obc.metta\t{digest}\n",
                encoding="utf-8",
            )
            with mock.patch.object(BENCH, "BENCH", bench):
                BENCH.verify_nil_source_provenance()
                artifact.write_text("changed\n", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "changed=.*obc.metta"):
                    BENCH.verify_nil_source_provenance()

    def test_compile_input_hash_changes_with_source_content(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "src" / "probe.c"
            source.parent.mkdir()
            source.write_text("first\n", encoding="utf-8")
            with mock.patch.object(BENCH, "ROOT", root), mock.patch.object(
                BENCH, "run_text", return_value="src/probe.c"
            ):
                first = BENCH.source_tree_sha256()
                source.write_text("second\n", encoding="utf-8")
                second = BENCH.source_tree_sha256()
            self.assertNotEqual(first, second)

    def test_dirty_petta_checkout_is_measured_and_recorded(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            checkout = Path(raw)
            (checkout / "run.sh").write_text("#!/bin/sh\n", encoding="utf-8")
            with mock.patch.object(
                BENCH,
                "run_text",
                side_effect=["current-revision", " M run.sh", "SWI-Prolog 9"],
            ), mock.patch("sys.stderr"):
                identity = BENCH.petta_checkout_identity(checkout)
            self.assertEqual(identity["revision"], "current-revision")
            self.assertEqual(identity["dirty"], "1")
            self.assertEqual(
                identity["status_sha256"],
                BENCH.sha256_bytes(b" M run.sh"),
            )

    def test_machine_and_runtime_observability_are_separated(self) -> None:
        stderr = (
            "PETTA_MACHINE_STATS transitions=7 max_goal_depth=3 "
            "time_to_first_answer_ns=11 first_answer_transition=5\n"
            "runtime-counter bindings-apply 13\n"
            "real diagnostic\n"
        )
        invocations, counters, ordinary = BENCH.extract_observability(stderr)
        self.assertEqual(invocations[0]["transitions"], 7)
        self.assertEqual(counters, {"bindings-apply": 13})
        self.assertEqual(ordinary, "real diagnostic\n")

    def test_duplicate_runtime_counter_is_killed(self) -> None:
        stderr = (
            "runtime-counter bindings-apply 1\n"
            "runtime-counter bindings-apply 2\n"
        )
        with self.assertRaisesRegex(ValueError, "duplicate runtime counter"):
            BENCH.extract_observability(stderr)

    def test_c0_admission_and_outcomes_are_recorded(self) -> None:
        expected = {
            "petta-equation-template-c0-admission-attempt",
            "petta-equation-template-c0-artifact-built",
            "petta-equation-template-c0-artifact-declined",
            "petta-equation-template-c0-execution-admitted",
            "petta-equation-template-c0-execution-match",
            "petta-equation-template-c0-execution-mismatch",
            "petta-equation-template-c0-execution-fallback",
        }
        self.assertTrue(expected.issubset(BENCH.MECHANISM_RUNTIME_COUNTERS))

    def test_machine_counter_schema_is_emitted_by_current_runtime(self) -> None:
        source = (BENCH.ROOT / "src/eval.c").read_text(encoding="utf-8")
        emitted = set(re.findall(r'" ([a-z0-9_]+)=%', source))
        emitted.add("invocations")
        missing = set(BENCH.MECHANISM_MACHINE_COUNTERS) - emitted
        self.assertEqual(missing, set())


if __name__ == "__main__":
    unittest.main()
