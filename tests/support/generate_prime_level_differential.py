#!/usr/bin/env python3
"""Cases for the differential test of Prime's universe levels against Lean.

The C level library (src/prime_level.c) follows definitions that are proved
in the Mettapedia Lean development: ordinal notations below epsilon-zero in
Cantor normal form, and the successor/maximum algebra of level expressions
over them.  This program writes a fixed, seeded set of cases in two forms:

  cases.txt                    read by tests/support/prime_level_differential.c
  PrimeLevelDifferential.lean  computes the verdicts of the same cases from
                               the Lean definitions and prints them

The two outputs must be equal line for line.  The Lean file also states a
few of the cases as propositions that Lean's kernel decides.  For a tree the
C side prints two verdicts, the level library's and the kernel's test of the
same tree in its core syntax; Lean's normal-form test is the answer to both.
For a pair of notations the C side also reads the maximum of the two as an
author writes it, `(max a b)`; Lean's maximum is the answer to that as well.

A notation is exchanged as its prefix code (Mettapedia's `Cnf.code`): 0 for
zero, and for omega^e * c + r the coefficient c, the code of e and the code
of r.  A number has any number of digits: a coefficient, a numeral and the
number of a level above the Cantor normal forms are natural numbers of any
size on both sides.  A level expression is exchanged in prefix form: 0 and a
notation code for a constant, 1 and an index for a parameter, 2 for a
successor, 3 for a maximum, and where levels above the Cantor normal forms
are exchanged 4 and the number of one of them, and 5 and a count for that
many successors.  An arithmetic expression over numerals and
omega, read by the C side as an author writes it, is exchanged in prefix
form too: 0 and a number for a numeral, 4 for omega, and 1, 2 or 3 followed
by two expressions for a sum, a product or a power.  Its verdict is the
notation of its value, the ordinal sum, product and power of Mettapedia's
`Cnf.add`, `Cnf.mul` and `Cnf.pow`.  A raise is exchanged as a level
expression, a closed bound and the three notations that the parameters 0, 1
and 2 hold; its verdict is what they hold after one step of the kernel's
search for the least level instance (Mettapedia's `LeastInstance.raise`), or
that no least raise exists.  A raise through a counted successor is the raise
through that many successors.

The levels above every Cantor normal form, the n-th of which is the ordinal
epsilon-zero + n, are Mettapedia's `Above Level`: the notations followed by
the natural numbers, with their order, successor and maximum.  The generated
Lean file adds the predecessor of such a level, with the proof that it is
one, so that the level under a successor is computed as it is for a notation.
Such a level is exchanged as 0 and a notation code, or as 1 and the number n.
For a pair of them the C side prints the verdicts of the level library and
asks its kernel whether the universe at the first is a member of the universe
at the second.

A level parameter stands for a level an author writes, a Cantor normal form.
In Lean that is a bound on every parameter: it lies below the first level
above the Cantor normal forms (`LevelBounds`, with the bounds that
Mettapedia's `AmbientSets.writtenBounds` states).  For a pair of level
expressions over the levels above the Cantor normal forms and a valuation of
the parameters 0, 1 and 2 in the Cantor normal forms, both sides print the
canonical forms of the two expressions, of the successor of the first and of
their maximum; whether the first is at most the second under every valuation
in the Cantor normal forms, and the second at most the first; whether they
are equal under those valuations; and the values of the two at the given
valuation.  Lean decides the order by `LevelBounds.CanonicalLeUnder`, which
Mettapedia proves to be the order under every valuation that respects the
bounds.  The C side also asks its kernel whether the universe at the first
expression is a member of the universe at the successor of the second, which
holds exactly when the first is at most the second.  Under a constant above
the Cantor normal forms the level library keeps no parameter; Lean prints the
canonical form without them once it has checked that dropping them changes
nothing under the bounds.

Every case has a value that both sides compute.  The generated arithmetic
expressions are kept to values of a few hundred terms and coefficients of a
few thousand binary digits; the cases listed by hand go to notations of five
thousand terms and towers of a hundred omegas.
"""

import argparse
from pathlib import Path
import random

ZERO = ()


def term(exponent, coefficient, remainder):
    return (exponent, coefficient, remainder)


def natural(value):
    return ZERO if value == 0 else term(ZERO, value, ZERO)


OMEGA = term(natural(1), 1, ZERO)


def compare(left, right):
    """The lexicographic comparison of trees, used only to build cases."""
    while True:
        if left == ZERO or right == ZERO:
            return (left != ZERO) - (right != ZERO)
        exponents = compare(left[0], right[0])
        if exponents:
            return exponents
        if left[1] != right[1]:
            return -1 if left[1] < right[1] else 1
        left, right = left[2], right[2]


def is_normal(notation):
    return notation == ZERO or (
        is_normal(notation[0]) and is_normal(notation[2]) and
        (notation[2] == ZERO or compare(notation[2][0], notation[0]) < 0))


def code(notation):
    if notation == ZERO:
        return [0]
    return [notation[1]] + code(notation[0]) + code(notation[2])


def text(numbers):
    return " ".join(str(number) for number in numbers)


def long_number(rng):
    """A number beyond the machine numbers: at their edge, with digits that
    are zero inside, and of twenty to forty-five digits."""
    if rng.random() < 0.4:
        return rng.choice([
            2 ** 63 - 1, 2 ** 63, 2 ** 63 + 1, 2 ** 64 - 1, 2 ** 64,
            2 ** 64 + 1, 10 ** 18 - 1, 10 ** 18, 10 ** 18 + 1, 10 ** 27,
            10 ** 27 + 1, 10 ** 36 - 1, 2 * 10 ** 27 + 1, 10 ** 27 + 2,
            10 ** 9, 10 ** 9 - 1, 10 ** 45])
    return rng.randint(10 ** 19, 10 ** 45)


def coefficient(rng):
    roll = rng.random()
    if roll < 0.65:
        return rng.randint(1, 3)
    if roll < 0.87:
        return rng.randint(4, 40)
    if roll < 0.93:
        return rng.randint(41, 2 ** 40)
    return long_number(rng)


def normal(rng, height):
    """A notation in normal form whose exponents nest at most `height` deep."""
    if height == 0:
        return ZERO
    exponents = []
    for _ in range(rng.choice([0, 1, 1, 1, 2, 2, 3])):
        exponent = normal(rng, rng.randint(0, height - 1))
        if all(compare(exponent, other) != 0 for other in exponents):
            exponents.append(exponent)
    result = ZERO
    for exponent in sorted(exponents, key=_Key):
        result = term(exponent, coefficient(rng), result)
    return result


class _Key:
    """Orders trees for sorting by `compare`."""

    def __init__(self, notation):
        self.notation = notation

    def __lt__(self, other):
        return compare(self.notation, other.notation) < 0


def successor(notation):
    if notation == ZERO:
        return natural(1)
    if notation[0] == ZERO:
        return term(ZERO, notation[1] + 1, ZERO)
    return term(notation[0], notation[1], successor(notation[2]))


