#!/usr/bin/env python3
"""Three-way gate for PeTTa's legacy whole-document splitter."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import tempfile

from gslt2parse_schema_v1 import SExpr
from gslt2parse_schema_v1 import Symbol
from gslt2parse_schema_v1 import parse_sexprs
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_gll_v1 import run_native
from test_petta_parser_authority_v1 import EXPECTED_PARSER_DIGEST
from test_petta_parser_authority_v1 import EXPECTED_READER_DIGEST
from test_petta_parser_authority_v1 import EXPECTED_REVISION
from test_petta_parser_authority_v1 import file_digest
from test_petta_parser_authority_v1 import revision
from test_petta_parser_authority_v1 import run_case as run_form_authority


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
START = "(pp-def petta-document)"

EXPECTED_AUTHORITY_ADAPTER_DIGEST = (
    "b754f23f66ca3646336f1cb9abe1bdf22a1a275a02f5fb52d8e084e8f4c9c7ec"
)
EXPECTED_CLASSES_DIGEST = (
    "a227411f64e0b6071e9be5a0995ab19e22313a8126ebf6953157e00aa6baf7e2"
)
EXPECTED_SPLITTER_DIGEST = (
    "6cd9ffbdd6b2b33896360fade27c07fcc619c6319f049cd776de3d97c315f6f7"
)
EXPECTED_SOURCE_DIGEST = (
    "538dcab8a48eaa8670378e4defd300d8e01f944e9f73f9dd4f6f2744a13baac0"
)
EXPECTED_COMPILER_DIGEST = (
    "bcc036af326a6ee1b56b285e0cf6ee2cd8cc174a8bccc209c51e76b079305cd6"
)
EXPECTED_ENVIRONMENT_DIGEST = (
    "a3e9da3e47a8a6bb9598d3e60daa410784aa5fffed42c23e033119138107ca9f"
)
EXPECTED_PACK_DIGEST = (
    "369958a39dec4e1073388f74381ef5d8d6293792318232e0fe7b677ecc5c54f3"
)
EXPECTED_MATRIX_DIGEST = (
    "3ef42cf57766ca0d5d21c48d4c7f5c85e74df563c2eeb09e804b7a1b6ae52e15"
)


class GateFailure(RuntimeError):
    pass


RawObservation = tuple[str, ...]


@dataclass(frozen=True)
class SplitterCase:
    label: str
    payload: bytes
    forms: tuple[RawObservation, ...] | None


CASES = (
    SplitterCase("empty", b"", ()),
    SplitterCase("blanks-only", b" \t\n\r", ()),
    SplitterCase(
        "function-and-runnable",
        b"; c\n(= (f $x) $x)\n!(f 1)\n",
        (("form", "(= (f $x) $x)"), ("runnable", "(f 1)")),
    ),
    SplitterCase(
        "delimiters-inside-strings",
        b"!(test \")\" \";\") ; tail\n",
        (("runnable", '(test ")" ";")'),),
    ),
    SplitterCase("bare-top-level-token", b"atom\n", None),
    SplitterCase("unbalanced-document", b"(a\n", None),
    SplitterCase("comment-at-eof", b"(a); tail", (("form", "(a)"),)),
    SplitterCase(
        "two-document-forms",
        b"(a)\n(b)\n",
        (("form", "(a)"), ("form", "(b)")),
    ),
    SplitterCase(
        "paired-escaped-quotes",
        b"!(test \"quote: \\\"\" \"quote: \\\"\")\n",
        (("runnable", '(test "quote: \\"" "quote: \\"")'),),
    ),
    SplitterCase(
        "single-escaped-quote-splitter-boundary",
        b"(a \"quote: \\\"\")\n",
        None,
    ),
    SplitterCase(
        "unicode-leading-blank",
        "\u1680(a)\n".encode("utf-8"),
        (("form", "(a)"),),
    ),
    SplitterCase(
        "unicode-between-forms",
        "(a)\u1680(b)\n".encode("utf-8"),
        (("form", "(a)"), ("form", "(b)")),
    ),
    SplitterCase("nul-inside-token", b"(a\x00b)\n", (("form", "(a\x00b)"),)),
    SplitterCase("runnable-without-expression", b"!a\n", None),
    SplitterCase("comment-only-at-eof", b";only", ()),
    SplitterCase("comment-joins-token", b"(a;gone\nb)", (("form", "(ab)"),)),
    SplitterCase(
        "comment-marker-inside-quote",
        b'(a ";still")',
        (("form", '(a ";still")'),),
    ),
    SplitterCase(
        "nested-and-quoted-parentheses",
        b'((a) "(;)")',
        (("form", '((a) "(;)")'),),
    ),
    SplitterCase(
        "dangling-string-escape-is-splitter-form",
        b'("a b\\")',
        (("form", '("a b\\")'),),
    ),
    SplitterCase("quote-inside-form-token", b'(a"b)\n', None),
    SplitterCase("semicolon-inside-form-token", b"(a;b)\n", None),
    SplitterCase("cr-does-not-end-comment", b";gone\r(a)", ()),
    SplitterCase(
        "comment-removal-adjoins-forms",
        b"(a);gone\n(b)",
        (("form", "(a)"), ("form", "(b)")),
    ),
    SplitterCase("vertical-tab-is-top-blank", b"\x0b(a)", (("form", "(a)"),)),
    SplitterCase("inner-comment-erases-line", b"(;gone\n)", (("form", "()"),)),
    SplitterCase("extra-close", b"(a))", None),
)


def decode_hex(field: bytes) -> str:
    try:
        return bytes.fromhex(field.decode("ascii")).decode("utf-8")
    except (UnicodeDecodeError, ValueError) as error:
        raise GateFailure("noncanonical splitter authority hexadecimal field") from error


def run_splitter_authority(
    oracle: Path, petta_root: Path, input_path: Path
) -> tuple[RawObservation, ...]:
    completed = subprocess.run(
        ["swipl", "-q", "-f", str(oracle), "--", str(input_path)],
        cwd=petta_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=30,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "PeTTa splitter authority process failed: "
            + completed.stderr.decode("utf-8", errors="replace")
        )
    lines = completed.stdout.splitlines()
    if (
        not lines
        or lines[0] != b"petta-document-splitter-authority-v1"
        or lines[-1] != b"end"
    ):
        raise GateFailure("malformed PeTTa splitter authority framing")
    observations: list[RawObservation] = []
    for line in lines[1:-1]:
        fields = line.split(b"\t")
        if len(fields) != 2 or fields[0] not in (b"form", b"runnable", b"error"):
            raise GateFailure("malformed PeTTa splitter authority record")
        observations.append((fields[0].decode("ascii"), decode_hex(fields[1])))
    return tuple(observations)


def run_reference(
    petta_root: Path, input_path: Path, max_depth: int = 4096
) -> dict[str, object]:
    script = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "petta_document_splitter_gslt_reference_v1.pl"
    )
    completed = subprocess.run(
        [
            "swipl",
            "-q",
            "-f",
            str(script),
            "--",
            str(PRESENTATIONS),
            str(input_path),
            str(max_depth),
        ],
        cwd=script.parent,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=180,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "PeTTa splitter LanguageDef reference failed: "
            + completed.stderr.strip()
        )
    try:
        row = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure("PeTTa splitter reference emitted non-JSON") from error
    if (
        not isinstance(row, dict)
        or row.get("protocol")
        != "petta-document-splitter-gslt-reference-v1"
    ):
        raise GateFailure("PeTTa splitter reference protocol changed")
    return row


def scalar_field(stream: bytes, name: str) -> str:
    prefix = name.encode("ascii") + b"\t"
    rows = [line[len(prefix) :] for line in stream.splitlines() if line.startswith(prefix)]
    if len(rows) != 1:
        raise GateFailure(f"ABI stream has no unique {name} field")
    try:
        return rows[0].decode("ascii")
    except UnicodeDecodeError as error:
        raise GateFailure(f"ABI {name} field is non-ASCII") from error


def require_abi_seals(stream: bytes) -> None:
    expected = {
        "source-digest": EXPECTED_SOURCE_DIGEST,
        "compiler-digest": EXPECTED_COMPILER_DIGEST,
        "environment-digest": EXPECTED_ENVIRONMENT_DIGEST,
        "pack-digest": EXPECTED_PACK_DIGEST,
        "start": "(pp-def petta-document)",
        "closure": "closed",
    }
    for name, value in expected.items():
        observed = scalar_field(stream, name)
        if value and observed != value:
            raise GateFailure(
                f"PeTTa splitter ABI {name} changed: {observed}"
            )
    productions = sum(
        line.startswith(b"production\t") for line in stream.splitlines()
    )
    classes = sum(
        line.startswith(b"class-clause\t") for line in stream.splitlines()
    )
    if (productions, classes) != ABI_CASES["petta-splitter"]["summary"][:2]:
        raise GateFailure("PeTTa splitter ParserPack shape changed")


def symbol(term: SExpr, expected: str, context: str) -> None:
    if not isinstance(term, Symbol) or term.text != expected:
        raise GateFailure(f"{context}: expected symbol {expected}")


def flatten_value(term: SExpr) -> str:
    if isinstance(term, Symbol):
        if term.text == "nil":
            return ""
        raise GateFailure(f"unexpected splitter semantic symbol: {term.text}")
    if not isinstance(term, tuple) or not term:
        raise GateFailure("malformed splitter semantic value")
    head = term[0]
    if not isinstance(head, Symbol):
        raise GateFailure("splitter semantic value has a nonsymbol head")
    if head.text == "cp" and len(term) == 2 and isinstance(term[1], int):
        try:
            return chr(term[1])
        except ValueError as error:
            raise GateFailure("splitter semantic value has a nonscalar cp") from error
    if head.text in ("pair", "cons") and len(term) == 3:
        return flatten_value(term[1]) + flatten_value(term[2])
    if head.text == "node" and len(term) == 3:
        return flatten_value(term[2])
    raise GateFailure(f"unknown splitter semantic constructor: {head.text}")


def list_items(term: SExpr) -> tuple[SExpr, ...]:
    items: list[SExpr] = []
    current = term
    while True:
        if isinstance(current, Symbol) and current.text == "nil":
            return tuple(items)
        if (
            not isinstance(current, tuple)
            or len(current) != 3
            or not isinstance(current[0], Symbol)
            or current[0].text != "cons"
        ):
            raise GateFailure("splitter document value is not a canonical list")
        items.append(current[1])
        current = current[2]


def semantic_forms(result_text: str) -> tuple[RawObservation, ...]:
    terms = parse_sexprs(result_text, source="native splitter result")
    if len(terms) != 1 or not isinstance(terms[0], tuple) or len(terms[0]) != 3:
        raise GateFailure("malformed native splitter result")
    result = terms[0]
    symbol(result[0], "result", "splitter result")
    symbol(result[2], "nil", "splitter result rest")
    document = result[1]
    if not isinstance(document, tuple) or len(document) != 3:
        raise GateFailure("splitter result omitted document node")
    symbol(document[0], "node", "splitter document")
    symbol(document[1], "document", "splitter document label")
    observations: list[RawObservation] = []
    for form in list_items(document[2]):
        if not isinstance(form, tuple) or len(form) != 3:
            raise GateFailure("splitter document has a malformed form node")
        symbol(form[0], "node", "splitter form")
        if not isinstance(form[1], Symbol) or form[1].text not in ("form", "runnable"):
            raise GateFailure("splitter document has an unknown form label")
        observations.append((form[1].text, flatten_value(form[2])))
    return tuple(observations)


def require_native_row(
    backend: str,
    row: dict[str, object],
    decision: str,
    payload: bytes,
) -> None:
    scalars = len(payload.decode("utf-8"))
    expected = {
        "outcome": "completed",
        "decision": decision,
        "forest-materialized": "1",
        "input-bytes": str(len(payload)),
        "input-scalars": str(scalars),
        "decoded-bytes": str(len(payload)),
        "source-passes": "1",
    }
    for name, value in expected.items():
        if row.get(name) != value:
            raise GateFailure(
                f"native {backend} splitter invariant changed at {name}"
            )


def reproduce_generated_files(directory: Path) -> int:
    classes = directory / "classes.metta"
    splitter = directory / "splitter.metta"
    generator = ROOT / "scripts" / "build_petta_document_splitter_gslt_v1.py"
    completed = subprocess.run(
        [
            "python3",
            str(generator),
            "--classes",
            str(classes),
            "--splitter",
            str(splitter),
        ],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=30,
    )
    if completed.returncode != 0:
        raise GateFailure("PeTTa splitter generator failed: " + completed.stderr)
    checked_classes = (
        PRESENTATIONS / "shared" / "petta_document_splitter_scalar_classes_v1.metta"
    )
    checked_splitter = (
        PRESENTATIONS / "languages" / "petta_document_splitter_v1.metta"
    )
    if classes.read_bytes() != checked_classes.read_bytes():
        raise GateFailure("generated PeTTa splitter scalar classes are stale")
    if splitter.read_bytes() != checked_splitter.read_bytes():
        raise GateFailure("generated PeTTa splitter presentation is stale")
    return 2


def pin_sources(petta_root: Path, authority_oracle: Path) -> None:
    if revision(petta_root) != EXPECTED_REVISION:
        raise GateFailure("PeTTa authority revision changed")
    if file_digest(petta_root / "src" / "parser.pl") != EXPECTED_PARSER_DIGEST:
        raise GateFailure("PeTTa per-form parser authority changed")
    if file_digest(petta_root / "src" / "filereader.pl") != EXPECTED_READER_DIGEST:
        raise GateFailure("PeTTa document reader authority changed")
    expected_files = (
        (authority_oracle, EXPECTED_AUTHORITY_ADAPTER_DIGEST),
        (
            PRESENTATIONS
            / "shared"
            / "petta_document_splitter_scalar_classes_v1.metta",
            EXPECTED_CLASSES_DIGEST,
        ),
        (
            PRESENTATIONS / "languages" / "petta_document_splitter_v1.metta",
            EXPECTED_SPLITTER_DIGEST,
        ),
    )
    for path, expected in expected_files:
        observed = file_digest(path)
        if expected and observed != expected:
            raise GateFailure(f"pinned splitter artifact changed: {observed}")


def matrix_digest(records: list[dict[str, object]]) -> str:
    canonical = json.dumps(
        records,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return sha256(canonical).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gll-binary", type=Path, required=True)
    parser.add_argument("--glr-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    gll_binary = arguments.gll_binary.resolve()
    glr_binary = arguments.glr_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if not gll_binary.is_file() or not glr_binary.is_file():
        raise GateFailure("native ParserPack splitter binary is absent")
    if not petta_root.is_dir():
        raise GateFailure("PeTTa authority checkout is absent")

    authority_oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "petta_document_splitter_authority_v1.pl"
    )
    form_oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "petta_parser_authority_v1.pl"
    )
    pin_sources(petta_root, authority_oracle)

    abi = export_stream(petta_root, ABI_CASES["petta-splitter"])
    require_abi_seals(abi)
    records: list[dict[str, object]] = []
    accepted = 0
    rejected = 0
    authority_agreements = 0
    dual_native_agreements = 0
    semantic_agreements = 0
    cross_layer_gates = 0

    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-petta-splitter-", dir="/run/user/1001"
    ) as raw:
        directory = Path(raw)
        abi_path = directory / "petta-splitter.abi"
        abi_path.write_bytes(abi)
        generated_gates = reproduce_generated_files(directory)

        inputs: dict[str, Path] = {}
        for index, case in enumerate(CASES):
            input_path = directory / f"case-{index}.input"
            input_path.write_bytes(case.payload)
            inputs[case.label] = input_path
            raw_observations = run_splitter_authority(
                authority_oracle, petta_root, input_path
            )
            if case.forms is None:
                if len(raw_observations) != 1 or raw_observations[0][0] != "error":
                    raise GateFailure(f"PeTTa splitter accepted negative case {case.label}")
                decision = "rejected"
                rejected += 1
            else:
                if raw_observations != case.forms:
                    raise GateFailure(
                        f"PeTTa splitter forms changed for {case.label}: "
                        f"{raw_observations!r}"
                    )
                decision = "accepted"
                accepted += 1

            reference = run_reference(petta_root, input_path)
            if (
                reference.get("decode") != "valid-utf8"
                or reference.get("outcome") != "completed"
                or reference.get("decision") != decision
                or reference.get("semantic_count") != (1 if decision == "accepted" else 0)
                or reference.get("derivation_count")
                != (1 if decision == "accepted" else 0)
            ):
                raise GateFailure(
                    f"PeTTa authority and splitter LanguageDef differ for {case.label}"
                )
            authority_agreements += 1

            gll = run_native(
                gll_binary, abi_path, START, input_path, backend="GLL"
            )
            glr = run_native(
                glr_binary,
                abi_path,
                START,
                input_path,
                protocol="parser-pack-glr-stream-v1",
                backend="GLR",
            )
            require_native_row("GLL", gll, decision, case.payload)
            require_native_row("GLR", glr, decision, case.payload)
            if (
                gll.get("forest-digest") != glr.get("forest-digest")
                or gll.get("results") != glr.get("results")
            ):
                raise GateFailure(f"native GLL and GLR differ for {case.label}")
            dual_native_agreements += 1

            native_results = gll["results"]
            assert isinstance(native_results, tuple)
            reference_results = reference.get("results")
            if native_results != tuple(reference_results or ()):
                raise GateFailure(
                    f"C and PeTTa splitter semantics differ for {case.label}"
                )
            if decision == "accepted":
                if len(native_results) != 1 or semantic_forms(native_results[0]) != case.forms:
                    raise GateFailure(
                        f"native splitter did not reproduce raw forms for {case.label}"
                    )
            elif native_results:
                raise GateFailure(f"rejected splitter case produced a semantic result")
            semantic_agreements += 1

            records.append(
                {
                    "decision": decision,
                    "forest": gll["forest-digest"],
                    "input": case.payload.hex(),
                    "label": case.label,
                    "raw": raw_observations,
                    "results": native_results,
                }
            )

        dangling_path = inputs["dangling-string-escape-is-splitter-form"]
        dangling_split = run_splitter_authority(
            authority_oracle, petta_root, dangling_path
        )
        dangling_form = directory / "dangling-form.input"
        dangling_form.write_text(dangling_split[0][1], encoding="utf-8")
        dangling_form_result = run_form_authority(
            form_oracle, petta_root, "form", dangling_form
        )
        if not dangling_form_result or dangling_form_result[0][0] != "error":
            raise GateFailure("splitter/form distinction lost for dangling escape")
        cross_layer_gates += 1

        quote_token = directory / "quote-token.input"
        quote_token.write_bytes(b'(a"b)')
        quote_form_result = run_form_authority(
            form_oracle, petta_root, "form", quote_token
        )
        quote_split_result = run_splitter_authority(
            authority_oracle, petta_root, quote_token
        )
        if (
            not quote_form_result
            or quote_form_result[0][0] != "term"
            or len(quote_split_result) != 1
            or quote_split_result[0][0] != "error"
        ):
            raise GateFailure("form/splitter distinction lost for quoted token")
        cross_layer_gates += 1

        invalid = directory / "invalid-utf8.input"
        invalid.write_bytes(b"(\xff)")
        invalid_reference = run_reference(petta_root, invalid)
        if (
            invalid_reference.get("decode") != "invalid-utf8"
            or invalid_reference.get("decision") != "rejected"
        ):
            raise GateFailure("splitter reference did not reject invalid UTF-8")
        run_native(
            gll_binary,
            abi_path,
            START,
            invalid,
            backend="GLL",
            expect_success=False,
        )
        run_native(
            glr_binary,
            abi_path,
            START,
            invalid,
            protocol="parser-pack-glr-stream-v1",
            backend="GLR",
            expect_success=False,
        )
        invalid_utf8_gates = 3

    digest = matrix_digest(records)
    if EXPECTED_MATRIX_DIGEST and digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(f"PeTTa splitter matrix changed: {digest}")

    observed_seals = (
        file_digest(authority_oracle),
        file_digest(
            PRESENTATIONS
            / "shared"
            / "petta_document_splitter_scalar_classes_v1.metta"
        ),
        file_digest(PRESENTATIONS / "languages" / "petta_document_splitter_v1.metta"),
        scalar_field(abi, "source-digest"),
        scalar_field(abi, "compiler-digest"),
        scalar_field(abi, "environment-digest"),
        scalar_field(abi, "pack-digest"),
    )
    if not all(
        (
            EXPECTED_AUTHORITY_ADAPTER_DIGEST,
            EXPECTED_CLASSES_DIGEST,
            EXPECTED_SPLITTER_DIGEST,
            EXPECTED_SOURCE_DIGEST,
            EXPECTED_COMPILER_DIGEST,
            EXPECTED_ENVIRONMENT_DIGEST,
            EXPECTED_PACK_DIGEST,
            EXPECTED_MATRIX_DIGEST,
        )
    ):
        print("UNPINNED_SEALS", *observed_seals, digest)
    print(
        f"(PeTTaDocumentSplitterV1Summary {len(CASES)} {accepted} {rejected} "
        f"{authority_agreements} {dual_native_agreements} {semantic_agreements} "
        f"{cross_layer_gates} {generated_gates} {invalid_utf8_gates} {digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
