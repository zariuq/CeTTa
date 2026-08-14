#!/usr/bin/env python3
"""Check ordered rule-name parity between the v3 Lean modules and langdef."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

import gslt2parse_schema_v1 as sx


class GateFailure(RuntimeError):
    pass


LEAN_RULE_BLOCK = re.compile(
    r"def LangdefRule\.name\s*:[^\n]*\n"
    r"(?P<body>.*?)\n\s*def langdefRules\s*:",
    flags=re.DOTALL,
)
LEAN_RULE_ROW = re.compile(
    r'^\s*\|\s+\.[A-Za-z][A-Za-z0-9_]*\s+=>\s+"([^"]+)"\s*$',
    flags=re.MULTILINE,
)
LEAN_SEAM_BLOCK = re.compile(
    r"def SeamRule\.name\s*:[^\n]*\n"
    r"(?P<body>.*?)\n\s*def seamRules\s*:",
    flags=re.DOTALL,
)


def reject_duplicates(names: list[str], label: str) -> None:
    seen: set[str] = set()
    duplicates: list[str] = []
    for name in names:
        if name in seen and name not in duplicates:
            duplicates.append(name)
        seen.add(name)
    if duplicates:
        raise GateFailure(f"{label} has duplicate rules: {', '.join(duplicates)}")


def lean_rule_names(path: Path, block_pattern: re.Pattern[str], label: str) -> list[str]:
    text = path.read_text(encoding="utf-8")
    block = block_pattern.search(text)
    if block is None:
        raise GateFailure(f"{label} omits its ordered rule-name inventory")
    names = LEAN_RULE_ROW.findall(block.group("body"))
    if not names:
        raise GateFailure(f"{label} rule-name inventory is empty")
    reject_duplicates(names, f"{label} inventory")
    return names


def metta_rule_names(path: Path) -> list[str]:
    presentation = sx.parse_presentation(path)
    names = [rule.name for rule in presentation.rules]
    if not names:
        raise GateFailure("MeTTa presentation has no rules")
    reject_duplicates(names, "MeTTa presentation")
    return names


def require_ordered_parity(lean_names: list[str], metta_names: list[str]) -> None:
    if lean_names == metta_names:
        return
    shared = min(len(lean_names), len(metta_names))
    mismatch = next(
        (index for index in range(shared)
         if lean_names[index] != metta_names[index]),
        shared,
    )
    lean_at = lean_names[mismatch] if mismatch < len(lean_names) else "<missing>"
    metta_at = metta_names[mismatch] if mismatch < len(metta_names) else "<missing>"
    raise GateFailure(
        "ordered H7 parity failed at rule "
        f"{mismatch}: Lean={lean_at}, MeTTa={metta_at}; "
        f"Lean count={len(lean_names)}, MeTTa count={len(metta_names)}"
    )


def check_negative_canaries(names: list[str]) -> None:
    if len(names) < 2:
        raise GateFailure("H7 inventory is too small for mutation canaries")
    mutations = (
        names[:-1],
        [names[1], names[0], *names[2:]],
        [names[0], *names],
    )
    for mutation in mutations:
        try:
            require_ordered_parity(names, mutation)
        except GateFailure:
            continue
        raise GateFailure("H7 negative mutation was not detected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lean-core", type=Path, required=True)
    parser.add_argument("--lean-seam", type=Path, required=True)
    parser.add_argument("--langdef", type=Path, required=True)
    arguments = parser.parse_args()

    core_names = lean_rule_names(
        arguments.lean_core, LEAN_RULE_BLOCK, "Lean core")
    seam_names = lean_rule_names(
        arguments.lean_seam, LEAN_SEAM_BLOCK, "Lean seam")
    lean_names = [*core_names, *seam_names]
    reject_duplicates(lean_names, "combined Lean inventory")
    metta_names = metta_rule_names(arguments.langdef)
    require_ordered_parity(lean_names, metta_names)
    check_negative_canaries(lean_names)
    print(
        "(PettaTypecheckV3CoreParityV1Summary "
        f"core-rules={len(core_names)} seam-rules={len(seam_names)} "
        f"ordered-rules={len(lean_names)} negative-canaries=3)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError, sx.SchemaError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
