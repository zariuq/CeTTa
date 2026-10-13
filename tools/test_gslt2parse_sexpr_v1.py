#!/usr/bin/env python3
"""Exact carrier and deep packed-evidence controls for S-expression reading."""

import unittest

from gslt2parse_schema_v1 import (SchemaError, StringLiteral, Symbol, Variable,
                                 parse_sexprs, render)


class CarrierControls(unittest.TestCase):
    def test_leaf_kinds_remain_distinct(self):
        self.assertEqual(parse_sexprs('x "x" ?x -12 0 1e0 ()'),
                         [Symbol('x'), StringLiteral('x'), Variable('x'),
                          -12, 0, Symbol('1e0'), ()])

    def test_strings_comments_and_unicode(self):
        source = '(root "(;not a comment)" "\\u0000\\n\\\"" α) ; omitted\n (tail ?v)'
        values = parse_sexprs(source)
        self.assertEqual(values, [(Symbol('root'), StringLiteral('(;not a comment)'),
                                   StringLiteral('\0\n"'), Symbol('α')),
                                  (Symbol('tail'), Variable('v'))])
        self.assertEqual(parse_sexprs(' '.join(render(value) for value in values)), values)

    def test_no_incomplete_or_malformed_input_is_accepted(self):
        for source in ('(', ')', '(x))', '((x)', '"unfinished', '"\\q"', '"\\ud800"'):
            with self.subTest(source=source), self.assertRaises(SchemaError):
                parse_sexprs(source, source='control')

    def test_complete_deep_evidence_is_not_truncated(self):
        depth = 25000
        values = parse_sexprs('(' * depth + 'leaf' + ')' * depth + ' tail')
        self.assertEqual(len(values), 2)
        value = values[0]
        for _ in range(depth):
            self.assertIsInstance(value, tuple)
            self.assertEqual(len(value), 1)
            value = value[0]
        self.assertEqual(value, Symbol('leaf'))
        self.assertEqual(values[1], Symbol('tail'))

    def test_deep_incomplete_evidence_is_rejected(self):
        with self.assertRaisesRegex(SchemaError, "unclosed"):
            parse_sexprs('(' * 25000 + 'leaf' + ')' * 24999)


if __name__ == '__main__':
    unittest.main()
