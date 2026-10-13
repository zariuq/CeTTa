#!/usr/bin/env python3
"""Positive and fail-closed controls for the typed rule consumer adapter."""
import unittest

from gslt2parse_schema_v1 import Symbol, StringLiteral, Variable, parse_sexprs, render
from module_chainer_v1 import export_rules, finite_rows, render_metta


CORE = '''(GSLTLanguageDefWireV1 "Small"
  (LCons (TypeDecl "T" CarrierAst) LNil)
  (LCons (GrammarRule "A" "T" LNil LNil EvalNone)
    (LCons (GrammarRule "F" "T" (LCons (TermSimple "x" (TBase "T")) LNil) LNil EvalNone) LNil))
  LNil
  (LCons (RewriteRule "step" (LCons (TypeBinding "x" (TBase "T")) LNil) LNil
    (PApp "F" (LCons (FVar "x") LNil)) (FVar "x")) LNil))'''


def view(core=CORE, origin="one"):
    parsed = parse_sexprs(core)[0]
    rows = finite_rows(parsed[5])
    occurrences = Symbol("LNil")
    for index, rule in reversed(list(enumerate(rows))):
        occurrences = (Symbol("LCons"), (Symbol("ModuleRuleOccurrenceV1"),
            (Symbol("SyntaxOriginV1"), StringLiteral(origin), StringLiteral(str(index))), rule), occurrences)
    return (Symbol("ModuleRewriteRulesV1"), parsed, occurrences)


def export(bundle):
    return export_rules(bundle, Variable("authority"), StringLiteral("small"), Symbol("kb"),
                        parse_sexprs("(CTV (STV 1.0 1.0) (STV 0.0 1.0))")[0])


class AdapterControls(unittest.TestCase):
    def test_actual_metta_variables_and_core_admission(self):
        source = render_metta(export(view())[0])
        self.assertIn('$module_v0', source)
        self.assertIn('$authority', source)
        self.assertNotIn('?authority', source)
        self.assertIn('Compute langdef:admit-term-data', source)
        self.assertIn('"one" "0"', source)

    def test_independent_origins_have_distinct_evidence_heads(self):
        first, second = export(view(origin="one"))[0], export(view(origin="two"))[0]
        self.assertNotEqual(first[2][1][0], second[2][1][0])
        self.assertIn('"step"', render(first))

    def test_duplicate_order_and_multiplicity_are_not_sets(self):
        core = parse_sexprs(CORE)[0]
        rule = finite_rows(core[5])[0]
        duplicate = (*core[:5], (Symbol("LCons"), rule, (Symbol("LCons"), rule, Symbol("LNil"))))
        calls = export(view(render(duplicate)))
        self.assertEqual(len(calls), 2)
        self.assertNotEqual(calls[0][2][1][0], calls[1][2][1][0])

    def test_malformed_occurrences_cannot_disappear(self):
        valid = view()
        with self.assertRaises(ValueError):
            export((*valid[:2], (Symbol("LCons"), Symbol("bad"), valid[2])))

    def test_core_rule_coverage_is_exact(self):
        valid = view()
        with self.assertRaises(ValueError):
            export((*valid[:2], Symbol("LNil")))

    def test_unknown_constructor(self):
        with self.assertRaises(ValueError):
            export(view(CORE.replace('(PApp "F"', '(PApp "Missing"')))

    def test_unbound_variable(self):
        with self.assertRaises(ValueError):
            export(view(CORE.replace('(FVar "x")', '(FVar "missing")')))

    def test_binding_profile_is_not_guessed(self):
        with self.assertRaises(ValueError):
            export(view(CORE.replace('(TBase "T")', '(TArrow (TBase "T") (TBase "T"))')))

    def test_raw_token_carrier_is_not_constructor_data(self):
        with self.assertRaises(ValueError):
            export(view(CORE.replace('CarrierAst', 'CarrierTokenRaw')))

    def test_unknown_condition_requires_provider(self):
        with self.assertRaises(ValueError):
            export(view(CORE.replace('LNil\n    (PApp',
                '(LCons (RelationQuery "Q" LNil (FVar "x")) LNil)\n    (PApp')))


if __name__ == "__main__":
    unittest.main()
