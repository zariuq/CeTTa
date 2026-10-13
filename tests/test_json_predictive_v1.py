import copy
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import json_predictive_v1 as compiler

ROOT = Path(__file__).resolve().parents[1]
SYNTAX = ROOT / "langdef/json/rfc8259_syntax_v1.metta"
PROFILE = ROOT / "langdef/json/rfc8259_parser_profile_v1.metta"
TARGET = ROOT / "langdef/json/occurrence_preserving_value_v1.metta"
PLAN = ROOT / "native/json_elaboration_plan_v1.c"


class PredictiveGrammarTest(unittest.TestCase):
    def test_authored_sources_have_disjoint_structural_choices(self):
        code, report, nfa, start = compiler.compile_sources(SYNTAX, PROFILE, TARGET, PLAN)
        self.assertEqual(report["structural_nonterminals"], 11)
        self.assertEqual(report["conflicts"], [])
        self.assertIn("return JD_START_OBJECT;", code)
        self.assertEqual(len(nfa.accepts), 3)

    def test_first_follow_conflict_is_refused(self):
        rules = {
            "first": dict(category="JsonText", rhs=[("n", "Choice")]),
            "a": dict(category="Choice", rhs=[("t", "x")]),
            "b": dict(category="Choice", rhs=[("t", "x")]),
        }
        with self.assertRaisesRegex(compiler.CompileError, "FIRST/FOLLOW conflict"):
            compiler.first_follow(rules)

    def mutated(self, path, label, field, replacement):
        original = compiler.grammar
        def grammar(candidate):
            rows = copy.deepcopy(original(candidate))
            if candidate == path:
                rows[label][field] = replacement
            return rows
        return mock.patch.object(compiler, "grammar", grammar)

    def test_number_token_cannot_change_without_its_elaboration(self):
        with self.mutated(SYNTAX, "json:frac", "rhs", [("t", ":"), ("n", "JsonDigit"), ("n", "JsonDigits")]):
            with self.assertRaisesRegex(compiler.CompileError, "outside lexical elaboration"):
                compiler.compile_sources(SYNTAX, PROFILE, TARGET, PLAN)

    def test_target_parameter_types_are_checked(self):
        with self.mutated(TARGET, "JsonMemberV1", "params", ["Value"] * 4):
            with self.assertRaisesRegex(compiler.CompileError, "target/elaboration disagreement"):
                compiler.compile_sources(SYNTAX, PROFILE, TARGET, PLAN)

    def mutated_plan(self, old, new):
        original = Path.read_text
        def read_text(path, *args, **kwargs):
            content = original(path, *args, **kwargs)
            return content.replace(old, new) if path == PLAN else content
        return mock.patch.object(Path, "read_text", read_text)

    def test_source_operation_cannot_change_without_its_decoder(self):
        with self.mutated_plan("CETTA_JSON_ELAB_VALUE_TRUE_V1", "CETTA_JSON_ELAB_VALUE_FALSE_V1"):
            with self.assertRaisesRegex(compiler.CompileError, "outside source elaboration operation"):
                compiler.compile_sources(SYNTAX, PROFILE, TARGET, PLAN)

    def test_source_elaboration_arity_is_checked(self):
        with self.mutated_plan('{"json:member", "JsonMember", 4u', '{"json:member", "JsonMember", 3u'):
            with self.assertRaisesRegex(compiler.CompileError, "outside source elaboration operation"):
                compiler.compile_sources(SYNTAX, PROFILE, TARGET, PLAN)

    def test_target_elaboration_arity_is_checked(self):
        with self.mutated_plan('{"JsonBoolV1", "Value", 1u', '{"JsonBoolV1", "Value", 2u'):
            with self.assertRaisesRegex(compiler.CompileError, "target/elaboration disagreement"):
                compiler.compile_sources(SYNTAX, PROFILE, TARGET, PLAN)

    def test_duplicate_source_operation_cannot_hide_a_missing_one(self):
        original = Path.read_text
        def read_text(path, *args, **kwargs):
            content = original(path, *args, **kwargs)
            if path == PLAN:
                content = content.replace('"json:value-false"', '"json:value-true"')
                content = content.replace("CETTA_JSON_ELAB_VALUE_FALSE_V1", "CETTA_JSON_ELAB_VALUE_TRUE_V1")
            return content
        with mock.patch.object(Path, "read_text", read_text):
            with self.assertRaisesRegex(compiler.CompileError, "source operation inventory"):
                compiler.compile_sources(SYNTAX, PROFILE, TARGET, PLAN)

    def test_duplicate_target_operation_cannot_hide_a_missing_one(self):
        with self.mutated_plan('{"JsonNullV1", "Value", 0u', '{"JsonNoSourceSpanV1", "Span", 0u'):
            with self.assertRaisesRegex(compiler.CompileError, "target operation inventory"):
                compiler.compile_sources(SYNTAX, PROFILE, TARGET, PLAN)

    def test_structural_literal_conflict_is_refused(self):
        with self.mutated(SYNTAX, "json:value-false", "rhs", [("t", "true")]):
            with self.assertRaisesRegex(compiler.CompileError, "FIRST/FOLLOW conflict"):
                compiler.compile_sources(SYNTAX, PROFILE, TARGET, PLAN)

    def test_nonregular_lexical_cycle_is_refused(self):
        rules = {
            "a": dict(category="A", rhs=[("n", "B")]),
            "b": dict(category="B", rhs=[("n", "A"), ("t", "x")]),
        }
        with self.assertRaisesRegex(compiler.CompileError, "nonregular lexical recursion"):
            compiler.regular("A", rules, {})


class PredictiveArtifactTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="json-artifact-test-", dir=ROOT / "runtime")
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        self.emitter = ROOT / "runtime/emit-regular-span-dfa-v1"
        compiler.build_artifact(SYNTAX, PROFILE, TARGET, PLAN, self.emitter, self.output)

    def files(self):
        return {name: (self.output/name).read_bytes() for name in compiler.ARTIFACT_FILES}

    def check_compilation_identities(self):
        source = '#include <stdint.h>\n'
        source += (self.output/compiler.ARTIFACT_FILES[0]).read_text()
        source += (self.output/compiler.ARTIFACT_FILES[1]).read_text()
        source += (self.output/compiler.ARTIFACT_FILES[2]).read_text().split("static JsonStartV1", 1)[0]
        return subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                              ["-std=c11", "-x", "c", "-fsyntax-only", "-"],
                              input=source, capture_output=True, text=True)

    def test_artifact_is_reproducible_and_identified(self):
        first = self.files()
        compiler.build_artifact(SYNTAX, PROFILE, TARGET, PLAN, self.emitter, self.output)
        self.assertEqual(first, self.files())
        report = json.loads(first[compiler.ARTIFACT_FILES[3]])
        self.assertEqual(len(report["artifact_sha256"]), 64)
        self.assertEqual(set(report["compiler_sha256"]),
                         {"lowering", "schema", "dfa", "predictive", "predictive_adapter", "publication"})

    def test_failed_dfa_build_does_not_publish(self):
        first = self.files()
        with mock.patch.object(compiler.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "dfa")):
            with self.assertRaises(subprocess.CalledProcessError):
                compiler.build_artifact(SYNTAX, PROFILE, TARGET, PLAN, self.emitter, self.output)
        self.assertEqual(first, self.files())

    def test_refused_grammar_does_not_publish(self):
        first = self.files()
        with mock.patch.object(compiler, "compile_sources", side_effect=compiler.CompileError("conflict")):
            with self.assertRaises(compiler.CompileError):
                compiler.build_artifact(SYNTAX, PROFILE, TARGET, PLAN, self.emitter, self.output)
        self.assertEqual(first, self.files())

    def test_c_compiler_refuses_mixed_artifacts(self):
        clean = self.check_compilation_identities()
        self.assertEqual(clean.returncode, 0, clean.stderr)
        path = self.output/compiler.ARTIFACT_FILES[0]
        path.write_text(path.read_text().replace("#define JD_LEXICAL_ARTIFACT_0 ",
                                                "#define JD_LEXICAL_ARTIFACT_0 ~", 1))
        mixed = self.check_compilation_identities()
        self.assertNotEqual(mixed.returncode, 0)
        self.assertIn("JSON generated artifacts differ", mixed.stderr)

    def test_interrupted_compatibility_publication_fails_closed_and_recovers(self):
        first = self.files()
        code, report, nfa, start = compiler.compile_sources(SYNTAX, PROFILE, TARGET, PLAN)
        replacement = (code + "\n", report, nfa, start)
        replace = compiler.publication.os.replace
        commits = 0

        def interrupt_after_first(source, destination):
            nonlocal commits
            if commits:
                raise OSError("interrupted artifact publication")
            commits += 1
            return replace(source, destination)

        with mock.patch.object(compiler, "compile_sources", return_value=replacement):
            with mock.patch.object(compiler.publication.os, "replace", side_effect=interrupt_after_first):
                with self.assertRaises(OSError):
                    compiler.build_artifact(SYNTAX, PROFILE, TARGET, PLAN, self.emitter, self.output)
        self.assertEqual(commits, 1)
        mixed = self.check_compilation_identities()
        self.assertNotEqual(mixed.returncode, 0)
        self.assertIn("JSON generated artifacts differ", mixed.stderr)
        compiler.build_artifact(SYNTAX, PROFILE, TARGET, PLAN, self.emitter, self.output)
        self.assertEqual(self.files(), first)
        self.assertEqual(self.check_compilation_identities().returncode, 0)


if __name__ == "__main__": unittest.main()
