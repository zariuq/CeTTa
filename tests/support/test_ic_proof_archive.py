#!/usr/bin/env python3
"""Positive and negative controls for the restricted Prover9 data adapter."""

import unittest
from io import StringIO
from unittest.mock import Mock

from check_ic_proof_archive import Row, Unsupported, choose, dag, equation, read_proof, render, shape


class ProofArchiveControls(unittest.TestCase):
    def test_equation_and_shared_identifiers(self):
        tree = equation(r'x \ (x * y) = y')
        x, y = ('leaf', b'x'), ('leaf', b'y')
        self.assertEqual(tree, ('node', b'=', (
            ('node', b'\\', (x, ('node', b'*', (x, y)))), y)))
        roots, records = dag([tree, tree])
        self.assertEqual(roots, [4, 4])
        self.assertEqual(records, [
            ('leaf', b'x', ()), ('leaf', b'y', ()),
            ('node', b'*', (0, 1)), ('node', b'\\', (0, 2)), ('node', b'=', (3, 1))])

    def test_functions_and_argument_order(self):
        first = equation('L(x,y,z) = R(z,y,x)')
        self.assertEqual(first[2][0][1], b'L')
        self.assertEqual([child[1] for child in first[2][0][2]], [b'x', b'y', b'z'])
        self.assertNotEqual(first, equation('L(x,y,z) = R(x,y,z)'))
        self.assertEqual(equation(render(first)[1:-1]), first)

    def test_parentheses_disambiguate(self):
        self.assertNotEqual(equation('(x * y) / z = w'), equation('x * (y / z) = w'))
        for text in ('x * y / z = w', 'x = y * z / w'):
            with self.subTest(text=text), self.assertRaises(Unsupported):
                equation(text)

    def test_not_equational_or_incomplete(self):
        for text in ('x != y', 'x = y | z = w', 'x = y -> y = x',
                     'x = y & y = z', 'x =', 'f(x,) = y', 'f(x = y',
                     'x = y)', '(x * y = z', 'x = y z', 'x = "y"', ''):
            with self.subTest(text=text), self.assertRaises(Unsupported):
                equation(text)

    def test_identifiers_are_not_alpha_merged(self):
        self.assertNotEqual(equation('x = x'), equation('y = y'))
        self.assertNotEqual(equation('x = x'), equation('x = y'))
        roots, records = dag([equation('x = x'), equation('x = y')])
        self.assertEqual(roots, [1, 3])
        self.assertEqual(records[1], ('node', b'=', (0, 0)))

    def test_bounded_stable_selection_keeps_occurrences(self):
        forms = ['f(g(x,x),h(x,x)) = f(g(y,y),h(y,y))',
                 'f(g(z,z),h(z,z)) = f(g(u,u),h(u,u))']
        rows = [Row(index + 20, index + 1, forms[index % 2], 'assumption',
                    equation(forms[index % 2])) for index in range(8)]
        self.assertEqual(choose(rows, 3), rows[:3])
        self.assertEqual(shape(rows[0].tree, 3), shape(rows[1].tree, 3))
        roots, _ = dag([row.tree for row in rows])
        self.assertEqual(len(roots), 8)
        self.assertEqual(roots[0], roots[2])

    def test_record_provenance_and_explicit_refusals(self):
        source = Mock()
        source.open.return_value = StringIO(
            '% a comment\n11 x = x.  [assumption].\n'
            '12 x != y | z = w.  [para(1(a,1),2(a,1))].\n'
            '13 x = y. [unadmitted-format].\n'
            '14 x = x # label("same").  [copy(11)].\n')
        rows, records, skipped = read_proof(source)
        self.assertEqual(records, 4)
        self.assertEqual(len(rows), 2)
        self.assertEqual(sum(skipped.values()), 2)
        self.assertEqual(skipped['unsupported-record-format'], 1)
        self.assertEqual([(row.line, row.number, row.inference) for row in rows],
                         [(2, 11, 'assumption'), (5, 14, 'copy(11)')])
        self.assertEqual(rows[0].tree, rows[1].tree)

    def test_depth_is_declared_not_recursion_limit(self):
        with self.assertRaisesRegex(Unsupported, 'term-depth'):
            equation('f(' * 130 + 'x' + ')' * 130 + ' = y')


if __name__ == '__main__':
    unittest.main()
