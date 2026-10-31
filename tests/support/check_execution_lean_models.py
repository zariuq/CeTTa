#!/usr/bin/env python3
"""Audit the maintained execution models and pin their imported local closure."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


MODULE = "Mettapedia.Machines.ExecutionAudit"
SCHEMA = "cetta.execution-theory.v1"
AXIOMS = {"propext", "Classical.choice", "Quot.sound"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def module_paths(root: Path, module: str) -> tuple[Path, Path]:
    if not re.fullmatch(r"Mettapedia(?:\.[A-Za-z0-9_]+)+", module):
        raise ValueError(f"invalid execution model module: {module!r}")
    path = Path(*module.split("."))
    return (root / path.with_suffix(".lean"),
            root / ".lake/build/lib/lean" / path.with_suffix(".olean"))


def configuration(root: Path) -> dict[str, str]:
    names = ["lean-toolchain", "lake-manifest.json"]
    names += [name for name in ("lakefile.lean", "lakefile.toml")
              if (root / name).is_file()]
    if len(names) < 3:
        raise ValueError("execution model project has no Lake configuration")
    return {name: sha256(root / name) for name in names}


def receipt_identity(root: Path, path: Path) -> dict:
    """Validate an audited closure against the live source and object files."""
    data = json.loads(path.read_text())
    if data.get("schema") != SCHEMA or data.get("status") != "passed":
        raise ValueError("execution theory receipt has no passing audit")
    summary = data["audit"]
    if (summary.get("schema") != "execution-foundations-v1" or
            not set(summary["usedAxioms"]) <= AXIOMS or
            set(summary["allowedAxioms"]) != AXIOMS):
        raise ValueError("execution theory receipt has invalid foundations")
    owned = data["owned_modules"]
    modules = data["modules"]
    if (not owned or len(owned) != summary["ownedModules"] or len(owned) != len(set(owned)) or
            not set(owned) <= modules.keys() or MODULE not in modules or
            len(modules) != summary["localSourceModules"] or
            not summary["checkedBodies"] or not summary["theorems"]):
        raise ValueError("execution theory receipt has incomplete model coverage")
    if data["configuration"] != configuration(root):
        raise ValueError("execution theory toolchain or dependencies changed")
    if data["audit_log_sha256"] != sha256(path.with_suffix(".audit.log")):
        raise ValueError("execution theory audit observations changed")
    for module, expected in modules.items():
        source, obj = module_paths(root, module)
        if (expected != {"source_sha256": sha256(source),
                        "object_sha256": sha256(obj)}):
            raise ValueError(f"execution theory source or object changed: {module}")
    return {key: data[key] for key in
            ("schema", "audit", "owned_modules", "modules", "configuration",
             "audit_log_sha256")}


def check(root: Path, receipt: Path) -> dict:
    receipt.parent.mkdir(parents=True, exist_ok=True)
    for label, command in (
            ("build", ["lake", "build", MODULE]),
            ("audit", ["lake", "env", "lean", "-DwarningAsError=true",
                       "Mettapedia/Machines/ExecutionAudit.lean"])):
        result = subprocess.run(command, cwd=root, capture_output=True,
                                text=True, timeout=900)
        output = result.stdout + result.stderr
        receipt.with_suffix(f".{label}.log").write_text(output)
        if result.returncode or "warning:" in output or "error:" in output:
            raise RuntimeError(f"execution theory {label} failed: {output[-3000:]}")
    modules = re.findall(r"EXECUTION-THEORY-SOURCE (Mettapedia\.[\w.]+)", output)
    summaries = [json.loads(line.split("EXECUTION-THEORY-MODULE ", 1)[1])
                 for line in output.splitlines() if "EXECUTION-THEORY-MODULE " in line]
    totals = [json.loads(line.split("EXECUTION-THEORY-PASS ", 1)[1])
              for line in output.splitlines() if "EXECUTION-THEORY-PASS " in line]
    if len(totals) != 1 or len(modules) != len(set(modules)):
        raise RuntimeError("execution theory emitted no unique complete audit")
    summary = totals[0]
    if (len(summaries) != summary["ownedModules"] or
            sum(item["checkedBodies"] for item in summaries) != summary["checkedBodies"] or
            sum(item["theorems"] for item in summaries) != summary["theorems"]):
        raise RuntimeError("execution theory module and total observations disagree")
    records = {}
    for module in sorted(modules):
        source, obj = module_paths(root, module)
        records[module] = {"source_sha256": sha256(source), "object_sha256": sha256(obj)}
    data = {"schema": SCHEMA, "status": "passed", "audit": summary,
            "owned_modules": sorted(item["module"] for item in summaries),
            "modules": records, "configuration": configuration(root),
            "audit_log_sha256": sha256(receipt.with_suffix(".audit.log")),
            "scope": "Kernel foundations of the selected models; native C correspondence is separately qualified."}
    receipt.write_text(json.dumps(data, indent=2) + "\n")
    return receipt_identity(root, receipt)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mettapedia", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    result = check(args.mettapedia.resolve(), args.receipt.resolve())
    audit = result["audit"]
    print(f"PASS: {audit['ownedModules']} execution model roots, "
          f"{audit['checkedBodies']} checked bodies, {audit['theorems']} theorems; "
          f"{len(result['modules'])} local source/object identities")


if __name__ == "__main__":
    main()
