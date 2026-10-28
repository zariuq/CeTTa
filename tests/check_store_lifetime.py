#!/usr/bin/env python3
"""Measure a growing store through repeated native MeTTa worker calls.

Both explicit close and request-arena retirement must release resources.
Memory measurements are Linux observations, not a proof of an allocation bound.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, encode
from value_worker_client import Worker


def process_sample(process):
    directory = Path("/proc") / str(process.pid)
    if not directory.exists():
        return {"rss_kib": None, "descriptors": None}
    status = dict(line.split(":", 1) for line in (directory / "status").read_text().splitlines())
    return {"rss_kib": int(status["VmRSS"].split()[0]),
            "descriptors": len(list((directory / "fd").iterdir()))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--rounds", type=int, default=500)
    parser.add_argument("--value-bytes", type=int, default=8192)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.rounds < 1 or not 0 < args.value_bytes < 1_000_000:
        parser.error("positive rounds and a value fitting the portable worker frame are required")
    module = ROOT / "tests/support/store_fixture.metta"
    worker = Worker(args.worker.resolve(), module, "store-fixture:run", language="petta", profile=True)
    samples, durations = [], []
    try:
        with tempfile.TemporaryDirectory(prefix="store-lifetime-") as folder:
            path = Path(folder) / "store"
            path.mkdir(mode=0o700)
            assert worker.call(E("Create", str(path))) == S("Created")
            body = b"x" * args.value_bytes
            for step in range(args.rounds + 64):
                key = ("record/%016x" % step).encode()
                started = time.perf_counter()
                assert worker.call(E("Apply", str(path), E("reads"), E("writes", E("point", key)),
                                     E("ops", E("put", key, body)))) == E("Applied", step + 1)
                for explicit in (True, False):
                    answer = worker.call(E("Resources", str(path), key, explicit))
                    expected = E("ResourcesRead", len(body), E("StoreBytes", body[:7], min(7, len(body))),
                                 E("StoreKey", key, len(body), 1))
                    assert encode(answer) == encode(expected), answer
                if step >= 64:
                    durations.append((time.perf_counter() - started) * 1000)
                if step == 63 or (step >= 64 and ((step - 63) % 50 == 0 or step == args.rounds + 63)):
                    # Another small request retires the preceding result arena.
                    assert worker.call(E("Codec", S("lifetime-sample"))) == E("StoreDecoded", S("lifetime-sample"))
                    samples.append(dict(records=step + 1, **process_sample(worker.process),
                                        worker_metrics=worker.last_metrics))
            known = [sample["descriptors"] for sample in samples if sample["descriptors"] is not None]
            assert not known or max(known) == min(known), samples
            receipt = {"rounds_after_warmup": args.rounds, "records": args.rounds + 64,
                       "value_bytes": args.value_bytes, "samples": samples,
                       "latency_ms_median": statistics.median(durations),
                       "latency_ms_max": max(durations),
                       "disk_bytes": sum(p.stat().st_size for p in path.iterdir()),
                       "module_sha256": hashlib.sha256(module.read_bytes()).hexdigest(),
                       "worker_sha256": hashlib.file_digest(args.worker.open("rb"), "sha256").hexdigest()}
            if args.output:
                args.output.write_text(json.dumps(receipt, indent=2) + "\n")
            print(json.dumps(receipt, indent=2))
    finally:
        worker.close()


if __name__ == "__main__":
    main()
