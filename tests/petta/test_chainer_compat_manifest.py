#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts" / "petta_chainer_compat.py"
SPEC = importlib.util.spec_from_file_location("petta_chainer_compat", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
compat = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = compat
SPEC.loader.exec_module(compat)


class ChainerCompatManifestTests(unittest.TestCase):
    def test_manifest_is_well_formed(self) -> None:
        manifest = compat.load_manifest(
            ROOT / "tests" / "petta" / "chainer_compat" / "manifest.json"
        )
        self.assertEqual(manifest["schema"], compat.SCHEMA)
        self.assertEqual(
            [example["name"] for example in manifest["examples"]],
            ["deductionrevision", "flyingraven", "raveninduction"],
        )
        self.assertIn("Not specialized ", manifest["forbidden_stdout"])
        self.assertIn("❌", manifest["forbidden_stdout"])

    def test_normalization_keeps_only_semantic_verdicts(self) -> None:
        raw = (
            "noise\n"
            "\x1b[32mis value, should value. ✅ \x1b[0m\n"
            "Not specialized f/2\n"
            "true\n"
        )
        self.assertEqual(
            compat.normalize_test_output(raw),
            "is value, should value. ✅\ntrue\n",
        )

    def test_normalization_quotients_only_printed_variable_names(self) -> None:
        left = "is (f $_441 $_441), should (f $_9 $_9). ✅\n"
        right = "is (f $V7 $V7), should (f $V2 $V2). ✅\n"
        distinct = "is (f $_441 $_442), should (f $_9 $_9). ✅\n"
        self.assertEqual(
            compat.normalize_test_output(left),
            compat.normalize_test_output(right),
        )
        self.assertNotEqual(
            compat.normalize_test_output(distinct),
            compat.normalize_test_output(right),
        )

    def test_unsafe_paths_are_rejected(self) -> None:
        with self.assertRaises(compat.CompatFailure):
            compat.checked_relative_path("../outside", "fixture")
        with self.assertRaises(compat.CompatFailure):
            compat.checked_relative_path("/absolute", "fixture")

    def test_executable_is_materialized_inside_the_library_image(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source-cetta"
            image = root / "image"
            image.mkdir()
            source.write_bytes(b"candidate")

            executable = compat.materialize_executable(source, image)

            self.assertEqual(executable, image / "cetta")
            self.assertEqual(executable.read_bytes(), b"candidate")
            self.assertTrue(executable.stat().st_mode & 0o100)

    def test_cetta_command_makes_the_space_engine_explicit(self) -> None:
        command = compat.cetta_example_command(
            Path("image/cetta"),
            "extended",
            "pathmap",
            Path("image/example.metta"),
        )

        self.assertEqual(
            command,
            [
                "image/cetta",
                "--lang",
                "petta",
                "--profile",
                "extended",
                "--space-engine",
                "pathmap",
                "image/example.metta",
            ],
        )
        with self.assertRaises(compat.CompatFailure):
            compat.cetta_example_command(
                Path("image/cetta"),
                "extended",
                "unknown",
                Path("image/example.metta"),
            )


if __name__ == "__main__":
    unittest.main()
