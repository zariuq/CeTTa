#!/usr/bin/env python3
"""Ordered declaration-import controls against the public NIK checker."""

import argparse
from dataclasses import replace
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

import megalodon_declaration_import_v1 as importer


class DeclarationImportTests(unittest.TestCase):
    cetta: Path
    megalodon: Path | None = None

    def check(self, goal, proof, accepted=True):
        article = importer.dag.compile_shared_article(goal, proof).article
        importer.poly.require_public_result(
            importer.poly.run_cetta(self.cetta, goal, article), accepted=accepted)

    def test_claim_construction_requests_all_declaration_kinds_without_evidence(self):
        items = importer.document('''
        (PRIM 0 "p" "primitive" (PROP) 1)
        (PARAM "q" "parameter" 0 (PROP) 2)
        (DEF "i" "identity" 1 (AR (TPVAR 0) (TPVAR 0))
          (LAM (TPVAR 0) (DB 0)) 3)
        (AXIOM "a" "assumption" 0 (TMH "parameter") 4)
        (THM "t" "theorem" "proof-t" 0 (TMH "parameter") 5)
        (PROOF "t" (KNOWN "assumption")) (QED)
        ''')
        initial = importer.State()
        with patch.object(importer.State, "_admission_transition",
                          side_effect=AssertionError("claim construction generated evidence")):
            requested = importer.document_claim(initial, items)
        self.assertEqual(initial, importer.State())
        goal, proof = importer.compile_document(importer.State(), items)
        self.assertEqual(requested, goal)
        self.check(requested, proof)
        # A source proof is not silently downgraded to an assumption when absent.
        with self.assertRaises(ValueError):
            importer.document_claim(initial, [replace(items[-1], proof=None)])

    def test_retained_evidence_rejects_changed_source_claims(self):
        items = importer.document('''
        (PARAM "p" "p" 0 (PROP) 1)
        (PARAM "q" "q" 0 (PROP) 2)
        (DEF "d" "d" 0 (PROP) (TMH "p") 3)
        (AXIOM "a" "a" 0 (TMH "p") 4)
        (THM "t" "t" "proof-t" 0 (TMH "p") 5)
        (PROOF "t" (KNOWN "a")) (QED)
        ''')
        goal, proof = importer.compile_document(importer.State(), items)
        self.check(goal, proof)
        variants = {
            "order": [items[1], items[0], *items[2:]],
            "omission": items[:-1],
            "duplication": [*items, items[-1]],
            "definition body": [*items[:2], replace(items[2], body=("named", "q")), *items[3:]],
            "parameter type": [replace(items[0], type=("base", 0)), *items[1:]],
            "identity": [*items[:2], replace(items[2], identifier="another-definition"), *items[3:]],
            "assumption instead of theorem": [*items[:-1], replace(items[-1], kind="AXIOM")],
            "theorem proposition": [*items[:-1], replace(items[-1], body=("named", "q"))],
        }
        for label, changed in variants.items():
            with self.subTest(change=label):
                requested = importer.document_claim(importer.State(), changed)
                self.assertNotEqual(goal, requested)
                self.check(requested, proof, False)
        for initial in [importer.State(primitives=importer.egal_initial_primitives()),
                        importer.State(known=[("extra-assumption", ("named", "p"))])]:
            with self.subTest(initial=initial):
                self.check(importer.document_claim(initial, items), proof, False)
        # Source presentation metadata is not an additional logical premise.
        relocated = [replace(item, label="renamed-" + item.label, line=item.line + 100)
                     for item in items]
        self.assertEqual(importer.document_claim(importer.State(), relocated), goal)

    def test_claim_is_not_an_admission_or_a_cached_acceptance(self):
        impossible = importer.document('''
        (THM "false" "false" "proof-false" 0 (ALL (PROP) (DB 0)) 1)
        (PROOF "false" (HYP 0)) (QED)
        ''')
        requested = importer.document_claim(importer.State(), impossible)
        empty_goal, empty_proof = importer.compile_document(importer.State(), [])
        self.assertNotEqual(requested, empty_goal)
        self.check(requested, empty_proof, False)

    def test_shared_encoding_preserves_order_duplicates_and_exact_format(self):
        from nik_pattern_list_v1 import SharedPatternListEncoder
        p = importer.poly
        encoder = SharedPatternListEncoder("RowsNil",
            lambda row: ("RowsCons", (importer.app(row),)))
        rows = ["a", "b", "a"]
        old = importer.app("RowsNil")
        for row in reversed(rows):
            old = importer.app("RowsCons", importer.app(row), old)
        self.assertEqual(encoder.encode(rows), old)
        self.assertIs(encoder.encode(rows), encoder.encode(tuple(rows)))
        self.assertNotEqual(encoder.encode(rows), encoder.encode(["b", "a", "a"]))
        self.assertNotEqual(encoder.encode(rows), encoder.encode(["a", "b"]))
        # Prefix extension adds precisely one physical cell, not a copy of its
        # complete tail; repeated rows remain distinct occurrences in that tail.
        before = len(encoder.nodes)
        extended = encoder.encode(["c", *rows])
        self.assertEqual(len(encoder.nodes), before + 1)
        self.assertIs(extended[2][2][1], encoder.encode(rows))
        known = [("a", ("prop",))]  # encoded through the signature, not a term
        self.assertEqual(p.encode_signature(known), importer.app("MSigCons",
            importer.app("a"), p.encode_tp(("prop",)), importer.app("MSigNil")))
        facts = [("a", ("named", "p")), ("a", ("named", "q"))]
        expected = importer.app("MKnownNil")
        for name, term in reversed(facts):
            expected = importer.app("MKnownCons", importer.app(name), p.encode_tm(term), expected)
        self.assertEqual(p.encode_known(facts), expected)
        self.assertIs(p.encode_known(facts), p.encode_known(list(facts)))

    def test_deep_article_replay_and_rejection_do_not_use_the_c_stack(self):
        p, d = importer.poly, importer.evidence
        term = ("named", "p")
        scoped = importer.app("MScopedTerm", d.encode_declarations([]), p.encode_tm(term))
        goal = importer.app("MDefinitionConverts", scoped, scoped)
        _, proof = d.demand_conversion_article([], term, term)
        compiled = importer.dag.compile_shared_article(goal, proof).article
        leaf = compiled[3][1]
        root = compiled[3][2][1]
        depth = 50000
        nodes = [(leaf[0], i, leaf[2], leaf[3]) for i in range(depth)]
        nodes.append((root[0], depth, root[2], root[3]))
        article = (*compiled[:3], p.mono.list_term(*nodes), depth, compiled[-1])
        # Chronological articles may contain repeated valid nodes; they are
        # still independently replayed, not trusted from their repeated shape.
        p.require_public_result(p.run_cetta(self.cetta, goal, article), accepted=True)
        wrong = importer.app("MDefinitionConverts", scoped,
            importer.app("MScopedTerm", d.encode_declarations([]), p.encode_tm(("var", 7))))
        p.require_public_result(p.run_cetta(self.cetta, wrong, article), accepted=False)

    def test_ordered_declarations_and_polymorphic_axiom(self):
        source = '''
        (PRIM 0 "p" "primitive" (PROP) 1)
        (PARAM "q" "parameter" 0 (PROP) 2)
        (DEF "i" "identity" 1 (AR (TPVAR 0) (TPVAR 0))
          (LAM (TPVAR 0) (DB 0)) 3)
        (AXIOM "a" "assumption" 1 (ALL (TPVAR 0)
          (IMP (PRIM 0) (TMH "parameter"))) 4)
        '''
        state = importer.State()
        for declaration in importer.declarations(source):
            goal, proof = state.admit(declaration)
            self.check(goal, proof)
        self.assertEqual(state.known[0][1][0], "typeAll")

    def test_demand_conversion_replays_every_congruence_and_rejects_forgery(self):
        d, p = importer.evidence, importer.poly
        prop = ("prop",)
        declarations = [("alias", prop, ("named", "p")),
                        ("p", prop, None), ("f", ("arr", prop, prop), None)]
        a, b = ("named", "alias"), ("named", "p")
        examples = [
            (a, b),
            (("imp", a, a), ("imp", b, b)),
            (("app", ("lam", prop, ("var", 0)), a), b),
            (("app", ("named", "f"), a), ("app", ("named", "f"), b)),
            (("lam", prop, a), ("lam", prop, b)),
            (("all", prop, a), ("all", prop, b)),
            (("typeLam", a), ("typeLam", b)),
            (("typeAll", a), ("typeAll", b)),
            (("typeApp", ("typeLam", a), prop), b),
            (("lam", prop, ("app", ("named", "f"), ("var", 0))), ("named", "f")),
        ]
        encoded = d.encode_declarations(declarations)
        def goal(left, right):
            return importer.app("MDefinitionConverts",
                importer.app("MScopedTerm", encoded, p.encode_tm(left)),
                importer.app("MScopedTerm", encoded, p.encode_tm(right)))
        for left, right in examples:
            with self.subTest(left=left):
                common, article = d.demand_conversion_article(declarations, left, right)
                self.check(goal(left, right), article)
                self.check(goal(left, ("var", 8)), article, False)
        with self.assertRaises(d.ConversionMismatch):
            d.demand_conversion_article(declarations, b, ("var", 8))

    def test_equal_aliases_need_no_delta_normalization(self):
        d, p = importer.evidence, importer.poly
        prop = ("prop",)
        typ = ("arr", prop, prop)
        alias = ("named", "same")
        # The declaration occurrence is well typed in its preceding signature,
        # but eager unfolding in the extended signature has a delta/eta cycle.
        state = importer.State(terms=[("same", typ, None)])
        declaration = importer.Declaration("DEF", "same_alias", "same", 1, typ,
            ("lam", prop, ("app", alias, ("var", 0))))
        goal, admission = state.admit(declaration)
        self.check(goal, admission)
        common, article = d.demand_conversion_article(state.terms, alias, alias, max_steps=0)
        self.assertEqual(common, alias)
        encoded = d.encode_declarations(state.terms)
        scoped = importer.app("MScopedTerm", encoded, p.encode_tm(alias))
        self.check(importer.app("MDefinitionConverts", scoped, scoped), article)
        with self.assertRaises(d.ConversionMismatch):
            d.weak_head_with_article(state.terms, ("app", alias, ("var", 0)))

    def test_beta_erasure_does_not_compare_discarded_arguments(self):
        d, p = importer.evidence, importer.poly
        prop = ("prop",)
        declarations = [("p", prop, None), ("q", prop, None)]
        value, other = ("named", "p"), ("named", "q")
        function = ("lam", prop, value)
        left, right = ("app", function, value), ("app", function, other)
        common, article = d.demand_conversion_article(declarations, left, right, max_steps=1)
        self.assertEqual(common, value)
        encoded = d.encode_declarations(declarations)
        def goal(a, b):
            return importer.app("MDefinitionConverts",
                importer.app("MScopedTerm", encoded, p.encode_tm(a)),
                importer.app("MScopedTerm", encoded, p.encode_tm(b)))
        self.check(goal(left, right), article)
        self.check(goal(left, other), article, False)
        with self.assertRaises(RuntimeError):
            d.demand_conversion_article(declarations, left, right, max_steps=0)

    def test_polymorphic_assumption_is_not_a_proof(self):
        state = importer.State()
        declaration = importer.declarations(
            '(AXIOM "f" "f" 1 (ALL (PROP) (DB 0)) 1)')[0]
        before = state.environment()
        _, proof = state.admit(declaration)
        goal = importer.app("MDefinitionProves", before, importer.poly.mono.nat(0),
                            importer.poly.encode_type_context([]),
                            importer.poly.encode_proof_context([]),
                            importer.poly.encode_tm(declaration.body))
        self.check(goal, proof, False)

    def test_wrong_declared_formula_rejected(self):
        state = importer.State()
        declaration = importer.declarations(
            '(AXIOM "f" "f" 1 (ALL (PROP) (DB 0)) 1)')[0]
        before = state.environment()
        _, proof = state.admit(declaration)
        wrong_body = ("all", ("prop",), ("var", 0))
        after = importer.State(known=[("f", wrong_body)])
        goal = importer.app("MMegalodonTheoryAdmits", before,
                            importer.app("MTheoryAxiom", importer.app("f"),
                                         importer.poly.encode_tm(wrong_body)),
                            after.environment())
        self.check(goal, proof, False)

    def test_later_declaration_cannot_justify_earlier_definition(self):
        declaration = importer.declarations(
            '(DEF "x" "x" 0 (PROP) (TMH "later") 1)')[0]
        state = importer.State()
        with self.assertRaises(SystemExit):
            state.admit(declaration)
        self.assertEqual(state.terms, [])

    def test_primitive_gap_rejected_without_mutation(self):
        declaration = importer.declarations('(PRIM 1 "p" "p" (PROP) 1)')[0]
        state = importer.State()
        with self.assertRaises(ValueError):
            state.admit(declaration)
        self.assertEqual(state.primitives, [])

    def test_bad_definition_type_rejected(self):
        declaration = importer.declarations(
            '(DEF "x" "x" 0 (SET) (ALL (PROP) (DB 0)) 1)')[0]
        with self.assertRaises(ValueError):
            importer.State().admit(declaration)

    def test_theorems_never_silently_imported_as_axioms(self):
        for source in ['(THM "x" "x" "none" 0 (ALL (PROP) (DB 0)) 1)',
                       '(ADMITTED)', '(QEDWITHADMITS)', '(UNKNOWN)']:
            with self.assertRaises(ValueError):
                importer.declarations(source)

    def test_invalid_prefix_count_rejected(self):
        with self.assertRaises(ValueError):
            importer.declarations('(AXIOM "f" "f" -1 (ALL (PROP) (DB 0)) 1)')

    def test_source_parameter_reversal_respects_both_binding_axes(self):
        sx, reverse = importer.sx, importer.source_prefix.to_binders
        raw = sx.parse_sexprs(
            '(TALL (AR (TPVAR 0) (AR (TPVAR 1) (AR (TPVAR 2) (TPVAR 3)))))')[0]
        expected = sx.parse_sexprs(
            '(TALL (AR (TPVAR 0) (AR (TPVAR 3) (AR (TPVAR 2) (TPVAR 1)))))')[0]
        self.assertEqual(reverse(raw, 3), expected)
        self.assertEqual(reverse(reverse(raw, 3), 3), raw)
        proof = sx.parse_sexprs('(TLAM (TPVAR 0) (PLAM (DB 0) (HYP 0)))')[0]
        expected_proof = sx.parse_sexprs('(TLAM (TPVAR 2) (PLAM (DB 0) (HYP 0)))')[0]
        self.assertEqual(reverse(proof, 3), expected_proof)
        self.assertEqual(reverse(reverse(proof, 3), 3), proof)
        source = '(DEF "k" "k" 2 (AR (TPVAR 0) (AR (TPVAR 1) (TPVAR 0))) ' \
                 '(LAM (TPVAR 0) (LAM (TPVAR 1) (DB 1))) 1)'
        item = importer.declarations(source)[0]
        self.assertEqual(item.type,
            ("all", ("all", ("arr", ("var", 1), ("arr", ("var", 0), ("var", 1))))))
        # Formation alone would accept the old, wrongly ordered binder type.
        # The concrete source-order equation is therefore a separate control.
        self.assertNotEqual(item.type,
            ("all", ("all", ("arr", ("var", 0), ("arr", ("var", 1), ("var", 0))))))

    def test_negative_indices_are_not_silently_zero(self):
        for source in ['(PARAM "x" "x" 0 (TPVAR -1) 1)',
                       '(PARAM "x" "x" 0 (BASE -1) 1)',
                       '(AXIOM "x" "x" 0 (DB -1) 1)',
                       '(AXIOM "x" "x" 0 (PRIM -1) 1)']:
            with self.assertRaises(SystemExit):
                importer.declarations(source)

    def test_primitive_theorem_and_later_use(self):
        source = '''
        (PRIM 0 "p" "primitive" (PROP) 1)
        (THM "id" "identity" "proof-id" 0
          (ALL (PROP) (IMP (DB 0) (DB 0))) 2)
        (PROOF "id" (TLAM (PROP) (PLAM (DB 0) (HYP 0))))
        (QED)
        (THM "use" "instance" "proof-use" 0 (IMP (PRIM 0) (PRIM 0)) 3)
        (USESKNOWN "identity")
        (PROOF "use" (PTMAP (KNOWN "identity") (PRIM 0)))
        (QED)
        (THM "direct" "direct-id" "proof-direct" 0 (IMP (PRIM 0) (PRIM 0)) 4)
        (PROOF "direct" (PLAM (PRIM 0) (HYP 0)))
        (QED)
        '''
        items = importer.document(source)
        state = importer.State()
        for item in items:
            goal, proof = state.admit(item)
            self.check(goal, proof)
        self.assertEqual(items[2].reported_known, ("identity",))
        self.assertEqual(items[1].proof_identifier, "proof-id")
        self.assertEqual(len(state.known), 3)

    def test_theorem_requires_formation_and_proof_separately(self):
        item = importer.document('''
        (THM "id" "id" "proof-id" 0 (ALL (PROP) (IMP (DB 0) (DB 0))) 1)
        (PROOF "id" (TLAM (PROP) (PLAM (DB 0) (HYP 0)))) (QED)
        ''')[0]
        state = importer.State()
        formation = state.formation(item.body)
        _, proof_article = importer.proofs.compile_proposition(
            state.terms, state.known, item.body, item.proof, state.primitives)
        goal, article = state.admit(item)
        self.check(goal, article)
        for children in [[formation, formation], [proof_article, proof_article],
                         [formation], [proof_article]]:
            forged = (article[0], article[1], importer.poly.mono.proof_list(*children))
            self.check(goal, forged, False)

    def test_later_theorem_cannot_justify_earlier_proof(self):
        item = importer.document('''
        (THM "x" "x" "proof-x" 0 (ALL (PROP) (IMP (DB 0) (DB 0))) 1)
        (PROOF "x" (KNOWN "later")) (QED)
        ''')[0]
        state = importer.State()
        with self.assertRaises(SystemExit):
            state.admit(item)
        self.assertEqual(state.known, [])

    def test_unfinished_or_mispaired_theorem_is_not_admitted(self):
        header = '(THM "x" "x" "proof-x" 0 (ALL (PROP) (IMP (DB 0) (DB 0))) 1)'
        proof = '(PROOF "x" (TLAM (PROP) (PLAM (DB 0) (HYP 0))))'
        for tail in ['', proof, '(QED)', proof + '(ADMITTED)',
                     proof + '(QEDWITHADMITS)', proof + proof + '(QED)',
                     '(PROOF "y" (HYP 0)) (QED)',
                     '(PARAM "p" "p" 0 (PROP) 2)' + proof + '(QED)']:
            with self.assertRaises(ValueError):
                importer.document(header + tail)

    def test_negative_hypothesis_cannot_select_last_premise(self):
        item = importer.document('''
        (THM "id" "id" "proof-id" 0 (ALL (PROP) (IMP (DB 0) (DB 0))) 1)
        (PROOF "id" (TLAM (PROP) (PLAM (DB 0) (HYP -1)))) (QED)
        ''')[0]
        with self.assertRaises(ValueError):
            importer.State().admit(item)

    def test_source_tactics_document_admitted_in_order(self):
        if self.megalodon is None:
            self.skipTest("Megalodon source exporter not supplied")
        source = Path(__file__).resolve().parents[1] / "tests/support/megalodon/positive_tactics_package.mg"
        exported = subprocess.run([str(self.megalodon.resolve()), "-sexprinfo", str(source)],
                                  capture_output=True, text=True, check=True)
        items = importer.document(exported.stdout)
        self.assertEqual([item.kind for item in items].count("DEF"), 8)
        self.assertEqual([item.kind for item in items].count("THM"), 10)
        self.assertNotIn("AXIOM", [item.kind for item in items])
        state = importer.State()
        for item in items:
            self.check(*state.admit(item))
        self.assertEqual(len(state.known), 10)
        batch_state = importer.State()
        self.check(*importer.compile_document(batch_state, items))
        self.assertEqual(batch_state.known, state.known)

    def test_document_cannot_skip_or_reorder_transitions(self):
        items = importer.declarations('''
        (PARAM "p" "p" 0 (PROP) 1)
        (DEF "q" "q" 0 (PROP) (TMH "p") 2)
        ''')
        goal, proof = importer.compile_document(importer.State(), items)
        self.check(goal, proof)
        children = proof[2]
        first, rest = children[1], children[2][1]
        swapped = (proof[0], proof[1], importer.poly.mono.proof_list(rest, first))
        self.check(goal, swapped, False)
        skipped = (proof[0], proof[1], importer.poly.mono.proof_list(first))
        self.check(goal, skipped, False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--megalodon", type=Path)
    args = parser.parse_args()
    DeclarationImportTests.cetta = args.cetta
    DeclarationImportTests.megalodon = args.megalodon
    unittest.main(argv=[__file__])
