#!/usr/bin/env python3

from __future__ import annotations

"""Porting-only support helpers that invoke native MeTTa/C parser probes."""

import ast
import os
import re
import shlex
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
GRAMMAR_FIXTURE = ROOT / "tests" / "support" / "lib_parse_metamath_grammar_v0.metta"


def parse_label_csv(text: str) -> list[str]:
    return sorted(item for item in text.split(",") if item)


def run_checked(cmd: list[str], cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or f"exit {proc.returncode}")
    return proc


def run_cetta_file(
    bin_path: str,
    metta_path: Path,
    *extra_args: str,
    timeout_s: int = 120,
) -> subprocess.CompletedProcess[str]:
    shell_cmd = (
        f"timeout {timeout_s} {shlex.quote(bin_path)} "
        f"{shlex.quote(str(metta_path.resolve()))}"
    )
    for arg in extra_args:
        shell_cmd += f" {shlex.quote(arg)}"
    return subprocess.run(
        ["bash", "-lc", shell_cmd],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def metta_fixture_string_value(grammar_file: Path | str, name: str) -> str:
    path = Path(grammar_file)
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        rf"\(= \({re.escape(name)}\)\s+\"((?:[^\"\\\\]|\\\\.)*)\"\)",
        re.MULTILINE,
    )
    match = pattern.search(text)
    if not match:
        raise RuntimeError(f"missing string fixture: {name}")
    return ast.literal_eval('"' + match.group(1) + '"')


def support_path_value(path_name: str) -> str:
    return str(Path(metta_fixture_string_value(GRAMMAR_FIXTURE, path_name)).resolve())


def support_summary_value(summary_name: str) -> tuple[int, list[str]]:
    text = GRAMMAR_FIXTURE.read_text(encoding="utf-8")
    pattern = re.compile(
        rf"\(= \({re.escape(summary_name)}\)\s+\(MMDbSummary (\d+) \"([^\"]*)\"\)\)",
        re.MULTILINE,
    )
    match = pattern.search(text)
    if not match:
        raise RuntimeError(f"missing summary fixture: {summary_name}")
    return int(match.group(1)), parse_label_csv(match.group(2))
