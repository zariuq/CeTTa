#!/usr/bin/env python3
"""Summarize `run.py --paired` results of the application panel.

    app_summary.py RESULTDIR [RESULTDIR ...] [--tsv FILE]

Prints one table per family: a row per case, a column per engine, each cell
the median wall time in milliseconds and the median peak memory in MiB over
the trials, or the kind of failure. Then, for each pair of engines, how many
cases are at least four times faster, one to four times faster, slower or
failing against the reference engine.
"""
import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path

ENGINES = ["cetta-petta", "cetta-he", "swi", "mork"]
TITLES = {"cetta-petta": "CeTTa PeTTa", "cetta-he": "CeTTa HE", "swi": "SWI-PeTTa",
          "mork": "native MORK"}
PAIRS = [("cetta-petta", "swi"), ("cetta-he", "swi"), ("cetta-petta", "cetta-he"),
         ("cetta-petta", "mork"), ("cetta-he", "mork"), ("swi", "mork")]


def failure(row):
    if row["status"] == "timeout":
        return "timeout"
    if row["status"] in ("wrong-answer", "invalid-output"):
        return "WRONG ANSWER"
    try:
        text = Path(row["stderr_file"]).read_text(errors="replace")
    except OSError:
        text = ""
    if "Stack overflow" in text or "overflowed its stack" in text:
        return "stack overflow"
    return f"exit {row.get('rc')}"


def load(results):
    """Cases in manifest order and one cell per (case, engine)."""
    manifest = json.loads((results / "cases-manifest.json").read_text())
    runs = defaultdict(list)
    for line in (results / "runs.jsonl").read_text().splitlines():
        row = json.loads(line)
        if row.get("phase") == "run":
            runs[row["case"], row["engine"]].append(row)
    cells = {}
    for case in manifest:
        for engine in ENGINES:
            rows = runs.get((case["case"], engine))
            if not rows:
                continue
            failed = [row for row in rows if row["status"] != "pass"]
            if failed:
                cells[case["case"], engine] = dict(
                    failure=failure(failed[0]), seconds=failed[0]["wall"], trials=len(rows))
            else:
                cells[case["case"], engine] = dict(
                    ms=statistics.median(row["wall"] for row in rows) * 1000,
                    mib=statistics.median(row["rss_kib"] for row in rows) / 1024,
                    instructions=statistics.median(row["instructions"] or 0 for row in rows),
                    trials=len(rows))
    return manifest, cells


def cell_text(cell):
    if cell is None:
        return "–"
    if "failure" in cell:
        return cell["failure"] + (f" after {cell['seconds']:.0f} s" if cell["seconds"] >= 1 else "")
    ms = f"{cell['ms']:,.0f}" if cell["ms"] >= 10 else f"{cell['ms']:.1f}"
    return f"{ms} / {cell['mib']:,.0f}"


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("results", type=Path, nargs="+")
    parser.add_argument("--tsv", type=Path)
    args = parser.parse_args()
    manifest, cells = [], {}
    for results in args.results:
        cases, found = load(results)
        manifest.extend(cases)
        cells.update(found)
    families = list(dict.fromkeys(case["family"] for case in manifest))
    for family in families:
        print(f"\n### {family}\n")
        print("| case | answers | " + " | ".join(TITLES[e] for e in ENGINES) + " |")
        print("|---|---:|" + "---:|" * len(ENGINES))
        for case in manifest:
            if case["family"] == family:
                shown = " | ".join(cell_text(cells.get((case["case"], e))) for e in ENGINES)
                print(f"| {case['case']} | {case['answers']} | {shown} |")
    for engine, reference in PAIRS:
        print(f"\n### {TITLES[engine]} against {TITLES[reference]}\n")
        print("| family | cases | 4x faster or more | 1 to 4x faster | slower | "
              "largest factor slower | fails |")
        print("|---|---:|---:|---:|---:|---:|---:|")
        total = defaultdict(float)
        for family in families + ["all"]:
            counts = total if family == "all" else defaultdict(float)
            for case in manifest:
                mine = cells.get((case["case"], engine))
                theirs = cells.get((case["case"], reference))
                if case["family"] != family or not mine or not theirs or "failure" in theirs:
                    continue
                counts["cases"] += 1
                if "failure" in mine:
                    counts["fails"] += 1
                elif theirs["ms"] >= 4 * mine["ms"]:
                    counts["much"] += 1
                elif theirs["ms"] >= mine["ms"]:
                    counts["some"] += 1
                else:
                    counts["slower"] += 1
                    counts["worst"] = max(counts["worst"], mine["ms"] / theirs["ms"])
            if family != "all":
                for key, value in counts.items():
                    total[key] = max(total[key], value) if key == "worst" else total[key] + value
            if counts["cases"]:
                worst = counts["worst"]
                factor = "" if not worst else (f"{worst:,.0f}x" if worst >= 10 else f"{worst:.1f}x")
                print(f"| {family} | {counts['cases']:.0f} | {counts['much']:.0f} | "
                      f"{counts['some']:.0f} | {counts['slower']:.0f} | {factor} | "
                      f"{counts['fails']:.0f} |")
    if args.tsv:
        with args.tsv.open("w") as out:
            out.write("case\tengine\tstatus\tms\tmib\tinstructions\ttrials\n")
            for case in manifest:
                for engine in ENGINES:
                    cell = cells.get((case["case"], engine))
                    if cell is None:
                        continue
                    if "failure" in cell:
                        out.write(f"{case['case']}\t{engine}\t{cell['failure']}\t\t\t\t"
                                  f"{cell['trials']}\n")
                    else:
                        out.write(f"{case['case']}\t{engine}\tpass\t{cell['ms']:.2f}\t"
                                  f"{cell['mib']:.1f}\t{cell['instructions']:.0f}\t"
                                  f"{cell['trials']}\n")


if __name__ == "__main__":
    main()
