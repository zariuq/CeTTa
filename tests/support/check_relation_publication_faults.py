#!/usr/bin/env python3
"""Pinned take-over NULL-allocation controls and one publication mutant.

Runs only owned child processes. Source/runtime files are never patched.
Every injected site must be reached exactly once. Fatal outcomes are reported
as such, never compared as empty answers or credited as rollback.
"""
import argparse
from dataclasses import asdict
import json
from pathlib import Path

from check_native_observers import ROOT, captured_run, compare, execute, route_environment, save_json, sha256
from native_observer_cases import Case, integer
from native_observer_probes import R82_SHA256, SCOPED_OWNERS_SHA256, SUCCESSOR_CANDIDATE_2_SHA256

SITES = (
    {"name": "moves-first", "symbol": "space_dense_promote", "offset": 0x74f, "nth": 1, "expect": "equal"},
    {"name": "moves-grow", "symbol": "space_dense_promote", "offset": 0x74f, "nth": 2, "expect": "equal"},
    {"name": "taken-array", "symbol": "space_dense_promote", "offset": 0x4de, "nth": 1, "expect": "equal"},
    {"name": "tuple-grow", "symbol": "cetta_tuple_table_add", "offset": 0xbb3, "nth": 2,
     "ancestor": "space_dense_promote", "expect": "equal", "give_back": True, "shape": "deep"},
    {"name": "hold-array-grow", "symbol": "cetta_tuple_table_add", "offset": 0x77c, "nth": 2,
     "ancestor": "space_dense_promote", "expect": "equal", "give_back": True, "shape": "deep"},
    {"name": "binary-holds-grow", "symbol": "dense_number_or_add.part.0", "offset": 0x19c, "nth": 2,
     "ancestor": "space_dense_promote", "expect": "equal", "give_back": True, "shape": "flat"},
    {"name": "detach-copy", "symbol": "cetta_malloc", "offset": 0x14, "nth": 1,
     "ancestor": "space_pinned_occurrences_detach", "expect": "fatal"},
    {"name": "skip-mark-removal", "symbol": "space_dense_promote", "offset": 0x63c, "nth": 1,
     "expect": "mismatch", "mutation": True},
)

SUCCESSOR_OFFSETS = {
    "moves-first": 0x75f, "moves-grow": 0x75f,
    "hold-array-grow": 0x75c, "binary-holds-grow": 0x1ac,
}


def sites_for(identity):
    if identity == R82_SHA256:
        return SITES
    if identity in (SUCCESSOR_CANDIDATE_2_SHA256, SCOPED_OWNERS_SHA256):
        return tuple({**site, "offset": SUCCESSOR_OFFSETS.get(site["name"], site["offset"])}
                     for site in SITES)
    raise ValueError("no reviewed fault profile for this executable")


