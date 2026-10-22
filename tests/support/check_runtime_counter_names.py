#!/usr/bin/env python3
"""Each runtime counter is reported under the name of its own enumerator.

`runtime-stats!` and `--emit-runtime-stats` print CETTA_RUNTIME_COUNTER_NAMES[i]
beside counters[i], so the name table must follow the enum position by
position. A table that drifts (for example when two branches append counters
at the same place) silently reports one gauge under another's name.

A name is the enumerator's suffix in lower kebab case, except for the
spellings listed in ESTABLISHED_SPELLINGS, which predate this check.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ESTABLISHED_SPELLINGS = {
    "REGISTRY_RESOLVE_HIT": "resolve-registry-hit",
    "REGISTRY_RESOLVE_REWRITE": "resolve-registry-rewrite",
    "REGISTRY_RESOLVE_NOOP": "resolve-registry-noop",
}


def enum_positions(header: str) -> dict[int, str]:
    body = re.search(r"typedef enum \{(.*?)\} CettaRuntimeCounter;", header, re.S)
    if not body:
        raise AssertionError("CettaRuntimeCounter enum not found")
    positions, current = {}, -1
    for name, value in re.findall(
            r"CETTA_RUNTIME_COUNTER_([A-Z0-9_]+)\s*(?:=\s*(\d+))?\s*,", body.group(1)):
        if name == "COUNT":
            continue
        current = int(value) if value else current + 1
        if current in positions:
            raise AssertionError(f"two enumerators at {current}: {positions[current]}, {name}")
        positions[current] = name
    return positions


def table_names(source: str) -> list[str]:
    body = re.search(
        r"CETTA_RUNTIME_COUNTER_NAMES\[CETTA_RUNTIME_COUNTER_COUNT\] = \{(.*?)\n\};",
        source, re.S)
    if not body:
        raise AssertionError("CETTA_RUNTIME_COUNTER_NAMES not found")
    return re.findall(r'"([^"]*)"', body.group(1))


def main() -> int:
    positions = enum_positions((ROOT / "src/generated/cetta_runtime_counters.generated.h").read_text())
    names = table_names((ROOT / "src/generated/cetta_runtime_counter_names.generated.h").read_text())
    errors = []
    if sorted(positions) != list(range(len(positions))):
        errors.append("enumerators are not dense from zero")
    if len(names) != len(positions):
        errors.append(f"{len(names)} names for {len(positions)} counters")
    for index, name in enumerate(names):
        enumerator = positions.get(index)
        if enumerator is None:
            continue
        expected = ESTABLISHED_SPELLINGS.get(
            enumerator, enumerator.lower().replace("_", "-"))
        if name != expected:
            errors.append(f"counter {index} ({enumerator}) is reported as "
                          f"{name!r}, expected {expected!r}")
    if errors:
        print("FAIL: runtime counter names do not follow the enum", file=sys.stderr)
        for error in errors[:40]:
            print("  " + error, file=sys.stderr)
        if len(errors) > 40:
            print(f"  ... {len(errors) - 40} more", file=sys.stderr)
        return 1
    print(f"PASS: {len(names)} runtime counters are reported under their own names")
    return 0


if __name__ == "__main__":
    sys.exit(main())
