#!/usr/bin/env python3
"""Prepare the regex lexical layer from its pinned Unicode decimal provider."""
import argparse
from pathlib import Path

from gslt2parse_schema_v1 import StringLiteral, Symbol, render
from unicode_presentation_v1 import read_catalog

ROOT = Path(__file__).resolve().parents[1]


def linked(items):
    result = Symbol("LNil")
    for item in reversed(items):
        result = (Symbol("LCons"), item, result)
    return result


def profile(catalog):
    def klass(name, excluded=None, points=None):
        return (Symbol("LexicalClassExcept" if excluded is not None else "LexicalClassPoints"),
                StringLiteral(name), linked(excluded if excluded is not None else points))
    classes = [
        klass("RxLiteralClass", excluded=[36, 40, 41, 42, 43, 46, 63, 91, 92, 93, 94, 123, 124, 125]),
        klass("RxEscapedClass", excluded=sorted([80, 107, 112, 117, 120] +
              [zero + digit for zero in catalog["decimal_zeroes"] for digit in range(10)])),
        klass("RxClassLiteralClass", excluded=[38, 45, 91, 92, 93, 126]),
        klass("RxClassFirstLiteralClass", excluded=[38, 45, 91, 92, 93, 94, 126]),
        klass("RxDigitClass", points=[zero + digit for zero in catalog["decimal_zeroes"] for digit in range(10)]),
        klass("RxNameClass", excluded=[10, 13, 40, 41, 62]),
        klass("RxPropertyNameClass", excluded=[10, 13, 40, 41, 123, 125]),
        klass("RxHexDigitClass", points=list(range(48, 58)) + list(range(65, 71)) + list(range(97, 103))),
    ]
    states = [
        ("RxScalar", "RxLiteralClass", "re-src:lex-scalar"),
        ("RxEscScalar", "RxEscapedClass", "re-src:lex-escaped"),
        ("RxClassScalar", "RxClassLiteralClass", "re-src:lex-class"),
        ("RxClassFirstScalar", "RxClassFirstLiteralClass", "re-src:lex-class-first"),
        ("RxDigit", "RxDigitClass", "re-src:lex-digit"),
        ("RxNameChar", "RxNameClass", "re-src:lex-name"),
        ("RxPropertyChar", "RxPropertyNameClass", "re-src:lex-property"),
        ("RxHexDigit", "RxHexDigitClass", "re-src:lex-hex"),
    ]
    return (Symbol("GSLTParserProfileLayerV1"), StringLiteral("RegexPatternSyntaxV1"),
            StringLiteral("RxPattern"), linked(classes), linked([
                (Symbol("LexicalState"), *(StringLiteral(value) for value in row)) for row in states]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    catalog = read_catalog(ROOT / "langdef/unicode/unicode_codepoint_classes16_v1.metta")
    output = ROOT / "langdef/regex/source_profile_v1.metta"
    prepared = render(profile(catalog)) + "\n"
    if args.check:
        if output.read_text() != prepared:
            raise SystemExit("regex lexical profile differs from pinned Unicode provider")
    else:
        output.write_text(prepared)
    print(f"(RegexLexicalPreparationV1 {catalog['version']} {len(catalog['decimal_zeroes']) * 10} 0)")


if __name__ == "__main__":
    main()