def classify(receipt, comparison):
    if receipt.get("errors") or len(receipt.get("events", [])) != 1 or not receipt.get("exit", {}).get("terminal"):
        return "inconclusive"
    if receipt.get("architecture") != "i386:x86-64":
        return "inconclusive"
    if receipt["exit"].get("code") == 0:
        return comparison
    if receipt.get("signals") == ["SIGABRT"]:
        return "fatal"
    return "unexpected-termination"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--shape", choices=("deep", "flat", "flat-star"), default="deep")
    parser.add_argument("--sites", nargs="+", choices=[site["name"] for site in SITES])
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    identity = sha256(binary)
    try:
        sites = sites_for(identity)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    backend = "deep" if args.shape == "deep" else "flat"
    applicable = [site for site in sites if site.get("shape", backend) == backend]
    if args.sites is None:
        args.sites = [site["name"] for site in applicable]
    if any(name not in {site["name"] for site in applicable} for name in args.sites):
        raise SystemExit("selected fault site does not apply to this fact shape")
    support = Path(__file__).parent
    harness_paths = [support / name for name in (
        "check_relation_publication_faults.py", "relation_fault_gdb.py",
        "check_native_observers.py", "native_observer_cases.py",
        "native_observer_probes.py", "native_observer_gdb.py", "compare_answers.py")]
    harness_hashes = {path.name: sha256(path) for path in harness_paths}
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    case = Case(0, "row-takeover", "bag", ("0", "0", "1", "2"), integer(0), history=4098, fact_shape=args.shape)
    source, contracts = case.render()
    source_path = root / "program.metta"
    source_path.write_text(source)
    source_hash = sha256(source_path)
    command = [str(binary), "--profile", "extended", "--lang", "petta", str(source_path)]
    native = execute(command, cwd=ROOT, environment=route_environment("on", False), timeout=30,
                     output_limit=2*1024*1024, dense_trace=True)
    reference = execute(command, cwd=ROOT, environment=route_environment("off", True), timeout=30,
                        output_limit=2*1024*1024, dense_trace=True)
    baseline = compare(native, reference, contracts)
    save_json(root / "baseline.json", {"case": case.to_dict(), "contracts": contracts,
        "verdict": baseline, "native": asdict(native), "reference": asdict(reference)})
    if baseline != "equal":
        raise SystemExit("baseline did not qualify")
    script = Path(__file__).with_name("relation_fault_gdb.py")
    records = []
    for site in sites:
        if site["name"] not in args.sites:
            continue
        directory = root / site["name"]
        directory.mkdir()
        config = {**site, "stdout": str(directory / "inferior.stdout"), "stderr": str(directory / "inferior.stderr"),
                  "receipt": str(directory / "fault.json")}
        environment = route_environment("on", False)
        environment["CETTA_RELATION_FAULT_CONFIG"] = json.dumps(config)
        debugger = execute(["gdb", "-q", "-nx", "-nh", "-batch", "-iex", "set auto-load off", "-iex",
                            "set debuginfod enabled off", "-x", str(script), "--args"] + command,
                           cwd=ROOT, environment=environment, timeout=60, output_limit=2*1024*1024, file_limit=2*1024*1024)
        save_json(directory / "debugger.json", asdict(debugger))
        try:
            receipt = json.loads(Path(config["receipt"]).read_text())
        except (OSError, ValueError):
            receipt = {"errors": ["missing-or-invalid-receipt"]}
        if debugger.status != "exited" or debugger.returncode != 0:
            receipt.setdefault("errors", []).append("debugger-execution-failed")
        raw = {kind: Path(config[kind]).read_bytes() if Path(config[kind]).exists() else b"" for kind in ("stdout", "stderr")}
        run = captured_run("exited", receipt.get("exit", {}).get("code"), raw, debugger.seconds, dense_trace=True)
        comparison = compare(run, reference, contracts)
        disposition = classify(receipt, comparison)
        passed = disposition == "value-mismatch" if site["expect"] == "mismatch" else disposition == site["expect"]
        if site.get("give_back"):
            passed = passed and any("gives " in line and " facts back to rows" in line and "gives 0 " not in line
                                    for line in run.diagnostics)
        if site["name"] == "binary-holds-grow":
            expected_kind = "bits" if args.shape == "flat-star" else "array"
            passed = passed and len(receipt.get("events", [])) == 1 and receipt["events"][0].get("row0_kind") == expected_kind
        record = {"site": site, "receipt": receipt, "run": asdict(run), "comparison": comparison,
                  "disposition": disposition, "passed": passed}
        save_json(directory / "result.json", record)
        records.append(record)
        print(site["name"], disposition, "PASS" if passed else "FAIL", flush=True)
    summary = {"binary_sha256": identity, "binary_unchanged": sha256(binary) == identity,
               "fact_shape": args.shape,
               "source_sha256": source_hash, "source_unchanged": sha256(source_path) == source_hash,
               "contracts": contracts, "files": harness_hashes,
               "harness_unchanged": harness_hashes == {path.name: sha256(path) for path in harness_paths},
               "records": records}
    save_json(root / "summary.json", summary)
    return 0 if (summary["binary_unchanged"] and summary["source_unchanged"] and summary["harness_unchanged"]
                 and all(record["passed"] for record in records)) else 1


if __name__ == "__main__":
    raise SystemExit(main())
