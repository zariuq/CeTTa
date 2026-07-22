#!/usr/bin/env python3
"""One-decode composition gate for PeTTa's splitter and per-form parser."""

from __future__ import annotations

import argparse
from contextlib import ExitStack
import ctypes
from dataclasses import dataclass
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile
import time

import gslt2parse_schema_v1 as schema
from test_he_reader_guard_exec_v1 import matrix_digest
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_guard_plan_prime_v1 import REGULAR_COMPILER
from test_parser_pack_guard_plan_prime_v1 import evidence_stream
from test_parser_pack_guard_regular_v1 import native_row as guard_nfa_row
from test_parser_pack_guarded_lexical_v1 import GUARDED_COMPILER
from test_parser_pack_guarded_lexical_v1 import LEXICAL_CASE as SHADOW_LEXICAL_CASE
from test_parser_pack_guarded_lexical_v1 import EXPECTED_EXECUTION_PLAN_DIGEST
from test_parser_pack_guarded_lexical_v1 import EXPECTED_NATIVE as EXPECTED_SHADOW_NATIVE
from test_parser_pack_guarded_lexical_v1 import guarded_native
from test_parser_pack_lexical_v1 import compile_case_tags
from test_petta_document_splitter_v1 import CASES
from test_petta_document_splitter_v1 import EXPECTED_PACK_DIGEST as SPLITTER_PACK
from test_petta_document_splitter_v1 import pin_sources
from test_petta_document_splitter_v1 import run_reference as run_splitter_reference
from test_petta_document_splitter_v1 import run_splitter_authority
from test_petta_form_guard_exec_v1 import EXPECTED_BASE_PACK_DIGEST as FORM_PACK
from test_petta_form_guard_exec_v1 import EXPECTED_GUARD_EVIDENCE_DIGEST
from test_petta_form_guard_exec_v1 import EXPECTED_GUARD_NFA_DIGEST
from test_petta_form_guard_exec_v1 import EXPECTED_GUARD_PLAN_DIGEST
from test_petta_form_guard_exec_v1 import PROFILE
from test_petta_form_guard_exec_v1 import expected_decision
from test_petta_form_guard_exec_v1 import guard_row
from test_petta_form_guard_exec_v1 import pin_authority as pin_form_authority
from test_petta_form_guard_exec_v1 import run_petta_reference
from test_petta_parser_authority_v1 import run_case as run_form_authority
from test_regular_span_dfa_v1 import abi_scalar


EXPECTED_MATRIX_DIGEST = (
    "300a4b8a8889a257d75eb7042a569ea2b5db4913439050670a6f322d978328b3"
)
EXPECTED_ACTION_MATRIX_DIGEST = (
    "d0f7e92aaeab48016c9b18453fcddbc4b12b881d4fb7993aff7967680f1d6368"
)
EXPECTED_NATIVE_API_MATRIX_DIGEST = (
    "ab341e4b507c00d00845531f01b4bc06e032d71fd6de9f13cd4ec18c096c243a"
)
EXPECTED_REFLECTIVE_API_MATRIX_DIGEST = (
    "e171879ad4dcfaf613d6d40cc5788f48691608982f8b1904d51847fdc1bc71a0"
)
EXPECTED_LEXICAL_SHADOW_MATRIX_DIGEST = (
    "894819360be2130c796a75027fec55ff6ea215438a9507205292aa43e68523aa"
)
EXPECTED_PREPARED_MATRIX_DIGEST = (
    "d710e41a9f26f099aa4e5d03828e69ab11dbdb482034426bc3dbb75d94a782cd"
)


class GateFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class NativeForm:
    index: int
    kind: str
    scalar_left: int
    scalar_right: int
    byte_left: int
    byte_right: int
    raw: bytes
    source: bytes
    decision: str
    forest_digest: str
    relation_digest: str
    source_passes: int
    guard_tokens: int
    results: tuple[str, ...]


@dataclass(frozen=True)
class NativePipeline:
    metadata: dict[str, str]
    forms: tuple[NativeForm, ...]


class PipelineLimits(ctypes.Structure):
    _fields_ = [
        ("dfa_state_limit", ctypes.c_uint32),
        ("dfa_transition_limit", ctypes.c_uint32),
        ("scan_work_limit", ctypes.c_uint64),
        ("scan_token_limit", ctypes.c_uint32),
        ("witness_work_limit", ctypes.c_uint32),
        ("parse_work_limit", ctypes.c_uint32),
        ("replay_depth", ctypes.c_uint32),
        ("result_limit", ctypes.c_uint32),
    ]


class PipelineResponse(ctypes.Structure):
    _fields_ = [
        ("bytes", ctypes.POINTER(ctypes.c_uint8)),
        ("len", ctypes.c_size_t),
    ]


class LexicalShadowReceipt(ctypes.Structure):
    _fields_ = [
        ("lexical_plan_digest", ctypes.c_char * 65),
        ("guard_plan_digest", ctypes.c_char * 65),
        ("guarded_plan_digest", ctypes.c_char * 65),
        ("execution_plan_digest", ctypes.c_char * 65),
        ("form_execution_count", ctypes.c_uint32),
        ("semantic_agreement_count", ctypes.c_uint32),
        ("source_pass_count", ctypes.c_uint32),
        ("dfa_scan_count", ctypes.c_uint32),
        ("splitter_parser_table_build_count", ctypes.c_uint32),
        ("scalar_form_parser_table_build_count", ctypes.c_uint32),
        (
            "scalar_form_witness_parser_family_build_count",
            ctypes.c_uint32,
        ),
        ("scalar_form_witness_prepared_start_len", ctypes.c_uint32),
        ("lexical_final_parser_table_build_count", ctypes.c_uint32),
        (
            "lexical_witness_parser_family_build_count",
            ctypes.c_uint32,
        ),
        ("lexical_witness_prepared_start_len", ctypes.c_uint32),
    ]


DEFAULT_LIMITS = PipelineLimits(
    65_536,
    2_000_000,
    20_000_000,
    2_000_000,
    10_000_000,
    50_000_000,
    4096,
    1_000_000,
)


def lexical_shadow_receipt_dict(
    receipt: LexicalShadowReceipt,
) -> dict[str, str | int]:
    return {
        "lexical-plan-digest": receipt.lexical_plan_digest.decode("ascii"),
        "guard-plan-digest": receipt.guard_plan_digest.decode("ascii"),
        "guarded-plan-digest": receipt.guarded_plan_digest.decode("ascii"),
        "execution-plan-digest": receipt.execution_plan_digest.decode("ascii"),
        "form-executions": receipt.form_execution_count,
        "semantic-agreements": receipt.semantic_agreement_count,
        "source-passes": receipt.source_pass_count,
        "dfa-scans": receipt.dfa_scan_count,
        "splitter-parser-table-builds": (
            receipt.splitter_parser_table_build_count
        ),
        "scalar-form-parser-table-builds": (
            receipt.scalar_form_parser_table_build_count
        ),
        "scalar-form-witness-parser-family-builds": (
            receipt.scalar_form_witness_parser_family_build_count
        ),
        "scalar-form-witness-prepared-starts": (
            receipt.scalar_form_witness_prepared_start_len
        ),
        "lexical-final-parser-table-builds": (
            receipt.lexical_final_parser_table_build_count
        ),
        "lexical-witness-parser-family-builds": (
            receipt.lexical_witness_parser_family_build_count
        ),
        "lexical-witness-prepared-starts": (
            receipt.lexical_witness_prepared_start_len
        ),
    }


def decode_hex(value: str, context: str) -> bytes:
    try:
        return bytes.fromhex(value)
    except ValueError as error:
        raise GateFailure(f"{context} is not canonical hexadecimal") from error


def positive_integer(value: str, context: str, *, allow_zero: bool = True) -> int:
    try:
        integer = int(value, 10)
    except ValueError as error:
        raise GateFailure(f"{context} is not a decimal integer") from error
    if integer < 0 or (not allow_zero and integer == 0):
        raise GateFailure(f"{context} is outside its nonnegative domain")
    return integer


def term_symbol(term: schema.SExpr, context: str) -> str:
    if not isinstance(term, schema.Symbol):
        raise GateFailure(f"{context} is not a symbol")
    return term.text


def term_string(term: schema.SExpr, context: str) -> str:
    if not isinstance(term, schema.StringLiteral):
        raise GateFailure(f"{context} is not a string")
    return term.text