def nearby(rng, notation):
    """A normal form close to `notation`: equal, or differing in one place."""
    roll = rng.random()
    if roll < 0.25 or notation == ZERO:
        return notation if roll < 0.25 else natural(rng.randint(0, 2))
    if roll < 0.45:
        return successor(notation)
    if roll < 0.65:
        return term(notation[0], notation[1] + 1, notation[2])
    if roll < 0.8:
        return term(notation[0], notation[1], ZERO)
    if roll < 0.9:
        return notation[2]
    changed = term(notation[0], notation[1], nearby(rng, notation[2]))
    return changed if is_normal(changed) else notation


def tree(rng, depth):
    """Any tree, in normal form or not."""
    if depth == 0 or rng.random() < 0.3:
        return ZERO
    count = long_number(rng) if rng.random() < 0.08 else rng.randint(1, 3)
    return term(tree(rng, depth - 1), count, tree(rng, depth - 1))


def constant(rng):
    roll = rng.random()
    if roll < 0.45:
        return natural(rng.randint(0, 6))
    if roll < 0.52:
        return natural(long_number(rng))
    if roll < 0.62:
        return OMEGA
    return normal(rng, rng.randint(1, 3))


def numeral(rng):
    """A numeral: mostly small, sometimes at the edge of the machine numbers
    and sometimes long."""
    roll = rng.random()
    if roll < 0.55:
        return rng.randint(0, 6)
    if roll < 0.75:
        return rng.randint(7, 70)
    if roll < 0.85:
        return rng.choice([2 ** 31, 2 ** 32, 2 ** 32 + 1, 3037000499,
                           3037000500, 10 ** 6, 10 ** 9])
    if roll < 0.93:
        return rng.choice([2 ** 62, 2 ** 62 - 1, 2 ** 63 - 1, 2 ** 63 - 2,
                           4611686018427387904, 9223372036854775806])
    return long_number(rng)


# An arithmetic expression is a numeral ("n", value), omega ("w",), or a sum,
# product or power ("+" | "*" | "^", left, right).

PREFIX = {"+": 1, "*": 2, "^": 3}


def prefix(expression):
    """An arithmetic expression in prefix form."""
    if expression[0] == "n":
        return [0, expression[1]]
    if expression[0] == "w":
        return [4]
    return ([PREFIX[expression[0]]] + prefix(expression[1])
            + prefix(expression[2]))


class Infeasible(Exception):
    """An expression whose value is too large to compute in a test."""


TERMS = 400
BITS = 2048
STEPS = 12


def measure(expression):
    """Bounds on the value of an arithmetic expression: the exact value where
    it has no omega, a bound on its natural last term, on the number of its
    terms and on the binary digits of its coefficients.  The bounds only
    decide which expressions become cases; no verdict comes from them."""
    kind = expression[0]
    if kind == "n":
        value = expression[1]
        return value, value, 1, value.bit_length()
    if kind == "w":
        return None, 0, 2, 1
    left_exact, left_last, left_terms, left_bits = measure(expression[1])
    right_exact, right_last, right_terms, right_bits = measure(expression[2])
    exact = None
    if left_exact is not None and right_exact is not None:
        if kind == "+":
            exact = left_exact + right_exact
        elif kind == "*":
            exact = left_exact * right_exact
        else:
            if left_exact >= 2 and (
                    right_exact > 256
                    or left_exact.bit_length() * right_exact > BITS):
                raise Infeasible
            exact = left_exact ** right_exact
        if exact.bit_length() > BITS:
            raise Infeasible
        return exact, exact, 1, exact.bit_length()
    if kind == "+":
        last = left_last + right_last
        terms = left_terms + right_terms
        bits = max(left_bits, right_bits) + 1
    elif kind == "*":
        last = max(left_last, left_last * right_last)
        terms = 2 * (left_terms + right_terms)
        bits = left_bits + right_bits + 1
    elif left_exact is None and left_last == 0:
        # A base without a natural last term: its power has the terms of the
        # base, with exponents multiplied by the exponent of the power.
        last = 1
        terms = 2 * (left_terms + right_terms)
        bits = left_bits + right_bits + 4
    else:
        # A natural base to a power is one coefficient; any other base puts
        # its terms into the power once for each step of the exponent.
        if left_exact is None and right_last > STEPS:
            raise Infeasible
        if left_last.bit_length() * max(right_last, 1) > BITS:
            raise Infeasible
        last = max(1, left_last, left_last ** right_last)
        terms = (2 * left_terms * (right_last + 2) + 2 * right_terms
                 if left_exact is None else 2 * (left_terms + right_terms))
        bits = left_bits * max(right_last, 1) + left_bits + right_bits + 4
    if terms > TERMS or bits > BITS or last.bit_length() > BITS:
        raise Infeasible
    return exact, last, terms, bits


def feasible(build):
    """The first expression `build` makes whose value is small enough."""
    while True:
        expression = build()
        try:
            measure(expression)
        except Infeasible:
            continue
        return expression


def arithmetic(rng, depth):
    """An arithmetic expression over numerals."""
    if depth == 0 or rng.random() < 0.35:
        return ("n", numeral(rng))
    return (rng.choice(["+", "+", "*", "*", "^"]),
            arithmetic(rng, depth - 1), arithmetic(rng, depth - 1))


def ordinal_arithmetic(rng, depth):
    """An arithmetic expression over numerals and omega."""
    if depth == 0 or rng.random() < 0.3:
        return ("w",) if rng.random() < 0.45 else ("n", numeral(rng))
    return (rng.choice(["+", "+", "+", "*", "*", "^", "^"]),
            ordinal_arithmetic(rng, depth - 1),
            ordinal_arithmetic(rng, depth - 1))


def monomial(rng):
    """A power of omega times a number, mostly a small one."""
    w = ("w",)
    power = rng.choice([
        w, w, ("^", w, ("n", 2)), ("^", w, ("n", 3)), ("^", w, w),
        ("^", w, ("+", w, ("n", 1))), ("^", w, ("*", w, ("n", 2)))])
    roll = rng.random()
    if roll < 0.5:
        return power
    if roll < 0.9:
        return ("*", power, ("n", rng.randint(1, 5)))
    return ("*", power, ("n", long_number(rng)))


def polynomial(rng):
    """A sum of a few such powers and a number, in any order and nested
    either way, so that summands are absorbed, merged and kept."""
    parts = [monomial(rng) for _ in range(rng.randint(1, 3))]
    if rng.random() < 0.6:
        parts.append(("n", rng.randint(0, 4)))
    rng.shuffle(parts)
    result = parts[0]
    for part in parts[1:]:
        result = ("+", result, part) if rng.random() < 0.5 else (
            "+", part, result)
    return result


def structured_arithmetic(rng):
    """A sum, product or power of two such sums; a power mostly to a small
    number, where the terms of the base are repeated."""
    left = polynomial(rng)
    operator = rng.choice(["+", "*", "*", "^", "^", "^"])
    if operator == "^" and rng.random() < 0.6:
        return ("^", left, ("n", rng.randint(0, 6)))
    return (operator, left, polynomial(rng))


