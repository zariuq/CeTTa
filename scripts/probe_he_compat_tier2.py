#!/usr/bin/env python3
"""Re-check Tier 2 HE-compat catalog classifications against upstream HE."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = (
    ROOT
    / "tests"
    / "generated"
    / "he_compat"
    / "he_compat_cases_2026-06-07.json"
)
DEFAULT_REPORT = ROOT / "runtime" / "he_compat" / "tier2_probe_latest.json"
CLASSIFIED_DIVERGENCES = {
    "cetta-bug",
    "lean-spec-bug",
    "official-spec-gap",
    "upstream-he-divergence",
    "intentional-cetta-divergence",
    "he-prime-only",
}


def digest(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def run_case(cmd: list[str], timeout_seconds: int) -> dict[str, Any]:
    try:
        proc = subprocess.run(
            cmd,
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=timeout_seconds,
        )
        output = proc.stdout + proc.stderr
        return {
            "returncode": proc.returncode,
            "sha256": digest(output),
            "bytes": len(output.encode("utf-8")),
            "head": output[:500],
            "timed_out": False,
        }
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or "") + (exc.stderr or "")
        return {
            "returncode": None,
            "sha256": digest(output),
            "bytes": len(output.encode("utf-8")),
            "head": output[:500],
            "timed_out": True,
        }


def check_row(row: dict[str, Any], cetta_bin: str, timeout_seconds: int) -> dict[str, Any]:
    case_rel = str(row["cetta_path"])
    expected = str(row["classification"])
    cetta_cmd = [cetta_bin, "--profile", "he-compat", "--lang", "he", case_rel]
    upstream_cmd = ["conda", "run", "-n", "hyperon", "metta", case_rel]

    cetta = run_case(cetta_cmd, timeout_seconds)
    upstream = run_case(upstream_cmd, timeout_seconds)
    same = (
        cetta["returncode"] == upstream["returncode"]
        and cetta["sha256"] == upstream["sha256"]
        and not cetta["timed_out"]
        and not upstream["timed_out"]
    )

    if expected == "needs-triage":
        ok = False
        reason = "Tier 2 case remains needs-triage"
    elif expected == "agree":
        ok = same
        reason = "expected agreement" if not ok else ""
    elif expected in CLASSIFIED_DIVERGENCES:
        ok = not same
        reason = "expected classified divergence, but outputs now agree" if not ok else ""
    else:
        ok = False
        reason = f"unknown classification: {expected}"

    return {
        "id": row["id"],
        "path": case_rel,
        "expected_classification": expected,
        "outputs_match": same,
        "ok": ok,
        "reason": reason,
        "cetta": cetta,
        "upstream": upstream,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--cetta", default=os.environ.get("CETTA_BIN", str(ROOT / "cetta")))
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()

    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    rows = [row for row in catalog["cases"] if int(row.get("tier", 99)) == 2]
    results = [check_row(row, args.cetta, args.timeout) for row in rows]
    summary: dict[str, int] = {
        "total": len(results),
        "ok": sum(1 for row in results if row["ok"]),
        "fail": sum(1 for row in results if not row["ok"]),
        "agree": sum(1 for row in results if row["expected_classification"] == "agree"),
        "classified_divergence": sum(
            1 for row in results if row["expected_classification"] in CLASSIFIED_DIVERGENCES
        ),
        "needs_triage": sum(
            1 for row in results if row["expected_classification"] == "needs-triage"
        ),
    }

    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(
        json.dumps({"summary": summary, "results": results}, indent=2, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )

    for row in results:
        label = "pass" if row["ok"] else "fail"
        print(f"[{label}] {row['id']} {row['expected_classification']}")
        if not row["ok"]:
            print(f"  {row['reason']}")
            print(f"  CeTTa head: {row['cetta']['head']!r}")
            print(f"  HE head:    {row['upstream']['head']!r}")
    print(
        "Tier 2 HE-compat probe: "
        f"ok={summary['ok']} fail={summary['fail']} total={summary['total']}"
    )
    print(f"report: {args.report}")
    return 0 if summary["fail"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
