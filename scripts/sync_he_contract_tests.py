#!/usr/bin/env python3
"""Sync generated HE contract tests from Mettapedia into CeTTa's test tree.

This keeps the local CeTTa runner path cheap and self-contained:
- copy the source `.metta` files into `tests/generated/he_contract/`
- generate simple `.expected` files with one `[()]` per top-level assertion
- never delete extra local files automatically

The preferred source is the Mettapedia artifact export.  The live tree does not
always have that export materialized, so this script falls back to the explicit
checked-in CeTTa seed copy under `tests/support/he_contract_sources/`.  The
fallback keeps regeneration working, but it is not an external authority.
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

from mettapedia_paths import discover_mettapedia_root

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = (
    discover_mettapedia_root()
    / "artifacts"
    / "conformance"
    / "he_contract_tests"
)
FALLBACK_SOURCE = ROOT / "tests" / "support" / "he_contract_sources"
DEFAULT_DEST = ROOT / "tests" / "generated" / "he_contract"


def count_assertions(text: str) -> int:
    return sum(1 for line in text.splitlines() if line.lstrip().startswith("!("))


def expected_text(assertion_count: int) -> str:
    if assertion_count <= 0:
        return ""
    return "\n".join("[()]" for _ in range(assertion_count)) + "\n"


def resolve_source(source: Path) -> Path:
    if source.is_dir():
        return source
    if source == DEFAULT_SOURCE and FALLBACK_SOURCE.is_dir():
        print(
            "Mettapedia HE contract export not found; "
            f"using local seed source {FALLBACK_SOURCE}"
        )
        return FALLBACK_SOURCE
    raise FileNotFoundError(f"source directory not found: {source}")


def sync_contract_tests(source: Path, dest: Path) -> int:
    source = resolve_source(source)

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
