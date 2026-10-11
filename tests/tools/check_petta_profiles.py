#!/usr/bin/env python3
"""Compare tracked upstream PeTTa examples, profiles, and optional timings."""

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time


TOKENS = re.compile(r'"(?:\\.|[^"\\])*"|\$[^\s()]+')
SKIPS = {
    "repl.metta": "interactive input",
    "greedy_chess.metta": "interactive input",
    "llm_cities.metta": "external LLM credentials",
}


def normalize(output, example):
    lines = []
    for line in output.splitlines():
        names = {}

        def rename(token):
            text = token.group()
            if not text.startswith("$"):
                return text
            return names.setdefault(text, f"$v{len(names)}")

        line = TOKENS.sub(rename, line).rstrip()
        if example == "test_datetime.metta":
            # Only this fixture's three observations of the live clock vary.
            if re.fullmatch(r"\(\d{10}(?:\.\d+)?\)", line):
                line = "(<clock-timestamp>)"
            elif re.fullmatch(r'\(("?)\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\1\)', line):
                line = '(<clock-date>)'
        lines.append(line)
    return "\n".join(lines) + ("\n" if lines else "")


def self_test():
    n = lambda text: normalize(text, "test.metta")
    assert n("(pair $q $q)") == n("(pair $_0 $_0)")
    assert n("(pair $q $q)") != n("(pair $q $r)")
    assert n("(pair $a $b $a)") == n("(pair $_1 $_2 $_1)")
    assert n('(text "$q")') != n('(text "$r")')
    assert n("a\na\n") != n("a\n")
    assert n("a\nb\n") != n("b\na\n")
    assert normalize("(604800)", "test_datetime.metta") == "(604800)\n"
    assert normalize('(\"08:30:45\")', "test_datetime.metta") == '(\"08:30:45\")\n'
    assert normalize("(1760000000)", "test_datetime.metta") == "(<clock-timestamp>)\n"
    for date in ('(2026-10-06 17:00:00)', '("2026-10-06 17:00:00")'):
        assert normalize(date, "test_datetime.metta") == "(<clock-date>)\n"
        assert normalize(date, "test.metta") == date + "\n"
    success = {"exit": 0, "stdout": "a\na\n"}
    assert compare(success, success, "test.metta")
    assert not compare(success, {"exit": 0, "stdout": "a\n"}, "test.metta")
    assert not compare(success, {"exit": 2, "stdout": "a\na\n"}, "test.metta")
    assert not compare({"exit": "timeout", "stdout": ""},
                       {"exit": "timeout", "stdout": ""}, "test.metta")
    print("PASS: normalization retains aliases, occurrences, order and string contents")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, cwd, destination, timeout):
    start = time.perf_counter()
    try:
        result = subprocess.run(command, cwd=cwd, stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=timeout)
        code, stdout, stderr = result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as error:
        code = "timeout"
        stdout = error.stdout or b""
        stderr = error.stderr or b""
        stdout = stdout.decode(errors="replace") if isinstance(stdout, bytes) else stdout
        stderr = stderr.decode(errors="replace") if isinstance(stderr, bytes) else stderr
    seconds = time.perf_counter() - start
    destination.with_suffix(".out").write_text(stdout)
    destination.with_suffix(".err").write_text(stderr)
    return {"exit": code, "seconds": seconds, "stdout": stdout, "stderr": stderr}


