#!/usr/bin/env python3

from __future__ import annotations

"""Oracle-only reference comparison for WIP lib_parse rho parser outputs.

Production rho <-> mrho translator checks live in the rhocalc runtime test lane.
This script keeps the bounded lib_parse/gparse research lane honest against the
runtime semantics.
"""

import ast
import os
import subprocess
import sys
from pathlib import Path

from rhocalc_bounded_bisimulation import bounded_barbed_bisimilar, normalize_file
from rhocalc_bounded_reachability import normalize_expr, run_cmd


ROOT = Path(__file__).resolve().parent.parent
SUPPORT_IMPORT = "./tests/support/rhocalc_lib_parse_translator_v3.metta"


def run_metta_expr(bin_path: str, expr: str) -> str:
    proc = subprocess.run(
        [
            bin_path,
            "-e",
            f"!(import! &self {SUPPORT_IMPORT})",
            "-e",
            f"!(println! {expr})",
        ],
        check=False,
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    payload = [line for line in lines if line != "[()]"]
    if len(payload) != 1:
        raise RuntimeError(
            f"expected one payload line from {expr}, got {len(payload)}: {payload}"
        )
    return payload[0]

def decode_metta_string(text: str) -> str:
    try:
        decoded = ast.literal_eval(text)
    except (SyntaxError, ValueError) as exc:
        if (
            text.startswith("(Err ")
            or text.startswith("(Rejected ")
            or text.startswith("(Error ")
        ):
            raise RuntimeError(f"expected native .mrho text payload, got: {text}") from exc
        if text == "rho:nil" or text.startswith("(rho:"):
            return text
        raise RuntimeError(f"expected native .mrho text payload, got: {text}") from exc
    if not isinstance(decoded, str):
        raise RuntimeError(f"expected native .mrho text payload, got: {text}")
    return decoded

def generated_mrho(bin_path: str, expr: str) -> str:
    payload = run_metta_expr(bin_path, f"(rho-canon-result->mrho-text {expr})")
    return decode_metta_string(payload)


def runtime_normalize_mrho(bin_path: str, expr: str) -> str:
    proc = run_cmd(
        [
            bin_path,
            "--translate",
            "--lang",
            "rhocalc",
            "--syntax",
            "mrho",
            "--lang",
            "rhocalc",
            "--syntax",
            "mrho",
            "-e",
            expr,
        ]
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return normalize_expr(bin_path, "mrho", proc.stdout.strip())


def check_bisim(
    bin_path: str,
    name: str,
    lhs_expr: str,
    rhs_syntax: str,
    rhs_fixture: str,
    depth: int,
    expect: str,
) -> None:
    lhs_mrho = generated_mrho(bin_path, lhs_expr)
    lhs = runtime_normalize_mrho(bin_path, lhs_mrho)
    rhs = normalize_file(bin_path, rhs_syntax, str(ROOT / rhs_fixture))
    ok, detail = bounded_barbed_bisimilar(bin_path, lhs, rhs, depth, {}, {})
    expected = expect == "yes"
    if ok != expected:
        message = [
            f"{name}: expected {expect}, got {'yes' if ok else 'no'}",
            f"generated-mrho: {lhs_mrho}",
            f"rhs-syntax: {rhs_syntax}",
            f"rhs-fixture: {rhs_fixture}",
        ]
        if detail:
            message.append(detail)
        raise RuntimeError("\n".join(message))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: rhocalc_lib_parse_oracle.py <bin>", file=sys.stderr)
        return 2

    bin_path = sys.argv[1]

    cases = [
        (
            "pure-surface",
            '(rho-file->canon "tests/rhocalc/pure_surface.rho")',
            "rho",
            "tests/rhocalc/pure_surface.rho",
            2,
            "yes",
        ),
        (
            "shadowing",
            '(rho-file->canon "tests/rhocalc/surface_shadowing.rho")',
            "rho",
            "tests/rhocalc/surface_shadowing.rho",
            3,
            "yes",
        ),
        (
            "commented-quote-inner",
            '(rho-file->canon "tests/rhocalc/surface_quote_inner_binder_restores_scope.rho")',
            "rho",
            "tests/rhocalc/surface_quote_inner_binder_restores_scope.rho",
            2,
            "yes",
        ),
        (
            "surface-name-output",
            '(rho-file->canon "tests/rhocalc/surface_name_output.rho")',
            "rho",
            "tests/rhocalc/surface_name_output.rho",
            1,
            "yes",
        ),
        (
            "surface-open-name-variable",
            '(rho-file->canon "tests/rhocalc/surface_open_name_variable.rho")',
            "rho",
            "tests/rhocalc/surface_open_name_variable.rho",
            1,
            "yes",
        ),
        (
            "quote-seals-outer-binder",
            '(rho-file->canon "tests/rhocalc/surface_quote_seals_outer_binder.rho")',
            "rho",
            "tests/rhocalc/surface_quote_seals_outer_binder.rho",
            1,
            "yes",
        ),
        (
            "paper-reflection-name-generation",
            '(rho-file->canon "tests/rhocalc/paper_reflection_name_generation_h4.rho")',
            "rho",
            "tests/rhocalc/paper_reflection_name_generation_h4.rho",
            1,
            "yes",
        ),
        (
            "pure-surface-core-witness",
            '(rho-file->canon "tests/rhocalc/pure_surface.rho")',
            "mrho",
            "tests/rhocalc/core_comm.mrho",
            2,
            "yes",
        ),
        (
            "shadowing-core-witness",
            '(rho-file->canon "tests/rhocalc/surface_shadowing.rho")',
            "mrho",
            "tests/rhocalc/mrho_shadow_same_spelling.mrho",
            3,
            "yes",
        ),
        (
            "quote-inner-core-witness",
            '(rho-file->canon "tests/rhocalc/surface_quote_inner_binder_restores_scope.rho")',
            "mrho",
            "tests/rhocalc/mrho_quote_inner_binder_restores_scope.mrho",
            2,
            "yes",
        ),
        (
            "quote-seals-core-witness",
            '(rho-file->canon "tests/rhocalc/surface_quote_seals_outer_binder.rho")',
            "mrho",
            "tests/rhocalc/mrho_quote_seals_outer_binder.mrho",
            1,
            "yes",
        ),
        (
            "reflection-core-witness",
            '(rho-file->canon "tests/rhocalc/paper_reflection_name_generation_h4.rho")',
            "mrho",
            "tests/rhocalc/paper_reflection_name_generation_h4.mrho",
            1,
            "yes",
        ),
        (
            "surface-name-output-core-witness",
            '(rho-file->canon "tests/rhocalc/surface_name_output.rho")',
            "mrho",
            "tests/rhocalc/name_quote_drop.mrho",
            2,
            "yes",
        ),
        (
            "open-name-core-witness",
            '(rho-file->canon "tests/rhocalc/surface_open_name_variable.rho")',
            "mrho",
            "tests/rhocalc/open_name_variable_roundtrip_h7.mrho",
            1,
            "yes",
        ),
        (
            "send-vs-comm-negative",
            '(rho-text->canon "@{0}!(0)")',
            "rho",
            "tests/rhocalc/pure_surface.rho",
            2,
            "no",
        ),
        (
            "open-name-vs-core-comm-negative",
            '(rho-file->canon "tests/rhocalc/surface_open_name_variable.rho")',
            "mrho",
            "tests/rhocalc/core_comm.mrho",
            2,
            "no",
        ),
        (
            "surface-name-output-vs-name-pass-negative",
            '(rho-file->canon "tests/rhocalc/surface_name_output.rho")',
            "mrho",
            "tests/rhocalc/name_pass_basic_h4.mrho",
            2,
            "no",
        ),
        (
            "quote-seals-vs-quote-inner-negative",
            '(rho-file->canon "tests/rhocalc/surface_quote_seals_outer_binder.rho")',
            "mrho",
            "tests/rhocalc/mrho_quote_inner_binder_restores_scope.mrho",
            2,
            "no",
        ),
    ]

    try:
        for name, lhs_expr, rhs_syntax, rhs_fixture, depth, expect in cases:
            check_bisim(bin_path, name, lhs_expr, rhs_syntax, rhs_fixture, depth, expect)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
