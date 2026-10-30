#!/usr/bin/env python3
"""Small native graph scaling sweep with checked results and source receipts."""
import argparse
from collections import deque
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import subprocess
import sys
import time


def graph(shape, count):
    if shape == "chain":
        return [(i, i + 1) for i in range(count - 1)]
    if shape == "star":
        return [(0, i) for i in range(1, count)]
    if shape == "cycle":
        return [(i, (i + 1) % count) for i in range(count)]
    if shape == "skew":
        width = max(1, math.isqrt(count))
        return [(0, i) for i in range(1, count)] + [(i, i // width) for i in range(1, count)]
    rng = random.Random(61029 + count)
    return [(i, rng.randrange(count)) for i in range(count) for _ in range(3)]


def oracle(edges):
    adjacency = {}
    for i, (source, target) in enumerate(edges):
        adjacency.setdefault(source, []).append((i, target))
    distances, pending, positive = {0: 0}, deque([0]), set()
    while pending:
        source = pending.popleft()
        for _, target in adjacency.get(source, []):
            positive.add(target)
            if target not in distances:
                distances[target] = distances[source] + 1
                pending.append(target)
    return adjacency, positive, distances


def memory(pid):
    values = {}
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        if line.startswith(("VmRSS:", "VmHWM:")):
            key, value, unit = line.split()
            values[key[:-1]] = int(value) * 1024
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--cetta-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sizes", type=int, nargs="+", default=[64, 128, 256, 512])
    parser.add_argument("--shapes", nargs="+", choices=["chain", "star", "skew", "cycle", "random"],
                        default=["chain", "star", "skew", "cycle", "random"])
    parser.add_argument("--repeats", type=int, default=5)
    args = parser.parse_args()
    if args.repeats < 1 or any(n < 2 for n in args.sizes):
        parser.error("repeats must be positive and sizes at least two")
    root, binary = args.cetta_root.resolve(), args.worker.resolve(strict=True)
    sys.path.insert(0, str(root / "tools"))
    from value_worker_client import Worker
    from space_service_client import E, S, encode
    args.output.mkdir(parents=True, exist_ok=False)
    records, measured_cases = [], []
    for shape in args.shapes:
        for count in args.sizes:
            edges = graph(shape, count)
            adjacency, reachable, distances = oracle(edges)
            path = (args.output / f"{shape}-{count}").resolve()
            path.mkdir(mode=0o700)
            worker = Worker(binary, root / "tests/support/kb_graph_probe.metta", "kbg-probe:run",
                            language="petta", import_mode="relative", profile=True, timeout=120)
            try:
                assert worker.call(E("Create", str(path)))[0] == S("KnowledgeReadyV1")
                baseline = memory(worker.process.pid)
                started = time.perf_counter_ns()
                for offset in range(0, len(edges), 128):
                    edits = tuple(E("KbPut", i, "edge", edges[i]) for i in range(offset, min(offset + 128, len(edges))))
                    result = worker.call(E("At", str(path), E("Commit", E("load", offset), edits)))
                    assert result[0] == S("KnowledgeCommittedV1"), result
                load_ns = time.perf_counter_ns() - started
                case_id = f"{shape}-{count}"
                case_record = {"case_id": case_id, "shape": shape, "vertices": count,
                               "edges": len(edges), "load_and_index_ns": load_ns,
                               "memory_before_load": baseline,
                               "memory_after_load": memory(worker.process.pid)}
                spec = E("KgRelation", "edge", 0, 1)
                cases = [("first-neighbour", E("KgNeighbours", spec, 0, S("out")), E("First", 1, 2)),
                         ("reach", E("KgReach", spec, 0, S("out")), E("Pages", 128, 512)),
                         ("shortest-one", E("KgShortest", spec, 0, count - 1, S("one"), S("out")), E("Pages", 1, 512))]
                for name, plan, command in cases:
                    samples, metrics = [], []
                    memory_before = memory(worker.process.pid)
                    for repeat in range(args.repeats + 1):
                        started = time.perf_counter_ns()
                        result = worker.call(E("Graph", str(path), plan, max(16, 4 * count), command))
                        elapsed = time.perf_counter_ns() - started
                        if name == "first-neighbour":
                            assert result[0] == S("KgFirst"), result
                            rows, work = result[1], result[3]
                            assert len(rows) == min(1, len(adjacency.get(0, [])))
                            for row in rows:
                                assert row[0] == S("KgEdge") and edges[row[1]] == row[2:]
                        else:
                            assert result[0] == S("KgPages"), result
                            rows = tuple(row for page in result[1] for row in page)
                            work = result[2]
                            if name == "reach":
                                assert len(rows) == len(reachable) and set(rows) == reachable
                            elif count - 1 not in distances:
                                assert not rows
                            else:
                                assert len(rows) == 1
                                _, nodes, ids, distance = rows[0]
                                assert nodes[0] == 0 and nodes[-1] == count - 1
                                assert distance == distances[count - 1] == len(ids)
                                assert all(edges[identity] == (nodes[i], nodes[i + 1]) for i, identity in enumerate(ids))
                        if repeat == 0:
                            first_ns = elapsed
                        else:
                            samples.append(elapsed)
                            metrics.append(dict(worker.last_metrics))
                    record = {"case_id": case_id, "shape": shape, "vertices": count,
                              "edges": len(edges), "operation": name, "first_query_ns": first_ns,
                              "samples_ns": samples, "median_ns": statistics.median(samples),
                              "sample_count": len(samples), "max_sample_ns": max(samples),
                              "empirical_p95_ns": sorted(samples)[math.ceil(.95 * len(samples)) - 1],
                              "result_rows": len(rows), "result_bytes": len(encode(rows)),
                              "steps": work[1], "candidate_keys": work[2], "decoded_source_bytes": work[3],
                              "frontier_admissions": work[4], "worker_metrics": metrics,
                              "memory_before_operation": memory_before,
                              "memory_after_operation": memory(worker.process.pid)}
                    records.append(record)
                    print(shape, count, name, f"{record['median_ns']/1e6:.3f} ms", "keys", work[2], flush=True)
                case_record["memory_after_queries"] = memory(worker.process.pid)
                insert_samples, delete_samples = [], []
                for repeat in range(args.repeats):
                    update_start = time.perf_counter_ns()
                    added = worker.call(E("At", str(path), E("Commit", E("benchmark-insert", repeat),
                                        (E("KbPut", S("temporary"), "edge", (count, 0)),))))
                    insert_samples.append(time.perf_counter_ns() - update_start)
                    delete_start = time.perf_counter_ns()
                    removed = worker.call(E("At", str(path), E("Commit", E("benchmark-delete", repeat),
                                          (E("KbDelete", S("temporary")),))))
                    delete_samples.append(time.perf_counter_ns() - delete_start)
                    assert added[0] == removed[0] == S("KnowledgeCommittedV1")
                files = [p.stat() for p in path.rglob("*") if p.is_file()]
                stored_bytes = sum(item.st_size for item in files)
                case_record.update(insert_samples_ns=insert_samples, delete_samples_ns=delete_samples,
                                   storage_bytes=stored_bytes,
                                   allocated_storage_bytes=sum(item.st_blocks * 512 for item in files),
                                   storage_bytes_per_edge=stored_bytes / len(edges),
                                   memory_after_updates=memory(worker.process.pid))
                measured_cases.append(case_record)
            finally:
                assert worker.close() == 0
    sources = ["lib/petta/kb.metta", "lib/petta/kb_graph.metta", "lib/petta/knowledge.metta",
               "tests/support/kb_graph_probe.metta", "tests/support/bench_kb_graph.py"]
    receipt = {"format_version": 2, "worker_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
               "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
               "working_tree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True)),
               "sources": {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sources},
               "scope": "small graph scaling; first query after loading does not imply a cold OS page cache; worker RPC and response materialization included; no service or cross-engine performance claim",
               "measurement_notes": {
                   "percentile": "empirical nearest-rank over the recorded warm samples; with fewer than 20 samples p95 equals the maximum, not a population-tail estimate",
                   "memory": "VmHWM is process-cumulative, including loading and earlier operations; no per-operation peak attribution. VmRSS is a point observation, not retained logical bytes",
                   "updates": "independent insert/delete samples belong to each case, not to each query operation; unique receipt IDs ensure real updates rather than replay",
                   "storage": "file lengths and allocated blocks include indexes, journal/metadata and all load/update receipts; not isolated index bytes"},
               "cases": measured_cases, "records": records}
    (args.output / "results.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