def term_integer(term: schema.SExpr, context: str) -> int:
    if not isinstance(term, int) or term < 0:
        raise GateFailure(f"{context} is not a nonnegative integer")
    return term


def carrier_list(
    term: schema.SExpr, context: str
) -> tuple[schema.SExpr, ...]:
    values: list[schema.SExpr] = []
    cursor = term
    while True:
        if isinstance(cursor, schema.Symbol) and cursor.text == "nil":
            return tuple(values)
        if (
            not isinstance(cursor, tuple)
            or len(cursor) != 3
            or term_symbol(cursor[0], context) != "cons"
        ):
            raise GateFailure(f"{context} is not a canonical carrier list")
        values.append(cursor[1])
        cursor = cursor[2]


def record_fields(
    term: schema.SExpr, root_name: str, context: str
) -> dict[str, schema.SExpr]:
    if (
        not isinstance(term, tuple)
        or not term
        or term_symbol(term[0], context) != root_name
    ):
        raise GateFailure(f"{context} has the wrong response root")
    fields: dict[str, schema.SExpr] = {}
    for field in term[1:]:
        if not isinstance(field, tuple) or len(field) != 2:
            raise GateFailure(f"{context} has a malformed field")
        name = term_symbol(field[0], f"{context} field")
        if name in fields:
            raise GateFailure(f"{context} duplicated field {name}")
        fields[name] = field[1]
    return fields


def filtered_scalar_bytes(term: schema.SExpr) -> bytes:
    codepoints: list[int] = []
    for value in carrier_list(term, "filtered scalar list"):
        if (
            not isinstance(value, tuple)
            or len(value) != 2
            or term_symbol(value[0], "filtered scalar") != "cp"
        ):
            raise GateFailure("filtered scalar is not canonical cp data")
        codepoint = term_integer(value[1], "filtered scalar value")
        if codepoint > 0x10FFFF or 0xD800 <= codepoint <= 0xDFFF:
            raise GateFailure("filtered scalar is outside Unicode")
        codepoints.append(codepoint)
    return "".join(chr(value) for value in codepoints).encode("utf-8")


def parse_api_response(payload: bytes, source: bytes) -> NativePipeline:
    try:
        forms = schema.parse_sexprs(
            payload.decode("utf-8"), source="PeTTa document native API"
        )
    except UnicodeDecodeError as error:
        raise GateFailure("document native API returned non-UTF-8") from error
    if len(forms) != 1:
        raise GateFailure("document native API response is not singular")
    fields = record_fields(
        forms[0],
        "petta-document-native-response-v1",
        "document native API response",
    )
    expected_root_fields = {
        "splitter-pack-digest",
        "form-pack-digest",
        "guard-plan-digest",
        "guard-evidence-digest",
        "guard-nfa-answer-digest",
        "document-decision",
        "document-gll-forest-digest",
        "document-glr-forest-digest",
        "document-gll-source-passes",
        "document-glr-source-passes",
        "source-passes",
        "input-bytes",
        "input-scalars",
        "form-count",
        "form-accepted",
        "form-rejected",
        "document-results",
        "forms",
    }
    if set(fields) != expected_root_fields:
        raise GateFailure("document native API response fields changed")
    metadata = {
        name: term_string(fields[name], name)
        for name in (
            "splitter-pack-digest",
            "form-pack-digest",
            "guard-plan-digest",
            "guard-evidence-digest",
            "guard-nfa-answer-digest",
            "document-gll-forest-digest",
            "document-glr-forest-digest",
        )
    }
    metadata["document-decision"] = term_symbol(
        fields["document-decision"], "document decision"
    )
    for name in (
        "document-gll-source-passes",
        "document-glr-source-passes",
        "source-passes",
        "input-bytes",
        "input-scalars",
        "form-count",
        "form-accepted",
        "form-rejected",
    ):
        metadata[name] = str(term_integer(fields[name], name))
    carrier_list(fields["document-results"], "document results")

    native_forms: list[NativeForm] = []
    expected_form_fields = {
        "index",
        "kind",
        "source-scalar-left",
        "source-scalar-right",
        "source-byte-left",
        "source-byte-right",
        "form-scalars",
        "form-bytes",
        "filtered-scalars",
        "decision",
        "gll-forest-digest",
        "glr-forest-digest",
        "gll-guard-relation-digest",
        "glr-guard-relation-digest",
        "scan-source-passes",
        "gll-source-passes",
        "glr-source-passes",
        "guard-tokens",
        "results",
    }
    for raw_form in carrier_list(fields["forms"], "document forms"):
        form = record_fields(
            raw_form,
            "petta-document-native-form-v1",
            "document native form",
        )
        if set(form) != expected_form_fields:
            raise GateFailure("document native form fields changed")
        index = term_integer(form["index"], "form index")
        scalar_left = term_integer(
            form["source-scalar-left"], "form scalar left"
        )
        scalar_right = term_integer(
            form["source-scalar-right"], "form scalar right"
        )
        byte_left = term_integer(form["source-byte-left"], "form byte left")
        byte_right = term_integer(form["source-byte-right"], "form byte right")
        raw = filtered_scalar_bytes(form["filtered-scalars"])
        if (
            term_integer(form["form-scalars"], "form scalar length")
            != len(raw.decode("utf-8"))
            or term_integer(form["form-bytes"], "form byte length") != len(raw)
            or byte_left > byte_right
            or byte_right > len(source)
        ):
            raise GateFailure("document native form accounting changed")
        gll_forest = term_string(form["gll-forest-digest"], "GLL forest")
        glr_forest = term_string(form["glr-forest-digest"], "GLR forest")
        gll_relation = term_string(
            form["gll-guard-relation-digest"], "GLL guard relation"
        )
        glr_relation = term_string(
            form["glr-guard-relation-digest"], "GLR guard relation"
        )
        if gll_forest != glr_forest or gll_relation != glr_relation:
            raise GateFailure("document native form dual execution changed")
        if (
            term_integer(form["gll-source-passes"], "GLL source passes") != 0
            or term_integer(form["glr-source-passes"], "GLR source passes")
            != 0
        ):
            raise GateFailure("document native form reread source")
        results = tuple(
            schema.render(value)
            for value in carrier_list(form["results"], "form results")
        )
        native_forms.append(
            NativeForm(
                index=index,
                kind=term_symbol(form["kind"], "form kind"),
                scalar_left=scalar_left,
                scalar_right=scalar_right,
                byte_left=byte_left,
                byte_right=byte_right,
                raw=raw,
                source=source[byte_left:byte_right],
                decision=term_symbol(form["decision"], "form decision"),
                forest_digest=gll_forest,
                relation_digest=gll_relation,
                source_passes=term_integer(
                    form["scan-source-passes"], "scan source passes"
                ),
                guard_tokens=term_integer(form["guard-tokens"], "guard tokens"),
                results=results,
            )
        )
    return NativePipeline(metadata, tuple(native_forms))


