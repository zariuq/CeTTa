#!/usr/bin/env python3
"""Checked space queries and transfers, measured by paired instruction slopes.

Requires perf instruction counters and two bridge-enabled CeTTa executables.
Each sample checks independently expected answer counts before reporting cost.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODES = (("he", "he"), ("he", "he-compat"), ("he", "extended"),
         ("petta", "extended"))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def query_program(kind, size, rounds, scope, language="he", profile="extended"):
    if kind == "join":
        facts = [f"(left k{i:05d} v{i:05d})\n(right v{i:05d} z{i:05d})"
                 for i in range(size)]
        query = "(match &self (left $k $v) (match &self (right $v $z) ($k $z)))"
        expected = size
    else:
        symbolic = kind in ("hit-symbol", "miss-symbol")
        facts = [f"(row k{i:05d} {f'v{i:05d}' if symbolic else i})" for i in range(size)]
        queries = {
            "hit": "(match &self (row k00042 42) hit)",
            "miss": "(match &self (row absent $value) $value)",
            "scan": "(match &self (row $key $value) $value)",
            "hit-symbol": "(match &self (row k00042 v00042) hit)",
            "miss-symbol": "(match &self (row absent missing) hit)",
        }
        query = queries[kind]
        expected = {"hit": 1, "miss": 0, "scan": size,
                    "hit-symbol": 1, "miss-symbol": 0}[kind]
    if scope == "owned":
        query = query.replace("&self", "&query-space")
        facts = ["!(bind! &query-space (new-space))"] + [
            f"!(add-atom &query-space {fact})" for text in facts for fact in text.splitlines()]
    observation = (f"!(let $rows (collapse {query}) "
                   f"(println! (pathprobe {kind} (size-atom $rows))))")
    if kind in ("hit-symbol", "miss-symbol"):
        if language == "petta" or profile == "extended":
            consumer = "size-atom" if language == "petta" else "size"
            observation = f"!(println! (pathprobe {kind} ({consumer} (collapse {query}))))"
    return "\n".join(facts + [observation] * rounds) + "\n", expected


def constructor(kind):
    return "(mork:new-space)" if kind == "mork" else f"(new-space {kind})"


def space_operation(kind, operation):
    return ("mork:" if kind == "mork" else "") + operation


def transfer_program(source, target, route, language, size, rounds):
    facts = " ".join(f"(row k{i:05d} {i})" for i in range(size))
    header = ["!(import! &self mork)"] if "mork" in (source, target) else []
    if language == "petta":
        header.append(f"!(bind! &source (new-state {constructor(source)}))")
        source_ref = "(get-state &source)"
        header.extend(f"!(add-atom {source_ref} (row k{i:05d} {i}))" for i in range(size))
    else:
        header.append(f"!(bind! &source {constructor(source)})")
        source_ref = "&source"
        header.append(f"!({space_operation(source, 'add-atoms')} {source_ref} ({facts}))")
    get_atoms = f"({space_operation(source, 'get-atoms')} {source_ref})"
    if route == "collapse-direct":
        get_atoms = f"(collapse {get_atoms})"
    if language == "petta":
        copy = f"(collapse (match {source_ref} $row (add-atom $target $row)))"
    else:
        copy = f"({space_operation(target, 'add-atoms')} $target {get_atoms})"
    body = (f"!(let $target {constructor(target)} "
            f"(let $done {copy} "
            f"(println! (pathprobe transfer ({space_operation(target, 'size')} $target)))))")
    return "\n".join(header + [body] * rounds) + "\n", size


def measured_run(binary, language, profile, engine, source, prefix, expected, rounds):
    command = [str(binary), "--quiet", "--lang", language, "--profile", profile,
               "--space-engine", engine, str(source)]
    process = subprocess.Popen(
        ["perf", "stat", "-x,", "-e", "instructions:u", "-o", str(prefix) + ".perf",
         "--", *command], cwd=ROOT, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.communicate()
        raise
    Path(str(prefix) + ".stdout").write_text(stdout)
    Path(str(prefix) + ".stderr").write_text(stderr)
    markers = re.findall(r"^\(pathprobe (\S+) (\d+)\)$", stdout, re.M)
    if process.returncode or stderr or len(markers) != rounds:
        raise RuntimeError((command, process.returncode, stderr, stdout[:2000]))
    if any(int(value) != expected for _, value in markers):
        raise AssertionError((command, expected, markers))
    counts = [int(line.split(",")[0]) for line in Path(str(prefix) + ".perf").read_text().splitlines()
              if ",instructions:u," in line]
    if len(counts) != 1:
        raise AssertionError((command, counts))
    return counts[0], digest(Path(str(prefix) + ".stdout"))


def main():
    parser = argparse.ArgumentParser(
        description="Compare checked native, PathMap and MORK work separately from loading.")
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("--output", type=Path, required=True,
                        help="new directory for programs, observations, counters and receipt")
    parser.add_argument("--category", choices=("query", "transfer"), default="query")
    parser.add_argument("--query-scope", choices=("self", "owned"), default="owned")
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--sizes", type=int, nargs="+", default=(128, 1024))
    args = parser.parse_args()
    if args.repeats < 1 or any(size <= 42 for size in args.sizes):
        parser.error("repeats must be positive and sizes must exceed the exact-hit key 42")
    output_dir = args.output.resolve()
    if output_dir.exists():
        parser.error("output directory already exists; choose a fresh evidence directory")
    output_dir.mkdir(parents=True)
    binaries = {"old": args.old.resolve(), "new": args.new.resolve()}
    identities = {side: digest(binary) for side, binary in binaries.items()}
    cases = []
    if args.category == "query":
        for language, profile in MODES:
            for engine in ("native", "pathmap"):
                for size in args.sizes:
                    for kind in ("hit", "miss", "scan", "join", "hit-symbol", "miss-symbol"):
                        cases.append((language, profile, engine, size, kind, None))
    else:
        for source, target in (("native", "pathmap"), ("pathmap", "native"),
                               ("native", "mork"), ("mork", "native"),
                               ("pathmap", "mork"), ("mork", "pathmap")):
            for route in ("get-atoms-direct", "collapse-direct"):
                for size in args.sizes:
                    cases.append(("he", "extended", "native", size,
                                  f"{source}-to-{target}", route))
        for source, target in (("native", "pathmap"), ("pathmap", "native")):
            for size in args.sizes:
                cases.append(("petta", "extended", "native", size,
                              f"{source}-to-{target}", "match-effect"))
    results = []
    for language, profile, engine, size, kind, route in cases:
        name = "-".join(str(x) for x in (language, profile, engine, size, kind, route) if x is not None)
        prefix = output_dir / name
        rounds_pair = (10, 100) if kind in ("hit", "miss", "hit-symbol", "miss-symbol") else (2, 12)
        sources = {}
        for rounds in rounds_pair:
            source = Path(str(prefix) + f"-{rounds}.metta")
            if args.category == "query":
                text, expected = query_program(kind, size, rounds, args.query_scope, language, profile)
            else:
                from_kind, to_kind = kind.split("-to-")
                text, expected = transfer_program(from_kind, to_kind, route, language, size, rounds)
            source.write_text(text)
            sources[rounds] = source
        row = {"name": name, "language": language, "profile": profile, "engine": engine,
               "size": size, "kind": kind, "route": route, "expected_per_round": expected,
               "query_scope": args.query_scope if args.category == "query" else None,
               "method": f"(instructions at {rounds_pair[1]} - instructions at {rounds_pair[0]}) / {rounds_pair[1] - rounds_pair[0]}",
               "binaries": identities, "sources": {str(k): digest(v) for k, v in sources.items()},
               "samples": {"old": [], "new": []}, "runs": []}
        for repeat in range(args.repeats):
            for side in (("old", "new") if repeat % 2 == 0 else ("new", "old")):
                values = {}
                for rounds in (rounds_pair if repeat % 2 == 0 else rounds_pair[::-1]):
                    run_prefix = Path(str(prefix) + f"-{side}-{repeat}-{rounds}")
                    instructions, output_hash = measured_run(
                        binaries[side], language, profile, engine, sources[rounds],
                        run_prefix, expected, rounds)
                    values[rounds] = instructions
                    row["runs"].append({"side": side, "repeat": repeat, "rounds": rounds,
                                        "instructions": instructions, "output_sha256": output_hash})
                cost = (values[rounds_pair[1]] - values[rounds_pair[0]]) / (rounds_pair[1] - rounds_pair[0])
                if cost <= 0:
                    raise AssertionError((name, cost))
                row["samples"][side].append(cost)
        old, new = row["samples"]["old"], row["samples"]["new"]
        row["median_change_percent"] = 100 * (statistics.median(new) / statistics.median(old) - 1)
        row["flagged"] = min(new) > max(old)
        results.append(row)
        receipt = output_dir / f"{args.category}-cost.json"
        receipt.write_text(json.dumps(results, indent=2) + "\n")
        print(f"{name}: {row['median_change_percent']:+.3f}% flagged={row['flagged']}", flush=True)


if __name__ == "__main__":
    main()
