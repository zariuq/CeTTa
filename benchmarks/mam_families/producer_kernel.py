#!/usr/bin/env python3
"""Measure cold cyclic producers and later consumers on the native search machine.

The same executable runs with and without pure-round reuse admission. This
isolates the mechanism; it is not a comparison with an older CeTTa executable
or another engine. Process measurements include source construction, both
consumers and result publication. Internal times separate preparation, first
answer and exhaustion. Sampled dependency peaks require a nonzero quantum.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics

from run import paired_measure


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--repetitions", type=int, default=7)
    parser.add_argument("--quantum", type=int, default=0)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--instructions", action="store_true")
    parser.add_argument("--scale", choices=("smoke", "measure"), default="measure")
    args = parser.parse_args()
    if args.trials < 1 or args.repetitions < 1 or args.quantum < 0:
        parser.error("trials and repetitions must be positive; quantum must be nonnegative")
    binary = args.binary.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cpu = args.cpu if args.cpu is not None else max(os.sched_getaffinity(0))
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    identity = dict(binary_sha256=digest, cpu=cpu, trials=args.trials,
                    repetitions=args.repetitions, quantum=args.quantum,
                    instructions=args.instructions, scale=args.scale,
                    comparison="same native machine, pure reuse admission off/on",
                    observation="exact ordered distinct seed answers for each consumer",
                    read_window="one immutable ring per process",
                    memory="whole-process peak RSS and separately charged table storage;"
                           " dependency peak sampled at machine yields")
    (out / "identity.json").write_text(json.dumps(identity, indent=2) + "\n")
    cases = [(nodes, seeds) for nodes in (4, 8) for seeds in (0, 1, nodes)] \
        if args.scale == "smoke" else (
            [(nodes, 0) for nodes in (16, 64, 256, 1024)] +
            [(nodes, 1) for nodes in (16, 32, 64, 128, 256)] +
            [(nodes, nodes) for nodes in (16, 32, 64, 128)])
    records, summaries = [], []
    for nodes, seeds in cases:
        orders = {}
        samples = []
        for trial in range(args.trials):
            for licensed in ((False, True) if trial % 2 == 0 else (True, False)):
                command = [str(binary), str(nodes), str(seeds), str(int(licensed)),
                           str(args.repetitions), str(args.quantum)]
                result = paired_measure(command, binary.parent, dict(os.environ),
                    out / "runs" / f"ring-{nodes}-{seeds}" / f"{trial}-{int(licensed)}",
                    120, cpu, args.instructions)
                assert result["rc"] == 0 and not result["timed_out"], result
                rows = [json.loads(line) for line in Path(result["stdout_file"]).read_text().splitlines()]
                assert len(rows) == args.repetitions * 2, rows
                for row in rows:
                    assert (row["nodes"], row["seeds"], row["licensed"], row["quantum"]) == \
                        (nodes, seeds, licensed, args.quantum), row
                    answer_order = row["answer_order"]
                    assert sorted(answer_order) == list(range(seeds)), row
                    query = row["query_node"]
                    assert query in (0, 1), row
                    if query in orders:
                        assert answer_order == orders[query], (nodes, seeds, query, row)
                    else:
                        orders[query] = answer_order
                    if query == 1:
                        assert row["rounds"] == row["reused_rounds"] == 0, row
                    if not args.quantum:
                        assert row["dependency_sample_peak_bytes"] is None, row
                    samples.append(dict(trial=trial, **row))
                records.append(dict(nodes=nodes, seeds=seeds, trial=trial,
                                    licensed=licensed, samples=rows, **result))
        medians = {str(int(licensed)): {
            str(query): {metric: statistics.median(row[metric] for row in samples
                if row["licensed"] == licensed and row["query_node"] == query)
                for metric in ("prepare_ns", "first_ns", "all_ns", "rounds", "reused_rounds",
                               "table_control_bytes", "answer_vector_bytes",
                               "table_arena_live_bytes", "table_arena_reserved_bytes")}
            for query in (0, 1)} for licensed in (False, True)}
        ratio = medians["1"]["0"]["all_ns"] / medians["0"]["0"]["all_ns"]
        summary = dict(nodes=nodes, seeds=seeds, medians=medians, cold_time_ratio=ratio)
        summaries.append(summary)
        print(f"ring {nodes:4} seeds {seeds:4}: cold ratio {ratio:.4f}; "
              f"rounds {medians['0']['0']['rounds']:.0f} -> "
              f"{medians['1']['0']['rounds']:.0f}", flush=True)
        (out / "results.json").write_text(json.dumps(
            dict(identity=identity, summaries=summaries, rows=records), indent=2) + "\n")
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest


if __name__ == "__main__":
    main()
