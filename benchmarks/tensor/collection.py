#!/usr/bin/env python3
"""Resident collection benchmark: generated vectors, exact oracle, optional ANN.

No corpus or model is downloaded. Temporary stores are removed; the receipt
retains aggregate results and hashes, never documents or per-query IDs.
"""
import argparse
from array import array
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
from memory_collection import NativeCollection
from space_service_client import Client, E


def latency(values):
    ordered = sorted(values)
    return {"samples": len(values), "p50_ms": statistics.median(values),
            "p95_ms": ordered[math.ceil(.95*len(values))-1]}


def matches(metadata, where):
    if where is None:
        return True
    key, value = next(iter(where.items()))
    if key == "$and":
        return all(matches(metadata, item) for item in value)
    if key == "$or":
        return any(matches(metadata, item) for item in value)
    return metadata.get(key) == value


def reference(rows, metas, query, where, k):
    qnorm = math.sqrt(math.fsum(x*x for x in query))
    scored = {}
    for item_id, vector in rows.items():
        if matches(metas[item_id], where):
            norm = math.sqrt(math.fsum(x*x for x in vector))
            scored[item_id] = math.fsum(a*b for a, b in zip(vector, query)) / (norm*qnorm)
    ranked = sorted(scored, key=lambda item_id: (-scored[item_id], item_id))[:k]
    return scored, ranked


def validate(actual, scores, expected, exact=True):
    ids, distances = actual["ids"][0], actual["distances"][0]
    if len(ids) != len(set(ids)) or len(ids) != len(expected) or len(distances) != len(ids):
        raise AssertionError("result cardinality")
    if not set(ids) <= scores.keys():
        raise AssertionError("deleted or filtered result")
    tolerance = 2e-11 if exact else 2e-5
    if any(not math.isfinite(distance) or abs(distance - (1-scores[item_id])) > tolerance
           for item_id, distance in zip(ids, distances)):
        raise AssertionError("result score")
    if not expected:
        return None
    boundary = scores[expected[-1]]
    mandatory = {item_id for item_id, score in scores.items() if score > boundary + 2e-11}
    acceptable = {item_id for item_id, score in scores.items() if score >= boundary - 2e-11}
    if exact and not mandatory <= set(ids) <= acceptable:
        raise AssertionError("exact top-k")
    return min(1., (len(set(ids) & mandatory) + len((set(ids)-mandatory) & acceptable)) / len(expected))


def rejection_controls():
    scores = {"a": .9, "b": .8, "c": .8, "d": .1}
    expected = ["a", "b"]
    wrong = [(["a"], [.1]), (["a", "a"], [.1, .1]),
             (["a", "absent"], [.1, .2]), (["a", "b"], [.1, .3]),
             (["a", "b"], [.1, float("nan")]),
             (["a", "b"], [.1, float("inf")]),
             (["a", "d"], [.1, .9]), (["a", "b"], [.1])]
    for ids, distances in wrong:
        try:
            validate({"ids": [ids], "distances": [distances]}, scores, expected)
        except AssertionError:
            continue
        raise AssertionError("oracle accepted an incorrect control")
    validate({"ids": [["a", "c"]], "distances": [[.1, .2]]}, scores, expected)
    return len(wrong)


