#!/usr/bin/env python3
"""Derive boundary probes from production capacity rules and exercise them."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import os
import re
import subprocess
import sys
from io import StringIO
from pathlib import Path
from typing import Any

from cetta_readiness_model import ReadinessModelError, sha256_file


ROOT = Path(__file__).resolve().parent.parent
SPACE_SOURCE = ROOT / "src/space.c"
TERM_SOURCE = ROOT / "src/term_universe.c"
TERM_TEST = ROOT / "tests/test_term_universe_store_abi.c"
BACKEND_SCRIPT = ROOT / "scripts/bench_space_backend_matrix.sh"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Exercise just-below/at/above probes derived from C capacity rules."
    )
    parser.add_argument(
        "--binary",
        type=Path,
        default=Path(
            os.environ.get(
                "CETTA_BIN", ROOT / "runtime/cetta-main-runtime-stats"
            )
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "runtime/main_readiness_thresholds_current.json",
    )
    return parser.parse_args()


def exactly_one(pattern: str, text: str, label: str) -> re.Match[str]:
    matches = list(re.finditer(pattern, text, flags=re.MULTILINE | re.DOTALL))
    if len(matches) != 1:
        raise ReadinessModelError(
            f"{label}: expected one source match, found {len(matches)}"
        )
    return matches[0]


def derive_thresholds() -> dict[str, Any]:
    space = SPACE_SOURCE.read_text(encoding="utf-8")
    term = TERM_SOURCE.read_text(encoding="utf-8")
    tests = TERM_TEST.read_text(encoding="utf-8")

    initial_capacity = int(
        exactly_one(
            r"CettaIndex new_cap = s->native\.cap \? s->native\.cap : ([0-9]+);",
            space,
            "space initial capacity",
        ).group(1)
    )
    exactly_one(
        r"while \(new_cap < min_cap\)\s*new_cap \*= 2;",
        space,
        "space doubling rule",
    )
    if initial_capacity <= 1:
        raise ReadinessModelError("space initial capacity must exceed one")

    headroom_divisor = int(
        exactly_one(
            r"uint64_t headroom = capacity / ([0-9]+)u;",
            term,
            "term-universe migration headroom",
        ).group(1)
    )
    exactly_one(
        r"if \(headroom == 0\)\s*headroom = 1u;",
        term,
        "term-universe minimum headroom",
    )
    test_body = exactly_one(
        r"static void test_compact_ceiling_migrate_then_continue_witness\(void\) \{(.*?)\n\}",
        tests,
        "compact migration threshold test",
    ).group(1)
    capacity_override = int(
        exactly_one(
            r"term_universe_diag_set_atom_id_capacity_override\(&universe, ([0-9]+)\);",
            test_body,
            "compact migration test capacity",
        ).group(1)
    )
    below_insertions = int(
        exactly_one(
            r"for \(int i = 0; i < ([0-9]+); i\+\+\)",
            test_body,
            "compact migration test below-threshold insertions",
        ).group(1)
    )
    crossing_value = int(
        exactly_one(
            r"atom_int\(&scratch, ([0-9]+)\)\);\s*assert\(widened",
            test_body,
            "compact migration test crossing insertion",
        ).group(1)
    )
    headroom = max(1, capacity_override // headroom_divisor)
    migration_threshold = capacity_override - headroom
    if below_insertions != migration_threshold or crossing_value != migration_threshold:
        raise ReadinessModelError(
            "compact migration test no longer straddles the production threshold"
        )

    space_points = sorted(
        {
            initial_capacity - 1,
            initial_capacity,
            initial_capacity + 1,
            initial_capacity * 2 - 1,
            initial_capacity * 2,
            initial_capacity * 2 + 1,
        }
    )
    return {
        "space_initial_capacity": initial_capacity,
        "space_growth_factor": 2,
        "space_probe_points": space_points,
        "term_migration_headroom_divisor": headroom_divisor,
        "term_test_capacity_override": capacity_override,
        "term_migration_threshold": migration_threshold,
        "term_test_below_insertions": below_insertions,
        "term_test_crossing_value": crossing_value,
    }


def parse_backend_row(output: str) -> dict[str, str]:
    lines = [
        line
        for line in output.splitlines()
        if line.startswith("kind\t") or line.startswith("backend\t")
    ]
    if len(lines) != 2:
        raise ReadinessModelError("threshold probe did not emit one backend row")
    row = next(csv.DictReader(StringIO("\n".join(lines)), delimiter="\t"))
    return dict(row)


def run_space_probe(binary: Path, size: int) -> dict[str, Any]:
    environment = os.environ.copy()
    environment["CETTA_BIN"] = str(binary)
    environment["CETTA_BENCH_EMIT_RUNTIME_STATS"] = "1"
    result = subprocess.run(
        [str(BACKEND_SCRIPT), "native", str(size), "1", "insert_only"],
        cwd=ROOT,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        raise ReadinessModelError(
            f"native threshold probe {size} exited {result.returncode}"
        )
    row = parse_backend_row(result.stdout)
    failures: list[str] = []
    if int(row["count"]) != size:
        failures.append(f"row count {row['count']} != {size}")
    if int(row["term_universe_inserts"]) < size:
        failures.append("term-universe insertion count is below input size")
    if any(int(row[name]) != 0 for name in (
        "pathmap_direct_store", "mork_add_call", "mork_add_batch_items"
    )):
        failures.append("native boundary probe used a non-native storage route")
    return {"size": size, "row": row, "failures": failures}


def main() -> int:
    args = parse_args()
    binary = args.binary.expanduser().resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print(f"FAIL: missing executable stats binary: {binary}", file=sys.stderr)
        return 2
    try:
        derived = derive_thresholds()
        probes = [
            run_space_probe(binary, size)
            for size in derived["space_probe_points"]
        ]
    except (OSError, ReadinessModelError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    failures = [probe for probe in probes if probe["failures"]]
    try:
        binary_label = binary.relative_to(ROOT).as_posix()
    except ValueError:
        binary_label = binary.name
    summary = {
        "schema": "cetta.main-readiness.thresholds.v1",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "status": "passed" if not failures else "failed",
        "binary": binary_label,
        "binary_sha256": sha256_file(binary),
        "source_sha256": {
            "src/space.c": sha256_file(SPACE_SOURCE),
            "src/term_universe.c": sha256_file(TERM_SOURCE),
            "tests/test_term_universe_store_abi.c": sha256_file(TERM_TEST),
        },
        "derived": derived,
        "probes": probes,
        "failures": failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    print(f"WITNESS_EVIDENCE_SCHEMA={summary['schema']}")
    print(f"WITNESS_EVIDENCE_STATUS={summary['status']}")
    print(f"WITNESS_EVIDENCE_SHA256={digest}")
    print(f"WITNESS_EVIDENCE_PROBES={len(probes)}")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
