#!/usr/bin/env python3
"""Run generated showcase tasks on MORK and on CeTTa's PeTTa lane.

A task is the files a generator in this directory writes: NAME.mm2 (for MORK),
NAME.metta (for CeTTa), NAME.heads (the output relations), and usually
NAME.expect.json (relation sizes from the generator's independent Python
oracle), NAME.values and NAME.oracle_values.json (the oracle's complete rows
for the value-bearing relations). The MeTTa program prints one count per head,
in the order of NAME.heads, as its last numeric lines, and prints the rows of
the value-bearing relations. MORK's counts and rows come from its final store.

A trial passes only if the engine exits normally, every count equals the
oracle's, and, where the oracle has rows, the engine's complete row set equals
the oracle's. Each row of the output records wall time, user and system CPU
time and peak resident set size of the measured job.

usage: run.py TASK... --mork BINARY --cetta BINARY --output DIR
              [--trials 3] [--timeout 1800] [--cpu N] [--unlimited-stack]
              [--address-limit-kib N]
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("application_measure", ROOT / "benchmarks/mam_families/run.py")
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


def sha(path):
    with Path(path).open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def value_lines(lines, heads):
    prefixes = tuple(f"({head} " for head in heads)
    return sorted(" ".join(line.split()) for line in lines if line.strip().startswith(prefixes))


def limited(command, unlimited_stack, address_kib):
    limits = []
    if unlimited_stack:
        limits.append("--stack=unlimited")
    if address_kib:
        limits.append(f"--as={address_kib * 1024}")
    return ["prlimit", *limits, "--", *command] if limits else command


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tasks", nargs="+", type=Path, help="task path without extension")
    parser.add_argument("--mork", required=True, type=Path)
    parser.add_argument("--cetta", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--engines", default="mork,cetta")
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=1800)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--unlimited-stack", action="store_true")
    parser.add_argument("--address-limit-kib", type=int,
                        help="address-space limit for CeTTa; MORK reserves large virtual ranges")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    engines = args.engines.split(",")
    identities = {"mork": sha(args.mork), "cetta": sha(args.cetta)}
    environment = dict(os.environ, RAYON_NUM_THREADS="1")
    results = output / "results.json"
    rows = json.loads(results.read_text())["rows"] if results.exists() else []
    failed = False
    for task in args.tasks:
        base = task.resolve()
        name = base.name
        heads = (base.parent / f"{name}.heads").read_text().split()
        expect_path = base.parent / f"{name}.expect.json"
        oracle = json.loads(expect_path.read_text()) if expect_path.exists() else {}
        values_path = base.parent / f"{name}.values"
        value_heads = values_path.read_text().split() if values_path.exists() else []
        oracle_values_path = base.parent / f"{name}.oracle_values.json"
        oracle_values = None
        if oracle_values_path.exists() and value_heads:
            table = json.loads(oracle_values_path.read_text())
            oracle_values = sorted(" ".join(line.split()) for head in value_heads for line in table.get(head, []))
        sources = {suffix: sha(base.parent / f"{name}.{suffix}") for suffix in ("mm2", "metta")}
        dead = set()
        for trial in range(args.trials):
            for engine in engines if trial % 2 == 0 else list(reversed(engines)):
                if engine in dead:
                    continue
                stem = output / "runs" / name / f"{engine}-{trial}"
                stem.parent.mkdir(parents=True, exist_ok=True)
                store = stem.with_suffix(".mm2")
                if engine == "mork":
                    command = limited([str(args.mork.resolve()), "run", str(base) + ".mm2", str(store),
                                       "--instrumentation", "0"], args.unlimited_stack, None)
                else:
                    command = limited([str(args.cetta.resolve()), "--profile", "extended", "--lang", "petta",
                                       str(base) + ".metta"], args.unlimited_stack, args.address_limit_kib)
                measured = measure.paired_measure(command, base.parent, environment, stem, args.timeout,
                                                  args.cpu, False)
                ok = measured["rc"] == 0 and not measured["timed_out"]
                got, values, execution_ms = None, None, None
                if ok and engine == "mork":
                    counts = {}
                    lines = store.read_text(errors="replace").splitlines() if store.exists() else []
                    for line in lines:
                        found = re.match(r"\((\S+)", line)
                        if found:
                            counts[found[1]] = counts.get(found[1], 0) + 1
                    got = [counts.get(head, 0) for head in heads]
                    values = value_lines(lines, value_heads) if value_heads else None
                    text = Path(measured["stdout_file"]).read_text(errors="replace")
                    found = re.search(r"executing (\d+) steps took (\d+) ms", text)
                    execution_ms = int(found[2]) if found else None
                elif ok:
                    lines = Path(measured["stdout_file"]).read_text(errors="replace").splitlines()
                    numbers = [int(line.strip().strip("[]")) for line in lines
                               if re.fullmatch(r"\[?\d+\]?", line.strip())]
                    got = numbers[-len(heads):] if len(numbers) >= len(heads) else None
                    values = value_lines(lines, value_heads) if value_heads else None
                counts_ok = ok and got is not None and bool(oracle) and all(
                    oracle.get(head) is None or count == oracle[head] for head, count in zip(heads, got))
                values_ok = None if oracle_values is None else (ok and values == oracle_values)
                passed = counts_ok and values_ok is not False
                status = ("pass" if passed else "timeout" if measured["timed_out"] else
                          "exit-failure" if measured["rc"] else "different-observation")
                user, system = measured["native_user"], measured["native_system"]
                row = dict(task=name, engine=engine, trial=trial, status=status, got=got,
                           oracle=[oracle.get(head) for head in heads], values_compared=values_ok is not None,
                           values_rows=len(values) if values is not None else None,
                           wall=measured["wall"], cpu=None if user is None or system is None else user + system,
                           user=user, system=system, rss_kib=measured["rss_kib"], rc=measured["rc"],
                           timed_out=measured["timed_out"], execution_ms=execution_ms, sources=sources,
                           load=os.getloadavg()[0], when=time.strftime("%Y-%m-%d %H:%M:%S"))
                rows.append(row)
                results.write_text(json.dumps(dict(identities=identities, rows=rows), indent=2) + "\n")
                store.unlink(missing_ok=True)
                for suffix in (".stdout", ".stderr"):
                    path = stem.with_suffix(suffix)
                    if passed and path.exists() and path.stat().st_size > 1_000_000:
                        path.unlink()  # Reproducible and potentially large.
                cpu = "-" if row["cpu"] is None else f"{row['cpu']:.2f}s cpu"
                print(f"{name}: {engine} trial {trial + 1}: {status}, {measured['wall']:.3f}s wall, {cpu}, "
                      f"{measured['rss_kib']} KiB, got={got}, values={values_ok}", flush=True)
                if not passed:
                    dead.add(engine)
                    failed = True
    if identities != {"mork": sha(args.mork), "cetta": sha(args.cetta)}:
        raise ValueError("executable changed during measurement")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
