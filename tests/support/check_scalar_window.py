"""Scalar term order and Int64 windows against an exact independent oracle."""
import argparse
from fractions import Fraction
import itertools
import json
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S, BigInt, Rational, FloatBits, Var, encode

MINIMUM, LIMIT = -(1 << 63), 1 << 63


def floating(value):
    return struct.unpack("<d", struct.pack("<Q", value.bits))[0]


def ordinal_cut(value, strict):
    """First integer >/>= a bound, using ceil/floor rather than binary search."""
    if isinstance(value, FloatBits):
        number = floating(value)
        if math.isnan(number) or number == -math.inf:
            first = MINIMUM
        elif number == math.inf:
            first = LIMIT
        else:
            # Equal-valued floats precede exact numbers in the established
            # term order, so both integer comparisons exclude equality.
            first = math.ceil(Fraction.from_float(number))
    elif type(value) is int or isinstance(value, (BigInt, Rational)):
        number = Fraction(value if type(value) is int else
                          value.decimal if isinstance(value, BigInt) else value.spelling)
        first = math.floor(number) + 1 if strict else math.ceil(number)
    else:
        # Strings and atom-like scalars sort after numbers.
        first = LIMIT
    return min(max(first, MINIMUM), LIMIT) - MINIMUM


def expected_window(lower, upper):
    start = 0 if lower == S("NoBound") else ordinal_cut(lower[1], lower[0] == S("Exclusive"))
    end = 1 << 64 if upper == S("NoBound") else ordinal_cut(upper[1], upper[0] != S("Exclusive"))
    if start >= end:
        return S("StoreIntegerWindowEmptyV1")
    return E("StoreIntegerWindowV1", start.to_bytes(8, "big"),
             S("NoBound") if end == 1 << 64 else end.to_bytes(8, "big"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    numbers = [MINIMUM, MINIMUM + 1, -(1 << 53) - 1, -1, 0, 1, (1 << 53),
               (1 << 53) + 1, LIMIT - 1, BigInt(str(-(1 << 100))), BigInt(str(1 << 100)),
               Rational("-1/3"), Rational("0/1"), Rational("5/2"),
               *map(FloatBits.of, [-math.inf, -float(1 << 63), -1.5, -0.0, 0.0, 0.5,
                                  float(1 << 53), float(1 << 63), math.inf]),
               FloatBits(0x7ff8000000000001), FloatBits(0xfff8000000000002)]
    scalars = numbers + ["", "a\x00b", "ž", S("a"), S("false"), S("true"), False, True]
    worker = Worker(args.worker.resolve(), ROOT / "tests/support/scalar_window_fixture.metta",
                    "scalar-test:run", language="petta")
    orders = windows = memberships = 0
    try:
        for left, right in itertools.product(scalars, repeat=2):
            answer = worker.call(E("Order", left, right))
            reference = worker.call(E("Reference", left, right))
            assert answer == E("StoreScalarOrderV1", reference), (left, right, answer, reference)
            orders += 1
        bounds = [S("NoBound"), *(E(tag, value) for value in scalars for tag in ["Inclusive", "Exclusive"])]
        for lower, upper in itertools.product(bounds, repeat=2):
            actual = worker.call(E("Window", lower, upper))
            assert encode(actual) == encode(expected_window(lower, upper)), (lower, upper, actual, expected_window(lower, upper))
            windows += 1
        for value in scalars:
            for lower, upper in [(E("Inclusive", value), E("Inclusive", value)),
                                 (E("Exclusive", value), E("Inclusive", value)),
                                 (S("NoBound"), S("NoBound"))]:
                answer = worker.call(E("Within", value, lower, upper))
                expected = S("StoreScalarOutsideV1") if lower != S("NoBound") and lower[0] == S("Exclusive") else S("StoreScalarWithinV1")
                assert answer == expected, (value, lower, upper, answer)
                memberships += 1
        for unsupported in [(), E("compound", 1), Var(0)]:
            assert worker.call(E("Within", unsupported, S("NoBound"), S("NoBound"))) == S("StoreScalarOutsideV1")
            assert worker.call(E("Window", E("Inclusive", unsupported), S("NoBound"))) == E("StoreRefused", S("invalid-scalar-bound"))
    finally:
        worker.close()
    result = {"order_reference_pairs": orders, "exact_window_pairs": windows,
              "scalar_memberships": memberships, "unsupported_controls": 6,
              "scope": "portable scalar term order; exact ceil/floor Int64 oracle; no range cursor/index completion claim"}
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
