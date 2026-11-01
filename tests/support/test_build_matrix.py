"""Compiler audit distinguishes project code from separately built dependencies."""
from pathlib import Path
import unittest

from check_build_matrix import compiler_output, project_compile


class ProjectCompilation(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).resolve().parents[2]

    def record(self, source, cwd=None):
        return {"cwd": str(cwd or self.root), "arguments": ["-c", source, "-o", "object.o"]}

    def test_project_sources_remain_warning_audited(self):
        for source in ["src/atom.c", "src/eval.c", "tools/langdef_compile_v1.c",
                       "experiments/reader/native/parser.c", "src/vendor_named_but_owned.c"]:
            with self.subTest(source=source):
                self.assertTrue(project_compile(self.record(source), self.root))

    def test_vendor_policy_is_independent_of_vendor_name_and_spelling(self):
        for source in ["vendor/sqlite/sqlite3.c", "vendor/secp256k1/src/secp256k1.c",
                       "./vendor/future/src/atom.c", "src/../vendor/future/src/atom.c",
                       str(self.root / "vendor/future/code.c")]:
            with self.subTest(source=source):
                self.assertFalse(project_compile(self.record(source), self.root))

    def test_external_same_named_source_is_not_project_owned(self):
        self.assertFalse(project_compile(self.record("src/atom.c", self.root / "runtime/dependency"), self.root))
        self.assertFalse(project_compile(self.record(str(self.root.parent / "dependency/src/atom.c")), self.root))

    def test_absolute_project_source_is_audited(self):
        self.assertTrue(project_compile(self.record(str(self.root / "src/atom.c")), self.root))

    def test_actual_object_path_preserves_configuration_suffix(self):
        record = self.record("src/atom.c")
        record["arguments"][-1] = "src/atom.core.matrix.config-fingerprint.o"
        self.assertEqual(compiler_output(record), self.root / record["arguments"][-1])

    def test_absolute_and_joined_output_flags(self):
        path = self.root / "src/actual.o"
        self.assertEqual(compiler_output({"cwd": str(self.root), "arguments": ["-c", "src/atom.c", "-o" + str(path)]}), path)
        self.assertIsNone(compiler_output({"cwd": str(self.root), "arguments": ["-c", "src/atom.c"]}))

    def test_atomic_object_uses_the_published_dependency_target(self):
        record = self.record("src/atom.c")
        record["arguments"] += ["-MT", "src/atom.config-fingerprint.o"]
        record["arguments"][3] = "src/.cetta-object.temporary"
        self.assertEqual(compiler_output(record), self.root / "src/atom.config-fingerprint.o")

    def test_nonatomic_output_is_not_replaced_by_an_unrelated_dependency(self):
        record = self.record("src/atom.c")
        record["arguments"] += ["-MT", "src/unrelated.o"]
        self.assertEqual(compiler_output(record), self.root / "object.o")


if __name__ == "__main__":
    unittest.main()