class PipelineApi:
    def __init__(self, library: Path):
        self.library = ctypes.CDLL(str(library))
        self.library.petta_document_pipeline_v1_response_init.argtypes = [
            ctypes.POINTER(PipelineResponse)
        ]
        self.library.petta_document_pipeline_v1_response_free.argtypes = [
            ctypes.POINTER(PipelineResponse)
        ]
        self.library.petta_document_pipeline_v1_lexical_shadow_receipt_init.argtypes = [
            ctypes.POINTER(LexicalShadowReceipt)
        ]
        self.library.petta_document_pipeline_v1_parse.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.c_char_p,
            ctypes.POINTER(PipelineLimits),
            ctypes.POINTER(PipelineResponse),
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        self.library.petta_document_pipeline_v1_parse.restype = ctypes.c_bool
        self.library.petta_document_pipeline_v1_parse_lexical_shadow.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(PipelineLimits),
            ctypes.POINTER(PipelineLimits),
            ctypes.POINTER(LexicalShadowReceipt),
            ctypes.POINTER(PipelineResponse),
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        self.library.petta_document_pipeline_v1_parse_lexical_shadow.restype = (
            ctypes.c_bool
        )
        self.library.petta_document_pipeline_v1_prepared_new.restype = (
            ctypes.c_void_p
        )
        self.library.petta_document_pipeline_v1_prepared_free.argtypes = [
            ctypes.c_void_p
        ]
        self.library.petta_document_pipeline_v1_prepared_build_count.argtypes = [
            ctypes.c_void_p
        ]
        self.library.petta_document_pipeline_v1_prepared_build_count.restype = (
            ctypes.c_uint32
        )
        self.library.petta_document_pipeline_v1_prepare.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(PipelineLimits),
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        self.library.petta_document_pipeline_v1_prepare.restype = ctypes.c_bool
        self.library.petta_document_pipeline_v1_prepare_lexical_shadow.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(PipelineLimits),
            ctypes.POINTER(PipelineLimits),
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        self.library.petta_document_pipeline_v1_prepare_lexical_shadow.restype = (
            ctypes.c_bool
        )
        self.library.petta_document_pipeline_v1_prepared_parse.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.POINTER(PipelineResponse),
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        self.library.petta_document_pipeline_v1_prepared_parse.restype = (
            ctypes.c_bool
        )
        self.library.petta_document_pipeline_v1_prepared_parse_lexical_shadow.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.POINTER(LexicalShadowReceipt),
            ctypes.POINTER(PipelineResponse),
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        self.library.petta_document_pipeline_v1_prepared_parse_lexical_shadow.restype = (
            ctypes.c_bool
        )

    def parse(
        self,
        splitter_abi: Path,
        form_abi: Path,
        guard_nfa: Path,
        evidence: Path,
        payload: bytes,
        compiler_digest: str,
        *,
        expect_success: bool = True,
    ) -> tuple[NativePipeline, bytes] | None:
        if payload:
            input_array = (ctypes.c_uint8 * len(payload)).from_buffer_copy(payload)
            input_pointer = ctypes.cast(
                input_array, ctypes.POINTER(ctypes.c_uint8)
            )
        else:
            input_array = None
            input_pointer = ctypes.POINTER(ctypes.c_uint8)()
        response = PipelineResponse()
        error = ctypes.create_string_buffer(1024)
        self.library.petta_document_pipeline_v1_response_init(
            ctypes.byref(response)
        )
        try:
            ok = self.library.petta_document_pipeline_v1_parse(
                str(splitter_abi).encode("utf-8"),
                str(form_abi).encode("utf-8"),
                str(guard_nfa).encode("utf-8"),
                str(evidence).encode("utf-8"),
                input_pointer,
                len(payload),
                compiler_digest.encode("ascii"),
                ctypes.byref(DEFAULT_LIMITS),
                ctypes.byref(response),
                error,
                len(error),
            )
            if not expect_success:
                if ok or not error.value:
                    raise GateFailure("document native API did not fail closed")
                return None
            if not ok:
                raise GateFailure(
                    "document native API failed: "
                    + error.value.decode("utf-8", errors="replace")
                )
            raw = ctypes.string_at(response.bytes, response.len)
            return parse_api_response(raw, payload), raw
        finally:
            self.library.petta_document_pipeline_v1_response_free(
                ctypes.byref(response)
            )
            del input_array

    def prepared_new(self) -> int:
        prepared = self.library.petta_document_pipeline_v1_prepared_new()
        if not prepared:
            raise GateFailure("failed to allocate prepared document pipeline")
        return int(prepared)

    def prepared_free(self, prepared: int) -> None:
        self.library.petta_document_pipeline_v1_prepared_free(prepared)

    def prepared_build_count(self, prepared: int) -> int:
        return int(
            self.library.petta_document_pipeline_v1_prepared_build_count(
                prepared
            )
        )

    def prepare(
        self,
        prepared: int,
        splitter_abi: Path,
        form_abi: Path,
        guard_nfa: Path,
        evidence: Path,
        compiler_digest: str,
        *,
        expect_success: bool = True,
    ) -> bool:
        error = ctypes.create_string_buffer(1024)
        ok = self.library.petta_document_pipeline_v1_prepare(
            prepared,
            str(splitter_abi).encode("utf-8"),
            str(form_abi).encode("utf-8"),
            str(guard_nfa).encode("utf-8"),
            str(evidence).encode("utf-8"),
            compiler_digest.encode("ascii"),
            ctypes.byref(DEFAULT_LIMITS),
            error,
            len(error),
        )
        if not expect_success:
            if ok or not error.value:
                raise GateFailure("prepared document pipeline did not fail closed")
            return False
        if not ok:
            raise GateFailure(
                "prepared document pipeline failed: "
                + error.value.decode("utf-8", errors="replace")
            )
        return True

    def prepare_lexical_shadow(
        self,
        prepared: int,
        splitter_abi: Path,
        form_abi: Path,
        lexical_nfa: Path,
        guard_nfa: Path,
        evidence: Path,
        guarded_nfa: Path,
        regular_compiler_digest: str,
        guarded_compiler_digest: str,
        *,
        expect_success: bool = True,
    ) -> bool:
        error = ctypes.create_string_buffer(1024)
        ok = self.library.petta_document_pipeline_v1_prepare_lexical_shadow(
            prepared,
            str(splitter_abi).encode("utf-8"),
            str(form_abi).encode("utf-8"),
            str(lexical_nfa).encode("utf-8"),
            str(guard_nfa).encode("utf-8"),
            str(evidence).encode("utf-8"),
            str(guarded_nfa).encode("utf-8"),
            regular_compiler_digest.encode("ascii"),
            guarded_compiler_digest.encode("ascii"),
            ctypes.byref(DEFAULT_LIMITS),
            ctypes.byref(DEFAULT_LIMITS),
            error,
            len(error),
        )
        if not expect_success:
            if ok or not error.value:
                raise GateFailure(
                    "prepared document lexical pipeline did not fail closed"
                )
            return False
        if not ok:
            raise GateFailure(
                "prepared document lexical pipeline failed: "
                + error.value.decode("utf-8", errors="replace")
            )
        return True

    def parse_prepared(
        self,
        prepared: int,
        payload: bytes,
        *,
        lexical_shadow: bool,
        expect_success: bool = True,
    ) -> (
        tuple[NativePipeline, bytes]
        | tuple[NativePipeline, bytes, dict[str, str | int]]
        | None
    ):
        if payload:
            input_array = (ctypes.c_uint8 * len(payload)).from_buffer_copy(payload)
            input_pointer = ctypes.cast(
                input_array, ctypes.POINTER(ctypes.c_uint8)
            )
        else:
            input_array = None
            input_pointer = ctypes.POINTER(ctypes.c_uint8)()
        response = PipelineResponse()
        receipt = LexicalShadowReceipt()
        error = ctypes.create_string_buffer(1024)
        self.library.petta_document_pipeline_v1_response_init(
            ctypes.byref(response)
        )
        self.library.petta_document_pipeline_v1_lexical_shadow_receipt_init(
            ctypes.byref(receipt)
        )
        try:
            if lexical_shadow:
                ok = self.library.petta_document_pipeline_v1_prepared_parse_lexical_shadow(
                    prepared,
                    input_pointer,
                    len(payload),
                    ctypes.byref(receipt),
                    ctypes.byref(response),
                    error,
                    len(error),
                )
            else:
                ok = self.library.petta_document_pipeline_v1_prepared_parse(
                    prepared,
                    input_pointer,
                    len(payload),
                    ctypes.byref(response),
                    error,
                    len(error),
                )
            if not expect_success:
                if ok or not error.value:
                    raise GateFailure(
                        "prepared document parse did not fail closed"
                    )
                return None
            if not ok:
                raise GateFailure(
                    "prepared document parse failed: "
                    + error.value.decode("utf-8", errors="replace")
                )
            raw = ctypes.string_at(response.bytes, response.len)
            parsed = parse_api_response(raw, payload)
            if lexical_shadow:
                return parsed, raw, lexical_shadow_receipt_dict(receipt)
            return parsed, raw
        finally:
            self.library.petta_document_pipeline_v1_response_free(
                ctypes.byref(response)
            )
            del input_array

    def parse_lexical_shadow(
        self,
        splitter_abi: Path,
        form_abi: Path,
        lexical_nfa: Path,
        guard_nfa: Path,
        evidence: Path,
        guarded_nfa: Path,
        payload: bytes,
        regular_compiler_digest: str,
        guarded_compiler_digest: str,
        *,
        expect_success: bool = True,
    ) -> tuple[NativePipeline, bytes, dict[str, str | int]] | None:
        if payload:
            input_array = (ctypes.c_uint8 * len(payload)).from_buffer_copy(payload)
            input_pointer = ctypes.cast(
                input_array, ctypes.POINTER(ctypes.c_uint8)
            )
        else:
            input_array = None
            input_pointer = ctypes.POINTER(ctypes.c_uint8)()
        response = PipelineResponse()
        receipt = LexicalShadowReceipt()
        error = ctypes.create_string_buffer(1024)
        self.library.petta_document_pipeline_v1_response_init(
            ctypes.byref(response)
        )
        self.library.petta_document_pipeline_v1_lexical_shadow_receipt_init(
            ctypes.byref(receipt)
        )
        try:
            ok = self.library.petta_document_pipeline_v1_parse_lexical_shadow(
                str(splitter_abi).encode("utf-8"),
                str(form_abi).encode("utf-8"),
                str(lexical_nfa).encode("utf-8"),
                str(guard_nfa).encode("utf-8"),
                str(evidence).encode("utf-8"),
                str(guarded_nfa).encode("utf-8"),
                input_pointer,
                len(payload),
                regular_compiler_digest.encode("ascii"),
                guarded_compiler_digest.encode("ascii"),
                ctypes.byref(DEFAULT_LIMITS),
                ctypes.byref(DEFAULT_LIMITS),
                ctypes.byref(receipt),
                ctypes.byref(response),
                error,
                len(error),
            )
            if not expect_success:
                if ok or not error.value:
                    raise GateFailure(
                        "document lexical shadow API did not fail closed"
                    )
                return None
            if not ok:
                raise GateFailure(
                    "document lexical shadow API failed: "
                    + error.value.decode("utf-8", errors="replace")
                )
            raw = ctypes.string_at(response.bytes, response.len)
            observed_receipt = lexical_shadow_receipt_dict(receipt)
            return parse_api_response(raw, payload), raw, observed_receipt
        finally:
            self.library.petta_document_pipeline_v1_response_free(
                ctypes.byref(response)
            )
            del input_array


