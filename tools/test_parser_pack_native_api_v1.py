#!/usr/bin/env python3
"""Exact direct-library gate for the canonical ParserPack native API."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import tempfile

import gslt2parse_schema_v1 as schema
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_gll_v1 import (
    CASES,
    MATRIX_DIGEST,
    STARTS,
    GateFailure,
    input_bytes,
    matrix_row_text,
    oracle_rows,
)


class NativeResponse(ctypes.Structure):
    _fields_ = [
        ("bytes", ctypes.POINTER(ctypes.c_uint8)),
        ("len", ctypes.c_size_t),
    ]


def symbol(term: schema.SExpr, context: str) -> str:
    if not isinstance(term, schema.Symbol):
        raise GateFailure(f"{context}: expected symbol")
    return term.text


def string(term: schema.SExpr, context: str) -> str:
    if not isinstance(term, schema.StringLiteral):
        raise GateFailure(f"{context}: expected string")
    return term.text


def carrier_list(term: schema.SExpr, context: str) -> tuple[schema.SExpr, ...]:
    values: list[schema.SExpr] = []
    cursor = term
    while True:
        if isinstance(cursor, schema.Symbol) and cursor.text == "nil":
            return tuple(values)
        if (
            not isinstance(cursor, tuple)
            or len(cursor) != 3
            or symbol(cursor[0], context) != "cons"
        ):
            raise GateFailure(f"{context}: malformed carrier list")
        values.append(cursor[1])
        cursor = cursor[2]


def response_fields(payload: bytes) -> dict[str, schema.SExpr]:
    forms = schema.parse_sexprs(payload.decode("utf-8"), source="native response")
    if len(forms) != 1 or not isinstance(forms[0], tuple) or not forms[0]:
        raise GateFailure("native API returned malformed response framing")
    root = forms[0]
    if symbol(root[0], "native response root") != "pp-native-response-v1":
        raise GateFailure("native API returned the wrong response root")
    fields: dict[str, schema.SExpr] = {}
    for raw_field in root[1:]:
        if not isinstance(raw_field, tuple) or len(raw_field) != 2:
            raise GateFailure("native API returned a malformed response field")
        name = symbol(raw_field[0], "native response field")
        if name in fields:
            raise GateFailure(f"native API duplicated response field {name}")
        fields[name] = raw_field[1]
    return fields


class NativeApi:
    def __init__(self, library: Path):
        self.library = ctypes.CDLL(str(library))
        self.library.ppnative_api_v1_response_init.argtypes = [
            ctypes.POINTER(NativeResponse)
        ]
        self.library.ppnative_api_v1_response_free.argtypes = [
            ctypes.POINTER(NativeResponse)
        ]
        self.library.ppnative_api_v1_parse.argtypes = [
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.POINTER(NativeResponse),
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        self.library.ppnative_api_v1_parse.restype = ctypes.c_bool

    def parse(
        self,
        backend: str,
        abi_path: Path,
        start: str,
        payload: bytes,
        expect_success: bool = True,
    ) -> dict[str, schema.SExpr]:
        start_bytes = start.encode("utf-8")
        start_array = (ctypes.c_uint8 * len(start_bytes)).from_buffer_copy(
            start_bytes
        )
        if payload:
            input_array = (ctypes.c_uint8 * len(payload)).from_buffer_copy(payload)
            input_pointer = ctypes.cast(
                input_array, ctypes.POINTER(ctypes.c_uint8)
            )
        else:
            input_array = None
            input_pointer = ctypes.POINTER(ctypes.c_uint8)()
        response = NativeResponse()
        error = ctypes.create_string_buffer(1024)
        self.library.ppnative_api_v1_response_init(ctypes.byref(response))
        try:
            ok = self.library.ppnative_api_v1_parse(
                0 if backend == "gll" else 1,
                str(abi_path).encode("utf-8"),
                start_array,
                len(start_bytes),
                input_pointer,
                len(payload),
                2_000_000,
                4096,
                65_536,
                ctypes.byref(response),
                error,
                len(error),
            )
            if not expect_success:
                if ok or not error.value:
                    raise GateFailure(
                        f"native {backend.upper()} API did not fail closed"
                    )
                return {}
            if not ok:
                raise GateFailure(
                    f"native {backend.upper()} API failed: "
                    + error.value.decode("utf-8", errors="replace")
                )
            raw = ctypes.string_at(response.bytes, response.len)
            return response_fields(raw)
        finally:
            self.library.ppnative_api_v1_response_free(ctypes.byref(response))
            del input_array


def require_case(
    backend: str,
    language: str,
    label: str,
    codepoints: tuple[int, ...],
    payload: bytes,
    fields: dict[str, schema.SExpr],
    oracle: dict[str, object],
) -> dict[str, object]:
    if symbol(fields.get("backend"), "backend") != backend:
        raise GateFailure(f"{language}/{label}: native API backend changed")
    if symbol(fields.get("outcome"), "outcome") != "completed":
        raise GateFailure(f"{language}/{label}: native API did not complete")
    decision = symbol(fields.get("decision"), "decision")
    forest_digest = string(fields.get("forest-digest"), "forest digest")
    forest_text = schema.render(fields.get("forest"))
    results = tuple(
        schema.render(term)
        for term in carrier_list(fields.get("results"), "results")
    )
    observed: dict[str, object] = {
        "decision": decision,
        "forest-digest": forest_digest,
        "results": results,
    }
    for field in ("decision", "forest-digest", "results"):
        if observed[field] != oracle[field]:
            raise GateFailure(
                f"{language}/{label}: direct {backend.upper()} API {field} "
                f"differs: C={observed[field]!r}, PeTTa={oracle[field]!r}"
            )
    if hashlib.sha256(forest_text.encode("utf-8")).hexdigest() != forest_digest:
        raise GateFailure(f"{language}/{label}: native API forest digest is false")
    if (
        fields.get("source-passes") != 1
        or fields.get("decoded-bytes") != len(payload)
        or fields.get("input-bytes") != len(payload)
        or fields.get("input-scalars") != len(codepoints)
    ):
        raise GateFailure(
            f"{language}/{label}: direct {backend.upper()} API one-pass "
            "accounting failed"
        )
    expected_offsets = [0]
    for codepoint in codepoints:
        expected_offsets.append(
            expected_offsets[-1] + len(chr(codepoint).encode("utf-8"))
        )
    offsets = carrier_list(fields.get("byte-offsets"), "byte offsets")
    if offsets != tuple(expected_offsets):
        raise GateFailure(f"{language}/{label}: byte-offset map differs")
    for digest_name in (
        "source-digest",
        "compiler-digest",
        "environment-digest",
        "pack-digest",
    ):
        digest = string(fields.get(digest_name), digest_name)
        if len(digest) != 64:
            raise GateFailure(f"{language}/{label}: malformed {digest_name}")
    return observed


def check_matrix(
    library: Path,
    petta_root: Path,
    isolate_oracle_environment: bool = False,
) -> tuple[int, int, int, int]:
    oracle_environment = None
    if isolate_oracle_environment:
        oracle_environment = os.environ.copy()
        oracle_environment.pop("LD_PRELOAD", None)
        oracle_environment.pop("ASAN_OPTIONS", None)
    api = NativeApi(library)
    expected = oracle_rows(petta_root, oracle_environment)
    gll_exact = 0
    glr_exact = 0
    gll_rows: list[str] = []
    glr_rows: list[str] = []
    with tempfile.TemporaryDirectory(prefix="gslt2parse-native-api-") as raw:
        temp = Path(raw)
        abi_paths: dict[str, Path] = {}
        for language in STARTS:
            abi_path = temp / f"{language}.abi"
            abi_path.write_bytes(
                export_stream(
                    petta_root, ABI_CASES[language], oracle_environment
                )
            )
            abi_paths[language] = abi_path
        prime_path = temp / "prime.abi"
        prime_path.write_bytes(
            export_stream(petta_root, ABI_CASES["prime"], oracle_environment)
        )
        abi_paths["prime"] = prime_path

        for language, label, codepoints in CASES:
            payload = input_bytes(codepoints)
            oracle = expected[(language, label)]
            try:
                gll = require_case(
                    "gll",
                    language,
                    label,
                    codepoints,
                    payload,
                    api.parse(
                        "gll", abi_paths[language], STARTS[language], payload
                    ),
                    oracle,
                )
                glr = require_case(
                    "glr",
                    language,
                    label,
                    codepoints,
                    payload,
                    api.parse(
                        "glr", abi_paths[language], STARTS[language], payload
                    ),
                    oracle,
                )
            except GateFailure as error:
                raise GateFailure(f"{language}/{label}: {error}") from error
            if gll != glr:
                raise GateFailure(f"{language}/{label}: direct GLL/GLR APIs differ")
            gll_rows.append(matrix_row_text(language, label, gll))
            glr_rows.append(matrix_row_text(language, label, glr))
            gll_exact += 1
            glr_exact += 1

        for backend in ("gll", "glr"):
            prime = api.parse(
                backend,
                abi_paths["prime"],
                "(pp-def cetta-prime-file)",
                b"",
            )
            if symbol(prime.get("outcome"), "Prime outcome") != (
                "unsupported-open-pack"
            ):
                raise GateFailure(
                    f"native {backend.upper()} API Prime boundary changed"
                )
            api.parse(
                backend,
                abi_paths["metamath"],
                STARTS["metamath"],
                b"\xc0\x80",
                expect_success=False,
            )

    for backend, rows in (("GLL", gll_rows), ("GLR", glr_rows)):
        digest = hashlib.sha256("\n".join(rows).encode("utf-8")).hexdigest()
        if digest != MATRIX_DIGEST:
            raise GateFailure(f"direct {backend} API matrix digest changed: {digest}")
    return gll_exact, glr_exact, 1, 2


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--isolate-oracle-environment", action="store_true")
    arguments = parser.parse_args()
    gll, glr, partial, invalid = check_matrix(
        arguments.library.resolve(),
        arguments.petta_root.resolve(),
        arguments.isolate_oracle_environment,
    )
    print(
        f"(ParserPackNativeAPIV1MatrixSummary {len(CASES)} {gll} {glr} "
        f"{partial} {invalid} {MATRIX_DIGEST} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
