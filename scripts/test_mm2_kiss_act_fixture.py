#!/usr/bin/env python3
"""Run the ACT join control with a fresh, isolated fixture."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f"expected one fixture marker: {old}")
    return text.replace(old, new, 1)


def resolve_command(command: list[str]) -> list[str]:
    executable = Path(command[0])
    if executable.is_file():
        return [str(executable.resolve()), *command[1:]]
    return command


def prepare_fixture(command: list[str]) -> tuple[Path, Path]:
    """Prepare fixture inputs without including their setup in query timing."""
    command = resolve_command(command)
    root = Path(__file__).resolve().parent.parent
    runtime = root / "runtime"
    runtime.mkdir(exist_ok=True)
    fixture_dir = Path(tempfile.mkdtemp(prefix="mm2-kiss-", dir=runtime))
    fixture = fixture_dir / "fruit_colors.act"
    prepare = replace_once(
        (root / "tests/support/prepare_mm2_kiss_fruit_colors_act.metta").read_text(),
        '"/dev/shm/cetta_mm2_fruit_colors.act"',
        json.dumps(str(fixture)),
    )
    prepare_path = fixture_dir / "prepare.metta"
    prepare_path.write_text(prepare)

    # MORK's ACT resolver prepends /dev/shm/. The process-relative path keeps
    # the resource name short while each invocation owns its fixture directory.
    join = replace_once(
        (root / "tests/mm2_kiss_act_join.mm2").read_text(),
        "(ACT cetta_mm2_fruit_colors ",
        "(ACT ../../proc/self/cwd/fruit_colors ",
    )
    join_path = fixture_dir / "join.mm2"
    join_path.write_text(join)
    prepared = subprocess.run(
        command + ["--quiet", "--profile", "extended", "--lang", "he", str(prepare_path)],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if prepared.returncode or prepared.stdout or not fixture.is_file():
        raise RuntimeError(f"mm2 KISS ACT prepare failed: {prepared.stdout}")
    return fixture_dir, join_path


def main() -> int:
    command = sys.argv[1:]
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        raise ValueError("a CeTTa command is required")
    command = resolve_command(command)
    fixture_dir, join_path = prepare_fixture(command)
    joined = subprocess.run(
        command + ["--lang", "mm2", str(join_path)],
        cwd=fixture_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    sys.stdout.write(joined.stdout)
    return joined.returncode


if __name__ == "__main__":
    raise SystemExit(main())