def parse_native(stdout: bytes) -> NativePipeline:
    try:
        lines = stdout.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise GateFailure("native document pipeline emitted non-ASCII framing") from error
    if not lines or lines[0] != "petta-document-pipeline-v1" or lines[-1] != "end":
        raise GateFailure("native document pipeline framing changed")
    metadata: dict[str, str] = {}
    raw_forms: dict[int, tuple[str, ...]] = {}
    raw_results: dict[int, list[str]] = {}
    for line in lines[1:-1]:
        fields = tuple(line.split("\t"))
        if len(fields) == 2 and fields[0] not in ("form", "form-result"):
            if fields[0] in metadata:
                raise GateFailure(f"duplicate native metadata field {fields[0]}")
            metadata[fields[0]] = fields[1]
            continue
        if len(fields) == 14 and fields[0] == "form":
            index = positive_integer(fields[1], "form index")
            if index in raw_forms:
                raise GateFailure("duplicate native form index")
            raw_forms[index] = fields[1:]
            continue
        if len(fields) == 3 and fields[0] == "form-result":
            index = positive_integer(fields[1], "form-result index")
            try:
                result = decode_hex(fields[2], "form result").decode("utf-8")
            except UnicodeDecodeError as error:
                raise GateFailure("native form result is not UTF-8") from error
            raw_results.setdefault(index, []).append(result)
            continue
        raise GateFailure("malformed native document pipeline record")

    count = positive_integer(metadata.get("form-count", ""), "form count")
    if tuple(sorted(raw_forms)) != tuple(range(count)):
        raise GateFailure("native form indexes are not dense and ordered")
    if any(index >= count for index in raw_results):
        raise GateFailure("native result refers to an absent form")
    forms: list[NativeForm] = []
    for index in range(count):
        fields = raw_forms[index]
        kind = fields[1]
        if kind not in ("form", "runnable"):
            raise GateFailure("native pipeline emitted an unknown form kind")
        decision = fields[8]
        if decision not in ("accepted", "rejected"):
            raise GateFailure("native pipeline emitted an unknown form decision")
        if re.fullmatch(r"[0-9a-f]{64}", fields[9]) is None or re.fullmatch(
            r"[0-9a-f]{64}", fields[10]
        ) is None:
            raise GateFailure("native form seal is not a SHA-256 digest")
        forms.append(
            NativeForm(
                index=index,
                kind=kind,
                scalar_left=positive_integer(fields[2], "form scalar-left"),
                scalar_right=positive_integer(fields[3], "form scalar-right"),
                byte_left=positive_integer(fields[4], "form byte-left"),
                byte_right=positive_integer(fields[5], "form byte-right"),
                raw=decode_hex(fields[6], "filtered form"),
                source=decode_hex(fields[7], "source form"),
                decision=decision,
                forest_digest=fields[9],
                relation_digest=fields[10],
                source_passes=positive_integer(fields[11], "form source passes"),
                guard_tokens=positive_integer(fields[12], "form guard tokens"),
                results=tuple(raw_results.get(index, ())),
            )
        )
    return NativePipeline(metadata, tuple(forms))


def run_native(
    binary: Path,
    splitter_abi: Path,
    form_abi: Path,
    guard_nfa: Path,
    evidence: Path,
    input_path: Path,
    compiler_digest: str,
    *,
    expect_success: bool = True,
) -> NativePipeline | None:
    completed = subprocess.run(
        [
            str(binary),
            str(splitter_abi),
            str(form_abi),
            str(guard_nfa),
            str(evidence),
            str(input_path),
            compiler_digest,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=180,
    )
    if not expect_success:
        if completed.returncode == 0:
            raise GateFailure("native pipeline mutation unexpectedly succeeded")
        return None
    if completed.returncode != 0:
        raise GateFailure(
            "native document pipeline failed: "
            + completed.stderr.decode("utf-8", errors="replace").strip()
        )
    return parse_native(completed.stdout)


def run_petta_native(
    petta_root: Path,
    splitter_abi: Path,
    form_abi: Path,
    guard_nfa: Path,
    evidence: Path,
    input_path: Path,
    compiler_digest: str,
) -> dict[str, object]:
    script = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "test_petta_document_native_v1.pl"
    )
    completed = subprocess.run(
        [
            "swipl",
            "-q",
            "-f",
            str(script),
            "--",
            str(splitter_abi),
            str(form_abi),
            str(guard_nfa),
            str(evidence),
            str(input_path),
            compiler_digest,
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
            "PeTTa document native API failed: " + completed.stderr.strip()
        )
    try:
        row = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure("PeTTa document native API emitted non-JSON") from error
    if (
        not isinstance(row, dict)
        or row.get("protocol") != "petta-document-native-v1"
    ):
        raise GateFailure("PeTTa document native API protocol changed")
    return row


def run_petta_native_surface(
    petta_root: Path,
    splitter_abi: Path,
    form_abi: Path,
    guard_nfa: Path,
    evidence: Path,
    compiler_digest: str,
) -> None:
    fixture = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "test_petta_document_native_surface_v1.metta"
    )
    completed = subprocess.run(
        [
            "bash",
            str(petta_root / "run.sh"),
            str(fixture),
            str(splitter_abi),
            str(form_abi),
            str(guard_nfa),
            str(evidence),
            compiler_digest,
            "--silent",
        ],
        cwd=petta_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=180,
    )
    values = [
        line.strip()
        for line in completed.stdout.splitlines()
        if line.strip() == "completed"
    ]
    if completed.returncode != 0 or values != ["completed"]:
        raise GateFailure(
            "PeTTa document MeTTa surface failed: " + completed.stdout.strip()
        )


def scalar_byte_offsets(payload: bytes) -> tuple[int, ...]:
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise GateFailure("positive pipeline fixture is not valid UTF-8") from error
    offsets = [0]
    for scalar in text:
        offsets.append(offsets[-1] + len(scalar.encode("utf-8")))
    return tuple(offsets)


