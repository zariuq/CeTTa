#!/usr/bin/env python3

from __future__ import annotations

"""Oracle-only sealed Metamath regression gate for the native MeTTa/C parser lane."""

import sys
from pathlib import Path

from lib_parse_metamath_native_probe_support import metta_fixture_string_value
from lib_parse_metamath_native_probe_support import run_cetta_file as run_cetta_file_proc
from lib_parse_metamath_native_probe_support import run_checked
from lib_parse_metamath_native_probe_support import ROOT
from lib_parse_metamath_native_probe_support import GRAMMAR_FIXTURE


SUMMARY_ORACLE = ROOT / "scripts" / "metamath_mmlean4_summary_oracle.py"

DIRECT_CASES = [
    ("grammar-core", "tests/test_lib_parse_metamath_grammar_v0.metta"),
    ("raw-stmt-lexer", "tests/test_lib_parse_metamath_raw_stmt_v0.metta"),
    ("theorem-normal", "tests/test_lib_parse_metamath_theorem_normal_v0.metta"),
    (
        "theorem-compressed",
        "tests/test_lib_parse_metamath_theorem_compressed_v0.metta",
    ),
    # Grammar-only negative: this fixture deliberately redeclares active
    # variables.  The parser must summarize it, while a Metamath verifier must
    # reject it rather than treating it as a valid database oracle.
    ("repeat-vars-grammar-only", "tests/test_lib_parse_metamath_repeat_vars_db_v0.metta"),
]

ORACLE_CASES = [
    (
        "demo0-block",
        "mm-demo0-block-path",
        "mm-demo0-block-summary",
        "tests/test_lib_parse_metamath_block_db_v0.metta",
    ),
    (
        "mini-thm",
        "mm-mini-thm-path",
        "mm-mini-thm-summary",
        "tests/test_lib_parse_metamath_mini_thm_db_v0.metta",
    ),
    (
        "mini-thm-compressed",
        "mm-mini-thm-compressed-path",
        "mm-mini-thm-compressed-summary",
        "tests/test_lib_parse_metamath_mini_thm_compressed_db_v0.metta",
    ),
    (
        "mmtest-toplevel-e",
        "mm-mmtest-toplevel-e-path",
        "mm-mmtest-toplevel-e-summary",
        "tests/test_lib_parse_metamath_mmtest_toplevel_e_db_v0.metta",
    ),
    (
        "mmtest-d-before-float",
        "mm-mmtest-d-before-float-path",
        "mm-mmtest-d-before-float-summary",
        "tests/test_lib_parse_metamath_mmtest_d_before_float_db_v0.metta",
    ),
    (
        "mmtest-compressed-z",
        "mm-mmtest-compressed-z-path",
        "mm-mmtest-compressed-z-summary",
        "tests/test_lib_parse_metamath_mmtest_compressed_z_db_v0.metta",
    ),
    (
        "mmtest-min-found",
        "mm-mmtest-min-found-path",
        "mm-mmtest-min-found-summary",
        "tests/test_lib_parse_metamath_mmtest_min_found_db_v0.metta",
    ),
    (
        "core-anatomy",
        "mm-core-anatomy-path",
        "mm-core-anatomy-summary",
        "tests/test_lib_parse_metamath_anatomy_db_v0.metta",
    ),
    (
        "core-local-var-good",
        "mm-core-local-var-good-path",
        "mm-core-local-var-good-summary",
        "tests/test_lib_parse_metamath_local_var_good_db_v0.metta",
    ),
    (
        "mmverify-nested-scope",
        "mm-mmverify-nested-scope-path",
        "mm-mmverify-nested-scope-summary",
        "tests/test_lib_parse_metamath_nested_scope_db_v0.metta",
    ),
    (
        "mmverify-dv-scope",
        "mm-mmverify-dv-scope-path",
        "mm-mmverify-dv-scope-summary",
        "tests/test_lib_parse_metamath_dv_scope_db_v0.metta",
    ),
    (
        "mmverify-dv-multivar",
        "mm-mmverify-dv-multivar-path",
        "mm-mmverify-dv-multivar-summary",
        "tests/test_lib_parse_metamath_dv_multivar_db_v0.metta",
    ),
]

def support_path_value(path_name: str) -> str:
    return str(Path(metta_fixture_string_value(GRAMMAR_FIXTURE, path_name)).resolve())


def run_cetta_file(bin_path: str, metta_path: Path) -> None:
    proc = run_cetta_file_proc(bin_path, metta_path)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or f"exit {proc.returncode}")


def run_direct_case(bin_path: str, label: str, rel_path: str) -> None:
    run_cetta_file(bin_path, ROOT / rel_path)
    print(f"PASS\t{label}")


def run_oracle_case(
    bin_path: str,
    label: str,
    path_name: str,
    summary_name: str,
    test_rel_path: str,
) -> None:
    proc = run_checked(
        [
            "python3",
            str(SUMMARY_ORACLE),
            bin_path,
            path_name,
            summary_name,
            str((ROOT / test_rel_path).resolve()),
        ],
        ROOT,
    )
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    summary_line = next((line for line in lines if line.startswith("MM_ORACLE_OK")), None)
    if summary_line is None:
        raise RuntimeError(
            f"missing MM_ORACLE_OK line for {label}: {proc.stdout.strip()}"
        )
    print(f"PASS\t{label}\t{summary_line}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: lib_parse_metamath_sealed_oracle.py <cetta-bin>", file=sys.stderr)
        return 2

    bin_path = sys.argv[1]
    total = 0

    for label, rel_path in DIRECT_CASES:
        run_direct_case(bin_path, label, rel_path)
        total += 1

    for label, path_name, summary_name, test_rel_path in ORACLE_CASES:
        run_oracle_case(bin_path, label, path_name, summary_name, test_rel_path)
        total += 1

    print(f"SEALED_OK\tcount={total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
