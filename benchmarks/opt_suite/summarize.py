#!/usr/bin/env python3
"""Summarize run.py results: medians per case and engine.

usage: summarize.py RESULTS.json... [--tsv OUT.tsv]

Wall time is the whole process, including startup, loading and result
delivery. CPU is user plus system time of the measured job. MORK's execute
column is its own reported execution time, which excludes loading its input
and writing its final store. Only cells whose every trial passed its result
check get medians; other cells report their statuses.
"""
import argparse
import json
from pathlib import Path
import statistics


def median(values):
    values = [value for value in values if value is not None]
    return statistics.median(values) if values else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", nargs="+", type=Path)
    parser.add_argument("--tsv", type=Path)
    args = parser.parse_args()
    groups, order = {}, []
    for path in args.results:
        for row in json.loads(path.read_text())["rows"]:
            key = row["case"], row["engine"]
            if key not in groups:
                groups[key] = []
                order.append(key)
            groups[key].append(row)
    columns = ["case", "engine", "trials", "status", "wall_s", "wall_min_s", "wall_max_s",
               "cpu_s", "rss_mib", "rss_max_mib", "execute_s", "instructions", "counts"]
    lines = ["\t".join(columns)]
    for key in order:
        rows = groups[key]
        statuses = sorted({row["status"] for row in rows})
        complete = statuses == ["pass"]
        walls = [row["wall"] for row in rows]
        rss = [row["rss_kib"] for row in rows if row["rss_kib"] is not None]
        execute = median([row.get("execution_ms") for row in rows])

        def show(value, digits=3):
            return "-" if value is None or not complete else f"{value:.{digits}f}"

        cells = [key[0], key[1], str(len(rows)), ",".join(statuses),
                 show(median(walls)), show(min(walls)), show(max(walls)),
                 show(median([row.get("cpu") for row in rows])),
                 show(median(rss) / 1024 if rss else None, 1),
                 show(max(rss) / 1024 if rss else None, 1),
                 show(execute / 1000 if execute is not None else None),
                 show(median([row.get("instructions") for row in rows]), 0),
                 json.dumps(rows[0]["counts"])]
        lines.append("\t".join(cells))
    text = "\n".join(lines) + "\n"
    if args.tsv:
        args.tsv.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
