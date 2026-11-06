#!/usr/bin/env python3
"""Answer-checked timing of PeTTa's examples on SWI-PeTTa and CeTTa.

The population is every tracked examples/*.metta file directly inside the
PeTTa checkout. Each example is first run once on both engines through the
corpus harness (scripts/petta_corpus_manifest.py), with its fixture policy
and output normalization, and classified (MATCH, MATCH_REORDERED,
EXIT_MISMATCH, STDOUT_MISMATCH, STDERR_MISMATCH or a timeout). Then each
example without a fixture is timed: TRIALS fresh processes per engine, in
alternating order, recording wall time, user and system CPU time and peak
resident set size. A timeout is a recorded incomplete cell, never a time.

usage: measure.py --petta-root DIR --cetta BINARY --output DIR
                  [--trials 3] [--timeout 600] [--cpu N] [--only a.metta,b.metta]
                  [--classify-only | --time-only]
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import petta_corpus_manifest as corpus  # noqa: E402
import bench_petta_live_sample as sample  # noqa: E402

spec = importlib.util.spec_from_file_location("application_measure", ROOT / "benchmarks/mam_families/run.py")
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


def classify_all(names, petta_root, cetta, timeout, output):
    actual = output / "actual"
    actual.mkdir(parents=True, exist_ok=True)
    rows = {}
    for index, name in enumerate(names):
        source = petta_root / "examples" / name
        fixture = corpus.FIXTURE_CASES.get(name)
        runs = (("petta", corpus.run_oracle, (ROOT, petta_root, source, timeout, fixture)),
                ("cetta", corpus.run_cetta, (cetta, petta_root, source, timeout, fixture)))
        if index % 2:
            runs = tuple(reversed(runs))
        results, elapsed = {}, {}
        for engine, function, arguments in runs:
            results[engine], elapsed[engine] = sample.run_timed(function, *arguments, record_timeouts=True)
        status, observation_equal, stream_equal, stderr_equal = sample.classify(
            results["petta"], results["cetta"], corpus.STDOUT_EXACT_STREAM)
        for engine in ("petta", "cetta"):
            (actual / f"{name}.{engine}.stdout").write_text(results[engine][1], encoding="utf-8")
            (actual / f"{name}.{engine}.stderr").write_text(results[engine][2], encoding="utf-8")
        rows[name] = dict(example=name, status=status, fixture=fixture.get("name") if fixture else None,
                          petta_exit=results["petta"][0], cetta_exit=results["cetta"][0],
                          stdout_observation_equal=observation_equal, stdout_stream_equal=stream_equal,
                          stderr_equal=stderr_equal, petta_classify_seconds=elapsed["petta"],
                          cetta_classify_seconds=elapsed["cetta"], source_sha256=corpus.sha256_file(source))
        print(f"[{index + 1}/{len(names)}] {name}: {status} "
              f"(SWI {elapsed['petta']:.2f}s, CeTTa {elapsed['cetta']:.2f}s)", flush=True)
    return rows


def time_all(names, classified, petta_root, cetta, timeout, trials, cpu, output):
    environment = corpus.fixture_environment(ROOT, None)
    timings = {}
    for index, name in enumerate(names):
        row = classified.get(name, {})
        if corpus.FIXTURE_CASES.get(name):
            continue  # Fixture workspaces are timed only by the classification pass.
        commands = {"petta": ["sh", "run.sh", f"examples/{name}", "--silent"],
                    "cetta": [str(cetta), "--lang", "petta", str(petta_root / "examples" / name)]}
        # An engine that did not complete within the generous classification
        # timeout is reported as a timeout, not re-run three more times.
        engines = [engine for engine in ("petta", "cetta") if row.get(f"{engine}_exit") != "timeout"]
        cells = {engine: [] for engine in engines}
        for trial in range(trials):
            order = engines if (index + trial) % 2 == 0 else list(reversed(engines))
            for engine in order:
                stem = output / "runs" / name / f"{engine}-{trial}"
                measured = measure.paired_measure(commands[engine], petta_root, environment, stem,
                                                  timeout, cpu, False)
                cells[engine].append(dict(trial=trial, rc=measured["rc"], timed_out=measured["timed_out"],
                                          wall=measured["wall"], user=measured["native_user"],
                                          system=measured["native_system"], rss_kib=measured["rss_kib"]))
                for suffix in (".stdout", ".stderr", ".time"):
                    stem.with_suffix(suffix).unlink(missing_ok=True)
        timings[name] = cells
        summary = []
        for engine, runs in cells.items():
            walls = [run["wall"] for run in runs if not run["timed_out"]]
            summary.append(f"{engine} {statistics.median(walls):.3f}s" if walls else f"{engine} timeout")
        print(f"[{index + 1}/{len(names)}] {name}: " + ", ".join(summary), flush=True)
    return timings


def median(values):
    values = [value for value in values if value is not None]
    return statistics.median(values) if values else None


def write_summary(path, names, classified, timings, timeout):
    columns = ["example", "status", "fixture", "swi_exit", "cetta_exit",
               "swi_wall_s", "swi_cpu_s", "swi_rss_mib", "cetta_wall_s", "cetta_cpu_s", "cetta_rss_mib",
               "trials"]
    lines = ["\t".join(columns)]
    for name in names:
        row = classified.get(name, {})
        cells = [name, row.get("status", "-"), row.get("fixture") or "-",
                 str(row.get("petta_exit", "-")), str(row.get("cetta_exit", "-"))]
        trials = []
        for engine in ("petta", "cetta"):
            runs = timings.get(name, {}).get(engine)
            if runs is None:
                timed_out = row.get(f"{engine}_exit") == "timeout"
                cells += [f">{timeout:g}" if timed_out else "-", "-", "-"]
                continue
            complete = [run for run in runs if not run["timed_out"]]
            trials.append(f"{engine}:{len(complete)}/{len(runs)}")
            if len(complete) != len(runs):
                cells += [f">{timeout:g}", "-", "-"]
                continue
            cpu = median([run["user"] + run["system"] for run in complete
                          if run["user"] is not None and run["system"] is not None])
            rss = median([run["rss_kib"] for run in complete])
            cells += [f"{median([run['wall'] for run in complete]):.4f}",
                      "-" if cpu is None else f"{cpu:.4f}",
                      "-" if rss is None else f"{rss / 1024:.1f}"]
        cells.append(",".join(trials) or "-")
        lines.append("\t".join(cells))
    path.write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--petta-root", required=True, type=Path)
    parser.add_argument("--cetta", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=600.0)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--only", help="comma-separated example names")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--classify-only", action="store_true")
    group.add_argument("--time-only", action="store_true",
                       help="reuse the classification in OUTPUT/classification.json")
    args = parser.parse_args()
    petta_root, cetta, output = args.petta_root.resolve(), args.cetta.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tracked = sample.git_text(petta_root, "ls-files", "--", "examples/*.metta").splitlines()
    names = [name for _, name in sample.select_tracked_examples(tracked)]
    if args.only:
        wanted = set(args.only.split(","))
        names = [name for name in names if name in wanted]
    identity = dict(petta_revision=sample.git_text(petta_root, "rev-parse", "HEAD"),
                    petta_tracked_status=sample.git_text(petta_root, "status", "--porcelain",
                                                         "--untracked-files=no").splitlines(),
                    petta_run_sh_sha256=corpus.sha256_file(petta_root / "run.sh"),
                    cetta_sha256=corpus.sha256_file(cetta), examples=len(names),
                    timeout_seconds=args.timeout, trials=args.trials, cpu=args.cpu)
    classification = output / "classification.json"
    if args.time_only:
        classified = json.loads(classification.read_text())["rows"]
    else:
        classified = classify_all(names, petta_root, cetta, args.timeout, output)
        classification.write_text(json.dumps(dict(identity=identity, rows=classified), indent=2) + "\n")
    timings = {}
    if not args.classify_only:
        timings = time_all(names, classified, petta_root, cetta, args.timeout, args.trials, args.cpu, output)
        (output / "timings.json").write_text(json.dumps(dict(identity=identity, rows=timings), indent=2) + "\n")
    write_summary(output / "summary.tsv", names, classified, timings, args.timeout)
    counts = {}
    for row in classified.values():
        counts[row["status"]] = counts.get(row["status"], 0) + 1
    print(json.dumps(dict(sorted(counts.items()))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
