#!/usr/bin/env python3
"""Probe manifest HE examples against upstream Rust HE when they can run.

This is an empirical conformance-expansion probe, not a proof artifact and not a
runtime feature.  It asks upstream Rust HE which manifest cases it can execute,
then compares those runnable cases against CeTTa's he-compat lane.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "tests" / "test_manifest.tsv"
DEFAULT_REPORT = (
    ROOT
    / "tests"
    / "generated"
    / "he_compat"
    / "he_compat_broad_corpus_2026-06-25.json"
)
DEFAULT_UPSTREAM = Path.home() / "miniforge3" / "envs" / "hyperon" / "bin" / "metta"


def digest(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def portable_output(text: str) -> str:
    """Remove workstation-specific prefixes from recorded differential evidence."""
    replacements = {
        str(ROOT): "<repo>",
        str(Path.home()): "<home>",
    }
    for prefix, replacement in sorted(
        replacements.items(), key=lambda item: len(item[0]), reverse=True
    ):
        text = text.replace(prefix, replacement)
    return text


def portable_path(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return path.name


def executable_label(executable: str) -> str:
    if executable == "conda-hyperon":
        return executable
    path = Path(executable)
    return path.name if path.is_absolute() else executable


def output_head(text: str, limit: int = 500) -> str:
    return text[:limit]


def normalize_term(term: str) -> str:
    normalized = re.sub(r"\s+", " ", term.strip())
    normalized = re.sub(r"<space 0x[0-9a-fA-F]+>", "<space>", normalized)
    normalized = re.sub(r"GroundingSpace-0x[0-9a-fA-F]+", "<space>", normalized)
    normalized = normalized.replace("ModuleSpace(GroundingSpace-top)", "<space>")
    normalized = normalized.replace("GroundingSpace-top", "<space>")
    normalized = normalized.replace("Failed to resolve module top:", "Failed to resolve module ")
    normalized = re.sub(
        r"Failed to clone git repo: [^,]+, failed to resolve path '[^']+': "
        r"No such file or directory; class=Os \(2\)",
        "git-module! failed to clone repository",
        normalized,
    )
    normalized = re.sub(r"&[A-Za-z_][A-Za-z0-9_:-]*", "<space>", normalized)
    normalized = re.sub(
        r"\(match (?:<space>|\(module-inventory!\)) \(module-",
        "(match <module-inventory> (module-",
        normalized,
    )
    normalized = re.sub(r"\$([^\s()\[\],\"]+?)#\d+", r"$\1#_", normalized)
    return normalized


def split_top_level_commas(payload: str) -> list[str]:
    parts: list[str] = []
    buf: list[str] = []
    paren = 0
    bracket = 0
    in_str = False
    esc = False
    for ch in payload:
        if in_str:
            buf.append(ch)
            if esc:
                esc = False
                continue
            if ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
            buf.append(ch)
            continue
        if ch == "(":
            paren += 1
            buf.append(ch)
            continue
        if ch == ")":
            paren -= 1
            buf.append(ch)
            continue
        if ch == "[":
            bracket += 1
            buf.append(ch)
            continue
        if ch == "]":
            bracket -= 1
            buf.append(ch)
            continue
        if ch == "," and paren == 0 and bracket == 0:
            token = "".join(buf).strip()
            if token:
                parts.append(token)
            buf = []
            continue
        buf.append(ch)
    tail = "".join(buf).strip()
    if tail:
        parts.append(tail)
    return parts


def parse_result_line(raw_line: str) -> list[str] | None:
    line = raw_line.strip()
    if not (line.startswith("[") and line.endswith("]")):
        return None
    inner = line[1:-1].strip()
    if inner == "":
        return []
    return [normalize_term(tok) for tok in split_top_level_commas(inner)]


def observation_sequence(output: str) -> list[dict[str, Any]]:
    observations: list[dict[str, Any]] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        terms = parse_result_line(line)
        if terms is None:
            observations.append({"kind": "line", "value": normalize_term(line)})
        else:
            observations.append({"kind": "result-bag", "value": sorted(Counter(terms).items())})
    return observations


def run_case(cmd: list[str], timeout_seconds: int) -> dict[str, Any]:
    try:
        proc = subprocess.run(
            cmd,
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=timeout_seconds,
        )
        output = portable_output(proc.stdout + proc.stderr)
        return {
            "returncode": proc.returncode,
            "sha256": digest(output),
            "bytes": len(output.encode("utf-8")),
            "head": output_head(output),
            "observations": observation_sequence(output),
            "timed_out": False,
        }
    except subprocess.TimeoutExpired as exc:
        output = portable_output((exc.stdout or "") + (exc.stderr or ""))
        return {
            "returncode": None,
            "sha256": digest(output),
            "bytes": len(output.encode("utf-8")),
            "head": output_head(output),
            "observations": observation_sequence(output),
            "timed_out": True,
        }


def load_manifest(path: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    header: list[str] | None = None
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if header is None:
            header = cols
            continue
        if len(cols) != len(header):
            continue
        rows.append(dict(zip(header, cols, strict=True)))
    return rows


def select_rows(
    rows: list[dict[str, str]],
    lanes: set[str],
    builds: set[str],
    space_engines: set[str],
    targets: set[str],
) -> list[dict[str, str]]:
    selected: list[dict[str, str]] = []
    for row in rows:
        if row.get("lang") != "he" or row.get("syntax") != "metta":
            continue
        if row.get("build") not in builds:
            continue
        if row.get("space_engine") not in space_engines:
            continue
        if row.get("lane") not in lanes:
            continue
        if row.get("expect") not in targets:
            continue
        case_path = ROOT / row["path"]
        if not case_path.exists():
            continue
        selected.append(row)
    return selected


def upstream_cmd(upstream: str, case_path: str) -> list[str]:
    if upstream == "conda-hyperon":
        return ["conda", "run", "-n", "hyperon", "metta", case_path]
    return [upstream, case_path]


def classify(cetta: dict[str, Any], upstream: dict[str, Any]) -> str:
    if upstream["timed_out"]:
        return "upstream-timeout"
    if upstream["returncode"] != 0:
        return "upstream-not-runnable"
    if cetta["timed_out"]:
        return "cetta-timeout"
    if cetta["returncode"] != 0:
        return "cetta-not-runnable"
    if cetta["observations"] == upstream["observations"]:
        return "agree"
    return "diff"


def probe_row(row: dict[str, str], cetta_bin: str, upstream: str, timeout: int) -> dict[str, Any]:
    case_rel = row["path"]
    cetta = run_case([cetta_bin, "--profile", "he-compat", "--lang", "he", case_rel], timeout)
    upstream_result = run_case(upstream_cmd(upstream, case_rel), timeout)
    cls = classify(cetta, upstream_result)
    return {
        "path": case_rel,
        "classification": cls,
        "manifest": row,
        "cetta": cetta,
        "upstream": upstream_result,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--cetta", default=os.environ.get("CETTA_BIN", str(ROOT / "cetta")))
    parser.add_argument(
        "--upstream",
        default=os.environ.get(
            "HE_UPSTREAM_BIN",
            str(DEFAULT_UPSTREAM) if DEFAULT_UPSTREAM.exists() else "conda-hyperon",
        ),
    )
    parser.add_argument("--timeout", type=int, default=10)
    parser.add_argument(
        "--lanes",
        default="test,test-heavy,test-backend-dedicated,test-profiles,test-import-modes",
    )
    parser.add_argument("--builds", default="main")
    parser.add_argument("--space-engines", default="native")
    parser.add_argument("--targets", default="golden")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--print-limit", type=int, default=40)
    parser.add_argument("--fail-on-diff", action="store_true")
    args = parser.parse_args()

    rows = load_manifest(args.manifest)
    selected = select_rows(
        rows,
        lanes={x for x in args.lanes.split(",") if x},
        builds={x for x in args.builds.split(",") if x},
        space_engines={x for x in args.space_engines.split(",") if x},
        targets={x for x in args.targets.split(",") if x},
    )
    if args.limit > 0:
        selected = selected[: args.limit]

    results = [probe_row(row, args.cetta, args.upstream, args.timeout) for row in selected]
    summary = Counter(row["classification"] for row in results)
    upstream_runnable = sum(
        1
        for row in results
        if not row["upstream"]["timed_out"] and row["upstream"]["returncode"] == 0
    )
    payload = {
        "schema_version": 1,
        "kind": "he-compat-runnable-corpus-probe",
        "subject": "CeTTa --profile he-compat --lang he",
        "upstream": executable_label(args.upstream),
        "selection": {
            "manifest": portable_path(args.manifest),
            "lanes": args.lanes,
            "builds": args.builds,
            "space_engines": args.space_engines,
            "targets": args.targets,
            "timeout_seconds": args.timeout,
            "limit": args.limit,
        },
        "summary": {
            "selected": len(selected),
            "upstream_runnable": upstream_runnable,
            **dict(sorted(summary.items())),
        },
        "results": results,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print("HE-compat runnable corpus probe")
    print(f"  selected: {len(selected)}")
    print(f"  upstream-runnable: {upstream_runnable}")
    for key, value in sorted(summary.items()):
        print(f"  {key}: {value}")
    printed = 0
    hidden = 0
    for row in results:
        if row["classification"] in {"diff", "cetta-not-runnable", "cetta-timeout"}:
            if printed >= args.print_limit:
                hidden += 1
                continue
            printed += 1
            print(f"[{row['classification']}] {row['path']}")
            print(f"  CeTTa:    {row['cetta']['head']!r}")
            print(f"  upstream: {row['upstream']['head']!r}")
    if hidden:
        print(f"... {hidden} more differing/problem cases omitted from stdout; see report.")
    print(f"report: {args.report}")

    if args.fail_on_diff and any(
        row["classification"] in {"diff", "cetta-not-runnable", "cetta-timeout"}
        for row in results
    ):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
