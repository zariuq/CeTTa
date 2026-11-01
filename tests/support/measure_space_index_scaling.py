"""Small chain-join scaling and full native-store density accounting.

Timings are observations, never correctness gates. Storage totals include live
receipts, keys, values, checksums and index nodes, not just fact payloads.
"""
import argparse
import json
from pathlib import Path
import statistics
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, encode
from value_worker_client import Worker


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--sizes", default="64,128,256,512,1024")
    parser.add_argument("--samples", type=int, default=9)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    sizes = [int(value) for value in args.sizes.split(",")]
    assert sizes and all(size >= 2 for size in sizes) and args.samples >= 3
    measurements = []
    for size in sizes:
        for indexed in [False, True]:
            with tempfile.TemporaryDirectory(prefix="space-index-scale-") as directory:
                path = Path(directory)
                worker = Worker(args.worker.resolve(), ROOT / "tests/support/indexed_space_fixture.metta",
                                "indexed-fixture:run", language="petta", profile=True)
                try:
                    create = E("Create", str(path), (E("SpaceIndexV1", 3, 0), E("SpaceIndexV1", 3, 1))) if indexed else E("CreatePlain", str(path))
                    assert worker.call(create)[0] == S("KnowledgeCommittedV1")
                    rows = [E("depends", f"program-{i}", f"program-{i + 1}") for i in range(size)]
                    rows.append(rows[0])  # Duplicate edge, distinct occurrence.
                    started = time.perf_counter_ns()
                    for first in range(0, len(rows), 32):
                        result = worker.call(E("At", str(path), E("Commit", f"seed-{first}",
                            tuple(E("SpaceInsertV1", i, value) for i, value in enumerate(rows[first:first + 32])))))
                        assert result[0] == S("KnowledgeCommittedV1"), result
                    load_ns = time.perf_counter_ns() - started
                    assert worker.call(E("At", str(path), E("Compact")))[0] == S("IndexedCompacted")
                    files = [entry for entry in path.iterdir() if entry.is_file()]
                    storage_bytes = sum(entry.stat().st_size for entry in files)
                    allocated_bytes = sum(entry.stat().st_blocks * 512 for entry in files)
                    x, y = Var(0), Var(1)
                    pattern = E(",", E("depends", "program-0", x), E("depends", x, y))
                    command = E("At", str(path), E("Query", pattern, E("endpoint", y)))
                    samples = []
                    for index in range(args.samples + 2):
                        before = time.perf_counter_ns()
                        result = worker.call(command)
                        elapsed = time.perf_counter_ns() - before
                        assert result[0] == S("IndexedRows") and len(result[1]) == 2, result
                        assert all(row[1] == E("endpoint", "program-2") for row in result[1]), result
                        if index >= 2:
                            samples.append((elapsed, worker.last_metrics["native_eval_ns"]))
                    info = result[2]
                    assert info[0] == S("StoreQueryInfo") and info[5] is False
                    if indexed:
                        assert info[2] > 0 and info[3] == 4, info
                    else:
                        assert info[3] == 3 * len(rows), info
                    roundtrip = sorted(value[0] for value in samples)
                    native = sorted(value[1] for value in samples)
                    measurements.append({"facts": len(rows), "indexed": indexed,
                        "load_native_rpc_ns": load_ns, "storage_logical_bytes": storage_bytes,
                        "storage_allocated_bytes": allocated_bytes,
                        "storage_bytes_per_fact_including_receipts": storage_bytes / len(rows),
                        "fact_cav_payload_bytes": sum(len(encode(row)) for row in rows),
                        "scan_traversals": info[1], "index_traversals": info[2],
                        "candidates": info[3], "decoded_cav_bytes": info[4],
                        "answers": len(result[1]), "median_native_eval_ns": statistics.median(native),
                        "p95_native_eval_ns": native[min(len(native) - 1, int(.95 * len(native)))],
                        "median_roundtrip_ns": statistics.median(roundtrip)})
                finally:
                    worker.close()
    report = {"workload": "small derived dependency chains; two-hop join with one duplicate edge",
              "samples_after_warmup": args.samples, "measurements": measurements,
              "scope": "warm local queries; no cold-cache, hardware superiority or compressed-format claim"}
    if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