def require_metadata(
    native: NativePipeline,
    payload: bytes,
    decision: str,
    form_count: int,
) -> None:
    offsets = scalar_byte_offsets(payload)
    expected = {
        "splitter-pack-digest": SPLITTER_PACK,
        "form-pack-digest": FORM_PACK,
        "guard-plan-digest": EXPECTED_GUARD_PLAN_DIGEST,
        "guard-evidence-digest": EXPECTED_GUARD_EVIDENCE_DIGEST,
        "guard-nfa-answer-digest": EXPECTED_GUARD_NFA_DIGEST,
        "document-decision": decision,
        "document-gll-source-passes": "1",
        "document-glr-source-passes": "0",
        "source-passes": "1",
        "input-bytes": str(len(payload)),
        "input-scalars": str(len(offsets) - 1),
        "form-count": str(form_count),
    }
    for field, value in expected.items():
        if native.metadata.get(field) != value:
            raise GateFailure(f"native pipeline invariant changed at {field}")
    if native.metadata.get("document-gll-forest-digest") != native.metadata.get(
        "document-glr-forest-digest"
    ):
        raise GateFailure("single-decode splitter GLL and GLR forests differ")
    accepted = sum(form.decision == "accepted" for form in native.forms)
    rejected = sum(form.decision == "rejected" for form in native.forms)
    if native.metadata.get("form-accepted") != str(accepted) or native.metadata.get(
        "form-rejected"
    ) != str(rejected):
        raise GateFailure("native per-form decision totals changed")


def require_lexical_shadow_receipt(
    receipt: dict[str, str | int], form_count: int
) -> None:
    expected: dict[str, str | int] = {
        "lexical-plan-digest": EXPECTED_SHADOW_NATIVE[
            "lexical-plan-digest"
        ],
        "guard-plan-digest": EXPECTED_SHADOW_NATIVE["guard-plan-digest"],
        "guarded-plan-digest": EXPECTED_SHADOW_NATIVE[
            "guarded-plan-digest"
        ],
        "execution-plan-digest": EXPECTED_EXECUTION_PLAN_DIGEST,
        "form-executions": form_count,
        "semantic-agreements": form_count,
        "source-passes": 0,
        "dfa-scans": form_count,
        "splitter-parser-table-builds": 1,
        "scalar-form-parser-table-builds": 1,
        "scalar-form-witness-parser-family-builds": 1,
        "scalar-form-witness-prepared-starts": 1,
        "lexical-final-parser-table-builds": 1,
        "lexical-witness-parser-family-builds": 3,
        "lexical-witness-prepared-starts": 4,
    }
    changed = {
        name: (value, receipt.get(name))
        for name, value in expected.items()
        if receipt.get(name) != value
    }
    if changed:
        raise GateFailure(f"document lexical shadow receipt changed: {changed}")
    for name in (
        "lexical-plan-digest",
        "guard-plan-digest",
        "guarded-plan-digest",
        "execution-plan-digest",
    ):
        value = receipt[name]
        if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
            raise GateFailure(f"document lexical shadow {name} is not a digest")


def require_provenance(
    form: NativeForm,
    payload: bytes,
    offsets: tuple[int, ...],
    previous_scalar_right: int,
) -> None:
    if (
        form.source_passes != 0
        or form.scalar_left > form.scalar_right
        or form.scalar_right >= len(offsets)
        or form.byte_left != offsets[form.scalar_left]
        or form.byte_right != offsets[form.scalar_right]
        or form.byte_left > form.byte_right
        or form.byte_right > len(payload)
        or form.source != payload[form.byte_left : form.byte_right]
        or previous_scalar_right > form.scalar_left
    ):
        raise GateFailure("native form provenance or zero-pass receipt changed")


def authority_decision(observation: tuple[tuple[str, ...], ...]) -> str:
    return expected_decision(observation)


