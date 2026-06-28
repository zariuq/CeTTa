#!/usr/bin/env python3

from __future__ import annotations

"""Oracle-only mm-lean4 summary comparison for native MeTTa/C parser outputs."""

import re
import shlex
import sys
from pathlib import Path

from lib_parse_metamath_native_probe_support import parse_label_csv
from lib_parse_metamath_native_probe_support import run_cetta_file as run_cetta_file_proc
from lib_parse_metamath_native_probe_support import run_checked
from lib_parse_metamath_native_probe_support import support_path_value
from lib_parse_metamath_native_probe_support import support_summary_value
from lib_parse_metamath_native_probe_support import ROOT
from lib_parse_metamath_native_probe_support import GRAMMAR_FIXTURE

SUMMARY_INPUT = ROOT / ".generated" / "metamath_mmlean4_summary_oracle_input.metta"
SUPPORT_IMPORT = "../tests/support/lib_parse_metamath_grammar_v0.metta"
MMLEAN4_ROOT = ROOT.parent / "metamath" / "mm-lean4"
MMLEAN4_ORACLE = ROOT / "scripts" / "mm_lean4_metamath_oracle.lean"


def run_cetta_file(bin_path: str, metta_path: str) -> None:
    proc = run_cetta_file_proc(bin_path, Path(metta_path))
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or f"exit {proc.returncode}")


def write_cetta_summary_input(path_name: str, summary_name: str) -> Path:
    SUMMARY_INPUT.parent.mkdir(parents=True, exist_ok=True)
    grammar_path = str(GRAMMAR_FIXTURE.resolve())
    SUMMARY_INPUT.write_text(
        "\n".join(
            [
                f"!(import! &self {SUPPORT_IMPORT})",
                "!(import! &self gparse)",
                "",
                "!(assertEqualToResult",
                f"  (gparse:slr-summary \"{grammar_path}\" mm-db-g db)",
                "  ((GParseSLRSummary 43 44 40 39 1 0)))",
                "",
                f"!(assertEqual (mm-db-summary ({path_name})) ({summary_name}))",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return SUMMARY_INPUT


def run_cetta_summary(bin_path: str, path_name: str, summary_name: str) -> None:
    input_path = write_cetta_summary_input(path_name, summary_name)
    shell_cmd = f"{shlex.quote(bin_path)} {shlex.quote(str(input_path))}"
    run_checked(
        ["bash", "-lc", shell_cmd],
        ROOT,
    )


def run_mmlean4_summary(mm_path: str) -> tuple[int, list[str]]:
    quoted_oracle = shlex.quote(str(MMLEAN4_ORACLE))
    quoted_file = shlex.quote(mm_path)
    shell_cmd = f"lake env lean --run {quoted_oracle} {quoted_file}"
    proc = run_checked(["bash", "-lc", shell_cmd], MMLEAN4_ROOT)
    count = None
    labels: list[str] = []
    saw_label_lines = False
    for raw_line in proc.stdout.splitlines():
        line = raw_line.strip()
        if line.startswith("count\t"):
            count = int(line.split("\t", 1)[1])
        elif line.startswith("label\t"):
            labels.append(line.split("\t", 1)[1])
            saw_label_lines = True
        elif line.startswith("labels\t"):
            labels = parse_label_csv(line.split("\t", 1)[1])
    if saw_label_lines:
        labels = sorted(labels)
    if count is None:
        raise RuntimeError(f"missing mm-lean4 summary in output: {proc.stdout}")
    return count, labels


def main() -> int:
    if len(sys.argv) not in (4, 5):
        print(
            "usage: metamath_mmlean4_summary_oracle.py <cetta-bin> <path-symbol> <summary-symbol> [cetta-test-file]",
            file=sys.stderr,
        )
        return 2

    cetta_bin = sys.argv[1]
    path_name = sys.argv[2]
    summary_name = sys.argv[3]
    cetta_test_file = str(Path(sys.argv[4]).resolve()) if len(sys.argv) == 5 else None

    mm_file = support_path_value(path_name)
    expected_count, expected_labels = support_summary_value(summary_name)
    lean_count, lean_labels = run_mmlean4_summary(mm_file)

    if lean_count != expected_count or lean_labels != expected_labels:
        print("MM_ORACLE_MISMATCH", file=sys.stderr)
        print(f"path-symbol={path_name}", file=sys.stderr)
        print(f"summary-symbol={summary_name}", file=sys.stderr)
        print(f"expected-count={expected_count}", file=sys.stderr)
        print(f"lean-count={lean_count}", file=sys.stderr)
        print(f"expected-labels={','.join(expected_labels)}", file=sys.stderr)
        print(f"lean-labels={','.join(lean_labels)}", file=sys.stderr)
        return 1

    run_cetta_summary(cetta_bin, path_name, summary_name)
    if cetta_test_file is not None:
        run_cetta_file(cetta_bin, cetta_test_file)

    print(f"MM_ORACLE_OK\tcount={lean_count}")
    print(f"labels\t{','.join(lean_labels)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
