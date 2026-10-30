#!/usr/bin/env python3
"""Measure native KB loading and indexed/incremental reads at explicit sizes."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--cetta-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sizes", type=int, nargs="+", default=[128, 256, 512, 1024, 2048, 4096])
    parser.add_argument("--repeats", type=int, default=9)
    args = parser.parse_args()
    if args.repeats < 1 or any(n < 1 for n in args.sizes):
        parser.error("sizes and repeats must be positive")
    root = args.cetta_root.resolve()
    worker_binary = args.worker.resolve(strict=True)
    sys.path.insert(0, str(root / "tools"))
    from value_worker_client import Worker
    from space_service_client import E, S, encode
    args.output.mkdir(parents=True, exist_ok=False)
    records = []
    worker = Worker(worker_binary, root / "tests/support/kb_probe.metta", "kb-probe:run",
                    language="petta", import_mode="relative", profile=True)
    try:
        for count in args.sizes:
            store = (args.output / str(count)).resolve()
            store.mkdir(mode=0o700)
            assert worker.call(E("Create", str(store)))[0] == S("KnowledgeReadyV1")
            started = time.perf_counter_ns()
            for offset in range(0, count, 128):
                changes = tuple(E("KbPut", i, "edge", (i, i + 1)) for i in range(offset, min(count, offset + 128)))
                result = worker.call(E("At", str(store), E("Commit", offset, changes)))
                assert result[0] == S("KnowledgeCommittedV1"), result
            load_ns = time.perf_counter_ns() - started
            for label, command in [
                ("exact-argument", E("Select", E("KbArgument", "edge", 0, count // 2))),
                ("first-page", E("FirstPage", E("KbRelation", "edge"), 1)),
            ]:
                samples, metrics = [], []
                for repeat in range(args.repeats + 1):
                    started = time.perf_counter_ns()
                    result = worker.call(E("At", str(store), command))
                    elapsed = time.perf_counter_ns() - started
                    if label == "exact-argument":
                        assert encode(result[1]) == encode((E("KbFact", count // 2, "edge", (count // 2, count // 2 + 1)),))
                        work = result[2]
                        assert work[1] == 1 and work[2] <= 2, work
                    else:
                        assert len(result[1]) == 1 and result[2] == S("KbMore"), result
                        work = result[3]
                        assert work[1:3] == (1, 1), work
                    if repeat == 0:
                        cold_ns = elapsed
                    else:
                        samples.append(elapsed)
                        metrics.append(dict(worker.last_metrics))
                ordered = sorted(samples)
                records.append({"rows": count, "operation": label, "load_ns": load_ns,
                    "cold_ns": cold_ns, "median_ns": statistics.median(samples),
                    "p95_ns": ordered[min(len(ordered) - 1, int(0.95 * len(ordered)))],
                    "examined_keys": work[2], "decoded_bytes": work[3],
                    "store_bytes": sum(p.stat().st_size for p in store.rglob("*") if p.is_file()),
                    "worker_metrics": metrics})
    finally:
        status = worker.close()
    receipt = {"worker_sha256": hashlib.sha256(worker_binary.read_bytes()).hexdigest(),
        "sources": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                    for name in ["lib/petta/kb.metta", "lib/petta/knowledge.metta", "tests/support/kb_probe.metta", "tests/support/bench_kb_selectors.py"]},
        "records": records, "worker_exit": status,
        "scope": "small native selector/first-page scaling; not a whole graph workload or comparative database benchmark"}
    (args.output / "results.json").write_text(json.dumps(receipt, indent=2) + "\n")
    for row in records:
        print(f"{row['rows']:7} {row['operation']:16} median_ms={row['median_ns']/1e6:.3f} keys={row['examined_keys']} bytes={row['decoded_bytes']}")
    assert status == 0, status


if __name__ == "__main__":
    main()