def compare(left, right, example):
    return (left["exit"] == right["exit"] == 0 and
            normalize(left["stdout"], example) == normalize(right["stdout"], example))


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=root / "cetta")
    parser.add_argument("--petta-root", type=Path, default=root.parent / "PeTTa")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--artifacts", type=Path, default=root / "results/petta-profile-gate")
    parser.add_argument("--examples", nargs="*")
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--max-ratio", type=float, default=2)
    parser.add_argument("--timing-floor", type=float, default=0.05)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    self_test()
    if args.self_test:
        return 0
    binary, petta = args.binary.resolve(), args.petta_root.resolve()
    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    if not os.access(binary, os.X_OK) or not (petta / "run.sh").exists():
        parser.error("an executable CeTTa build and an upstream PeTTa checkout are required")
    tracked = subprocess.check_output(
        ["git", "ls-files", "examples/*.metta"], cwd=petta, text=True).splitlines()
    files = [path for path in tracked if Path(path).name not in SKIPS]
    if args.examples:
        selected = {name.removesuffix(".metta") for name in args.examples}
        files = [path for path in files if Path(path).stem in selected]
        if {Path(path).stem for path in files} != selected:
            parser.error("an example name was not found among the tracked runnable examples")
    baseline = args.baseline.resolve() if args.baseline else None
    receipt = {
        "binary_sha256": sha(binary),
        "baseline_sha256": sha(baseline) if baseline else None,
        "upstream_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=petta, text=True).strip(),
        "upstream_status": subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=petta, text=True),
        "upstream_source_hashes": {
            path: sha(petta / path) for path in subprocess.check_output(
                ["git", "ls-files", "src", "lib", "run.sh"], cwd=petta,
                text=True).splitlines() if (petta / path).is_file()},
        "skips": SKIPS, "settings": {"max_ratio": args.max_ratio,
            "timing_floor": args.timing_floor, "repeats": args.repeats},
        "cases": [], "failures": [], "oracle_failures": [],
    }
    for path in files:
        example = Path(path).name
        commands = {
            "swi": ["bash", "./run.sh", "--silent", path],
            "plain": [str(binary), "--lang", "petta", path],
            "extended": [str(binary), "--lang", "petta", "--profile", "extended", path],
        }
        if baseline:
            commands["baseline"] = [str(baseline), "--lang", "petta", path]
        case = {"example": path, "source_sha256": sha(petta / path), "runs": {}}
        for mode, command in commands.items():
            case["runs"][mode] = run(command, petta,
                artifacts / f"{Path(path).stem}-{mode}", args.timeout)
        runs = case["runs"]
        if runs["swi"]["exit"] != 0:
            # Retain incomplete clients as visible failures. Agreement with
            # a missing dependency does not qualify the client's behavior.
            receipt["oracle_failures"].append(path)
        for left, right in [("swi", "plain"), ("plain", "extended")]:
            if not compare(runs[left], runs[right], example):
                receipt["failures"].append(f"{path}: {left}/{right} output or exit")
                difference = "".join(difflib.unified_diff(
                    normalize(runs[left]["stdout"], example).splitlines(keepends=True),
                    normalize(runs[right]["stdout"], example).splitlines(keepends=True),
                    fromfile=left, tofile=right))
                (artifacts / f"{Path(path).stem}-{left}-{right}.diff").write_text(difference)
        comparisons = [("plain", "extended")]
        if baseline and compare(runs["baseline"], runs["plain"], example):
            comparisons.append(("baseline", "plain"))
        for reference, candidate in comparisons:
            if (compare(runs[reference], runs[candidate], example) and
                runs[reference]["seconds"] >= args.timing_floor and
                runs[candidate]["seconds"] > args.max_ratio * runs[reference]["seconds"]):
                repeated = {reference: [], candidate: []}
                for repetition in range(args.repeats):
                    for mode in (reference, candidate):
                        result = run(commands[mode], petta,
                            artifacts / f"{Path(path).stem}-{mode}-{repetition}", args.timeout)
                        repeated[mode].append(result)
                        if not compare(runs["swi"], result, example):
                            receipt["failures"].append(f"{path}: repeat {mode} output or exit")
                medians = {mode: statistics.median(r["seconds"] for r in series)
                           for mode, series in repeated.items()}
                case.setdefault("timing_rechecks", []).append({
                    "reference": reference, "candidate": candidate,
                    "medians": medians, "runs": repeated})
                if medians[candidate] > args.max_ratio * medians[reference]:
                    receipt["failures"].append(f"{path}: {candidate}/{reference} timing")
        receipt["cases"].append(case)
        (artifacts / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(f"{len(receipt['cases'])}/{len(files)} {example}: "
              f"plain {runs['plain']['seconds']:.3f}s, extended {runs['extended']['seconds']:.3f}s",
              flush=True)
    if receipt["failures"]:
        print("FAIL:\n" + "\n".join(receipt["failures"]))
        if receipt["oracle_failures"]:
            print("Unqualified upstream clients: " + ", ".join(receipt["oracle_failures"]))
        return 1
    print(f"PASS: {len(files)} upstream examples, both PeTTa profiles, ordered outputs and timings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