def long_arithmetic(rng):
    """Sums, products and powers of long numerals, alone and as the
    coefficient, the last term and the exponent of a notation."""
    w = ("w",)
    left = ("n", long_number(rng))
    right = ("n", long_number(rng))
    small = ("n", rng.randint(2, 7))
    return rng.choice([
        ("+", left, right), ("*", left, right), ("^", left, small),
        ("^", small, ("n", rng.randint(60, 130))),
        ("+", w, left), ("+", left, w), ("*", w, left), ("*", left, w),
        ("^", w, left), ("^", small, ("+", w, ("n", rng.randint(60, 70)))),
        ("+", ("*", w, left), ("*", w, right)),
        ("*", ("+", w, left), right),
        ("^", ("+", w, left), small),
        ("+", ("^", w, left), ("^", w, right)),
        ("*", ("^", w, left), ("^", w, right)),
        ("^", ("^", w, left), right),
    ])


def tower(omegas):
    """omega to the power of omega to the power of ..., in prefix form."""
    return [4] if omegas == 1 else [3, 4] + tower(omegas - 1)


def raised(rng, depth):
    """A level expression for a raise: parameters under successors and under
    maxima with a closed side, and now and then a maximum of two of them."""
    roll = rng.random()
    if depth == 0 or roll < 0.3:
        if rng.random() < 0.8:
            return [1, rng.randint(0, 2)]
        return [0] + code(constant(rng))
    if roll < 0.6:
        return [2] + raised(rng, depth - 1)
    if roll < 0.9:
        closed = [0] + code(constant(rng))
        inner = raised(rng, depth - 1)
        return [3] + (closed + inner if rng.random() < 0.5 else inner + closed)
    return [3] + raised(rng, depth - 1) + raised(rng, depth - 1)


def raised_counted(rng, depth):
    """A level expression for a raise whose successors are counted: a count
    of none, of one and of several, over parameters, over maxima with a
    closed side and over one another."""
    roll = rng.random()
    if depth == 0 or roll < 0.3:
        if rng.random() < 0.8:
            return [1, rng.randint(0, 2)]
        return [0] + code(constant(rng))
    if roll < 0.4:
        return [2] + raised_counted(rng, depth - 1)
    if roll < 0.7:
        count = rng.choice([0, 1, 2, 2, 3, 3, 5, 9, 30])
        return [5, count] + raised_counted(rng, depth - 1)
    if roll < 0.92:
        closed = [0] + code(constant(rng))
        inner = raised_counted(rng, depth - 1)
        return [3] + (closed + inner if rng.random() < 0.5 else inner + closed)
    return ([3] + raised_counted(rng, depth - 1)
            + raised_counted(rng, depth - 1))


def expression(rng, depth):
    roll = rng.random()
    if depth == 0 or roll < 0.25:
        if rng.random() < 0.5:
            return [0] + code(constant(rng))
        return [1, rng.randint(0, 2)]
    if roll < 0.6:
        return [2] + expression(rng, depth - 1)
    return [3] + expression(rng, depth - 1) + expression(rng, depth - 1)


def extended(rng):
    """A level that may lie above the Cantor normal forms: 0 and a notation
    code, or 1 and the number of a level above them."""
    roll = rng.random()
    if roll < 0.4:
        return [0] + code(constant(rng))
    if roll < 0.8:
        return [1, rng.randint(0, 4)]
    return [1, long_number(rng)]


def extended_nearby(rng, level):
    """A level close to `level`: the same, or the one before or after it
    above the Cantor normal forms."""
    if level[0] == 0 or rng.random() < 0.3:
        return list(level)
    return [1, max(0, level[1] + rng.choice([-1, 1]))]


def written(rng, depth):
    """A level expression over the levels above the Cantor normal forms:
    written constants, parameters and levels above them under successors,
    counted successors and maxima."""
    roll = rng.random()
    if depth == 0 or roll < 0.3:
        leaf = rng.random()
        if leaf < 0.4:
            return [1, rng.randint(0, 2)]
        if leaf < 0.75:
            return [0] + code(constant(rng))
        if leaf < 0.97:
            return [4, rng.randint(0, 3)]
        return [4, long_number(rng)]
    if roll < 0.45:
        return [2] + written(rng, depth - 1)
    if roll < 0.6:
        count = rng.randint(0, 40) if rng.random() < 0.9 else 200
        return [5, count] + written(rng, depth - 1)
    return [3] + written(rng, depth - 1) + written(rng, depth - 1)


def written_nearby(rng, level):
    """A level expression close to `level`: the same, one raised, or a
    maximum of it with a parameter, a level above the Cantor normal forms or
    a written constant."""
    roll = rng.random()
    if roll < 0.2:
        return list(level)
    if roll < 0.35:
        return [2] + list(level)
    if roll < 0.5:
        return [5, rng.randint(0, 5)] + list(level)
    if roll < 0.65:
        return [3] + list(level) + [4, rng.randint(0, 2)]
    if roll < 0.85:
        return [3, 1, rng.randint(0, 2)] + list(level)
    return [3] + list(level) + [0] + code(constant(rng))


def written_values(rng):
    """Three Cantor normal forms for the parameters 0, 1 and 2."""
    return " ; ".join(
        text(code(ZERO if rng.random() < 0.3 else constant(rng)))
        for _ in range(3))


OMEGA_TO_OMEGA = term(OMEGA, 1, ZERO)
TOWER = term(OMEGA_TO_OMEGA, 1, ZERO)

