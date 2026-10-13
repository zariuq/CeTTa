#!/usr/bin/env python3
"""Controls for dialect spelling of an explicitly activated held body and of
the declarations of native handoffs."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from library_spelling import read_forms, show, spelling, transform


def spell(source, dialect="petta"):
    return show(transform(read_forms(source)[0], dialect))


HANDOFF = "(= (F $x $y) (function (chain (eval (N $x $y)) $r (return $r))))"


def declarations(source, dialect="petta"):
    return [line for line in spelling(source, dialect).splitlines()
            if line.startswith("(:")]


class HandoffDeclarations(unittest.TestCase):
    def test_datum_parameters_survive_with_a_running_body(self):
        for dialect in ("petta", "prime"):
            for declared, spelled in (
                ("(: F (-> Atom Atom Atom))", "(: F (-> Atom Atom %Undefined%))"),
                ("(: F (-> Number Atom Atom))",
                 "(: F (-> Number Atom %Undefined%))"),
            ):
                with self.subTest(dialect=dialect, declared=declared):
                    self.assertEqual(
                        declarations(declared + "\n" + HANDOFF, dialect),
                        [spelled])

    def test_value_handoff_declarations_are_dropped(self):
        for declared in ("(: F (-> Number Number Atom))",
                         "(: F (-> %Undefined% %Undefined% Atom))",
                         '(: F (-> "Atom" Number Atom))'):
            with self.subTest(declared=declared):
                self.assertEqual(declarations(declared + "\n" + HANDOFF), [])

    def test_native_declarations_are_dropped(self):
        self.assertEqual(
            declarations("(: N (-> Atom Atom Atom))\n" + HANDOFF), [])

    def test_other_declarations_are_kept_as_written(self):
        for source in (
            "(: G (-> Atom Atom))\n(= (G $x) $x)",
            "(: F (-> Atom Atom Atom))\n" + HANDOFF + "\n(= (F $x $y) $x)",
        ):
            with self.subTest(source=source):
                self.assertEqual(declarations(source),
                                 [source.splitlines()[0]])


class HeldBodySpelling(unittest.TestCase):
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


class WrittenOutIterationSpelling(unittest.TestCase):
    """HE's map-atom and foldl-atom written out by their definitions are
    PeTTa's map-atom and foldl-atom; nothing else of that shape is."""

    def test_definitions_are_petta_iterations(self):
        for source, expected in (
            ("(function (eval (_minimal-map-atom $xs $var (metta $body %Undefined% (context-space)))))",
             "(map-atom $xs $var (eval $body))"),
            ("(function (chain (context-space) $space"
             " (eval (_minimal-foldl-atom $xs $init $acc $item $step $space))))",
             "(foldl-atom $xs $init $acc $item $step)"),
            ("(let $m (function (eval (_minimal-map-atom $xs $v (f $v)))) $m)",
             "(let $m (map-atom $xs $v (f $v)) $m)"),
        ):
            with self.subTest(source=source):
                self.assertEqual(spell(source), expected)

    def test_other_shapes_are_not_claimed(self):
        # Other rules may still apply to them (a chain's computed body loses
        # its one step); they are never read as PeTTa iterations.
        for source in (
            "(function (eval (_minimal-map-atom $xs $var)))",
            "(function (chain (context-space) $space"
            " (eval (_minimal-foldl-atom $xs $init $acc $item $step $other))))",
            "(function (chain (self) $space"
            " (eval (_minimal-foldl-atom $xs $init $acc $item $step $space))))",
            "(_minimal-map-atom $xs $var $body)",
        ):
            with self.subTest(source=source):
                spelled = spell(source)
                self.assertNotIn("(map-atom", spelled)
                self.assertNotIn("(foldl-atom", spelled)
                self.assertIn("_minimal-", spelled)

    def test_prime_keeps_the_written_out_definition(self):
        source = "(function (eval (_minimal-map-atom $xs $var $body)))"
        self.assertEqual(spell(source, "prime"), source)


if __name__ == "__main__":
    unittest.main()
