import copy
from pathlib import Path
import sys
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


if __name__ == "__main__": unittest.main()
