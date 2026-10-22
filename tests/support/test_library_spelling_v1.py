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
CASE_HANDOFF = ("(= (F $x $y) (function (case (__cetta_lib_probe $x $y) "
                "(((return $r) (return $r)) ($raw (return $raw))))))")


def declarations(source, dialect="petta"):
    return [line for line in spelling(source, dialect).splitlines()
            if line.startswith("(:")]


class HandoffDeclarations(unittest.TestCase):
    def test_standard_dependencies_are_explicit_only_in_petta(self):
        self.assertEqual(spell('(import! &self tensor)'), '(import! &self (library tensor))')
        self.assertEqual(spell('(import! &self tensor)', 'prime'), '(import! &self tensor)')
        self.assertEqual(spell('(import! &self ./custom.metta)'), '(import! &self ./custom.metta)')
    def test_fallback_value_is_computed_before_the_handler_returns(self):
        source='(function (case (N $x) (((Box $v) (return $v)) ($other (let $r (fallback $x) (return $r))))))'
        self.assertEqual(spell(source), '(case (N $x) (((Box $v) $v) ($other (let $r (fallback $x) $r))))')
        escaping=source.replace('(fallback $x)', '(return early)')
        self.assertIn('(function',spell(escaping))
        self.assertEqual(spell('(function (return (application x)))'), '(function (return (application x)))')
    def test_case_result_after_value_bindings_runs_in_petta(self):
        source = '(function (let* (($a (prepare $x)) ($b (use $a))) (case (N $b) (((Box $v) (return (Box $v))) ($other (return $other))))))'
        self.assertEqual(spell(source),
            '(let $a (prepare $x) (let $b (use $a) (case (N $b) (((Box $v) (Box $v)) ($other $other)))))')
        escaping = source.replace('(prepare $x)', '(return stop)')
        self.assertIn('(function', spell(escaping))
    def test_structural_projection_runs_and_keeps_its_forcing_boundary(self):
        source = ("(: F (-> %Undefined% Atom))\n"
                  "(= (F $value) (function (case $value (((Box $field) (return $field))))))")
        self.assertEqual(declarations(source), [])
        self.assertIn("(let (Box $field) $value $field)", spelling(source, "petta"))
        self.assertIn("(function (case $value", spelling(source, "prime"))
        for changed in (source.replace("(return $field)", "(return different)"),
                        source.replace("(return $field)", "(return (effect $field))")):
            self.assertEqual(declarations(changed), ["(: F (-> %Undefined% Atom))"])
        constrained = source.replace("%Undefined%", "Number")
        self.assertEqual(declarations(constrained), ["(: F (-> Number Atom))"])
        datum = source.replace("%Undefined%", "Atom")
        self.assertEqual(declarations(datum), ["(: F (-> Atom %Undefined%))"])

    def test_io_handoff_declarations_run_instead_of_returning_private_calls(self):
        source = (Path(__file__).resolve().parents[2] / "lib/io.metta").read_text()
        self.assertIn("(: io:submit (-> Atom %Undefined%))", declarations(source))
        for name in ("io:poll", "io:capabilities", "io:cancel"):
            self.assertFalse(any(line.startswith(f"(: {name} ") for line in declarations(source)))
        self.assertFalse(any("__cetta_lib_io_" in line for line in declarations(source)))

    def test_administrative_bindings_preserve_computation_and_data_parameters(self):
        body = "(function (let $p (prepare $x) (chain (eval (N $p $y)) $r (return $r))))"
        source = "(: F (-> %Undefined% Atom Atom))\n(: N (-> Atom Atom Atom))\n(= (F $x $y) " + body + ")"
        for dialect in ("petta", "prime"):
            self.assertEqual(declarations(source, dialect), ["(: F (-> %Undefined% Atom %Undefined%))"])
        self.assertEqual(spell(body), "(let $p (prepare $x) (N $p $y))")
        self.assertEqual(spell(body, "prime"),
                         "(let $p (prepare $x) (function (case (N $p $y) (($result (return $result))))))")

    def test_nested_administrative_bindings_keep_dependency_order(self):
        source = "(function (let $a (f $x) (let $b (g $a) (chain (eval (N $b)) $r (return $r)))))"
        self.assertEqual(spell(source), "(let $a (f $x) (let $b (g $a) (N $b)))")

    def test_nontransparent_binding_tails_do_not_erase_function_control(self):
        source = "(function (let $p (prepare $x) (chain (eval (N $p)) $r (return changed))))"
        self.assertIn("(function", spell(source))
        self.assertEqual(declarations("(: F (-> Number Atom))\n(= (F $x) " + source + ")"),
                         ["(: F (-> Number Atom))"])

    def test_early_return_in_binding_is_not_an_administrative_handoff(self):
        source = "(function (let $p (return stop) (chain (eval (N $p)) $r (return $r))))"
        self.assertIn("(function", spell(source))
        self.assertIn("(return stop)", spell(source))

    def test_native_case_handoff_retains_data_parameters_not_he_result_type(self):
        for dialect in ("petta", "prime"):
            source = ("(: F (-> Number Atom Atom))\n"
                      "(: __cetta_lib_probe (-> Number Atom %Undefined%))\n" + CASE_HANDOFF)
            with self.subTest(dialect=dialect):
                self.assertEqual(declarations(source, dialect),
                                 ["(: F (-> Number Atom %Undefined%))"])

    def test_native_case_handoff_keeps_return_envelope_spelling(self):
        self.assertIn("(case (__cetta_lib_probe $x $y)", spelling(CASE_HANDOFF, "petta"))
        self.assertIn("((return $r) $r)", spelling(CASE_HANDOFF, "petta"))

    def test_nontransparent_cases_do_not_drop_declarations(self):
        for body in (
            CASE_HANDOFF.replace("(return $raw)", "(return changed)"),
            CASE_HANDOFF.replace("(return $r))", "(return $x))"),
            CASE_HANDOFF.replace("__cetta_lib_probe", "ordinary"),
            CASE_HANDOFF.replace("(return $r)", '("return" $r)'),
            CASE_HANDOFF.replace("(return $raw)", '("return" $raw)'),
        ):
            with self.subTest(body=body):
                self.assertEqual(declarations("(: F (-> Number Atom Atom))\n" + body),
                                 ["(: F (-> Number Atom Atom))"])

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

    def test_explicit_space_interpretation(self):
        for space in ("$scope", "&library"):
            self.assertEqual(spell(f"(metta $body %Undefined% {space})"),
                             f"(evalc $body {space})")
            self.assertEqual(spell(f"(metta $body %Undefined% {space})", "prime"),
                             f"(metta $body %Undefined% {space})")

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


if __name__ == "__main__":
    unittest.main()
