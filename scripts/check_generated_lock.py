#!/usr/bin/env python3
"""Check source-derived lock fields and the locally built cursor's checksum.

Compiled ELF bytes depend on the compiler and build profile. Their checksum
must match the actual cursor, while every other lock field must match the
tracked source-derived lock.
"""
from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path


CURSOR_DIGEST = re.compile(
    r'^\s*\(compiled-cursor-sha256 "([0-9a-f]{64})"\)[ \t]*$', re.MULTILINE)


def digest_field(text: str):
    matches = list(CURSOR_DIGEST.finditer(text))
    if len(matches) != 1 or text.count("compiled-cursor-sha256") != 1:
        raise ValueError("lock must contain one well-formed compiled-cursor checksum")
    return matches[0]


def validate(expected: str, actual: str, compiled_cursor: bytes) -> None:
    expected_field = digest_field(expected)
    actual_field = digest_field(actual)
    actual_digest = hashlib.sha256(compiled_cursor).hexdigest()
    if actual_field.group(1) != actual_digest:
        raise ValueError("lock checksum does not match the compiled cursor")

    def source_fields(text: str, field) -> str:
        start, end = field.span(1)
        return text[:start] + "<compiled-cursor>" + text[end:]

    if source_fields(expected, expected_field) != source_fields(actual, actual_field):
        raise ValueError("source-derived lock fields differ from the tracked lock")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected", type=Path, required=True)
    parser.add_argument("--actual", type=Path, required=True)
    parser.add_argument("--compiled-cursor", type=Path, required=True)
    args = parser.parse_args()
    validate(args.expected.read_text(), args.actual.read_text(),
             args.compiled_cursor.read_bytes())


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
