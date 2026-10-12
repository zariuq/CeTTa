#!/usr/bin/env python3
"""Positive and negative controls for the independent C AST projection."""

import hashlib
import struct
import unittest
from pathlib import Path
from unittest.mock import patch

from gslt2parse_schema_v1 import StringLiteral, Symbol, render
from lbnf_reference_ast_v1 import (Constructor, InputDomainError, ReferenceLayout,
                                   GoldenReader, c_category, golden_comparable,
                                   golden_tokens, normalize_ast, visitor_source)
from qualify_lbnf_corpus_v1 import evidence_sha256, matching_erratum, observe


def form(name, *children):
    return (Symbol(name), *children)


def finite_list(values):
    result = Symbol("LNil")
    for value in reversed(values):
        result = form("LCons", value, result)
    return result


SPAN = form("LbnfSourceSpanV1", 0, 1, 0, 1)


def token(category, payload):
    return form("LbnfAstTokenV1", StringLiteral(category), Symbol("RawLexemeV1"),
                StringLiteral(payload), Symbol("LbnfNoTokenPositionV1"), SPAN, StringLiteral("origin"))


def node(category, label, *children):
    return form("LbnfAstNodeV1", StringLiteral(category), StringLiteral(label), SPAN,
                finite_list(children))


class TypedReferenceControls(unittest.TestCase):
    def test_iterative_evidence_hash_preserves_canonical_text(self):
        value = form("outer", StringLiteral('a"λ'), form("inner", -3), ())
        self.assertEqual(evidence_sha256(value), hashlib.sha256(render(value).encode()).hexdigest())
        deep = Symbol("nil")
        for index in range(30000):
            deep = form("cons", index, deep)
        self.assertEqual(len(evidence_sha256(deep)), 64)

    def test_category_coercions_and_nested_lists(self):
        self.assertEqual(c_category("Exp12"), "Exp")
        self.assertEqual(c_category("[[Exp3]]"), "ListListExp")

    def test_constructor_category_and_field_order(self):
        tree = normalize_ast(node("Exp3", "Pair", node("Exp", "Left"), node("Exp", "Right")))
        self.assertEqual(tree, ["node", "Exp", "Pair", [
            ["node", "Exp", "Left", []], ["node", "Exp", "Right", []]]])
        self.assertNotEqual(tree, normalize_ast(node("Exp", "Pair", node("Exp", "Right"),
                                                     node("Exp", "Left"))))
        self.assertNotEqual(normalize_ast(node("A", "Same")), normalize_ast(node("B", "Same")))

    def test_custom_token_is_exact_bytes_not_viewer_syntax(self):
        payload = 'a"),(Other ["λ\\n'
        self.assertEqual(normalize_ast(token("MyToken", payload)),
                         ["token", "MyToken", payload.encode().hex()])
        self.assertNotEqual(normalize_ast(token("MyToken", payload)),
                            normalize_ast(token("MyToken", payload + '"')))

    def test_decoded_string_and_character(self):
        self.assertEqual(normalize_ast(token("String", "line1\n\tline2")),
                         ["token", "String", "line1\n\tline2".encode().hex()])
        self.assertEqual(normalize_ast(token("Char", "\t")), ["token", "Char", "09"])
        with self.assertRaises(InputDomainError):
            normalize_ast(token("Char", "λ"))

    def test_integer_domain_is_not_silently_wrapped(self):
        self.assertEqual(normalize_ast(token("Integer", "2147483647")),
                         ["token", "Integer", "2147483647"])
        self.assertEqual(normalize_ast(token("Integer", "-2147483648")),
                         ["token", "Integer", "-2147483648"])
        for spelling in ("2147483648", "-2147483649"):
            with self.assertRaises(InputDomainError):
                normalize_ast(token("Integer", spelling))

    def test_double_keeps_binary64_bits_and_negative_zero(self):
        self.assertEqual(normalize_ast(token("Double", "0.1")),
                         ["token", "Double", struct.pack(">d", 0.1).hex()])
        self.assertNotEqual(normalize_ast(token("Double", "-0.0")),
                            normalize_ast(token("Double", "0.0")))
        with self.assertRaises(InputDomainError):
            normalize_ast(token("Double", "1e999"))

    def test_lists_keep_order_and_repeated_elements(self):
        value = form("LbnfAstListV1", StringLiteral("[Exp]"), SPAN,
                     finite_list([node("Exp", "X"), node("Exp", "X")]))
        self.assertEqual(normalize_ast(value), ["list", [["node", "Exp", "X", []]] * 2])
        with self.assertRaises(ValueError):
            normalize_ast(form("NotAnAst"))

    def test_independent_visitor_uses_constructor_not_grouped_field_order(self):
        constructor = Constructor("Root", "Pair", (("String", "p1"), ("Integer", "p2")),
                                  ("u.pair_.string_", "u.pair_.integer_"), True)
        layout = ReferenceLayout(("String",), (constructor,), "Root", "pRoot")
        source = visitor_source(layout)
        self.assertLess(source.index("ast_String(x->u.pair_.string_)"),
                        source.index("ast_Integer(x->u.pair_.integer_)"))
        self.assertIn("pRoot(input)", source)
        with self.assertRaises(ValueError):
            layout.signature("Wrong", "Pair")

    def test_evidence_preserves_ast_multiplicity(self):
        empty_relation = Symbol("nil")
        forest = form("pf-v1", 1, 0, 0, empty_relation, empty_relation)
        ast = node("Exp", "X")
        value = form("LangDef:AuthoredParseEvidenceAccepted", Symbol("Corpus"),
                     StringLiteral("binding"), StringLiteral("source"), StringLiteral("forest"),
                     forest, (ast, ast))
        reference = {"reference": {"status": 0}}
        typed = {"format": "BnfcTypedAstV1", "int_bits": 32, "double_bytes": 8,
                 "ast": normalize_ast(ast)}
        observed = observe(value, "source", reference, typed)
        self.assertTrue(observed["matches"])
        self.assertEqual(observed["projected_tree_count"], 2)
        self.assertEqual(list(observed["projected_tree_multiplicities"].values()), [2])
        wrong = {**typed, "ast": normalize_ast(node("Exp", "Y"))}
        self.assertFalse(observe(value, "source", reference, wrong)["matches"])
        self.assertFalse(observe(value, "different-source", reference, typed)["matches"])

    def test_only_positioned_scoped_grammar_refusals_are_allowed(self):
        reference = {"reference": {"status": 0}}
        for cause, allowed in [("layout", True), ("define", True), ("other", False)]:
            value = form("LbnfCorpusGrammarRefusedV1", form("LbnfRefusedV1",
                         Symbol(cause), SPAN, StringLiteral("reason")))
            result = observe(value, "source", reference, None)
            self.assertEqual(result["allowed_refusal"], allowed)
            self.assertFalse(result["matches"])

    def test_golden_notation_retains_typed_fields(self):
        layout = ReferenceLayout(("String", "Ident"), (
            Constructor("Exp", "X", (("Ident", "p1"),), ("u.x_.ident_",), True),
            Constructor("Exp", "Pair", (("Exp", "p1"), ("Exp", "p2")),
                        ("u.pair_.exp_1", "u.pair_.exp_2"), True)), "Exp", "pExp")
        a = GoldenReader('Pair (X (Ident "a")) (X (Ident "b"))', layout).document()
        b = GoldenReader('(Pair (X "a") (X "b"))', layout).document()
        self.assertEqual(a, b)
        for invalid in ('Pair (X "a")', 'Pair (X "a") (X (String "b"))', '(X "a"))'):
            with self.assertRaises(ValueError):
                GoldenReader(invalid, layout).document()

    def test_golden_decodes_unicode_decimal_escapes_and_char_lists(self):
        self.assertEqual(golden_tokens(r'"\228\246\252\223//\\%#FOO"'),
                         [("string", "äöüß//\\%#FOO")])
        row = Constructor("ListChar", "ListChar", (("Char", "p1"), ("ListChar", "p2")),
                          ("char_", "listchar_"), False)
        layout = ReferenceLayout((), (row,), "ListChar", "pListChar")
        self.assertEqual(GoldenReader(r'"ABC\t"', layout).document(),
                         GoldenReader(r"['A','B','C','\t']", layout).document())

    def test_category_called_list_is_not_a_list_carrier(self):
        row = Constructor("List", "List_Mk", (), (), True)
        layout = ReferenceLayout((), (row,), "List", "pList")
        self.assertEqual(GoldenReader("List_Mk", layout).document(), ["node", "List", "List_Mk", []])

    def test_golden_double_display_does_not_weaken_live_bit_comparison(self):
        a = ["token", "Double", struct.pack(">d", 1.234567890123451).hex()]
        b = ["token", "Double", struct.pack(">d", 1.234567890123452).hex()]
        self.assertNotEqual(a, b)
        self.assertEqual(golden_comparable(a), golden_comparable(b))

    def test_erratum_expires_when_any_source_hash_changes(self):
        grammar, source = Path("fixture/test.cf"), Path("fixture/good01.in")
        content = {grammar: b"grammar", source: b"input", source.with_suffix(".out"): b"golden"}
        entry = {"grammar": "fixture/test.cf", "input": "fixture/good01.in",
                 "golden": "fixture/good01.out", "sha256": {
                    kind: hashlib.sha256(content[path]).hexdigest() for kind, path in
                    (("grammar", grammar), ("input", source), ("golden", source.with_suffix(".out")))}}
        with patch.object(Path, "read_bytes", lambda path: content[path]):
            for path in content:
                self.assertEqual(matching_erratum(grammar, source, [entry]), entry)
                original = content[path]
                content[path] = b"changed source"
                self.assertIsNone(matching_erratum(grammar, source, [entry]))
                content[path] = original


if __name__ == "__main__":
    unittest.main()