def benchmark_call_pair(cold_call, prepared_call, samples: int) -> tuple[int, int]:
    cold_call()
    prepared_call()
    cold_samples: list[int] = []
    prepared_samples: list[int] = []

    def elapsed(call) -> int:
        start = time.perf_counter_ns()
        call()
        return time.perf_counter_ns() - start

    for sample in range(samples):
        if sample % 2 == 0:
            cold_samples.append(elapsed(cold_call))
            prepared_samples.append(elapsed(prepared_call))
        else:
            prepared_samples.append(elapsed(prepared_call))
            cold_samples.append(elapsed(cold_call))
    return int(statistics.median(cold_samples)), int(
        statistics.median(prepared_samples)
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--pipeline-binary", type=Path, required=True)
    parser.add_argument("--pipeline-library", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--isolate-oracle-environment", action="store_true")
    parser.add_argument("--benchmark-prepared", action="store_true")
    parser.add_argument("--benchmark-samples", type=int, default=3)
    arguments = parser.parse_args()
    if arguments.benchmark_samples <= 0:
        parser.error("--benchmark-samples must be positive")

    chart_binary = arguments.chart_binary.resolve()
    pipeline_binary = arguments.pipeline_binary.resolve()
    pipeline_library = arguments.pipeline_library.resolve()
    petta_root = arguments.petta_root.resolve()
    if (
        not chart_binary.is_file()
        or not pipeline_binary.is_file()
        or not pipeline_library.is_file()
    ):
        raise GateFailure("native pipeline dependency is absent")
    if not petta_root.is_dir():
        raise GateFailure("PeTTa authority checkout is absent")
    pipeline_api = PipelineApi(pipeline_library)
    if arguments.isolate_oracle_environment:
        os.environ.pop("LD_PRELOAD", None)
        os.environ.pop("ASAN_OPTIONS", None)

    splitter_oracle = (
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
    pin_sources(petta_root, splitter_oracle)
    pin_form_authority(petta_root)

    splitter_stream = export_stream(petta_root, ABI_CASES["petta-splitter"])
    form_stream = export_stream(petta_root, ABI_CASES["petta"])
    evidence_row = guard_row(chart_binary)
    guard_row_native = guard_nfa_row(chart_binary, "petta")
    guard_terms = tuple(str(term) for term in guard_row_native["terms"])
    compiler_digest = sha256(REGULAR_COMPILER.read_bytes()).hexdigest()
    guarded_compiler_digest = sha256(GUARDED_COMPILER.read_bytes()).hexdigest()
    lexical_terms, _, _ = compile_case_tags(
        chart_binary,
        petta_root,
        SHADOW_LEXICAL_CASE,
        abi_scalar(form_stream, "source-digest"),
    )
    guarded_terms = guarded_native(chart_binary)

    records: list[dict[str, object]] = []
    accepted_documents = 0
    rejected_documents = 0
    document_agreements = 0
    form_executions = 0
    form_authority_agreements = 0
    form_semantic_agreements = 0
    form_host_action_agreements = 0
    form_classification_agreements = 0
    action_records: list[dict[str, object]] = []
    native_api_records: list[dict[str, object]] = []
    native_api_agreements = 0
    reflective_records: list[dict[str, object]] = []
    reflective_document_agreements = 0
    reflective_form_agreements = 0
    reflective_host_agreements = 0
    one_pass_receipts = 0
    lexical_shadow_records: list[dict[str, object]] = []
    lexical_shadow_agreements = 0
    lexical_shadow_form_executions = 0
    lexical_shadow_semantic_agreements = 0
    lexical_shadow_source_passes = 0
    lexical_shadow_dfa_scans = 0
    lexical_shadow_mutations = 0
    prepared_records: list[dict[str, object]] = []
    prepared_scalar_agreements = 0
    prepared_lexical_agreements = 0
    prepared_build_count = 0
    prepared_mutations = 0

    with (
        tempfile.TemporaryDirectory(prefix="gslt2parse-petta-pipeline-") as raw,
        ExitStack() as prepared_stack,
    ):
        directory = Path(raw)
        splitter_abi = directory / "splitter.abi"
        form_abi = directory / "form.abi"
        lexical_nfa = directory / "lexical.nfa"
        guard_nfa = directory / "guard.nfa"
        evidence = directory / "guard.evidence"
        guarded_nfa = directory / "guarded.nfa"
        splitter_abi.write_bytes(splitter_stream)
        form_abi.write_bytes(form_stream)
        lexical_nfa.write_text(
            "\n".join(lexical_terms) + "\n", encoding="utf-8"
        )
        guard_nfa.write_text("\n".join(guard_terms) + "\n", encoding="utf-8")
        evidence.write_bytes(evidence_stream(PROFILE, evidence_row))
        guarded_nfa.write_text(
            "\n".join(guarded_terms) + "\n", encoding="utf-8"
        )
        scalar_prepared = pipeline_api.prepared_new()
        lexical_prepared = pipeline_api.prepared_new()
        prepared_stack.callback(pipeline_api.prepared_free, scalar_prepared)
        prepared_stack.callback(pipeline_api.prepared_free, lexical_prepared)
        if (
            pipeline_api.prepared_build_count(scalar_prepared) != 0
            or pipeline_api.prepared_build_count(lexical_prepared) != 0
        ):
            raise GateFailure("fresh prepared pipeline reported a build")
        pipeline_api.prepare(
            scalar_prepared,
            splitter_abi,
            form_abi,
            guard_nfa,
            evidence,
            compiler_digest,
        )
        pipeline_api.prepare_lexical_shadow(
            lexical_prepared,
            splitter_abi,
            form_abi,
            lexical_nfa,
            guard_nfa,
            evidence,
            guarded_nfa,
            compiler_digest,
            guarded_compiler_digest,
        )
        prepared_build_count = (
            pipeline_api.prepared_build_count(scalar_prepared)
            + pipeline_api.prepared_build_count(lexical_prepared)
        )
        if prepared_build_count != 2:
            raise GateFailure("prepared pipelines were not each built once")
        pipeline_api.prepare(
            scalar_prepared,
            splitter_abi,
            form_abi,
            guard_nfa,
            evidence,
            compiler_digest,
            expect_success=False,
        )
        pipeline_api.prepare_lexical_shadow(
            lexical_prepared,
            splitter_abi,
            form_abi,
            lexical_nfa,
            guard_nfa,
            evidence,
            guarded_nfa,
            compiler_digest,
            guarded_compiler_digest,
            expect_success=False,
        )
        prepared_mutations += 2
        pipeline_api.parse_prepared(
            scalar_prepared,
            CASES[0].payload,
            lexical_shadow=True,
            expect_success=False,
        )
        pipeline_api.parse_prepared(
            lexical_prepared,
            CASES[0].payload,
            lexical_shadow=False,
            expect_success=False,
        )
        prepared_mutations += 2
        pipeline_api.parse_prepared(
            scalar_prepared,
            b"(\xff)",
            lexical_shadow=False,
            expect_success=False,
        )
        pipeline_api.parse_prepared(
            lexical_prepared,
            b"(\xff)",
            lexical_shadow=True,
            expect_success=False,
        )
        prepared_mutations += 2
        run_petta_native_surface(
            petta_root,
            splitter_abi,
            form_abi,
            guard_nfa,
            evidence,
            compiler_digest,
        )
        reflective_surface_agreements = 1

        for case_index, case in enumerate(CASES):
            input_path = directory / f"document-{case_index}.input"
            input_path.write_bytes(case.payload)
            observed = run_splitter_authority(
                splitter_oracle, petta_root, input_path
            )
            decision = "accepted" if case.forms is not None else "rejected"
            if case.forms is None:
                if len(observed) != 1 or observed[0][0] != "error":
                    raise GateFailure(f"splitter accepted negative {case.label}")
                rejected_documents += 1
                expected_forms: tuple[tuple[str, ...], ...] = ()
            else:
                if observed != case.forms:
                    raise GateFailure(f"splitter authority changed for {case.label}")
                accepted_documents += 1
                expected_forms = case.forms

            reference = run_splitter_reference(petta_root, input_path)
            if reference.get("decision") != decision:
                raise GateFailure(
                    f"splitter authority and LanguageDef differ for {case.label}"
                )
            document_agreements += 1

            native = run_native(
                pipeline_binary,
                splitter_abi,
                form_abi,
                guard_nfa,
                evidence,
                input_path,
                compiler_digest,
            )
            assert native is not None
            require_metadata(native, case.payload, decision, len(expected_forms))
            native_api_result = pipeline_api.parse(
                splitter_abi,
                form_abi,
                guard_nfa,
                evidence,
                case.payload,
                compiler_digest,
            )
            assert native_api_result is not None
            native_api, native_api_raw = native_api_result
            if native_api != native:
                raise GateFailure(
                    f"document native API and CLI differ for {case.label}"
                )
            native_api_agreements += 1
            native_api_records.append(
                {
                    "input": case.payload.hex(),
                    "label": case.label,
                    "response": sha256(native_api_raw).hexdigest(),
                }
            )
            shadow_result = pipeline_api.parse_lexical_shadow(
                splitter_abi,
                form_abi,
                lexical_nfa,
                guard_nfa,
                evidence,
                guarded_nfa,
                case.payload,
                compiler_digest,
                guarded_compiler_digest,
            )
            assert shadow_result is not None
            shadow_api, shadow_raw, shadow_receipt = shadow_result
            if shadow_api != native_api or shadow_raw != native_api_raw:
                raise GateFailure(
                    f"document lexical shadow changed response for {case.label}"
                )
            require_lexical_shadow_receipt(
                shadow_receipt, len(expected_forms)
            )
            lexical_shadow_agreements += 1
            lexical_shadow_form_executions += int(
                shadow_receipt["form-executions"]
            )
            lexical_shadow_semantic_agreements += int(
                shadow_receipt["semantic-agreements"]
            )
            lexical_shadow_source_passes += int(
                shadow_receipt["source-passes"]
            )
            lexical_shadow_dfa_scans += int(shadow_receipt["dfa-scans"])
            lexical_shadow_records.append(
                {
                    "input": case.payload.hex(),
                    "label": case.label,
                    "receipt": shadow_receipt,
                    "response": sha256(shadow_raw).hexdigest(),
                }
            )
            prepared_scalar_result = pipeline_api.parse_prepared(
                scalar_prepared,
                case.payload,
                lexical_shadow=False,
            )
            assert prepared_scalar_result is not None
            prepared_scalar, prepared_scalar_raw = prepared_scalar_result
            if (
                prepared_scalar != native_api
                or prepared_scalar_raw != native_api_raw
            ):
                raise GateFailure(
                    f"prepared scalar pipeline changed {case.label}"
                )
            prepared_scalar_agreements += 1
            prepared_lexical_result = pipeline_api.parse_prepared(
                lexical_prepared,
                case.payload,
                lexical_shadow=True,
            )
            assert prepared_lexical_result is not None
            (
                prepared_lexical,
                prepared_lexical_raw,
                prepared_lexical_receipt,
            ) = prepared_lexical_result
            if (
                prepared_lexical != shadow_api
                or prepared_lexical_raw != shadow_raw
                or prepared_lexical_receipt != shadow_receipt
            ):
                raise GateFailure(
                    f"prepared lexical pipeline changed {case.label}"
                )
            prepared_lexical_agreements += 1
            if (
                pipeline_api.prepared_build_count(scalar_prepared) != 1
                or pipeline_api.prepared_build_count(lexical_prepared) != 1
            ):
                raise GateFailure("document parsing rebuilt a prepared pipeline")
            prepared_records.append(
                {
                    "input": case.payload.hex(),
                    "label": case.label,
                    "scalar-response": sha256(prepared_scalar_raw).hexdigest(),
                    "lexical-response": sha256(prepared_lexical_raw).hexdigest(),
                    "lexical-receipt": prepared_lexical_receipt,
                }
            )
            petta_native = run_petta_native(
                petta_root,
                splitter_abi,
                form_abi,
                guard_nfa,
                evidence,
                input_path,
                compiler_digest,
            )
            expected_petta_metadata = {
                "outcome": "completed",
                "decision": decision,
                "input_bytes": len(case.payload),
                "input_scalars": len(scalar_byte_offsets(case.payload)) - 1,
                "source_passes": 1,
                "document_forest_digest": native.metadata[
                    "document-gll-forest-digest"
                ],
                "splitter_pack_digest": SPLITTER_PACK,
                "form_pack_digest": FORM_PACK,
                "guard_plan_digest": EXPECTED_GUARD_PLAN_DIGEST,
                "guard_evidence_digest": EXPECTED_GUARD_EVIDENCE_DIGEST,
                "guard_nfa_answer_digest": EXPECTED_GUARD_NFA_DIGEST,
                "mutation_count": 1,
            }
            for field, value in expected_petta_metadata.items():
                if petta_native.get(field) != value:
                    raise GateFailure(
                        f"PeTTa reflective API changed {field} for {case.label}: "
                        f"{petta_native!r}"
                    )
            petta_native_forms = petta_native.get("forms")
            if (
                not isinstance(petta_native_forms, list)
                or len(petta_native_forms) != len(expected_forms)
                or not isinstance(petta_native.get("document_results"), list)
            ):
                raise GateFailure(
                    f"PeTTa reflective document shape changed for {case.label}"
                )
            reflective_document_agreements += 1
            reflective_records.append(petta_native)
            one_pass_receipts += 1
            offsets = scalar_byte_offsets(case.payload)
            previous_right = 0
            form_records: list[dict[str, object]] = []
            for form_index, (
                native_form,
                expected_form,
                petta_native_form,
            ) in enumerate(
                zip(
                    native.forms,
                    expected_forms,
                    petta_native_forms,
                    strict=True,
                )
            ):
                if not isinstance(petta_native_form, dict):
                    raise GateFailure("PeTTa reflective form is not an object")
                kind, raw_text = expected_form
                raw_bytes = raw_text.encode("utf-8")
                if (
                    native_form.index != form_index
                    or native_form.kind != kind
                    or native_form.raw != raw_bytes
                ):
                    raise GateFailure(f"filtered form changed for {case.label}")
                require_provenance(
                    native_form, case.payload, offsets, previous_right
                )
                expected_reflective_form = {
                    "index": native_form.index,
                    "kind": native_form.kind,
                    "scalar_left": native_form.scalar_left,
                    "scalar_right": native_form.scalar_right,
                    "byte_left": native_form.byte_left,
                    "byte_right": native_form.byte_right,
                    "filtered": native_form.raw.decode("utf-8"),
                    "decision": native_form.decision,
                    "semantic_results": list(native_form.results),
                    "source_passes": native_form.source_passes,
                    "guard_tokens": native_form.guard_tokens,
                    "forest_digest": native_form.forest_digest,
                    "guard_relation_digest": native_form.relation_digest,
                }
                for field, value in expected_reflective_form.items():
                    if petta_native_form.get(field) != value:
                        raise GateFailure(
                            f"PeTTa reflective form changed {field} "
                            f"for {case.label}"
                        )
                reflective_form_agreements += 1
                previous_right = native_form.scalar_right
                raw_path = directory / f"form-{case_index}-{form_index}.input"
                raw_path.write_bytes(raw_bytes)
                form_authority = run_form_authority(
                    form_oracle, petta_root, "form", raw_path
                )
                form_reference = run_petta_reference(petta_root, raw_path)
                expected_form_decision = authority_decision(form_authority)
                if native_form.decision != expected_form_decision:
                    raise GateFailure(
                        f"form authority and native pipeline differ for {case.label}"
                    )
                form_authority_agreements += 1
                if form_reference.get("decision") != expected_form_decision:
                    raise GateFailure(
                        f"form authority and LanguageDef differ for {case.label}"
                    )
                reference_results = tuple(form_reference.get("results") or ())
                if native_form.results != reference_results:
                    raise GateFailure(
                        f"form neutral semantics differ for {case.label}"
                    )
                form_semantic_agreements += 1
                host_results = tuple(form_reference["host_results"])
                host_classifications = tuple(
                    form_reference["host_classifications"]
                )
                classified_path = (
                    directory / f"classified-{case_index}-{form_index}.input"
                )
                classified_path.write_bytes(
                    (b"!" if native_form.kind == "runnable" else b"")
                    + raw_bytes
                )
                classified_authority = run_form_authority(
                    form_oracle, petta_root, "document", classified_path
                )
                if expected_form_decision == "accepted":
                    authority_terms = tuple(
                        observation[1]
                        for observation in form_authority
                        if observation[0] == "term"
                    )
                    if (
                        len(authority_terms) != 1
                        or host_results != authority_terms
                        or len(host_classifications) != 1
                    ):
                        raise GateFailure(
                            f"host term projection differs for {case.label}"
                        )
                    form_host_action_agreements += 1
                    expected_classification = (
                        "runnable"
                        if native_form.kind == "runnable"
                        else host_classifications[0]
                    )
                    expected_document_record = (
                        "form",
                        expected_classification,
                        raw_bytes.decode("utf-8"),
                        host_results[0],
                    )
                    if classified_authority != (expected_document_record,):
                        raise GateFailure(
                            f"document classification differs for {case.label}"
                        )
                    form_classification_agreements += 1
                    classification: str | None = expected_classification
                    if (
                        petta_native_form.get("host_results")
                        != list(host_results)
                        or petta_native_form.get("classifications")
                        != [expected_classification]
                    ):
                        raise GateFailure(
                            f"PeTTa reflective host action differs for {case.label}"
                        )
                    reflective_host_agreements += 1
                else:
                    if (
                        host_results
                        or host_classifications
                        or len(classified_authority) != 1
                        or classified_authority[0][0] != "error"
                    ):
                        raise GateFailure(
                            f"rejected form retained a host action for {case.label}"
                        )
                    if (
                        petta_native_form.get("host_results") != []
                        or petta_native_form.get("classifications") != []
                    ):
                        raise GateFailure(
                            f"PeTTa reflective rejection retained action "
                            f"for {case.label}"
                        )
                    classification = None
                form_executions += 1
                form_records.append(
                    {
                        "decision": native_form.decision,
                        "forest": native_form.forest_digest,
                        "kind": native_form.kind,
                        "raw": native_form.raw.hex(),
                        "relation": native_form.relation_digest,
                        "results": native_form.results,
                        "span": (
                            native_form.scalar_left,
                            native_form.scalar_right,
                            native_form.byte_left,
                            native_form.byte_right,
                        ),
                    }
                )
                action_records.append(
                    {
                        "classification": classification,
                        "decision": expected_form_decision,
                        "host_results": host_results,
                        "kind": native_form.kind,
                        "label": case.label,
                        "raw": native_form.raw.hex(),
                    }
                )
            records.append(
                {
                    "decision": decision,
                    "document_forest": native.metadata[
                        "document-gll-forest-digest"
                    ],
                    "forms": form_records,
                    "input": case.payload.hex(),
                    "label": case.label,
                }
            )

        invalid = directory / "invalid-utf8.input"
        invalid.write_bytes(b"(\xff)")
        run_native(
            pipeline_binary,
            splitter_abi,
            form_abi,
            guard_nfa,
            evidence,
            invalid,
            compiler_digest,
            expect_success=False,
        )
        pipeline_api.parse(
            splitter_abi,
            form_abi,
            guard_nfa,
            evidence,
            b"(\xff)",
            compiler_digest,
            expect_success=False,
        )
        pipeline_api.parse_lexical_shadow(
            splitter_abi,
            form_abi,
            lexical_nfa,
            guard_nfa,
            evidence,
            guarded_nfa,
            b"(\xff)",
            compiler_digest,
            guarded_compiler_digest,
            expect_success=False,
        )
        lexical_shadow_mutations += 1
        first_input = directory / "document-0.input"
        corrupt_lexical_nfa = directory / "corrupt-lexical.nfa"
        corrupt_guarded_nfa = directory / "corrupt-guarded.nfa"
        corrupt_lexical_nfa.write_bytes(b"")
        corrupt_guarded_nfa.write_bytes(b"")
        recovered_prepared = pipeline_api.prepared_new()
        prepared_stack.callback(pipeline_api.prepared_free, recovered_prepared)
        pipeline_api.prepare_lexical_shadow(
            recovered_prepared,
            splitter_abi,
            form_abi,
            corrupt_lexical_nfa,
            guard_nfa,
            evidence,
            guarded_nfa,
            compiler_digest,
            guarded_compiler_digest,
            expect_success=False,
        )
        if pipeline_api.prepared_build_count(recovered_prepared) != 0:
            raise GateFailure("failed preparation left a live configuration")
        pipeline_api.prepare_lexical_shadow(
            recovered_prepared,
            splitter_abi,
            form_abi,
            lexical_nfa,
            guard_nfa,
            evidence,
            guarded_nfa,
            compiler_digest,
            guarded_compiler_digest,
        )
        recovered_result = pipeline_api.parse_prepared(
            recovered_prepared,
            first_input.read_bytes(),
            lexical_shadow=True,
        )
        assert recovered_result is not None
        _, recovered_raw, _ = recovered_result
        if sha256(recovered_raw).hexdigest() != prepared_records[0][
            "lexical-response"
        ]:
            raise GateFailure("recovered preparation changed its first parse")
        prepared_build_count += pipeline_api.prepared_build_count(
            recovered_prepared
        )
        prepared_mutations += 1
        pipeline_api.parse_lexical_shadow(
            splitter_abi,
            form_abi,
            corrupt_lexical_nfa,
            guard_nfa,
            evidence,
            guarded_nfa,
            first_input.read_bytes(),
            compiler_digest,
            guarded_compiler_digest,
            expect_success=False,
        )
        lexical_shadow_mutations += 1
        pipeline_api.parse_lexical_shadow(
            splitter_abi,
            form_abi,
            lexical_nfa,
            guard_nfa,
            evidence,
            corrupt_guarded_nfa,
            first_input.read_bytes(),
            compiler_digest,
            guarded_compiler_digest,
            expect_success=False,
        )
        lexical_shadow_mutations += 1
        wrong_compiler = run_native(
            pipeline_binary,
            splitter_abi,
            form_abi,
            guard_nfa,
            evidence,
            first_input,
            "0" * 64,
        )
        assert wrong_compiler is not None
        wrong_compiler_api_result = pipeline_api.parse(
            splitter_abi,
            form_abi,
            guard_nfa,
            evidence,
            first_input.read_bytes(),
            "0" * 64,
        )
        assert wrong_compiler_api_result is not None
        wrong_compiler_api, _ = wrong_compiler_api_result
        if wrong_compiler_api != wrong_compiler:
            raise GateFailure("wrong-compiler native API and CLI differ")
        wrong_compiler_petta = run_petta_native(
            petta_root,
            splitter_abi,
            form_abi,
            guard_nfa,
            evidence,
            first_input,
            "0" * 64,
        )
        if (
            wrong_compiler_petta.get("outcome") != "completed"
            or wrong_compiler_petta.get("guard_plan_digest")
            == EXPECTED_GUARD_PLAN_DIGEST
        ):
            raise GateFailure(
                "wrong compiler digest preserved the reflective plan seal"
            )
        if (
            wrong_compiler.metadata.get("guard-plan-digest")
            == EXPECTED_GUARD_PLAN_DIGEST
        ):
            raise GateFailure("wrong compiler digest preserved the guard-plan seal")
        mutations = 2
        if arguments.benchmark_prepared:
            payloads = tuple(case.payload for case in CASES)
            payload_bytes = sum(len(payload) for payload in payloads)

            def cold_scalar_batch() -> None:
                for payload in payloads:
                    pipeline_api.parse(
                        splitter_abi,
                        form_abi,
                        guard_nfa,
                        evidence,
                        payload,
                        compiler_digest,
                    )

            def prepared_scalar_batch() -> None:
                for payload in payloads:
                    pipeline_api.parse_prepared(
                        scalar_prepared,
                        payload,
                        lexical_shadow=False,
                    )

            def cold_lexical_batch() -> None:
                for payload in payloads:
                    pipeline_api.parse_lexical_shadow(
                        splitter_abi,
                        form_abi,
                        lexical_nfa,
                        guard_nfa,
                        evidence,
                        guarded_nfa,
                        payload,
                        compiler_digest,
                        guarded_compiler_digest,
                    )

            def prepared_lexical_batch() -> None:
                for payload in payloads:
                    pipeline_api.parse_prepared(
                        lexical_prepared,
                        payload,
                        lexical_shadow=True,
                    )

            scalar_cold_ns, scalar_prepared_ns = benchmark_call_pair(
                cold_scalar_batch,
                prepared_scalar_batch,
                arguments.benchmark_samples,
            )
            lexical_cold_ns, lexical_prepared_ns = benchmark_call_pair(
                cold_lexical_batch,
                prepared_lexical_batch,
                arguments.benchmark_samples,
            )
            if (
                pipeline_api.prepared_build_count(scalar_prepared) != 1
                or pipeline_api.prepared_build_count(lexical_prepared) != 1
            ):
                raise GateFailure("timing rebuilt a prepared pipeline")
            print(
                "(PeTTaDocumentPreparedTimingObservationV1 "
                f"scalar {len(payloads)} {payload_bytes} "
                f"{arguments.benchmark_samples} {scalar_cold_ns} "
                f"{scalar_prepared_ns} "
                f"{scalar_cold_ns / scalar_prepared_ns:.3f})"
            )
            print(
                "(PeTTaDocumentPreparedTimingObservationV1 "
                f"lexical-shadow {len(payloads)} {payload_bytes} "
                f"{arguments.benchmark_samples} {lexical_cold_ns} "
                f"{lexical_prepared_ns} "
                f"{lexical_cold_ns / lexical_prepared_ns:.3f})"
            )

    digest = matrix_digest(records)
    if EXPECTED_MATRIX_DIGEST and digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(f"PeTTa document pipeline matrix changed: {digest}")
    action_digest = matrix_digest(action_records)
    if (
        EXPECTED_ACTION_MATRIX_DIGEST
        and action_digest != EXPECTED_ACTION_MATRIX_DIGEST
    ):
        raise GateFailure(f"PeTTa document action matrix changed: {action_digest}")
    native_api_digest = matrix_digest(native_api_records)
    if (
        EXPECTED_NATIVE_API_MATRIX_DIGEST
        and native_api_digest != EXPECTED_NATIVE_API_MATRIX_DIGEST
    ):
        raise GateFailure(
            f"PeTTa document native API matrix changed: {native_api_digest}"
        )
    reflective_digest = matrix_digest(reflective_records)
    if (
        EXPECTED_REFLECTIVE_API_MATRIX_DIGEST
        and reflective_digest != EXPECTED_REFLECTIVE_API_MATRIX_DIGEST
    ):
        raise GateFailure(
            f"PeTTa reflective document API matrix changed: {reflective_digest}"
        )
    lexical_shadow_digest = matrix_digest(lexical_shadow_records)
    if (
        EXPECTED_LEXICAL_SHADOW_MATRIX_DIGEST
        and lexical_shadow_digest != EXPECTED_LEXICAL_SHADOW_MATRIX_DIGEST
    ):
        raise GateFailure(
            "PeTTa document lexical shadow matrix changed: "
            + lexical_shadow_digest
        )
    prepared_digest = matrix_digest(prepared_records)
    if (
        EXPECTED_PREPARED_MATRIX_DIGEST
        and prepared_digest != EXPECTED_PREPARED_MATRIX_DIGEST
    ):
        raise GateFailure(
            "PeTTa prepared document matrix changed: " + prepared_digest
        )
    if (
        prepared_scalar_agreements != len(CASES)
        or prepared_lexical_agreements != len(CASES)
        or prepared_build_count != 3
        or prepared_mutations != 7
    ):
        raise GateFailure("prepared document lifecycle receipt changed")
    print(
        f"(PeTTaDocumentPipelineV1Summary {len(CASES)} "
        f"{accepted_documents} {rejected_documents} {document_agreements} "
        f"{form_executions} {form_authority_agreements} "
        f"{form_semantic_agreements} {one_pass_receipts} {mutations} "
        f"{digest} 0)"
    )
    print(
        f"(PeTTaDocumentActionBridgeV1Summary {len(action_records)} "
        f"{form_host_action_agreements} {form_classification_agreements} "
        f"{action_digest} 0)"
    )
    print(
        f"(PeTTaDocumentNativeAPIV1Summary {len(CASES)} "
        f"{native_api_agreements} {mutations} {native_api_digest} 0)"
    )
    print(
        f"(PeTTaDocumentNativePeTTaV1Summary {len(CASES)} "
        f"{reflective_document_agreements} {reflective_form_agreements} "
        f"{reflective_host_agreements} {reflective_surface_agreements} "
        f"{mutations} {reflective_digest} 0)"
    )
    print(
        f"(PeTTaDocumentLexicalShadowV1Summary {len(CASES)} "
        f"{lexical_shadow_agreements} {lexical_shadow_form_executions} "
        f"{lexical_shadow_semantic_agreements} "
        f"{lexical_shadow_source_passes} {lexical_shadow_dfa_scans} "
        f"{lexical_shadow_mutations} {EXPECTED_EXECUTION_PLAN_DIGEST} "
        f"{lexical_shadow_digest} 0)"
    )
    print(
        f"(PeTTaDocumentPreparedV1Summary {len(CASES)} "
        f"{prepared_scalar_agreements} {prepared_lexical_agreements} "
        f"{prepared_build_count} {prepared_mutations} "
        f"{prepared_digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
