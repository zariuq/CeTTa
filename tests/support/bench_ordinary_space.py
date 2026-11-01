#!/usr/bin/env python3
"""Checked shape/size sweeps over ordinary durable Space query adapters.

Timing samples include RPC and result materialization. Fresh-process queries
do not establish a cold OS cache. Logical namespace bytes and physical file
bytes are different metrics. Performance observations are not pass thresholds.
"""
import argparse
from collections import Counter, deque
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S, Var, encode
from check_native_kb_service import Server, Client, await_resources
from check_space_graph import open_graph, service_rows
from check_space_query import open_query, drain


def graph(shape, size):
    if shape == "chain": return [(i, i + 1) for i in range(size - 1)]
    if shape == "star": return [(0, i) for i in range(1, size)]
    if shape == "cycle": return [(i, (i + 1) % size) for i in range(size)]
    if shape == "skew":
        width = max(1, math.isqrt(size))
        return [(0, i) for i in range(1, size)] + [(i, i // width) for i in range(1, size)]
    rng = random.Random(61031 + size)
    return [(i, rng.randrange(size)) for i in range(size) for _ in range(3)]


def node(value): return f"node-{value:08d}"
def bag(values): return Counter(map(encode, values))


def memory(pid):
    status = dict(line.split(":", 1) for line in (Path("/proc") / str(pid) / "status").read_text().splitlines())
    return {field: int(status[field].split()[0]) * 1024 for field in ("VmRSS", "VmHWM")}


def physical(path):
    items = [item.stat() for item in path.iterdir() if item.is_file()]
    return {"file_bytes": sum(s.st_size for s in items), "allocated_bytes": sum(s.st_blocks * 512 for s in items)}


def timed(function):
    start = time.perf_counter_ns()
    value = function()
    return value, time.perf_counter_ns() - start


def summary(samples):
    ordered = sorted(samples)
    return {"samples_ns": samples, "median_ns": statistics.median(samples), "min_ns": ordered[0],
            "max_ns": ordered[-1], "empirical_p95_ns": ordered[math.ceil(.95 * len(samples)) - 1],
            "serial_operations_per_second_at_median": 1e9 / statistics.median(samples)}


def queries(edges, identities, size):
    adjacency = {}
    for identity, (source, target) in zip(identities, edges):
        adjacency.setdefault(source, []).append((identity, target))
    reached, distances, pending = set(), {0: 0}, deque([0])
    while pending:
        source = pending.popleft()
        for identity, target in adjacency.get(source, []):
            reached.add(target)
            if target not in distances:
                distances[target] = distances[source] + 1; pending.append(target)
    x, y, left_id = Var(0), Var(1), Var(2)
    lookup = E("KqSpace", S("sample"), E("edge", node(size // 2), x), E("Edge", x))
    lookup_answers = [E("SpaceAnswerV1", E("Edge", node(target)), (identity,)) for identity, target in adjacency.get(size // 2, [])]
    left = E("KqSpace", S("sample"), E("edge", node(0), x), E("Via", x))
    binding = E("SpaceAnswerV1", E("Via", x), (left_id,))
    right = E("KqSpace", S("sample"), E("edge", x, y), E("Hop2", left_id, x, y))
    join = E("KqBind", left, binding, right)
    join_answers = [E("SpaceAnswerV1", E("Hop2", first, node(mid), node(target)), (second,))
                    for first, mid in adjacency.get(0, []) for second, target in adjacency.get(mid, [])]
    count = E("KqFold", E("KqSpace", S("sample"), E("edge", x, y), S("row")), 0, S("bench-space:count"), 1048576)
    spec = E("KgRelation", S("edge"), 0, 1)
    plans = [("lookup", "rel", lookup, lookup_answers), ("two-hop", "rel", join, join_answers),
             ("count", "rel", count, [len(edges)]),
             ("first-neighbour", "first", E("KgNeighbours", spec, node(0), S("out")),
              [E("KgEdge", identity, node(0), node(target)) for identity, target in adjacency.get(0, [])]),
             ("reach", "graph", E("KgReach", spec, node(0), S("out")), [node(v) for v in reached]),
             ("shortest-one", "graph", E("KgShortest", spec, node(0), node(size - 1), S("one"), S("out")), None)]
    def check(name, actual, expected):
        if name == "first-neighbour":
            assert len(actual) == min(1, len(expected)) and all(encode(v) in bag(expected) for v in actual), actual
        elif name == "shortest-one":
            if size - 1 not in distances:
                assert not actual, actual
            else:
                assert len(actual) == 1, actual
                _, nodes, ids, distance = actual[0]
                assert distance == distances[size - 1] == len(ids) and nodes[0] == node(0) and nodes[-1] == node(size - 1), actual
                lookup = {encode(identity): pair for identity, pair in zip(identities, edges)}
                assert all(lookup[encode(identity)] == (int(nodes[i][5:]), int(nodes[i + 1][5:])) for i, identity in enumerate(ids)), actual
        else:
            assert bag(actual) == bag(expected), (name, actual, expected)
    return plans, check


def embedded(worker, path, kind, plan, count, states):
    command = E("First", str(path), plan, states) if kind == "first" else (
        E("Rel", str(path), plan, count, 256) if kind == "rel" else E("Graph", str(path), plan, count, 256, states))
    result = worker.call(command)
    if kind == "first":
        assert result[0] == S("BenchFirstV1"), result
        return list(result[1]), result[3]
    assert result[0] == S("KqPages" if kind == "rel" else "GraphCompleteV1"), result
    return [row for page in result[1] for row in page], result[2]


def served(client, kind, plan, count, states):
    view, _ = client.snapshot()
    try:
        token = open_query(client, view, plan) if kind == "rel" else open_graph(client, view, plan, states=states)
    finally:
        client.drop(view)
    if kind != "first":
        return drain(client, token, count, 256) if kind == "rel" else service_rows(client, token, count, 256)
    for _ in range(20000):
        reply = client.call(E("UseValueV1", token, S("GraphPage"), E("GraphPageV1", 1, 32)))
        if reply[0] != S("ServiceResourceV1"):
            assert reply[0] == S("GraphPageV1") and reply[2][0] == S("Complete"), reply
            return list(reply[1]), reply[2][1], None
        token, page = reply[1:]
        if page[1]:
            inspected = client.call(E("UseValueV1", token, S("GraphInspect"), ()))
            assert inspected[0] == S("ServiceResourceV1"), inspected
            token = inspected[1]
            client.call(E("UseValueV1", token, S("GraphCancel"), ()))
            return list(page[1]), None, inspected[2]
    raise AssertionError("first answer did not settle")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sizes", type=int, nargs="+", default=[32, 64, 128, 256])
    parser.add_argument("--shapes", nargs="+", choices=["chain", "star", "skew", "cycle", "random"], default=["chain", "star", "skew", "cycle", "random"])
    parser.add_argument("--indexing", choices=["both", "indexed", "scan"], default="indexed")
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.repeats < 1 or any(n < 2 for n in args.sizes): parser.error("positive repeats and sizes>=2 required")
    binary = args.worker.resolve()
    output = args.output.resolve(); output.mkdir(mode=0o700, parents=True, exist_ok=False)
    cases, records = [], []
    fixture = ROOT / "tests/support/space_benchmark_fixture.metta"
    sources = [str(p.relative_to(ROOT)) for p in [Path(__file__).resolve(), fixture,
        ROOT / "lib/petta/durable_space.metta", ROOT / "lib/petta/durable_indexes.metta",
        ROOT / "lib/petta/kb_query.metta", ROOT / "lib/petta/query_space_source.metta",
        ROOT / "lib/petta/kb_graph.metta", ROOT / "lib/petta/graph_space_source.metta",
        ROOT / "examples/native_space_graph_server.metta", ROOT / "examples/native_space_query_server.metta",
        ROOT / "examples/native_kb_server.metta", ROOT / "tests/support/space_graph_fixture.metta",
        ROOT / "tests/support/space_query_fixture.metta", ROOT / "tests/support/kb_query_probe.metta",
        ROOT / "tools/value_worker_client.py", ROOT / "tools/space_service_client.py"]]
    receipt = {"version": 1, "source_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
               "sources": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources},
               "worker_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "cases": cases, "queries": records,
               "scope": "ordinary captured durable Space adapters, embedded and local CSP1 service; synthetic small derived neighbourhood/aggregation shapes, not an LDBC benchmark",
               "measurement_notes": {
                   "cold": "fresh query process after explicit Echo admission; module/startup admission measured separately; OS caches not dropped",
                   "warm": "repeated complete queries, fresh captured view each time; RPC and result materialization included",
                   "p95": "nearest rank empirical p95; fewer than20samples means max, not a population-tail estimate",
                   "work": "logical steps, candidate visits and decoded source bytes; not all I/O or CPU instructions",
                   "storage": "logical namespace key+value bytes separate from all physical file lengths/blocks, which include receipts and journal/index overhead",
                   "memory": "RSS point samples/total bytes per fact include runtime/program baseline; HWM process cumulative,not query-specific. Native permanent/scratch counters separately retained",
                   "throughput": "reciprocal median serial latency,not a concurrent service saturation measurement; result-row rate reported separately",
                   "durability": "acknowledged update latency includes existing validation,journal and synchronization; no isolated fsync-time or power-loss claim"}}
    def save(): (output / "results.json").write_text(json.dumps(receipt, indent=2) + "\n")
    save()
    for indexed in ([False, True] if args.indexing == "both" else [args.indexing == "indexed"]):
        for shape in args.shapes:
            for size in args.sizes:
                case_id = f"{shape}-{size}-{'indexed' if indexed else 'scan'}"
                folder = output / case_id; folder.mkdir(mode=0o700)
                path = folder / "store"; path.mkdir(mode=0o700)
                writer = Worker(binary, ROOT / "tests/support/indexed_space_fixture.metta", "indexed-fixture:run", language="petta", profile=True)
                maintenance = Worker(binary, ROOT / "tests/support/durable_reindex_fixture.metta", "reindex-fixture:run", language="petta")
                socket_directory = tempfile.TemporaryDirectory(prefix="space-bench-")
                server = Server(binary, Path(socket_directory.name), module=fixture, entry="bench-space:configuration")
                server.environment["CETTA_KB_DIRECTORY"] = str(path)
                client = evaluator = None
                try:
                    edges = graph(shape, size)
                    specs = tuple(E("SpaceIndexV1", 3, i) for i in [0, 1, 2])
                    created = writer.call(E("Create", str(path), specs) if indexed else E("CreatePlain", str(path)))
                    assert created[0] == S("KnowledgeCommittedV1"), created
                    before_load = memory(writer.process.pid)
                    identities, load_samples = [], []
                    for start in range(0, len(edges), 64):
                        changes = tuple(E("SpaceInsertV1", i, E("edge", node(s), node(t))) for i, (s, t) in enumerate(edges[start:start + 64]))
                        answer, elapsed = timed(lambda: writer.call(E("At", str(path), E("Commit", f"load-{start}", changes))))
                        assert answer[0] == S("KnowledgeCommittedV1"), answer
                        identities.extend(answer[1][1]); load_samples.append(elapsed)
                    case = {"id": case_id, "shape": shape, "vertices": size, "facts": len(edges), "indexed": indexed,
                            "load_and_initial_indexes_samples_ns": load_samples, "load_and_initial_indexes_ns": sum(load_samples),
                            "before_rebuild": physical(path), "writer_before_load": before_load, "writer_after_load": memory(writer.process.pid)}
                    rebuilt, rebuild_ns = timed(lambda: maintenance.call(E("At", str(path), E("Reindex", "rebuild", 64 * 1024 * 1024, 64))))
                    assert rebuilt[0] == S("KnowledgeCommittedV1"), rebuilt
                    case["rebuild_ns"] = rebuild_ns
                    assert writer.call(E("At", str(path), E("Compact")))[0] == S("IndexedCompacted")
                    evaluator = Worker(binary, fixture, "bench-space:run", language="petta", profile=True, timeout=120)
                    assert evaluator.call(S("Echo")) == S("Echo")
                    census = evaluator.call(E("Census", str(path)))
                    assert census[0] == S("BenchStorageV1") and census[6] == len(edges), census
                    case["namespace_key_value_bytes"] = dict(zip(["facts", "indexes", "receipts", "metadata", "other"], census[1:6]))
                    case["physical_after_compact"] = physical(path)
                    case["physical_bytes_per_fact"] = case["physical_after_compact"]["file_bytes"] / len(edges)
                    server.start(); client = Client(server.path)
                    plans, check = queries(edges, identities, size)
                    for name, kind, plan, expected in plans:
                        states = max(64, 8 * size)
                        fresh = Worker(binary, fixture, "bench-space:run", language="petta", profile=True, timeout=120)
                        try:
                            echo, startup_ns = timed(lambda: fresh.call(S("Echo"))); assert echo == S("Echo")
                            (actual, work), first_ns = timed(lambda: embedded(fresh, path, kind, plan, 32, states))
                            check(name, actual, expected)
                            cold_metrics = dict(fresh.last_metrics)
                        finally: assert fresh.close() == 0
                        samples, metrics = [], []
                        for _ in range(args.repeats):
                            (actual, work), elapsed = timed(lambda: embedded(evaluator, path, kind, plan, 32, states))
                            check(name, actual, expected); samples.append(elapsed); metrics.append(dict(evaluator.last_metrics))
                        service_samples, inspections = [], []
                        for page_count in [1, 32]:
                            response, elapsed = timed(lambda: served(client, kind, plan, page_count, states))
                            check(name, response[0], expected); service_samples.append({"page_rows": page_count, "ns": elapsed, "result_rows": len(response[0])})
                            if kind == "first": inspections.append(repr(response[2]))
                            else: assert bag(response[0]) == bag(actual) or name == "shortest-one"
                        record = {"case": case_id, "operation": name, "fresh_admission_ns": startup_ns, "fresh_query_ns": first_ns,
                                  "warm": summary(samples), "fresh_worker_metrics": cold_metrics, "warm_worker_metrics": metrics,
                                  "result_rows": len(actual), "result_cav_bytes": len(encode(tuple(actual))), "work": {"steps": work[1], "candidates": work[2], "decoded_bytes": work[3]},
                                  "service": service_samples, "inspection": inspections, "memory_after_operation": memory(evaluator.process.pid)}
                        record["result_rows_per_second_at_median"] = len(actual) * 1e9 / record["warm"]["median_ns"]
                        record["process_rss_bytes_per_fact_including_runtime"] = record["memory_after_operation"]["VmRSS"] / len(edges)
                        records.append(record); save()
                        print(case_id, name, f"{record['warm']['median_ns']/1e6:.3f}ms", f"candidates={work[2]}", f"rows={len(actual)}", flush=True)
                    inserts, deletes = [], []
                    for repeat in range(args.repeats):
                        added, elapsed = timed(lambda: writer.call(E("At", str(path), E("Commit", f"insert-{repeat}", (E("SpaceInsertV1", 0, E("edge", "temporary", node(0))),)))))
                        assert added[0] == S("KnowledgeCommittedV1"), added
                        inserts.append(elapsed)
                        removed, elapsed = timed(lambda: writer.call(E("At", str(path), E("Commit", f"delete-{repeat}", (E("SpaceDeleteV1", added[1][1][0]),)))))
                        assert removed[0] == S("KnowledgeCommittedV1"), removed
                        deletes.append(elapsed)
                    case.update(insert=summary(inserts), delete=summary(deletes), after_updates=physical(path), query_worker_after=memory(evaluator.process.pid))
                    await_resources(client, 0)
                    cases.append(case); save()
                finally:
                    if client: client.close()
                    server.stop()
                    socket_directory.cleanup()
                    if evaluator: assert evaluator.close() == 0
                    assert maintenance.close() == 0
                    assert writer.close() == 0
    receipt["complete"] = True
    save()


if __name__ == "__main__": main()