def run_size(args, root, n, chromadb):
    rng = random.Random(args.seed)
    data = root / str(n); data.mkdir(mode=0o700)
    socket = data / "owner.sock"
    process = subprocess.Popen([str(args.server), "--socket", str(socket), "--data", str(data),
        "--max-live-bytes", str(256*1024*1024), "--max-query-bytes", str(128*1024*1024),
        "--max-snapshot-bytes", str(256*1024*1024)], stdout=subprocess.DEVNULL)
    native = chroma = client = None
    try:
        for _ in range(200):
            try:
                probe = Client(str(socket)); probe.hello(); probe.close(); break
            except (OSError, EOFError):
                if process.poll() is not None:
                    raise RuntimeError("space owner stopped")
                time.sleep(.01)
        else:
            raise TimeoutError("space owner startup")
        native = NativeCollection(socket, "generated", {"embedding_model": "generated",
            "embedding_output_dim": args.dimension, "hnsw:space": "cosine"}, args.worker, args.module)
        ids = [f"r{i:07d}" for i in range(n)]
        rows = {item_id: array("d", (rng.uniform(-1, 1) for _ in range(args.dimension))) for item_id in ids}
        # Two equal vectors exercise a top-k boundary with distinct IDs.
        rows[ids[1]] = array("d", rows[ids[0]])
        metas = {item_id: {"group": i % 4, "slot": i} for i, item_id in enumerate(ids)}
        if chromadb:
            from chromadb.config import Settings
            client = chromadb.EphemeralClient(Settings(anonymized_telemetry=False))
            chroma = client.create_collection(f"generated-{n}", embedding_function=None,
                configuration={"hnsw": {"space": "cosine", "ef_search": args.ef_search,
                                         "batch_size": 32, "num_threads": 1}})
        start = time.perf_counter()
        for offset in range(0, n, 16):
            chunk = ids[offset:offset+16]
            native.add(chunk, [rows[i] for i in chunk], ["generated" for _ in chunk], [metas[i] for i in chunk])
            if chroma:
                chroma.add(ids=chunk, embeddings=[list(rows[i]) for i in chunk],
                           documents=["generated" for _ in chunk], metadatas=[metas[i] for i in chunk])
        load_seconds = time.perf_counter()-start
        start = time.perf_counter(); native.query([rows[ids[0]]], args.k)
        first_query_ms = (time.perf_counter()-start)*1000
        execution = native._call(E("execution-profile"))
        samples = {kind: {"native": [], **({"chroma": []} if chroma else {})}
                   for kind in ("all", "group", "one", "boolean", "empty")}
        recalls = []
        for iteration in range(args.repeats):
            chosen = rng.randrange(n)
            query = [x + rng.gauss(0, .01) for x in rows[ids[chosen]]]
            cases = {"all": None, "group": {"group": chosen % 4}, "one": {"slot": chosen},
                "boolean": {"$and": [{"$or": [{"group": 0}, {"group": 1}]}, {"slot": chosen}]},
                "empty": {"group": -1}}
            for kind, where in cases.items():
                scores, expected = reference(rows, metas, query, where, args.k)
                methods = [("native", native), *([("chroma", chroma)] if chroma else [])]
                if iteration % 2:
                    methods.reverse()
                for label, collection in methods:
                    start = time.perf_counter()
                    actual = collection.query(query_embeddings=[query], n_results=args.k, where=where)
                    samples[kind][label].append((time.perf_counter()-start)*1000)
                    recall = validate(actual, scores, expected, exact=label == "native")
                    if label == "chroma" and recall is not None:
                        recalls.append(recall)
                        if recall < args.min_chroma_recall:
                            raise AssertionError("ANN recall threshold")
        # Lifecycle checks use the same inputs and unchanged query contract.
        rows[ids[0]] = array("d", (rng.uniform(-1, 1) for _ in range(args.dimension)))
        metas[ids[0]] = {"group": 3, "slot": n}
        base_before_write = native._call(E("index-status"))[2]
        native.upsert([ids[0]], [rows[ids[0]]], ["updated"], [metas[ids[0]]])
        native.delete(ids=[ids[1]])
        if chroma:
            chroma.upsert(ids=[ids[0]], embeddings=[list(rows[ids[0]])], documents=["updated"], metadatas=[metas[ids[0]]])
            chroma.delete(ids=[ids[1]])
        del rows[ids[1]], metas[ids[1]]
        query = list(rows[ids[0]])
        scores, expected = reference(rows, metas, query, None, args.k)
        start = time.perf_counter(); actual = native.query([query], args.k)
        after_write_ms = (time.perf_counter()-start)*1000
        validate(actual, scores, expected)
        if native._call(E("index-status"))[2] != base_before_write:
            raise AssertionError("ordinary write rebuilt the base matrix")
        for where in [None, {"slot": 1}, {"slot": 0}, {"slot": n}]:
            scores, expected = reference(rows, metas, query, where, args.k)
            validate(native.query([query], args.k, where=where), scores, expected)
            if chroma:
                validate(chroma.query(query_embeddings=[query], n_results=args.k, where=where), scores, expected, exact=False)
        native.worker.process.kill(); native.worker.process.wait()
        scores, expected = reference(rows, metas, query, None, args.k)
        validate(native.query([query], args.k), scores, expected)
        disk_bytes = sum(p.stat().st_size for p in data.rglob("*") if p.is_file())
        return {"records": n, "dimension": args.dimension, "load_seconds_both_backends": load_seconds,
            "execution": {"provider": execution[1].name, "dtype": execution[2].name, "reduction": execution[3].name},
            "first_native_query_ms": first_query_ms, "first_native_query_after_write_ms": after_write_ms,
            "latency": {kind: {label: latency(values) for label, values in d.items()} for kind, d in samples.items()},
            "chroma_recall": {"samples": len(recalls), "mean": statistics.mean(recalls), "min": min(recalls)} if recalls else None,
            "temporary_store_bytes": disk_bytes, "retained_dataset_bytes": 0, "lifecycle": "passed"}
    finally:
        if native: native.close()
        if client and chroma: client.delete_collection(chroma.name)
        if process.poll() is None: process.terminate(); process.wait(timeout=10)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--server", required=True, type=Path)
    p.add_argument("--worker", required=True, type=Path)
    p.add_argument("--module", type=Path, default=ROOT / "lib/petta/lib_memory_collection.metta")
    p.add_argument("--records", type=int, nargs="+", default=[1000, 4000])
    p.add_argument("--dimension", type=int, default=384)
    p.add_argument("--repeats", type=int, default=24)
    p.add_argument("--k", type=int, default=5)
    p.add_argument("--seed", type=int, default=109107)
    p.add_argument("--chroma", choices=["off", "optional", "required"], default="optional")
    p.add_argument("--ef-search", type=int, default=128)
    p.add_argument("--min-chroma-recall", type=float, default=0)
    p.add_argument("--check-growth", action="store_true")
    p.add_argument("--growth-overhead", type=float, default=1.5)
    p.add_argument("--work-dir", type=Path)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if (min(args.records) < 2 or len(set(args.records)) != len(args.records) or args.dimension < 1 or
        args.repeats < 1 or not 1 <= args.k <= 1000 or not 0 <= args.min_chroma_recall <= 1 or
        args.growth_overhead <= 0 or args.ef_search < 1):
        p.error("invalid qualification profile")
    args.server, args.worker, args.module = args.server.resolve(), args.worker.resolve(), args.module.resolve()
    receipt = {"status": "failed", "programme_complete": False, "seed": args.seed, "sizes": [],
               "scope": "resident cosine collection queries; no embeddings or model calls", "python": sys.version.split()[0]}
    paths = [Path(__file__).resolve(), ROOT/"lib/petta/lib_memory_collection.metta",
             ROOT/"lib/petta/lib_row_index.metta", ROOT/"lib/petta/lib_tensor.metta",
             ROOT/"tools/memory_collection.py", ROOT/"tools/value_worker_client.py",
             ROOT/"tools/space_service_client.py", ROOT/"src/tensor.c"]
    def hashes():
        result = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        result.update(selected_module=hashlib.sha256(args.module.read_bytes()).hexdigest(),
                      server_binary=hashlib.sha256(args.server.read_bytes()).hexdigest(),
                      worker_binary=hashlib.sha256(args.worker.read_bytes()).hexdigest())
        return result
    before_hashes = hashes()
    try:
        receipt["rejected_controls"] = rejection_controls()
        chromadb = None
        if args.chroma != "off":
            try: import chromadb
            except ImportError:
                if args.chroma == "required": raise
        receipt["chroma"] = {"version": chromadb.__version__, "ef_search": args.ef_search,
            "minimum_recall": args.min_chroma_recall} if chromadb else "not-run"
        with tempfile.TemporaryDirectory(prefix="cetta-resident-", dir=args.work_dir) as temporary:
            for n in sorted(args.records):
                result = run_size(args, Path(temporary), n, chromadb)
                receipt["sizes"].append(result)
                print(json.dumps({"records": n, "latency": result["latency"]}), flush=True)
        if args.check_growth:
            for old, new in zip(receipt["sizes"], receipt["sizes"][1:]):
                bound = new["records"] / old["records"] * args.growth_overhead
                for kind in ("all", "group"):
                    ratio = new["latency"][kind]["native"]["p50_ms"] / old["latency"][kind]["native"]["p50_ms"]
                    if ratio > bound: raise AssertionError("resident query growth")
        receipt["status"] = "passed"
    finally:
        after_hashes = hashes()
        changed = [name for name, value in before_hashes.items() if after_hashes[name] != value]
        receipt["hashes"] = before_hashes
        receipt["artifacts_unchanged"] = not changed
        if changed:
            receipt.update(status="failed", changed_artifacts=changed)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(receipt, indent=2)+"\n")
        if changed:
            raise RuntimeError("qualification artifacts changed during the run")


if __name__ == "__main__": main()
