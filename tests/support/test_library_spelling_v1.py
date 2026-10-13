#!/usr/bin/env python3
"""Controls for dialect spelling of an explicitly activated held body."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from library_spelling import read_forms, show, transform


def spell(source, dialect="petta"):
    return show(transform(read_forms(source)[0], dialect))


class HeldBodySpelling(unittest.TestCase):
    def test_petta_library_import_does_not_select_python_module(self):
        self.assertEqual(spell("!(import! &self math)"), "!(import! &self (library math))")
        self.assertEqual(spell("!(import! &self ./local.metta)"), "!(import! &self ./local.metta)")
        self.assertEqual(spell("!(import! &self math)", "prime"), "!(import! &self math)")

    def test_current_space_interpretation(self):
        self.assertEqual(spell("(metta $body %Undefined% &self)"), "(eval $body)")
        self.assertEqual(spell("(metta $body %Undefined% (context-space))"),
                         "(eval $body)")

    def test_activation_survives_computed_positions(self):
        for source, expected in (
            ("(let $x 1 (metta $body %Undefined% &self))",
             "(let $x 1 (eval $body))"),
            ("(= (run $body) (metta $body %Undefined% &self))",
             "(= (run $body) (eval $body))"),
            ("(if $ok (metta $yes %Undefined% &self) (metta $no %Undefined% &self))",
             "(if $ok (eval $yes) (eval $no))"),
            ("(unify $value $pattern (metta $body %Undefined% &self) (empty))",
             "(if (= $value $pattern) (eval $body) (empty))"),
            ("(let $x 1 (metta $body %Undefined% (context-space)))",
             "(let $x 1 (eval $body))"),
        ):
            with self.subTest(source=source):
                self.assertEqual(spell(source), expected)

    def test_ordinary_one_step_rule_is_unchanged(self):
        self.assertEqual(spell("(let $x (eval (f)) (eval $body))"),
                         "(let $x (f) $body)")

    def test_other_contexts_are_not_claimed(self):
        for source in (
            "(metta $body Atom &self)",
            "(metta $body %Undefined% $space)",
            "(metta $body Atom (context-space))",
            "(metta $body %Undefined% (context-space $space))",
            '(metta $body %Undefined% ("context-space"))',
            '(metta $body "%Undefined%" &self)',
            '(metta $body %Undefined% "&self")',
            '("metta" $body %Undefined% &self)',
            "(metta $body %Undefined%)",
        ):
            with self.subTest(source=source):
                self.assertEqual(spell(source), source)

    def test_prime_keeps_its_interpreter(self):
        source = "(let $x 1 (metta $body %Undefined% &self))"
        self.assertEqual(spell(source, "prime"), source)

    def test_error_case_keeps_its_handler_scope(self):
        self.assertEqual(
            spell('(function (case (run) (((Error $s $r) (return (failed $r))) ($v (return $v)))))'),
            '(case (catch (run)) (((Error $s $r) (failed $r)) ($v $v)))')
        self.assertEqual(
            spell('(function (case (run) (($v (return $v)))))'), '(run)')


if __name__ == "__main__":
    unittest.main()
