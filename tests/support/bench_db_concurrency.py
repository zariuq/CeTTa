#!/usr/bin/env python3
"""Exact indexed queries over one fixed database at concurrent client counts.

This reports observed throughput and process memory. It has no performance
threshold and does not infer a hardware-cache boundary from dataset size.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import math
from pathlib import Path
import statistics
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var
from value_worker_client import Worker
from check_native_kb_service import Server, Client, await_resources
from check_space_query import open_query, drain


def node(value):
    return f"node-{value:09d}"


def physical(path):
    stats = [item.stat() for item in path.iterdir() if item.is_file()]
    return dict(file_bytes=sum(s.st_size for s in stats),
                allocated_bytes=sum(s.st_blocks * 512 for s in stats))


def percentile(samples, fraction):
    ordered = sorted(samples)
    return ordered[math.ceil(fraction * len(ordered)) - 1]


def one_query(client, source, expected):
    target = Var(0)
    plan = E("KqSpace", S("sample"), E("edge", source, target), target)
    started = time.perf_counter_ns()
    view, _ = client.snapshot()
    try:
        token = open_query(client, view, plan)
    finally:
        client.drop(view)
    rows, work = drain(client, token, 1, 8)
    elapsed = time.perf_counter_ns() - started
    assert len(rows) == 1 and rows[0][0] == S("SpaceAnswerV1"), rows
    assert rows[0][1] == expected and len(rows[0][2]) == 1, rows
    return elapsed, work


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--facts", type=int, default=131072)
    parser.add_argument("--operations", type=int, default=50,
                        help="queries per concurrent client")
    parser.add_argument("--clients", type=int, nargs="+", default=[1, 2, 4, 8])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.facts < 2 or args.operations < 1 or any(n < 1 or n > 15 for n in args.clients):
        parser.error("facts>=2, operations>=1 and client counts1..15 are required")

    with tempfile.TemporaryDirectory(prefix="db-concurrency-") as temporary:
        root = Path(temporary)
        path = root / "store"
        path.mkdir(mode=0o700)
        binary = args.worker.resolve()
        writer = Worker(binary, ROOT / "tests/support/indexed_space_fixture.metta",
                        "indexed-fixture:run", language="petta", timeout=120)
        census = Worker(binary, ROOT / "tests/support/space_benchmark_fixture.metta",
                        "bench-space:run", language="petta", timeout=120)
        server = Server(binary, root, module=ROOT / "tests/support/space_benchmark_fixture.metta",
                        entry="bench-space:configuration")
        control = None
        try:
            specs = (E("SpaceIndexV1", 3, 0), E("SpaceIndexV1", 3, 1), E("SpaceIndexV1", 3, 2))
            assert writer.call(E("Create", str(path), specs))[0] == S("KnowledgeCommittedV1")
            load_started = time.perf_counter_ns()
            for start in range(0, args.facts, 512):
                changes = tuple(E("SpaceInsertV1", offset,
                                  E("edge", node(index), node(index + 1)))
                                for offset, index in enumerate(
                                    range(start, min(args.facts, start + 512))))
                result = writer.call(E("At", str(path), E("Commit", f"load-{start}", changes)))
                assert result[0] == S("KnowledgeCommittedV1"), result
            load_ns = time.perf_counter_ns() - load_started
            storage = census.call(E("Census", str(path)))
            assert storage[0] == S("BenchStorageV1") and storage[6] == args.facts, storage

            server.start()
            control = Client(server.path)
            baseline = server.sample(control, -1)
            records = []
            for client_count in args.clients:
                barrier = threading.Barrier(client_count)

                def run_client(worker_index):
                    client = Client(server.path)
                    samples, work = [], []
                    try:
                        barrier.wait()
                        for operation in range(args.operations):
                            index = (worker_index * args.operations + operation) % args.facts
                            elapsed, observed = one_query(client, node(index), node(index + 1))
                            samples.append(elapsed)
                            work.append(observed)
                    finally:
                        client.close()
                    return samples, work

                started = time.perf_counter_ns()
                with ThreadPoolExecutor(max_workers=client_count) as executor:
                    results = list(executor.map(run_client, range(client_count)))
                elapsed = time.perf_counter_ns() - started
                await_resources(control, 0)
                after = server.sample(control, client_count)
                samples = [sample for result, _ in results for sample in result]
                work = [value for _, values in results for value in values]
                assert all(value[0] == S("KqWork") and value[2] == 1 for value in work), work[:4]
                total = client_count * args.operations
                records.append(dict(
                    clients=client_count, operations=total, elapsed_ns=elapsed,
                    operations_per_second=total * 1e9 / elapsed,
                    latency_median_ns=statistics.median(samples),
                    latency_p95_ns=percentile(samples, .95),
                    server_rss_kib=after["rss_kib"],
                    server_rss_delta_kib=after["rss_kib"] - baseline["rss_kib"],
                    server_descriptors=after["descriptors"],
                    server_resources=after["stats"][1]))

            report = dict(
                source_head=__import__("subprocess").check_output(
                    ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                facts=args.facts, load_ns=load_ns,
                namespace_bytes=dict(facts=storage[1], indexes=storage[2],
                                     receipts=storage[3], metadata=storage[4], other=storage[5]),
                physical=physical(path), baseline=baseline, concurrency=records,
                scope=("one fixed indexed database; exact one-row queries; all answers and candidate counts checked; "
                       "throughput includes snapshots, cursor ownership, CSP1 framing and result materialization; "
                       "no speed threshold or hardware-cache claim"))
            args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2))
        finally:
            if control:
                control.close()
            server.stop()
            writer.close()
            census.close()


if __name__ == "__main__":
    main()