ANCHOR_CASES = [
    # 7 < omega, omega < omega + 1, omega * 2 < omega^2 < omega^omega.
    "N " + text(code(natural(7))) + " ; " + text(code(OMEGA)),
    "N " + text(code(OMEGA)) + " ; " + text(code(successor(OMEGA))),
    "N " + text(code(term(natural(1), 2, ZERO))) + " ; "
    + text(code(term(natural(2), 1, ZERO))),
    "N " + text(code(term(natural(2), 1, ZERO))) + " ; "
    + text(code(term(OMEGA, 1, ZERO))),
    # Numbers of forty digits: as natural numbers, as coefficients and as
    # exponents, one apart, and against a machine number.
    "N " + text(code(natural(1234567890123456789012345678901234567890)))
    + " ; " + text(code(natural(1234567890123456789012345678901234567891))),
    "N " + text(code(term(natural(1), 1234567890123456789012345678901234567890,
                          ZERO)))
    + " ; " + text(code(term(natural(1), 2 ** 64, natural(10 ** 27)))),
    "N " + text(code(term(natural(10 ** 40), 1, natural(2 ** 64 - 1))))
    + " ; " + text(code(term(natural(10 ** 40 + 1), 1, ZERO))),
    # Of two numbers of the same length the one that leads with the larger
    # digit is the larger, whatever their last digits are.
    "N " + text(code(natural(2 * 10 ** 27 + 1))) + " ; "
    + text(code(natural(10 ** 27 + 2))),
    "N " + text(code(term(natural(1), 10 ** 27 + 2, ZERO))) + " ; "
    + text(code(term(natural(1), 2 * 10 ** 27 + 1, ZERO))),
    # 1 + omega, written with increasing exponents, is not a normal form.
    "T " + text(code(term(ZERO, 1, OMEGA))),
    "T " + text(code(term(natural(10 ** 30), 1,
                          term(natural(10 ** 30 + 1), 1, ZERO)))),
    "T " + text(code(term(natural(10 ** 30 + 1), 1,
                          term(natural(10 ** 30), 10 ** 30, ZERO)))),
    # max(omega, p0) <= max(omega + 1, p0 + 1), and not the other way.
    "E 3 0 " + text(code(OMEGA)) + " 1 0 ; 3 2 0 " + text(code(OMEGA))
    + " 2 1 0",
    # omega is not below p0 + 3: at p0 = 0 the right side is 3.
    "E 0 " + text(code(OMEGA)) + " ; 2 2 2 1 0",
    # A natural constant under an offset is dropped; omega is not.
    "E 3 0 " + text(code(natural(2))) + " 2 2 1 0 ; 3 0 " + text(code(OMEGA))
    + " 2 1 0",
    # 1 + 2, 2 * 3 and 2^3; 0^0 is 1; 2^63 and a product that passes through
    # it.
    "A 1 0 1 0 2",
    "A 2 0 2 0 3",
    "A 3 0 2 0 3",
    "A 3 0 0 0 0",
    "A 3 0 2 0 63",
    "A 2 3 0 2 0 63 0 0",
    # Around the largest machine integer, 2^63 - 1, and the largest machine
    # number, 2^64 - 1.
    "A 1 0 9223372036854775806 0 1",
    "A 1 0 9223372036854775807 0 1",
    "A 1 0 18446744073709551615 0 1",
    "A 2 0 3037000499 0 3037000499",
    "A 2 0 3037000500 0 3037000500",
    "A 2 0 4294967296 0 4294967296",
    "A 3 0 2 0 62",
    "A 3 0 2 0 64",
    "A 3 0 3 0 39",
    "A 3 0 3 0 40",
    "A 3 0 9223372036854775807 0 1",
    "A 3 0 9223372036854775807 0 0",
    "A 3 0 9223372036854775807 0 2",
    # Long numbers: a sum that carries through every digit, a product and
    # a power.
    "A 1 0 999999999999999999999999999 0 1",
    "A 2 0 1234567890123456789012345678901234567890 0 "
    "1234567890123456789012345678901234567890",
    "A 3 0 7 0 100",
    "A 3 0 2 0 200",
    "A 3 0 1000000000 0 9",
    # Zero and one to a power beyond every loop.
    "A 3 0 1 0 9223372036854775807",
    "A 3 0 0 0 9223372036854775807",
    "A 3 0 1 0 1234567890123456789012345678901234567890",
    "A 3 0 0 0 1234567890123456789012345678901234567890",
    # Beyond the natural numbers the operations are not commutative:
    # 1 + omega, 2 * omega and 2^omega are omega; omega + 1, omega * 2 and
    # omega^2 are not.
    "A 1 0 1 4",
    "A 1 4 0 1",
    "A 2 0 2 4",
    "A 2 4 0 2",
    "A 3 0 2 4",
    "A 3 4 0 2",
    # (omega + 1) * (omega + 1) and (omega + 1)^3; 2^(omega * 2 + 3) is
    # omega^2 * 8; omega^5000 is one term.
    "A 2 1 4 0 1 1 4 0 1",
    "A 3 1 4 0 1 0 3",
    "A 3 0 2 1 2 4 0 2 0 3",
    "A 3 4 0 5000",
    # A summand absorbed by a later one: the largest machine integer, then
    # 1 + omega; and 2^63 + omega.
    "A 1 0 9223372036854775807 1 0 1 4",
    "A 1 1 0 9223372036854775807 0 1 4",
    "A 1 3 0 2 0 63 4",
    # Long coefficients, exponents and last terms: omega * 2^62, omega * 2^65
    # as 2^(omega + 65), omega to a long power, and a long number after
    # omega.
    "A 3 0 2 1 4 0 62",
    "A 3 0 2 1 4 0 65",
    "A 3 4 0 1234567890123456789012345678901234567890",
    "A 1 4 0 1234567890123456789012345678901234567890",
    "A 2 4 0 1234567890123456789012345678901234567890",
    # Long notations: (omega + 1)^n has n + 1 terms, n of them with an
    # exponent of one term.
    "A 3 1 4 0 1 0 2047",
    "A 3 1 4 0 1 0 2048",
    "A 3 1 4 0 1 0 5000",
    "A 2 3 1 4 0 1 0 100 3 1 4 0 1 0 100",
    "A 3 3 1 4 0 1 0 20 0 20",
    # Deep notations: towers of 31, 32, 33 and 100 omegas.
    "A " + text(tower(31)),
    "A " + text(tower(32)),
    "A " + text(tower(33)),
    "A " + text(tower(100)),
    # Raises.  Under a successor a limit bound stays: p0 + 1 reaches omega
    # only at p0 = omega.  A closed side that does not reach the bound leaves
    # it to the parameter.  A maximum of two parameters has no least raise.
    "R 2 1 0 ; " + text(code(OMEGA)) + " ; 0 ; 0 ; 0",
    "R 3 0 " + text(code(natural(4))) + " 1 1 ; " + text(code(natural(6)))
    + " ; 0 ; 0 ; 0",
    "R 3 1 0 1 1 ; " + text(code(natural(1))) + " ; 0 ; 0 ; 0",
    # Under a successor a long bound steps back through every digit.
    "R 2 1 0 ; " + text(code(natural(10 ** 27))) + " ; 0 ; 0 ; 0",
    "R 2 2 1 1 ; " + text(code(term(natural(1), 10 ** 30, natural(10 ** 18))))
    + " ; 0 ; 0 ; 0",
    # The levels above every Cantor normal form: a natural number, omega and
    # a tower are below the 0-th; the 0-th is below the 1-st and is a limit;
    # under the 1-st is the 0-th; long numbers of them.
    "U 0 " + text(code(natural(7))) + " ; 1 0",
    "U 0 " + text(code(OMEGA)) + " ; 1 0",
    "U 0 " + text(code(term(term(OMEGA, 1, ZERO), 1, ZERO))) + " ; 1 0",
    "U 1 0 ; 0 " + text(code(term(term(OMEGA, 1, ZERO), 1, ZERO))),
    "U 1 0 ; 1 0",
    "U 1 0 ; 1 1",
    "U 1 1 ; 1 0",
    "U 1 1 ; 1 1",
    "U 1 2 ; 1 1",
    "U 1 1234567890123456789012345678901234567890 ; "
    "1 1234567890123456789012345678901234567891",
    "U 1 1000000000000000000000000000 ; 1 999999999999999999999999999",
    "U 0 0 ; 1 0",
    "U 1 0 ; 0 0",
    # A level parameter stands for a Cantor normal form.  Raised by any
    # count it is below the first level above them, and that level is below
    # no parameter, whatever the parameter holds.
    "W 2 1 0 ; 4 0 ; 0 ; 0 ; 0",
    "W 4 0 ; 2 1 0 ; " + text(code(TOWER)) + " ; 0 ; 0",
    "W 5 40 1 0 ; 4 0 ; " + text(code(TOWER)) + " ; 0 ; 0",
    "W 1 2 ; 4 1 ; 0 ; 0 ; " + text(code(OMEGA)),
    "W 4 1 ; 5 200 1 2 ; 0 ; 0 ; " + text(code(OMEGA)),
    # Under a constant above the Cantor normal forms a parameter is
    # absorbed: max(above 0, p1 + 7) is above 0, and its successor above 1.
    "W 3 4 0 5 7 1 1 ; 4 0 ; 0 ; " + text(code(OMEGA)) + " ; 0",
    "W 2 3 4 0 5 7 1 1 ; 4 1 ; 0 ; " + text(code(OMEGA)) + " ; 0",
    "W 3 4 2 1 0 ; 3 1 1 4 2 ; 0 ; 0 ; 0",
    "W 3 4 0 1 0 ; 3 4 1 1 0 ; 0 ; 0 ; 0",
    # A parameter and a Cantor normal form are ordered neither way: the
    # parameter may stand for a larger form and for zero.  Beside the
    # parameter the form is below.
    "W 1 0 ; 0 " + text(code(TOWER)) + " ; " + text(code(successor(TOWER)))
    + " ; 0 ; 0",
    "W 0 " + text(code(TOWER)) + " ; 1 0 ; 0 ; 0 ; 0",
    "W 0 " + text(code(TOWER)) + " ; 3 1 0 0 " + text(code(TOWER))
    + " ; 0 ; 0 ; 0",
    "W 3 1 0 0 " + text(code(TOWER)) + " ; 0 " + text(code(TOWER))
    + " ; " + text(code(successor(TOWER))) + " ; 0 ; 0",
    # A counted successor is that many successors.
    "W 5 3 1 0 ; 2 2 2 1 0 ; " + text(code(OMEGA)) + " ; 0 ; 0",
    "W 5 0 1 1 ; 1 1 ; 0 ; " + text(code(natural(7))) + " ; 0",
    "W 5 2 4 0 ; 4 2 ; 0 ; 0 ; 0",
    "W 5 200 0 " + text(code(OMEGA)) + " ; 0 "
    + text(code(term(natural(1), 1, natural(200)))) + " ; 0 ; 0 ; 0",
    "W 5 4 3 0 " + text(code(natural(3))) + " 1 0 ; 3 0 "
    + text(code(natural(7))) + " 5 4 1 0 ; " + text(code(natural(9)))
    + " ; 0 ; 0",
    # Raises through a counted successor: under a count of two a bound of
    # five steps back to three, omega + 1 to omega, and one to zero; a count
    # of none passes the bound on; and counts add.
    "R 5 2 1 0 ; " + text(code(natural(5))) + " ; 0 ; 0 ; 0",
    "R 5 2 1 0 ; " + text(code(successor(OMEGA))) + " ; 0 ; 0 ; 0",
    "R 5 2 1 0 ; " + text(code(natural(1))) + " ; 0 ; 0 ; 0",
    "R 5 0 1 1 ; " + text(code(natural(5))) + " ; 0 ; 0 ; 0",
    "R 5 2 2 5 3 1 2 ; " + text(code(natural(40))) + " ; 0 ; 0 ; 0",
    "R 5 30 3 0 " + text(code(natural(4))) + " 1 1 ; "
    + text(code(natural(36))) + " ; 0 ; 0 ; 0",
    "R 5 30 1 0 ; " + text(code(term(natural(1), 10 ** 30, natural(10 ** 18))))
    + " ; 0 ; 0 ; 0",
]

