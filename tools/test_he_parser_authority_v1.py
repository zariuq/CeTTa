#!/usr/bin/env python3
"""Pin exact HE parser behavior before lowering it to a LanguageDef."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any

from run_he_parser_oracle_v1 import authority_digest
from run_he_parser_oracle_v1 import authority_revision
from run_he_parser_oracle_v1 import build_oracle


EXPECTED_REVISION = "3f76dc460da6961f57f69f6c3e550c59c74ada83"
EXPECTED_SOURCE_DIGEST = "c4bda32a09da1d091a5d725fb541e239dcea1339c8f312ad0e98763dad426a28"
EXPECTED_MATRIX_DIGEST = "c73c71fadee417f9eb585e29f7061b0453068580a8dc3647e68a99f83031e92e"


class GateFailure(RuntimeError):
    pass


AtomValue = tuple[Any, ...]
SyntaxValue = tuple[Any, ...]


ATOM_CASES: dict[str, tuple[bytes, tuple[AtomValue, ...]]] = {
    "word": (b"alpha", (("sym", "alpha"),)),
    "variable": (b"$x", (("var", "x"),)),
    "bare-dollar-variable": (b"$", (("var", ""),)),
    "repeated-variable": (
        b"(a $x $x)",
        (("expr", (("sym", "a"), ("var", "x"), ("var", "x"))),),
    ),
    "comment-in-expression": (
        b"(a;comment\n b)",
        (("expr", (("sym", "a"), ("sym", "b"))),),
    ),
    "string-escapes": (
        b"\"\\n\\r\\t\\'\\\"\\\\\\x7f\\u{03bb}\"",
        (("sym", "\"\n\r\t'\"\\\x7fλ\""),),
    ),
    "unicode-word": ("λ".encode("utf-8"), (("sym", "λ"),)),
    "quote-inside-word": (b"word\"tail", (("sym", "word\"tail"),)),
    "semicolon-and-quote-inside-variable": (
        b"$a;\"b",
        (("var", "a;\"b"),),
    ),
    "unicode-whitespace": (
        "a\u1680b".encode("utf-8"),
        (("sym", "a"), ("sym", "b")),
    ),
    "top-level-comment": (b"a;b", (("sym", "a"),)),
    "empty-expression": (b"()", (("expr", ()),)),
    "nul-in-word": (b"a\x00b", (("sym", "a\x00b"),)),
    "unexpected-right-paren": (
        b")",
        (("error", "Unexpected right bracket"),),
    ),
    "unclosed-expression": (
        b"(a",
        (("error", "Unexpected end of expression"),),
    ),
    "unclosed-string": (
        b"\"abc",
        (("error", "Unclosed String Literal"),),
    ),
    "unknown-string-escape": (
        b"\"\\q\"",
        (("error", "Invalid escape sequence"),),
    ),
    "out-of-range-byte-escape": (
        b"\"\\x80\"",
        (("error", "Invalid escape sequence"),),
    ),
    "out-of-range-unicode-escape": (
        b"\"\\u{110000}\"",
        (("error", "Invalid escape sequence"),),
    ),
    "reserved-variable-hash": (
        b"$a#rest",
        (("error", "'#' char is reserved for internal usage"),),
    ),
    "invalid-utf8": (
        b"\xff",
        (("error", "Input read error at position 0: Bad UTF-8: [255]"),),
    ),
}


SYNTAX_CASES: dict[str, tuple[bytes, tuple[SyntaxValue, ...]]] = {
    "unicode-whitespace-range": (
        "a\u1680b".encode("utf-8"),
        (
            ("word", 0, 1, True, "a", None, ()),
            ("whitespace", 1, 2, True, None, None, ()),
            ("word", 4, 5, True, "b", None, ()),
        ),
    ),
    "decoded-unicode-string": (
        b"\"\\u{03bb}\"",
        (("string", 0, 10, True, "\"λ\"", None, ()),),
    ),
    "unclosed-expression-tree": (
        b"(a",
        (
            (
                "error-group",
                0,
                2,
                False,
                None,
                "Unexpected end of expression",
                (
                    ("open-paren", 0, 1, True, None, None, ()),
                    ("word", 1, 2, True, "a", None, ()),
                ),
            ),
        ),
    ),
    "reserved-variable-hash-tree": (
        b"$a#rest",
        (
            (
                "leftover",
                0,
                7,
                False,
                None,
                "'#' char is reserved for internal usage",
                (),
            ),
        ),
    ),
    "comment-ranges": (
        b"a;b\n",
        (
            ("word", 0, 1, True, "a", None, ()),
            ("comment", 1, 3, True, None, None, ()),
            ("whitespace", 3, 4, True, None, None, ()),
        ),
    ),
}


SYNTAX_NAMES = (
    "comment",
    "variable",
    "string",
    "word",
    "open-paren",
    "close-paren",
    "whitespace",
    "leftover",
    "expression",
    "error-group",
)


def take_u64(raw: bytes, position: int) -> tuple[int, int]:
    end = position + 8
    if end > len(raw):
        raise GateFailure("truncated canonical u64")
    return int.from_bytes(raw[position:end], "big"), end


def take_bytes(raw: bytes, position: int) -> tuple[bytes, int]:
    length, position = take_u64(raw, position)
    end = position + length
    if end > len(raw):
        raise GateFailure("truncated canonical byte string")
    return raw[position:end], end


def take_optional_text(raw: bytes, position: int) -> tuple[str | None, int]:
    if position >= len(raw):
        raise GateFailure("truncated canonical option")
    present = raw[position]
    position += 1
    if present == 0:
        return None, position
    if present != 1:
        raise GateFailure("noncanonical option discriminator")
    value, position = take_bytes(raw, position)
    return value.decode("utf-8"), position


def decode_atom(raw: bytes, position: int = 0) -> tuple[AtomValue, int]:
    if position >= len(raw):
        raise GateFailure("truncated canonical atom")
    tag = raw[position]
    position += 1
    if tag in (ord("S"), ord("V"), ord("G")):
        value, position = take_bytes(raw, position)
        kind = {ord("S"): "sym", ord("V"): "var", ord("G"): "grounded"}[tag]
        return (kind, value.decode("utf-8")), position
    if tag == ord("E"):
        count, position = take_u64(raw, position)
        children = []
        for _ in range(count):
            child, position = decode_atom(raw, position)
            children.append(child)
        return ("expr", tuple(children)), position
    raise GateFailure("unknown canonical atom tag")


def decode_syntax(raw: bytes, position: int = 0) -> tuple[SyntaxValue, int]:
    if position + 2 > len(raw) or raw[position] != ord("N"):
        raise GateFailure("invalid canonical syntax-node tag")
    kind = raw[position + 1]
    position += 2
    if kind >= len(SYNTAX_NAMES):
        raise GateFailure("invalid canonical syntax-node kind")
    start, position = take_u64(raw, position)
    end, position = take_u64(raw, position)
    if position >= len(raw) or raw[position] not in (0, 1):
        raise GateFailure("invalid syntax completeness flag")
    complete = bool(raw[position])
    position += 1
    parsed_text, position = take_optional_text(raw, position)
    message, position = take_optional_text(raw, position)
    count, position = take_u64(raw, position)
    children = []
    for _ in range(count):
        child, position = decode_syntax(raw, position)
        children.append(child)
    return (
        SYNTAX_NAMES[kind],
        start,
        end,
        complete,
        parsed_text,
        message,
        tuple(children),
    ), position


def run_case(binary: Path, mode: str, path: Path) -> tuple[tuple[Any, ...], ...]:
    completed = subprocess.run(
        [str(binary), mode, str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=30,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"HE oracle process failed: {completed.stderr.decode('utf-8', errors='replace')}"
        )
    lines = completed.stdout.splitlines()
    if lines[:2] != [b"he-parser-oracle-v1", f"mode\t{mode}".encode("ascii")]:
        raise GateFailure("HE oracle header mismatch")
    if not lines or lines[-1] != b"end":
        raise GateFailure("HE oracle end record missing")
    decoded = []
    record_name = b"atom" if mode == "atoms" else b"syntax"
    decoder = decode_atom if mode == "atoms" else decode_syntax
    for line in lines[2:-1]:
        fields = line.split(b"\t")
        if len(fields) != 2 or fields[0] not in (record_name, b"error"):
            raise GateFailure("malformed HE oracle record")
        try:
            raw = bytes.fromhex(fields[1].decode("ascii"))
        except (UnicodeDecodeError, ValueError) as error:
            raise GateFailure("noncanonical HE oracle hexadecimal field") from error
        if fields[0] == b"error":
            decoded.append(("error", raw.decode("utf-8")))
        else:
            value, position = decoder(raw)
            if position != len(raw):
                raise GateFailure("trailing bytes in canonical HE oracle record")
            decoded.append(value)
    return tuple(decoded)


def check_cases(binary: Path) -> tuple[int, int, list[dict[str, Any]]]:
    positive = 0
    negative = 0
    records: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="gslt2parse-he-authority-") as raw:
        directory = Path(raw)
        for mode, cases in (("atoms", ATOM_CASES), ("syntax", SYNTAX_CASES)):
            for index, (label, (input_bytes, expected)) in enumerate(cases.items()):
                path = directory / f"{mode}-{index}.input"
                path.write_bytes(input_bytes)
                observed = run_case(binary, mode, path)
                if observed != expected:
                    raise GateFailure(
                        f"{mode}/{label}: expected {expected!r}, observed {observed!r}"
                    )
                is_negative = any(value and value[0] == "error" for value in observed)
                if is_negative:
                    negative += 1
                else:
                    positive += 1
                records.append(
                    {
                        "input": input_bytes.hex(),
                        "label": label,
                        "mode": mode,
                        "result": observed,
                    }
                )
    return positive, negative, records


def matrix_digest(records: list[dict[str, Any]]) -> str:
    canonical = json.dumps(
        records,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return sha256(canonical).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--he-root", type=Path, required=True)
    arguments = parser.parse_args()
    he_root = arguments.he_root.resolve()

    revision = authority_revision(he_root)
    if revision != EXPECTED_REVISION:
        raise GateFailure(
            f"HE authority revision mismatch: expected {EXPECTED_REVISION}, got {revision}"
        )
    source_digest = authority_digest(he_root)
    if source_digest != EXPECTED_SOURCE_DIGEST:
        raise GateFailure(
            f"HE parser source mismatch: expected {EXPECTED_SOURCE_DIGEST}, got {source_digest}"
        )

    binary = build_oracle(he_root)
    positive, negative, records = check_cases(binary)
    digest = matrix_digest(records)
    if digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(
            f"HE authority matrix digest mismatch: expected {EXPECTED_MATRIX_DIGEST}, got {digest}"
        )
    print(
        f"(HEParserAuthorityV1Summary {positive} {negative} "
        f"{len(ATOM_CASES)} {len(SYNTAX_CASES)} {digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
