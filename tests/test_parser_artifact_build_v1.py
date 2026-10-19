from pathlib import Path
import copy
import json
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import parser_artifact_build_v1 as build
import test_parser_artifacts_v1 as hosts
from gslt2parse_schema_v1 import Symbol, parse_sexprs


class ModuleArtifactBuildTest(unittest.TestCase):
    def setUp(self):
        self.manifest = ROOT / "tests/module_values/baked_v1.json"
        self.rows = build.load_manifest(self.manifest)
        temporary = tempfile.TemporaryDirectory(prefix="module-build-test-", dir=ROOT / "runtime")
        self.addCleanup(temporary.cleanup)
        self.output = Path(temporary.name)

    def load_rows(self, rows):
        manifest = self.output / "manifest.json"
        manifest.write_text(json.dumps({"format": "ParserBuildManifestV1", "parsers": rows}))
        return build.load_manifest(manifest)

    def test_module_input_and_explicit_catalog_are_dependencies(self):
        dependencies = build.dependencies(self.manifest, self.rows)
        row = self.rows[1]
        for name in (row["source"], *row["catalog"].values()):
            self.assertIn(build.source_path(self.manifest, name), dependencies)
        self.assertIn(ROOT / "lib/lib_module.metta", dependencies)
        self.assertIn(ROOT / "lib/petta/lib_module.metta", dependencies)

    def test_missing_catalog_source_is_not_ignored(self):
        rows = copy.deepcopy(self.rows)
        rows[1]["catalog"]["Missing"] = "missing-module-build-input.module"
        with self.assertRaisesRegex(ValueError, "input does not exist"):
            build.dependencies(self.manifest, rows)

    def test_module_requires_named_reader_source_and_closed_entrypoint(self):
        for key in ("reader", "source", "theory", "start"):
            for bad in (None, "", [], 123):
                with self.subTest(key=key, bad=bad):
                    rows = copy.deepcopy(self.rows)
                    rows[1][key] = bad
                    with self.assertRaisesRegex(ValueError, "module input requires"):
                        self.load_rows(rows)
        rows = copy.deepcopy(self.rows)
        rows[1]["reader"] = "not-in-this-manifest"
        with self.assertRaisesRegex(ValueError, "module input requires"):
            self.load_rows(rows)

    def test_catalog_is_an_explicit_location_to_file_map(self):
        for bad in (None, [], "search-the-network", {"location": 4}):
            with self.subTest(bad=bad):
                rows = copy.deepcopy(self.rows)
                rows[1]["catalog"] = bad
                with self.assertRaisesRegex(ValueError, "catalog maps explicit"):
                    self.load_rows(rows)

    def test_module_bootstrap_compiles_reader_without_baked_dependency(self):
        program = build.Inputs(self.manifest, self.rows).parser("composed-binding")
        self.assertNotIn("load-baked-parser", program)
        for operation in ("lbnf:read-file", "module:read-file", "module:assemble",
                          "module:language", "langdef:compile-parser", "langdef:close-parser"):
            self.assertIn(operation, program)
        self.assertIn('"build-module:composed-binding"', program)
        self.assertIn('"build-import:RewriteBundles"', program)

    def test_compiler_runs_on_pinned_petta_not_prime(self):
        completed = SimpleNamespace(returncode=0, stdout="(CompiledV1 7)\n", stderr="")
        with mock.patch.object(build.subprocess, "run", return_value=completed) as invoke:
            self.assertEqual(build.run_compiler(ROOT / "cetta", self.output, "compiler",
                                               "!(CompiledV1 7)\n", 1),
                             [(Symbol("CompiledV1"), 7)])
        self.assertEqual(invoke.call_args.args[0][1:5],
                         ["--lang", "petta@1.0.6", "--profile", "extended"])

    def test_shared_hosts_preserve_profiles_and_frozen_prime(self):
        completed = SimpleNamespace(returncode=0, stdout="(HostV1 7)\n", stderr="")
        for language, (endpoint, profile) in hosts.HOST_ENDPOINTS.items():
            with self.subTest(language=language), \
                    mock.patch.object(hosts.subprocess, "run", return_value=completed) as invoke:
                self.assertEqual(hosts.run_file(ROOT / "cetta", language, self.output,
                                               "host", ROOT / "examples/module_derived_values_v1.metta"),
                                 [(Symbol("HostV1"), 7)])
                expected = [str(ROOT / "cetta"), "--lang", endpoint]
                if profile:
                    expected += ["--profile", profile]
                self.assertEqual(invoke.call_args.args[0][:-1], expected)
        self.assertEqual(hosts.HOST_ENDPOINTS["prime"], ("prime@0.0.1", None))

    def test_host_and_compiler_failure_do_not_publish_success(self):
        completed = SimpleNamespace(returncode=2, stdout="(HostV1 7)\n", stderr="refused")
        with mock.patch.object(build.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(ValueError, "compiler failed"):
                build.run_compiler(ROOT / "cetta", self.output, "compiler-failed", "!x", 1)
            with self.assertRaisesRegex(AssertionError, "refused"):
                hosts.run_file(ROOT / "cetta", "petta", self.output, "host-failed",
                               ROOT / "examples/module_derived_values_v1.metta")

    def test_nested_construction_does_not_capture_outer_bindings(self):
        rows = self.rows + [{"name": "joined", "format": "join", "start": "Expr",
                            "operands": ["composed-binding", "composed-binding"]}]
        program = build.Inputs(self.manifest, rows).parser("joined")
        bindings = []
        pending = list(parse_sexprs(program))
        while pending:
            value = pending.pop()
            if not isinstance(value, tuple):
                continue
            if value and value[0] == Symbol("let"):
                bindings.append(value[1])
            pending.extend(value)
        self.assertGreater(len(bindings), 15)
        self.assertEqual(len(bindings), len(set(bindings)), bindings)

    def test_recursive_reader_dependency_is_refused_and_unwinds(self):
        rows = copy.deepcopy(self.rows)
        rows[1]["reader"] = "composed-binding"
        inputs = build.Inputs(self.manifest, rows)
        with self.assertRaisesRegex(ValueError, "cyclic build-manifest"):
            inputs.parser("composed-binding")
        self.assertEqual(inputs.active, set())
        self.assertIn("lbnf:read-file", inputs.parser("module-core-reader"))

    def test_reader_failure_cannot_publish_a_partial_catalog(self):
        header = self.output / "catalog.h"
        header.write_bytes(b"previous catalog")
        generation = self.output / "old-generation"
        generation.mkdir()
        (generation / "parser.tpp").write_bytes(b"previous parser")
        for result in ([], [(Symbol("Error"), Symbol("bad-reader"))]):
            with self.subTest(result=result), mock.patch.object(build, "run_compiler", return_value=result):
                with self.assertRaisesRegex(ValueError, "construction or checked restoration refused"):
                    build.compile_catalog(ROOT / "cetta", self.manifest, self.rows,
                                          self.output, header, 1)
            self.assertEqual(header.read_bytes(), b"previous catalog")
            self.assertEqual((generation / "parser.tpp").read_bytes(), b"previous parser")
            self.assertEqual(set(self.output.iterdir()), {header, generation})


if __name__ == "__main__":
    unittest.main()