LEAN_ANCHORS = """\
/-! ## Cases decided by the kernel -/

example : Cnf.cmp (Cnf.ofNat 7) Cnf.omega = .lt := by decide

example : Level.omega < Level.succ Level.omega := by decide

example : Cnf.cmp (.oadd (Cnf.ofNat 1) 1 .zero) (.oadd (Cnf.ofNat 2) 0 .zero) = .lt := by
  decide

example : Cnf.cmp (.oadd (Cnf.ofNat 2) 0 .zero) (.oadd Cnf.omega 0 .zero) = .lt := by decide

example : ¬ Cnf.NF (.oadd .zero 0 Cnf.omega) := by decide

example : LevelNF.CanonicalLe
    (LevelNF.normalize (.max (.const Level.omega) (.param 0) : LevelExpr Level))
    (LevelNF.normalize (.max (.succ (.const Level.omega)) (.succ (.param 0)))) := by
  decide

example : ¬ LevelNF.CanonicalLe
    (LevelNF.normalize (.max (.succ (.const Level.omega)) (.succ (.param 0)) :
      LevelExpr Level))
    (LevelNF.normalize (.max (.const Level.omega) (.param 0))) := by
  decide

example : ¬ LevelNF.CanonicalLe
    (LevelNF.normalize (.const Level.omega : LevelExpr Level))
    (LevelNF.normalize (.succ (.succ (.succ (.param 0))))) := by
  decide

example : LevelNF.normalize
      (.max (.const (Level.ofNat 2)) (.succ (.succ (.param 0))) : LevelExpr Level) =
    ⟨Level.zero, [(0, 2)]⟩ := by
  decide

example : LevelNF.normalize
      (.max (.const Level.omega) (.succ (.param 0)) : LevelExpr Level) =
    ⟨Level.omega, [(0, 1)]⟩ := by
  decide

example : (LeastInstance.raise LeastInstance.levelBounds (fun i => decide (i < 3))
    (.succ (.param 0)) Level.omega fun _ => Level.zero).map (fun held => held 0) =
      some Level.omega := by
  decide

example : (LeastInstance.raise LeastInstance.levelBounds (fun i => decide (i < 3))
    (.max (.param 0) (.param 1)) (Level.ofNat 1) fun _ => Level.zero).isNone := by
  decide

example : Level.pred? (Level.succ Level.omega) = some Level.omega := by decide

example : Level.pred? Level.omega = none ∧ Level.pred? Level.zero = none := by decide

example : PredLevelOrder.underSucc (Level.ofNat 3) = Level.ofNat 2 ∧
    PredLevelOrder.underSucc Level.omega = Level.omega := by
  decide

example : (Arith.sum (.numeral 1) (.numeral 2)).value = Cnf.ofNat 3 := by decide

example : (Arith.sum (.numeral 1) .omega).value = Cnf.omega ∧
    (Arith.sum .omega (.numeral 1)).value = Cnf.succ Cnf.omega := by
  decide

example : (Arith.power (.numeral 2) .omega).value = Cnf.omega ∧
    (Arith.power .omega (.numeral 2)).value = .oadd (Cnf.ofNat 2) 0 .zero := by
  decide

example : (Above.below Level.omega : Above Level) < Above.above 0 ∧
    ¬ (Above.above 0 : Above Level) < Above.below Level.omega ∧
    (Above.above 0 : Above Level) < Above.above 1 ∧
    ¬ (Above.above 1 : Above Level) < Above.above 1 := by
  decide

example : LevelOrder.succ (Above.above 0 : Above Level) = Above.above 1 ∧
    PredLevelOrder.underSucc (Above.above 0 : Above Level) = Above.above 0 ∧
    PredLevelOrder.underSucc (Above.above 2 : Above Level) = Above.above 1 ∧
    max (Above.below Level.omega) (Above.above 0) = Above.above 0 ∧
    LevelOrder.succ (Above.below Level.omega) = Above.below (Level.succ Level.omega) := by
  decide

example : LevelOrder.succ (Above.above 0 : Above Level) ≠ Above.above 0 ∧
    PredLevelOrder.underSucc (Above.above 1 : Above Level) ≠ Above.above 1 ∧
    PredLevelOrder.pred? (Above.above 0 : Above Level) = none := by
  decide

example : written.Positive := fun _ c known => by
  cases known
  exact Above.below_lt_above _ 0

example : ∀ ν : Nat → Level, written.Valid fun i => Above.below (ν i) := fun ν i c known => by
  cases known
  exact Above.below_lt_above (ν i) 0

example : leWritten (LevelNF.normalize (.succ (.param 0)))
    (LevelNF.normalize (.const (.above 0))) = true := by
  decide

example : leWritten (LevelNF.normalize (.const (.above 0)))
    (LevelNF.normalize (.succ (.param 0))) = false := by
  decide

example : leWritten (LevelNF.normalize (.param 0))
    (LevelNF.normalize (.const (.below Level.omega))) = false ∧
    leWritten (LevelNF.normalize (.const (.below Level.omega)))
      (LevelNF.normalize (.param 0)) = false := by
  decide

example : (canonical (.max (.const (.above 0)) (.succ (.param 3)))) =
    some ⟨Above.above 0, []⟩ := by
  decide
"""

