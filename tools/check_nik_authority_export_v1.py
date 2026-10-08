#!/usr/bin/env python3
"""Compare saved NIK packages with an export without revising either input."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import generate_nik_authority_runtime_v1 as nik
import gslt2parse_schema_v1 as sx


def compare(checked: Path, exported: Path) -> tuple[int, dict]:
    catalogs = {}
    for label, path in (("checked", checked), ("exported", exported)):
        try:
            catalogs[label] = nik.parse_catalog(path)
        except (OSError, UnicodeError, nik.GenerationError, sx.SchemaError) as error:
            return 2, {"status": "invalid", "input": label, "error": str(error)}
    old = {item.alias: item for item in catalogs["checked"]}
    new = {item.alias: item for item in catalogs["exported"]}
    changes = []
    for alias in sorted(old.keys() | new.keys()):
        if alias not in old or alias not in new:
            changes.append({"authority": alias,
                            "change": "added" if alias in new else "removed"})
            continue
        first, second = old[alias], new[alias]
        if first == second:
            continue
        fields = [name for name in (
            "system_id", "revision", "digest", "constructors",
            "positive_goal", "positive_proof",
        ) if getattr(first, name) != getattr(second, name)]
        for index, name in ((3, "judgments"), (5, "conversion")):
            if first.presentation[index] != second.presentation[index]:
                fields.append(name)
        old_rules = {rule.identifier: rule for rule in first.rules}
        new_rules = {rule.identifier: rule for rule in second.rules}
        changed_rules = []
        for identifier in sorted(old_rules.keys() & new_rules.keys()):
            before, after = old_rules[identifier], new_rules[identifier]
            if before != after:
                changed_rules.append({
                    "rule": identifier,
                    "fields": [name for name in (
                        "formals", "premises", "conclusion", "side_conditions",
                    ) if getattr(before, name) != getattr(after, name)],
                    "premises": [len(before.premises), len(after.premises)],
                    "formals": [len(before.formals), len(after.formals)],
                })
        changes.append({
            "authority": alias, "fields": fields,
            "added_rules": sorted(new_rules.keys() - old_rules.keys()),
            "removed_rules": sorted(old_rules.keys() - new_rules.keys()),
            "changed_rules": changed_rules,
            "rule_order_changed": tuple(old_rules) != tuple(new_rules),
        })
    exact = checked.read_bytes() == exported.read_bytes()
    return (0 if exact else 1), {
        "status": "exact" if exact else "different",
        "checked_authorities": len(old), "exported_authorities": len(new),
        "changes": changes,
        "authority_order_changed": tuple(old) != tuple(new),
        "serialization_only": not exact and catalogs["checked"] == catalogs["exported"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checked", type=Path, required=True)
    parser.add_argument("--exported", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    status, report = compare(args.checked, args.exported)
    rendered = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
