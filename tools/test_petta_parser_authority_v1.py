#!/usr/bin/env python3
"""Pin PeTTa's per-form DCG and whole-document reader behavior."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import tempfile
from typing import Any


EXPECTED_REVISION = "eb85359f8aae8ffae34f8c7c210593d3733fccfd"
EXPECTED_PARSER_DIGEST = "54d90136f61ef14c78c06809f116a4d4990572691a55ea0985a49ac937ec2088"
EXPECTED_READER_DIGEST = "e9a6d1cc98c133feba44153bfb1aa66d55ef5ccbe9635b72adacd683bb097286"
EXPECTED_MATRIX_DIGEST = "1cd322cb281e183dbc1b8f0ca20f101e6231cc9ca18828af1923abd6d605cd0d"


class GateFailure(RuntimeError):
    pass


Observation = tuple[str, ...]


FORM_CASES: dict[str, tuple[bytes, tuple[Observation, ...]]] = {
    "variables": (b"(a $x $x $_ $_)", (("term", "[a,A,A,B,C]"),)),
    "empty": (b"()", (("term", "[]"),)),
    "numbers-and-symbol-boundaries": (
        b"(1 -2 +3 1.2 .5 1e2 1_2_3 True False)",
        (("term", "[1,-2,3,1.2,'.5',100.0,'1_2_3',true,false]"),),
    ),
    "named-string-escapes": (
        b"(\"a\\n\\t\\r\")",
        (("term", "[\"a\\n\\t\\r\"]"),),
    ),
    "identity-string-escape": (b"(\"a\\q\")", (("term", "[\"aq\"]"),)),
    "quote-and-backslash-escapes": (
        b"(\"\\\"\" \"\\\\\")",
        (("term", "[\"\\\"\",\"\\\\\"]"),),
    ),
    "semicolon-token": (b"(a;b)", (("term", "['a;b']"),)),
    "quote-inside-token": (b"(a\"b)", (("term", "['a\"b']"),)),
    "unicode-leading-blank": ("\u1680(a)".encode("utf-8"), (("term", "[a]"),)),
    "unicode-inside-token": (
        "(a\u1680b)".encode("utf-8"),
        (("term", "['a\u1680b']"),),
    ),
    "vertical-tab-number-boundary": (
        b"(1\x0b 2)",
        (("term", "['1\\v',2]"),),
    ),
    "bare-dollar-symbol": (b"($)", (("term", "[$]"),)),
    "comment-marker-is-form-token": (b"(a ; b)", (("term", "[a,;,b]"),)),
    "semicolon-inside-variable-name": (b"($a;$q)", (("term", "[A]"),)),
    "two-forms-rejected-by-form-parser": (
        b"(a) (b)",
        (("error", "error(syntax_error('Parse error in form: (a) (b)'),none)"),),
    ),
    "unclosed-form": (
        b"(a",
        (("error", "error(syntax_error('Parse error in form: (a'),none)"),),
    ),
    "unclosed-string": (
        b"(\"a)",
        (("error", "error(syntax_error('Parse error in form: (\"a)'),none)"),),
    ),
    "incomplete-numbers-are-symbols": (
        b"(1. 1e 1e+)",
        (("term", "['1.','1e','1e+']"),),
    ),
}


DOCUMENT_CASES: dict[str, tuple[bytes, tuple[Observation, ...]]] = {
    "function-and-runnable": (
        b"; c\n(= (f $x) $x)\n!(f 1)\n",
        (
            ("form", "function", "(= (f $x) $x)", "[=,[f,A],A]"),
            ("form", "runnable", "(f 1)", "[f,1]"),
        ),
    ),
    "delimiters-inside-strings": (
        b"!(test \")\" \";\") ; tail\n",
        (("form", "runnable", "(test \")\" \";\")", "[test,\")\",\";\"]"),),
    ),
    "bare-top-level-token": (
        b"atom\n",
        (
            (
                "error",
                "error(syntax_error('expected \\'(\\' or \\'!(\\', line 1:\\natom'),none)",
            ),
        ),
    ),
    "unbalanced-document": (
        b"(a\n",
        (
            (
                "error",
                "error(syntax_error('missing \\')\\', starting at line 1:\\na'),none)",
            ),
        ),
    ),
    "comment-at-eof": (
        b"(a); tail",
        (("form", "expression", "(a)", "[a]"),),
    ),
    "two-document-forms": (
        b"(a)\n(b)\n",
        (
            ("form", "expression", "(a)", "[a]"),
            ("form", "expression", "(b)", "[b]"),
        ),
    ),
    "paired-escaped-quotes": (
        b"!(test \"quote: \\\"\" \"quote: \\\"\")\n",
        (
            (
                "form",
                "runnable",
                "(test \"quote: \\\"\" \"quote: \\\"\")",
                "[test,\"quote: \\\"\",\"quote: \\\"\"]",
            ),
        ),
    ),
    "single-escaped-quote-splitter-boundary": (
        b"(a \"quote: \\\"\")\n",
        (
            (
                "error",
                "error(syntax_error('missing \\')\\', starting at line 1:\\na \"quote: "
                "\\\\"
                "\"\")'),none)",
            ),
        ),
    ),
    "unicode-leading-blank": (
        "\u1680(a)\n".encode("utf-8"),
        (("form", "expression", "(a)", "[a]"),),
    ),
    "unicode-between-forms": (
        "(a)\u1680(b)\n".encode("utf-8"),
        (
            ("form", "expression", "(a)", "[a]"),
            ("form", "expression", "(b)", "[b]"),
        ),
    ),
    "nul-inside-token": (
        b"(a\x00b)\n",
        (("form", "expression", "(a\x00b)", "['a\x00b']"),),
    ),
    "runnable-without-expression": (
        b"!a\n",
        (
            (
                "error",
                "error(syntax_error('expected \\'(\\' or \\'!(\\', line 1:\\na'),none)",
            ),
        ),
    ),
}


def file_digest(path: Path) -> str:
    if not path.is_file():
        raise GateFailure(f"authority source missing: {path.name}")
    return sha256(path.read_bytes()).hexdigest()


def revision(root: Path) -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=30,
    )
    if completed.returncode != 0:
        raise GateFailure(f"cannot read PeTTa revision: {completed.stderr.strip()}")
    return completed.stdout.strip()


def decode_hex(field: bytes) -> str:
    try:
        return bytes.fromhex(field.decode("ascii")).decode("utf-8")
    except (UnicodeDecodeError, ValueError) as error:
        raise GateFailure("noncanonical PeTTa authority hexadecimal field") from error


def run_case(oracle: Path, root: Path, mode: str, input_path: Path) -> tuple[Observation, ...]:
    completed = subprocess.run(
        ["swipl", "-q", "-f", str(oracle), "--", mode, str(input_path)],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=30,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"PeTTa authority process failed: {completed.stderr.decode('utf-8', errors='replace')}"
        )
    lines = completed.stdout.splitlines()
    expected_header = [b"petta-parser-authority-v1", f"mode\t{mode}".encode("ascii")]
    if lines[:2] != expected_header or not lines or lines[-1] != b"end":
        raise GateFailure("malformed PeTTa authority stream framing")
    observations: list[Observation] = []
    for line in lines[2:-1]:
        fields = line.split(b"\t")
        if len(fields) == 2 and fields[0] in (b"term", b"error"):
            observations.append((fields[0].decode("ascii"), decode_hex(fields[1])))
        elif len(fields) == 4 and fields[0] == b"form":
            try:
                tag = fields[1].decode("ascii")
            except UnicodeDecodeError as error:
                raise GateFailure("non-ASCII PeTTa document tag") from error
            if tag not in ("function", "expression", "runnable"):
                raise GateFailure("unknown PeTTa document tag")
            observations.append(
                ("form", tag, decode_hex(fields[2]), decode_hex(fields[3]))
            )
        else:
            raise GateFailure("malformed PeTTa authority record")
    return tuple(observations)


def check_cases(oracle: Path, root: Path) -> tuple[int, int, list[dict[str, Any]]]:
    positive = 0
    negative = 0
    records: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="gslt2parse-petta-authority-") as raw:
        directory = Path(raw)
        for mode, cases in (("form", FORM_CASES), ("document", DOCUMENT_CASES)):
            for index, (label, (input_bytes, expected)) in enumerate(cases.items()):
                path = directory / f"{mode}-{index}.input"
                path.write_bytes(input_bytes)
                observed = run_case(oracle, root, mode, path)
                if observed != expected:
                    raise GateFailure(
                        f"{mode}/{label}: expected {expected!r}, observed {observed!r}"
                    )
                if observed and observed[0][0] == "error":
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


def check_invalid_mode(oracle: Path, root: Path) -> int:
    completed = subprocess.run(
        ["swipl", "-q", "-f", str(oracle), "--", "invalid", "missing"],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=30,
    )
    if completed.returncode == 0 or b"usage:" not in completed.stderr:
        raise GateFailure("invalid PeTTa authority mode did not fail closed")
    return 1


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
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()
    root = arguments.petta_root.resolve()

    observed_revision = revision(root)
    if observed_revision != EXPECTED_REVISION:
        raise GateFailure(
            f"PeTTa authority revision mismatch: expected {EXPECTED_REVISION}, got {observed_revision}"
        )
    parser_digest = file_digest(root / "src" / "parser.pl")
    reader_digest = file_digest(root / "src" / "filereader.pl")
    if parser_digest != EXPECTED_PARSER_DIGEST:
        raise GateFailure("PeTTa per-form parser authority digest mismatch")
    if reader_digest != EXPECTED_READER_DIGEST:
        raise GateFailure("PeTTa document reader authority digest mismatch")

    oracle = (
        root
        / "experiments"
        / "gslt2parse_foundation"
        / "petta_parser_authority_v1.pl"
    )
    positive, negative, records = check_cases(oracle, root)
    corruptions = check_invalid_mode(oracle, root)
    digest = matrix_digest(records)
    if digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(
            f"PeTTa authority matrix digest mismatch: expected {EXPECTED_MATRIX_DIGEST}, got {digest}"
        )
    print(
        f"(PeTTaParserAuthorityV1Summary {positive} {negative} "
        f"{len(FORM_CASES)} {len(DOCUMENT_CASES)} {corruptions} {digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