LEAN_PRELUDE = """\
import Mettapedia.TypeTheory.UniverseLevel.Above
import Mettapedia.TypeTheory.UniverseLevel.NotationCode
import Mettapedia.TypeTheory.UniverseLevel.NotationArithmetic
import Mettapedia.TypeTheory.UniverseLevel.LeastInstance

/-!
# Verdicts of the level definitions on generated cases

Generated by tests/support/generate_prime_level_differential.py of CeTTa.
Each case is read from its exchange code, the verdict is computed from the
definitions of `Mettapedia.TypeTheory.UniverseLevel`, and one line is printed
per case.  The C level library prints the same lines for the same cases.
-/

set_option autoImplicit false

open Mettapedia.TypeTheory.UniverseLevel

namespace PrimeLevelDifferential

/-- The numbers of one field; any other word, such as the case letter, is skipped. -/
def numbers (field : String) : List Nat :=
  (field.splitOn " ").filterMap String.toNat?

def showCnf (x : Cnf) : String :=
  " ".intercalate ((Cnf.code x).map toString)

def showNF (nf : LevelNF Level) : String :=
  showCnf nf.constPart.1 ++ "/" ++
    ",".intercalate (nf.params.map fun ik => toString ik.1 ++ "+" ++ toString ik.2)

def bit (b : Bool) : String := if b then "1" else "0"

def showOrdering : Ordering → String
  | .lt => "lt"
  | .eq => "eq"
  | .gt => "gt"

/-- Read one level expression from the front of a list of numbers: `0` and a notation code for
a constant, `1` and an index for a parameter, `2` for a successor, `3` for a maximum, and `5`
and a count for that many successors. -/
partial def readExpr : List Nat → Option (LevelExpr Level × List Nat)
  | 0 :: rest =>
    match Cnf.readFuel rest.length rest with
    | some (x, rest') =>
      if normal : x.NF then some (.const (⟨x, normal⟩ : Level), rest') else none
    | none => none
  | 1 :: i :: rest => some (.param i, rest)
  | 2 :: rest =>
    match readExpr rest with
    | some (e, rest') => some (.succ e, rest')
    | none => none
  | 3 :: rest =>
    match readExpr rest with
    | some (a, rest') =>
      match readExpr rest' with
      | some (b, rest'') => some (.max a b, rest'')
      | none => none
    | none => none
  | 5 :: k :: rest =>
    match readExpr rest with
    | some (e, rest') => some (Nat.iterate LevelExpr.succ k e, rest')
    | none => none
  | _ => none

def exprOfCode (l : List Nat) : Option (LevelExpr Level) :=
  match readExpr l with
  | some (e, []) => some e
  | _ => none

/-- An arithmetic expression over numerals and omega. -/
inductive Arith where
  | numeral : Nat → Arith
  | omega : Arith
  | sum : Arith → Arith → Arith
  | product : Arith → Arith → Arith
  | power : Arith → Arith → Arith

/-- Read one arithmetic expression from the front of a list of numbers. -/
partial def readArith : List Nat → Option (Arith × List Nat)
  | 0 :: n :: rest => some (.numeral n, rest)
  | 4 :: rest => some (.omega, rest)
  | op :: rest =>
    if op = 1 ∨ op = 2 ∨ op = 3 then
      match readArith rest with
      | some (a, rest') =>
        match readArith rest' with
        | some (b, rest'') =>
          some ((if op = 1 then Arith.sum a b else if op = 2 then Arith.product a b
            else Arith.power a b), rest'')
        | none => none
      | none => none
    else none
  | _ => none

/-- The notation an expression denotes; the sum, product and power are those of the
ordinals. -/
def Arith.value : Arith → Cnf
  | .numeral n => Cnf.ofNat n
  | .omega => Cnf.omega
  | .sum a b => Cnf.add a.value b.value
  | .product a b => Cnf.mul a.value b.value
  | .power a b => Cnf.pow a.value b.value

/-- The predecessor in a level order followed by the natural numbers: that of the order on
its own levels, none at the first new level, which is a limit, and the new level before among
the others. -/
def abovePred? {L : Type} [PredLevelOrder L] : Above L → Option (Above L)
  | .below a => (PredLevelOrder.pred? a).map .below
  | .above 0 => none
  | .above (n + 1) => some (.above n)

/-- **A level order with predecessors, followed by the natural numbers, has predecessors.** -/
instance {L : Type} [PredLevelOrder L] : PredLevelOrder (Above L) where
  pred? := abovePred?
  pred?_eq_some := by
    intro l p
    cases l with
    | below a =>
      cases p with
      | below b =>
        constructor
        · intro h
          have h' : (PredLevelOrder.pred? a).map Above.below = some (Above.below b) := h
          cases hp : PredLevelOrder.pred? a with
          | none => rw [hp] at h'; exact nomatch h'
          | some c =>
            rw [hp] at h'
            have hc : c = b := Above.below.inj (Option.some.inj h')
            subst hc
            exact congrArg Above.below (PredLevelOrder.pred?_eq_some.mp hp)
        · intro h
          have h' : Above.below a = Above.below (LevelOrder.succ b) := h
          show (PredLevelOrder.pred? a).map Above.below = some (Above.below b)
          rw [PredLevelOrder.pred?_eq_some.mpr (Above.below.inj h')]
          rfl
      | above n =>
        constructor
        · intro h
          have h' : (PredLevelOrder.pred? a).map Above.below = some (Above.above n) := h
          cases hp : PredLevelOrder.pred? a with
          | none => rw [hp] at h'; exact nomatch h'
          | some c => rw [hp] at h'; exact nomatch (Option.some.inj h')
        · intro h
          have h' : Above.below a = Above.above (n + 1) := h
          exact nomatch h'
    | above m =>
      cases m with
      | zero =>
        constructor
        · intro h
          have h' : (none : Option (Above L)) = some p := h
          exact nomatch h'
        · intro h
          cases p with
          | below b =>
            have h' : Above.above 0 = Above.below (LevelOrder.succ b) := h
            exact nomatch h'
          | above n =>
            have h' : Above.above 0 = Above.above (n + 1) := h
            exact absurd (Above.above.inj h') (Nat.succ_ne_zero n).symm
      | succ m =>
        cases p with
        | below b =>
          constructor
          · intro h
            have h' : some (Above.above m) = some (Above.below b) := h
            exact nomatch (Option.some.inj h')
          · intro h
            have h' : Above.above (m + 1) = Above.below (LevelOrder.succ b) := h
            exact nomatch h'
        | above n =>
          constructor
          · intro h
            have h' : some (Above.above m) = some (Above.above n) := h
            have hm : m = n := Above.above.inj (Option.some.inj h')
            subst hm
            rfl
          · intro h
            have h' : Above.above (m + 1) = Above.above (n + 1) := h
            have hm : m = n := Nat.succ.inj (Above.above.inj h')
            subst hm
            rfl

/-- The exchange code of a level that may lie above the Cantor normal forms: `0` and the code
of a notation, or `1` and the number. The `0`-th of the levels above is the level of the sort of
all sets and the `1`-st the level of its sort. -/
def showAbove : Above Level → String
  | .below x => "0 " ++ showCnf x.1
  | .above n => "1 " ++ toString n

/-- The level a list of numbers is the code of, if any. -/
def decodeAbove : List Nat → Option (Above Level)
  | 0 :: rest => (Level.decode rest).map .below
  | [1, n] => some (.above n)
  | _ => none

/-- **The bounds under which every level parameter stands for a Cantor normal form**: each
parameter is below the first level above them all. These are the bounds that
`TowerInterpretation.AmbientSets.writtenBounds` states. -/
def written : LevelBounds (Above Level) := fun _ => some (Above.above 0)

/-- Read one level expression over the levels that may lie above the Cantor normal forms from
the front of a list of numbers: `0` and a notation code for a written constant, `1` and an
index for a parameter, `2` for a successor, `3` for a maximum, `4` and a number for a level
above the Cantor normal forms, and `5` and a count for that many successors. -/
partial def readAboveExpr : List Nat → Option (LevelExpr (Above Level) × List Nat)
  | 0 :: rest =>
    match Cnf.readFuel rest.length rest with
    | some (x, rest') =>
      if normal : x.NF then some (.const (.below (⟨x, normal⟩ : Level)), rest') else none
    | none => none
  | 1 :: i :: rest => some (.param i, rest)
  | 2 :: rest =>
    match readAboveExpr rest with
    | some (e, rest') => some (.succ e, rest')
    | none => none
  | 3 :: rest =>
    match readAboveExpr rest with
    | some (a, rest') =>
      match readAboveExpr rest' with
      | some (b, rest'') => some (.max a b, rest'')
      | none => none
    | none => none
  | 4 :: n :: rest => some (.const (.above n), rest)
  | 5 :: k :: rest =>
    match readAboveExpr rest with
    | some (e, rest') => some (Nat.iterate LevelExpr.succ k e, rest')
    | none => none
  | _ => none

def aboveExprOfCode (l : List Nat) : Option (LevelExpr (Above Level)) :=
  match readAboveExpr l with
  | some (e, []) => some e
  | _ => none

def showAboveNF (nf : LevelNF (Above Level)) : String :=
  showAbove nf.constPart ++ "/" ++
    ",".intercalate (nf.params.map fun ik => toString ik.1 ++ "+" ++ toString ik.2)

/-- The order of two canonical forms under every valuation of the parameters in the Cantor
normal forms: the criterion that `LevelBounds.canonicalLeUnder_iff_eval_le` proves exact. -/
def leWritten (a b : LevelNF (Above Level)) : Bool :=
  decide (LevelBounds.CanonicalLeUnder written a b)

/-- A canonical form without its parameter atoms where its constant is above the Cantor
normal forms: there the level library keeps none. -/
def absorb (nf : LevelNF (Above Level)) : LevelNF (Above Level) :=
  match nf.constPart with
  | .above _ => ⟨nf.constPart, []⟩
  | .below _ => nf

/-- The canonical form of an expression as the level library keeps it; `none` if dropping the
parameter atoms changed the level under the bounds. -/
def canonical (e : LevelExpr (Above Level)) : Option (LevelNF (Above Level)) :=
  let nf := LevelNF.normalize e
  if leWritten (absorb nf) nf && leWritten nf (absorb nf) then some (absorb nf) else none

/-- The verdict line of one case. -/
def verdict (line : String) : String :=
  let fields := (line.splitOn ";").map numbers
  match line.toList.head?, fields with
  | some 'N', [a, b] =>
    match Level.decode a, Level.decode b with
    | some x, some y =>
      "N cmp=" ++ showOrdering (compare x y) ++ " eq=" ++ bit (decide (x = y)) ++
        " le=" ++ bit (decide (x ≤ y)) ++ " succ=" ++ showCnf (LevelOrder.succ x).1 ++
        " max=" ++ showCnf (max x y).1 ++ " under=" ++ showCnf (PredLevelOrder.underSucc x).1 ++
        " read-max=" ++ showCnf (max x y).1
    | _, _ => "N not a notation in normal form"
  | some 'U', [a, b] =>
    match decodeAbove a, decodeAbove b with
    | some x, some y =>
      let order := if x < y then Ordering.lt else if x = y then Ordering.eq else Ordering.gt
      "U cmp=" ++ showOrdering order ++ " eq=" ++ bit (decide (x = y)) ++
        " le=" ++ bit (decide (x ≤ y)) ++ " succ=" ++ showAbove (LevelOrder.succ x) ++
        " max=" ++ showAbove (max x y) ++
        " under=" ++ showAbove (PredLevelOrder.underSucc x) ++
        " member=" ++ bit (decide (x < y))
    | _, _ => "U not a level"
  | some 'W', [a, b, c₀, c₁, c₂] =>
    match aboveExprOfCode a, aboveExprOfCode b, Level.decode c₀, Level.decode c₁,
      Level.decode c₂ with
    | some e₁, some e₂, some v₀, some v₁, some v₂ =>
      let ν : Nat → Above Level := fun i =>
        match i with
        | 0 => .below v₀
        | 1 => .below v₁
        | 2 => .below v₂
        | _ => .below Level.zero
      match canonical e₁, canonical e₂, canonical (.succ e₁), canonical (.max e₁ e₂) with
      | some nf₁, some nf₂, some raised, some larger =>
        let le := leWritten nf₁ nf₂
        let ge := leWritten nf₂ nf₁
        "W a=" ++ showAboveNF nf₁ ++ " b=" ++ showAboveNF nf₂ ++ " eq=" ++ bit (le && ge) ++
          " le=" ++ bit le ++ " ge=" ++ bit ge ++ " succ=" ++ showAboveNF raised ++
          " max=" ++ showAboveNF larger ++ " at=" ++ showAbove (LevelExpr.eval ν e₁) ++
          " | " ++ showAbove (LevelExpr.eval ν e₂) ++ " member=" ++ bit le
      | _, _, _, _ => "W dropping parameters changes a level"
    | _, _, _, _, _ => "W not a level expression"
  | some 'A', [a] =>
    match readArith a with
    | some (e, []) => "A " ++ showCnf e.value
    | _ => "A not an arithmetic expression"
  | some 'T', [a] =>
    match Cnf.decode a with
    | some t => "T nf=" ++ bit (decide t.NF) ++ " core=" ++ bit (decide t.NF)
    | none => "T not a tree"
  | some 'E', [a, b] =>
    match exprOfCode a, exprOfCode b with
    | some e₁, some e₂ =>
      let nf₁ := LevelNF.normalize e₁
      let nf₂ := LevelNF.normalize e₂
      "E a=" ++ showNF nf₁ ++ " b=" ++ showNF nf₂ ++ " eq=" ++ bit (decide (nf₁ = nf₂)) ++
        " le=" ++ bit (decide (LevelNF.CanonicalLe nf₁ nf₂)) ++
        " ge=" ++ bit (decide (LevelNF.CanonicalLe nf₂ nf₁)) ++
        " succ=" ++ showNF (LevelNF.normalize (.succ e₁)) ++
        " max=" ++ showNF (LevelNF.normalize (.max e₁ e₂))
    | _, _ => "E not a level expression"
  | some 'S', [a, s₀, s₁, s₂] =>
    match exprOfCode a, exprOfCode s₀, exprOfCode s₁, exprOfCode s₂ with
    | some e, some t₀, some t₁, some t₂ =>
      let σ : Nat → LevelExpr Level := fun i =>
        match i with
        | 0 => t₀
        | 1 => t₁
        | 2 => t₂
        | i => .param i
      "S " ++ showNF (LevelNF.normalize (LevelExpr.subst σ e))
    | _, _, _, _ => "S not a level expression"
  | some 'V', [a, c₀, c₁, c₂] =>
    match exprOfCode a, Level.decode c₀, Level.decode c₁, Level.decode c₂ with
    | some e, some v₀, some v₁, some v₂ =>
      let v : Nat → Level := fun i =>
        match i with
        | 0 => v₀
        | 1 => v₁
        | 2 => v₂
        | _ => Level.zero
      "V " ++ showCnf (LevelExpr.eval v e).1
    | _, _, _, _ => "V not a level expression"
  | some 'R', [a, b, s₀, s₁, s₂] =>
    match exprOfCode a, Level.decode b, Level.decode s₀, Level.decode s₁, Level.decode s₂ with
    | some e, some c, some v₀, some v₁, some v₂ =>
      let σ : Nat → Level := fun i =>
        match i with
        | 0 => v₀
        | 1 => v₁
        | 2 => v₂
        | _ => Level.zero
      match LeastInstance.raise LeastInstance.levelBounds (fun i => decide (i < 3)) e c σ with
      | some τ =>
        "R " ++ showCnf (τ 0).1 ++ " | " ++ showCnf (τ 1).1 ++ " | " ++ showCnf (τ 2).1
      | none => "R outside"
    | _, _, _, _, _ => "R not a raise"
  | _, _ => "unknown case"

end PrimeLevelDifferential

open PrimeLevelDifferential

"""

