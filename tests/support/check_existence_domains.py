#!/usr/bin/env python3
"""Exercise the HE coordinate caller's compact/row decomposition on pinned r82.

This is not a cross-dialect oracle. Both spellings use HE: direct empty-case
observation and an explicitly materialized collection. The latter must avoid
the specialized view-existence caller in its debugger receipt. Expected
outputs are independent fixed controls. Natural calls are observed; no
inferior functions or mutations are injected.
"""
import argparse
from dataclasses import asdict
import json
from pathlib import Path

from check_native_observers import ROOT, captured_run, execute, route_environment, save_json, sha256
from native_observer_probes import REVIEWED_SHA256S

VIEW = "space_match_exists_flat_linear_view64"
DENSE = "space_dense_count_columns"
COORDINATES = "space_match_exists_ground_exact_expression_coordinates"
SETTLE = "space_dense_settle_for_pattern"


def program(kind, materialized):
    if kind == "ground":
        facts, queries, expected = ["(p 2)", "(p 3)"], ["(p 2)", "(p 9)"], "[()]\n" * 3 + "[absent]\n"
    elif kind == "open":
        facts, queries, expected = ["(p $open)"], ["(p 2)"], "[()]\n" * 2
    elif kind == "unrelated-open":
        facts, queries, expected = ["(q $open)"], ["(p 2)"], "[()]\n" * 2 + "[absent]\n"
    else:
        raise ValueError("unknown existence domain control")
    lines = ["!(bind! &s (new-space set))"] + [f"!(add-atom &s {fact})" for fact in facts]
    for query in queries:
        collection = f"(collapse (match &s {query} t))"
        lines += [f"!(let $ready {collection} (case $ready ((() absent))))" if materialized
                  else f"!(case {collection} ((() absent)))"]
    return "\n".join(lines) + "\n", expected


def evidence(receipt):
    events = receipt.get("events", [])
    valid = (receipt.get("architecture") == "i386:x86-64" and not receipt.get("errors")
             and receipt.get("pending") == 0 and receipt.get("active") == []
             and receipt.get("exit") == {"code": 0, "terminal": True}
             and all(e.get("returned") for e in events))
    views = [e for e in events if e["symbol"] == VIEW]
    branches = []
    for view in views:
        valid = valid and view.get("result") == 1 and view.get("found") in (0, 1)
        children = [e for e in events if e.get("parent") == view["call"]]
        valid = valid and all(e.get("space") == view.get("space") for e in children)
        dense = [e for e in children if e["symbol"] == DENSE]
        coordinates = [e for e in children if e["symbol"] == COORDINATES]
        settled = [e for e in children if e["symbol"] == SETTLE]
        settled_before_coordinate = ([e for e in settled if e["call"] < coordinates[0]["call"]]
                                     if coordinates else settled)
        if dense and dense[0].get("result") == 1 and dense[0].get("count", 0) > 0:
            branch = "compact-hit"
            valid = valid and not coordinates and not settled_before_coordinate and view.get("found") == 1
        elif dense and dense[0].get("result") == 1:
            branch = "compact-miss-then-rows"
            valid = valid and len(coordinates) == 1 and not settled_before_coordinate and dense[0].get("count") == 0
            valid = valid and dense[0]["call"] < coordinates[0]["call"]
        else:
            branch = "settle-then-rows"
            valid = valid and len(settled_before_coordinate) == 1 and len(coordinates) == 1
            valid = valid and settled_before_coordinate[0]["call"] < coordinates[0]["call"]
        for coordinate in coordinates:
            valid = valid and coordinate.get("applicable") in (0, 1)
            if coordinate.get("applicable") == 1:
                valid = valid and coordinate.get("result") == view.get("found")
        branches.append({"call": view["call"], "branch": branch, "result": view.get("result"),
                         "found": view.get("found"),
                         "coordinate_results": [(e.get("result"), e.get("applicable")) for e in coordinates]})
    return {"valid": bool(valid), "view_calls": len(views), "branches": branches}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    identity = sha256(binary)
    if identity not in REVIEWED_SHA256S:
        raise SystemExit("no reviewed existence-domain profile")
    support = Path(__file__).parent
    paths = [support / name for name in ("check_existence_domains.py", "existence_domains_gdb.py",
        "check_native_observers.py", "compare_answers.py", "native_observer_cases.py",
        "native_observer_probes.py", "native_observer_gdb.py")]
    identities = {p.name: sha256(p) for p in paths}
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    records = []
    for kind in ("ground", "open", "unrelated-open"):
        for mode in ("off", "eager"):
            for materialized in (False, True):
                directory = root / f"{kind}-{mode}-{'materialized' if materialized else 'direct'}"
                directory.mkdir()
                source, expected = program(kind, materialized)
                source_path = directory / "program.metta"
                source_path.write_text(source)
                source_hash = sha256(source_path)
                command = [str(binary), "--profile", "extended", "--lang", "he", str(source_path)]
                env = route_environment(mode, False)
                normal = execute(command, cwd=ROOT, environment=env, timeout=15, output_limit=2*1024*1024, dense_trace=True)
                config = {"stdout": str(directory / "inferior.stdout"), "stderr": str(directory / "inferior.stderr"),
                          "receipt": str(directory / "probe.json"), "event_limit": 500}
                debugger = execute(["gdb", "-q", "-nx", "-nh", "-batch", "-iex", "set auto-load off", "-iex",
                    "set debuginfod enabled off", "-x", str(support / "existence_domains_gdb.py"), "--args"] + command,
                    cwd=ROOT, environment=dict(env, CETTA_EXISTENCE_DOMAINS_GDB=json.dumps(config)),
                    timeout=30, output_limit=2*1024*1024, file_limit=2*1024*1024)
                save_json(directory / "debugger.json", asdict(debugger))
                try:
                    receipt = json.loads(Path(config["receipt"]).read_text())
                except (OSError, ValueError):
                    receipt = {"errors": ["missing-or-invalid-receipt"]}
                if debugger.status != "exited" or debugger.returncode != 0:
                    receipt.setdefault("errors", []).append("debugger-execution-failed")
                raw = {key: Path(config[key]).read_bytes() if Path(config[key]).exists() else b"" for key in ("stdout", "stderr")}
                traced = captured_run(debugger.status, receipt.get("exit", {}).get("code"), raw, debugger.seconds, dense_trace=True)
                observed = evidence(receipt)
                output_ok = all(r.status == "exited" and r.returncode == 0 and r.stdout == expected and r.stderr == ""
                                for r in (normal, traced))
                route_ok = observed["view_calls"] == (0 if materialized else (2 if kind == "ground" else 1))
                passed = output_ok and observed["valid"] and route_ok and sha256(source_path) == source_hash
                record = {"kind": kind, "mode": mode, "materialized": materialized, "source_sha256": source_hash,
                          "expected": expected, "normal": asdict(normal), "traced": asdict(traced),
                          "evidence": observed, "passed": passed}
                save_json(directory / "result.json", record)
                records.append(record)
                print(directory.name, "PASS" if passed else "FAIL", observed, flush=True)
    stable = sha256(binary) == identity and identities == {p.name: sha256(p) for p in paths}
    save_json(root / "summary.json", {"binary_sha256": identity, "identities_unchanged": stable,
                                     "source_files": identities, "results": records})
    return 0 if stable and all(r["passed"] for r in records) else 1


if __name__ == "__main__":
    raise SystemExit(main())
