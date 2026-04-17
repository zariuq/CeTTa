#!/usr/bin/env python3
"""Sync generated HE contract tests from Mettapedia into CeTTa's test tree.

This keeps the local CeTTa runner path cheap and self-contained:
- copy the generated `.metta` files into `tests/generated/he_contract/`
- generate simple `.expected` files with one `[()]` per top-level assertion
- never delete extra local files automatically
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
DEFAULT_SOURCE = (
    WORKSPACE
    / "lean-projects"
    / "mettapedia"
    / "artifacts"
    / "conformance"
    / "he_contract_tests"
)
DEFAULT_DEST = ROOT / "tests" / "generated" / "he_contract"


def count_assertions(text: str) -> int:
    return sum(1 for line in text.splitlines() if line.lstrip().startswith("!("))


def expected_text(assertion_count: int) -> str:
    if assertion_count <= 0:
        return ""
    return "\n".join("[()]" for _ in range(assertion_count)) + "\n"


def sync_contract_tests(source: Path, dest: Path) -> int:
    if not source.is_dir():
        raise FileNotFoundError(f"source directory not found: {source}")

    dest.mkdir(parents=True, exist_ok=True)
    synced = 0
    source_names: set[str] = set()

    for src in sorted(source.glob("*.metta")):
        text = src.read_text(encoding="utf-8")
        source_names.add(src.name)
        shutil.copyfile(src, dest / src.name)
        (dest / f"{src.stem}.expected").write_text(
            expected_text(count_assertions(text)),
            encoding="utf-8",
        )
        synced += 1

    extras = sorted(
        path.name
        for path in dest.iterdir()
        if path.is_file()
        and path.suffix in {".metta", ".expected"}
        and (
            (path.suffix == ".metta" and path.name not in source_names)
            or (
                path.suffix == ".expected"
                and f"{path.stem}.metta" not in source_names
            )
        )
    )

    print(f"synced {synced} HE contract test files into {dest}")
    if extras:
        print("left extra local files untouched:")
        for name in extras:
            print(f"  {name}")
    return synced


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--dest", type=Path, default=DEFAULT_DEST)
    args = parser.parse_args()
    sync_contract_tests(args.source, args.dest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
