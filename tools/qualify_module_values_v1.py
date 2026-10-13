#!/usr/bin/env python3
"""Account for frozen historical modules without modifying their sources."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed
from test_parser_artifacts_v1 import run

ROOT = Path(__file__).resolve().parents[1]


def declarations(value):
    # Compact declaration lists can exceed the host's recursive call limit.
    # Count occurrences, not distinct tuple values: sharing does not remove rows.
    pending, count = [value], 0
    while pending:
        value = pending.pop()
        if isinstance(value, tuple) and value:
            count += int(headed(value, "ModuleDeclarationV1", 2))
            pending.extend(value[1:])
    return count


def summarize(rows, drift, module_count, languages):
    """Strict compact-value qualification; non-accepted reads are not successes."""
    expected = module_count * len(languages)
    counts = Counter(row["result"] for row in rows)
    return {"format": "ModuleValueInventoryV1", "rows": rows,
            "fixture_drift": drift, "module_count": module_count,
            "expected_observations": expected, "observations": len(rows),
            "outcome_counts": dict(counts), "compact": counts["ModuleValueV1"],
            "projection_refusals": counts["ModuleRefusedV1"],
            "noncompact": [r for r in rows if r["result"] != "ModuleValueV1"],
            "passed": bool(expected and not drift and len(rows) == expected and
                           counts["ModuleValueV1"] == expected and
                           all(r.get("frozen_match", True) for r in rows))}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--cetta", type=Path, required=True)
    p.add_argument("--reader-artifact", type=Path, required=True)
    p.add_argument("--inventory", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--language", choices=("he", "petta", "prime"), action="append")
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    frozen = [v for v in json.loads(args.inventory.read_text()) if v["family"] == "module-input"]
    if not frozen:
        raise AssertionError("the inventory contains no module inputs")
    rows, drift = [], []
    languages = tuple(args.language or ("he", "petta", "prime"))
    if len(set(languages)) != len(languages):
        p.error("language selections must be distinct")
    for fixture in frozen:
        path = Path(fixture["path"])
        digest = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
        if digest != fixture["sha256"]:
            drift.append({"path": str(path), "frozen_sha256": fixture["sha256"], "current_sha256": digest})
    for language in languages:
        for n, fixture in enumerate(frozen):
            path = Path(fixture["path"])
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            program = '!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n!' + lets([
                ("$reader", form("langdef:load-parser-artifact", text(str(args.reader_artifact.resolve())))),
                ("$value", form("module:read-file", "$reader", text(digest), text(str(path)))),
                ("$closed", form("langdef:close-parser", "$reader")),
            ], "(function (return (ModuleReadControlV1 $value)))") + "\n"
            records = [v for v in run(args.cetta.resolve(), language, args.output, f"{n:02d}-{path.stem}", program)
                       if headed(v, "ModuleReadControlV1", 1)]
            if len(records) != 1:
                raise AssertionError((language, path, "missing result", records))
            value = records[0][1]
            head = value[0].text if isinstance(value, tuple) and value else str(value)
            row = {"language": language, "path": str(path), "sha256": digest,
                   "frozen_sha256": fixture["sha256"],
                   "frozen_match": digest == fixture["sha256"], "result": head,
                   "compact_declarations": declarations(value)}
            if headed(value, "ModuleRefusedV1", 3):
                row["reason"] = value[1].text
            rows.append(row)
            print(json.dumps(row), flush=True)
    summary = summarize(rows, drift, len(frozen), languages)
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    if not summary["passed"]:
        raise AssertionError("fixture drift or incomplete declaration projection; see the inventory summary")


if __name__ == "__main__":
    main()
