#!/usr/bin/env python3

from __future__ import annotations

import fcntl
import os
import subprocess
import sys
from pathlib import Path


SKIP_INCOMPATIBLE_TOOLCHAIN = 85


def imported_modules(lean_file: Path) -> list[str]:
    modules: list[str] = []
    seen: set[str] = set()
    for raw_line in lean_file.read_text().splitlines():
        line = raw_line.strip()
        if not line.startswith("import "):
            continue
        for module in line.split()[1:]:
            if module not in seen:
                seen.add(module)
                modules.append(module)
    return modules


def expected_toolchain_file(lean_file: Path) -> Path:
    return lean_file.with_suffix(".toolchain")


def read_expected_toolchain(lean_file: Path) -> str | None:
    toolchain_path = expected_toolchain_file(lean_file)
    if not toolchain_path.is_file():
        return None
    try:
        text = toolchain_path.read_text(encoding="utf-8").strip()
    except OSError as exc:
        raise ValueError(str(exc)) from exc
    if not text:
        raise ValueError(f"empty expected toolchain file: {toolchain_path}")
    return text


def check_toolchain_compatibility(mettapedia_root: Path, lean_file: Path) -> int | None:
    expected = read_expected_toolchain(lean_file)
    if expected is None:
        return None

    actual_path = mettapedia_root / "lean-toolchain"
    if not actual_path.is_file():
        print(
            "SKIP: rhocalc lean microcheck incompatible Mettapedia checkout "
            f"(missing {actual_path})",
            file=sys.stderr,
        )
        return SKIP_INCOMPATIBLE_TOOLCHAIN

    try:
        actual = actual_path.read_text(encoding="utf-8").strip()
    except OSError as exc:
        print(
            "SKIP: rhocalc lean microcheck incompatible Mettapedia checkout "
            f"({exc})",
            file=sys.stderr,
        )
        return SKIP_INCOMPATIBLE_TOOLCHAIN

    if not actual:
        print(
            "SKIP: rhocalc lean microcheck incompatible Mettapedia checkout "
            f"(empty {actual_path})",
            file=sys.stderr,
        )
        return SKIP_INCOMPATIBLE_TOOLCHAIN

    if actual != expected:
        print(
            "SKIP: rhocalc lean microcheck incompatible Mettapedia toolchain "
            f"(expected {expected}, found {actual})",
            file=sys.stderr,
        )
        return SKIP_INCOMPATIBLE_TOOLCHAIN

    return None


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "usage: rhocalc_lean_microcheck.py <mettapedia-root> <lean-file>",
            file=sys.stderr,
        )
        return 2

    mettapedia_root = Path(sys.argv[1]).resolve()
    lean_file = Path(sys.argv[2]).resolve()

    if not mettapedia_root.is_dir():
        print(f"missing Mettapedia root: {mettapedia_root}", file=sys.stderr)
        return 2
    if not lean_file.is_file():
        print(f"missing Lean microcheck file: {lean_file}", file=sys.stderr)
        return 2
    try:
        compatibility = check_toolchain_compatibility(mettapedia_root, lean_file)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    if compatibility is not None:
        return compatibility

    build_targets = imported_modules(lean_file)
    if not build_targets:
        build_targets = ["Mettapedia.Languages.ProcessCalculi.RhoCalculus.OperationalBridge"]

    env = os.environ.copy()
    lock_dir = mettapedia_root / ".lake"
    lock_dir.mkdir(parents=True, exist_ok=True)
    lock_path = lock_dir / "rhocalc_lean_microcheck.lock"

    with lock_path.open("w", encoding="utf-8") as lock_file:
        fcntl.flock(lock_file, fcntl.LOCK_EX)

        for target in build_targets:
            build = subprocess.run(
                ["lake", "build", target],
                cwd=str(mettapedia_root),
                env=env,
                check=False,
                text=True,
                capture_output=True,
            )
            if build.returncode != 0:
                sys.stderr.write(build.stdout)
                sys.stderr.write(build.stderr)
                return build.returncode

        proc = subprocess.run(
            ["lake", "env", "lean", str(lean_file)],
            cwd=str(mettapedia_root),
            env=env,
            check=False,
            text=True,
            capture_output=True,
        )
        if proc.returncode == 0:
            return 0

        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