LEAN_RUN = """\

#eval show IO Unit from do
  for line in generatedCases.splitOn "\\n" do
    if line ≠ "" then IO.println (verdict line)
"""


def cases(seed, scale):
    rng = random.Random(seed)
    lines = list(ANCHOR_CASES)
    for _ in range(2000 * scale):
        left = normal(rng, rng.randint(1, 4))
        right = nearby(rng, left) if rng.random() < 0.5 else normal(
            rng, rng.randint(1, 4))
        assert is_normal(left) and is_normal(right)
        lines.append("N " + text(code(left)) + " ; " + text(code(right)))
    for _ in range(600 * scale):
        lines.append("T " + text(code(tree(rng, 4))))
    for _ in range(2000 * scale):
        left = expression(rng, 4)
        right = left if rng.random() < 0.1 else expression(rng, 4)
        lines.append("E " + text(left) + " ; " + text(right))
    for _ in range(400 * scale):
        lines.append("S " + " ; ".join(
            text(expression(rng, depth)) for depth in (4, 2, 2, 2)))
    for _ in range(400 * scale):
        lines.append(
            "V " + text(expression(rng, 4)) + " ; " + " ; ".join(
                text(code(constant(rng))) for _ in range(3)))
    for _ in range(600 * scale):
        lines.append("A " + text(prefix(feasible(
            lambda: arithmetic(rng, 3)))))
    for _ in range(1500 * scale):
        held = [ZERO if rng.random() < 0.5 else constant(rng) for _ in range(3)]
        lines.append(
            "R " + text(raised(rng, rng.randint(0, 4))) + " ; "
            + text(code(constant(rng))) + " ; "
            + " ; ".join(text(code(notation)) for notation in held))
    for _ in range(1500 * scale):
        lines.append("A " + text(prefix(feasible(
            lambda: ordinal_arithmetic(rng, 3)))))
    for _ in range(1500 * scale):
        lines.append("A " + text(prefix(feasible(
            lambda: structured_arithmetic(rng)))))
    for _ in range(600 * scale):
        lines.append("A " + text(prefix(feasible(
            lambda: long_arithmetic(rng)))))
    for _ in range(800 * scale):
        left = extended(rng)
        right = extended_nearby(rng, left) if rng.random() < 0.5 else (
            extended(rng))
        lines.append("U " + text(left) + " ; " + text(right))
    for _ in range(2400 * scale):
        left = written(rng, rng.randint(0, 4))
        right = written_nearby(rng, left) if rng.random() < 0.5 else written(
            rng, rng.randint(0, 4))
        lines.append("W " + text(left) + " ; " + text(right) + " ; "
                     + written_values(rng))
    for _ in range(1200 * scale):
        held = [ZERO if rng.random() < 0.5 else constant(rng) for _ in range(3)]
        lines.append(
            "R " + text(raised_counted(rng, rng.randint(1, 4))) + " ; "
            + text(code(constant(rng))) + " ; "
            + " ; ".join(text(code(notation)) for notation in held))
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", required=True, type=Path,
                        help="directory for cases.txt and the Lean file")
    parser.add_argument("--seed", type=int, default=20261002)
    parser.add_argument("--scale", type=int, default=1,
                        help="multiplies the number of generated cases")
    args = parser.parse_args()
    lines = cases(args.seed, args.scale)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "cases.txt").write_text("\n".join(lines) + "\n")
    (args.output / "PrimeLevelDifferential.lean").write_text(
        LEAN_PRELUDE + LEAN_ANCHORS
        + "\n/-! ## The generated cases -/\n\ndef generatedCases : String := \""
        + "\\n".join(lines) + "\"\n" + LEAN_RUN)
    print(f"(PrimeLevelDifferentialCases {len(lines)} seed={args.seed})")


if __name__ == "__main__":
    main()
