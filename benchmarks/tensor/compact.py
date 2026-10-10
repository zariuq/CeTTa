#!/usr/bin/env python3
"""Generated public-API tensor and retrieval qualification; no retained corpus."""
import argparse
from dataclasses import dataclass, replace
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import re
import statistics
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def current_image_peak_rss_kib():
    if sys.platform.startswith("linux"):
        # VmHWM describes this process image, including imported comparison clients.
        status = Path("/proc/self/status")
        if status.is_file():
            match = re.search(r"^VmHWM:\s*(\d+)\s+kB$", status.read_text(), re.M)
            if match:
                return int(match[1])
    return None


def expression(items):
    return "(" + " ".join(map(str, items)) + ")"


def tensor(dtype, shape, values):
    return f"(tensor:from {dtype} {expression(shape)} {expression(values)})"


class Runtime:
    def __init__(self, binary, directory, threads):
        self.binary, self.directory, self.threads = binary, directory, threads
        self.largest_input = 0
        self.processes = 0

    def run(self, source):
        # One overwritten input file, removed with the enclosing directory.
        path = self.directory / "input.metta"
        path.write_text(source)
        self.largest_input = max(self.largest_input, path.stat().st_size)
        command = [str(self.binary), "--lang", "petta", "--num-threads",
                   str(self.threads), str(path)]
        timed = sys.platform.startswith("linux") and Path("/usr/bin/time").is_file()
        if timed:
            command = ["/usr/bin/time", "-f", "CETTA_BENCH_RSS_KIB=%M", *command]
        start = time.perf_counter()
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, timeout=120)
        wall = time.perf_counter() - start
        self.processes += 1
        require(result.returncode == 0,
                f"Runtime exited {result.returncode}: {result.stderr[-1500:]}")
        match = re.search(r"^CETTA_BENCH_RSS_KIB=(\d+)$", result.stderr, re.M)
        measurement = {"wall_seconds": wall,
                       "max_rss_kib": int(match[1]) if match else None}
        return result.stdout.splitlines(), measurement


def numerical_checks(runtime, rng, use_numpy):
    np = None
    if use_numpy != "off":
        try:
            import numpy as np
        except ImportError:
            require(use_numpy != "required", "NumPy requested but unavailable")
    shapes = ((), (0,), (1,), (7,), (16,), (2, 3), (2, 1, 3),
              (2, 2, 2, 2), (0, 3), (3, 0, 2))
    source = ["!(import! &self (library lib_tensor))"]
    names = []
    numpy_checks = 0

    def check(name, actual, expected):
        names.append(name)
        source.append(f"!(== {actual} {expected})")

    for dtype in ("i64", "f64", "u8"):
        for shape in shapes:
            count = math.prod(shape)
            if dtype == "u8":
                left = [rng.randrange(4) for _ in range(count)]
                right = [rng.randrange(4) for _ in range(count)]
            elif dtype == "i64":
                left = [rng.randrange(-8, 9) for _ in range(count)]
                right = [rng.randrange(-8, 9) for _ in range(count)]
            else:
                # Dyadic inputs keep these reference arithmetic results exact.
                left = [rng.randrange(-31, 32) / 8.0 for _ in range(count)]
                right = [rng.randrange(-31, 32) / 8.0 for _ in range(count)]
            a, b = tensor(dtype, shape, left), tensor(dtype, shape, right)
            plus = [x + y for x, y in zip(left, right)]
            times = [x * y for x, y in zip(left, right)]
            total = sum(left, 0.0 if dtype == "f64" else 0)
            dot = sum(times, 0.0 if dtype == "f64" else 0)
            label = f"{dtype}/{shape}"
            check(label + "/add", f"(tensor:add {a} {b})", tensor(dtype, shape, plus))
            check(label + "/mul", f"(tensor:mul {a} {b})", tensor(dtype, shape, times))
            check(label + "/sum", f"(tensor:sum {a})", str(total))
            check(label + "/copy", f"(tensor:copy {a})", a)
            check(label + "/reshape", f"(tensor:reshape {a} ({count}))",
                  tensor(dtype, (count,), left))
            check(label + "/codec", f"(tensor:decode (tensor:encode {a}))", a)
            if len(shape) == 1:
                check(label + "/dot", f"(tensor:dot {a} {b})", str(dot))
            if count:
                changed = [1.0 if dtype == "f64" else 1, *left[1:]]
                check(label + "/with", f"(tensor:with {a} 0 1)",
                      tensor(dtype, shape, changed))
                check(label + "/saved-input",
                      f"(let $old {a} (let $new (tensor:with $old 0 1) $old))", a)
            if np is not None:
                numpy_dtype = {"i64": "int64", "f64": "float64", "u8": "uint8"}[dtype]
                x = np.array(left, dtype=numpy_dtype).reshape(shape)
                y = np.array(right, dtype=numpy_dtype).reshape(shape)
                require(np.array_equal((x + y).reshape(-1), plus), label + "/NumPy add")
                require(np.array_equal((x * y).reshape(-1), times), label + "/NumPy mul")
                require(float(x.sum()) == total, label + "/NumPy sum")
                if len(shape) == 1:
                    # Widen integers: CeTTa refuses overflow rather than wrapping.
                    require(float(np.dot(x.astype("float64"), y.astype("float64"))) == dot,
                            label + "/NumPy dot")
                    numpy_checks += 1
                numpy_checks += 3
    actual, measurement = runtime.run("\n".join(source))
    require(len(actual) == len(names) + 1 and actual[0] == "true",
            "Numerical result count/import receipt mismatch")
    for name, result in zip(names, actual[1:]):
        require(result == "true", f"Numerical disagreement: {name}: {result}")
    return {"status": "pass", "public_checks": len(names),
            "numpy_checks": numpy_checks,
            "numpy_version": np.__version__ if np is not None else None,
            "numpy_status": "pass" if np is not None else "not-run",
            **measurement}


@dataclass(frozen=True)
class Record:
    id: str
    revision: int
    profile: str
    vector: tuple
    group: str

    def metadata(self):
        return {"revision": self.revision, "profile": self.profile, "group": self.group}

    def metta(self):
        return (f"(Embedding {self.id} {self.revision} {self.profile} "
                f"$v (Group {self.group}))")

    def command(self, operation):
        value = tensor("f64", (len(self.vector),), self.vector)
        return f"!(let $v {value} ({operation} &bench {self.metta()}))"


@dataclass(frozen=True)
class Query:
    name: str
    profile: str
    group: str | None
    vector: tuple
    k: int


@dataclass(frozen=True)
class Hit:
    score: float
    id: str
    revision: int
    profile: str
    group: str


def selected(records, query):
    return [r for r in records if r.profile == query.profile and
            (query.group is None or r.group == query.group)]


def reference_hits(records, query):
    hits = [Hit(sum(x * y for x, y in zip(query.vector, r.vector)),
                r.id, r.revision, r.profile, r.group) for r in selected(records, query)]
    return sorted(hits, key=lambda h: -h.score)[:query.k]


def validate_hits(actual, records, query, approximate=False):
    expected = reference_hits(records, query)
    require(len(actual) == len(expected), query.name + "/result count")
    require(len({h.id for h in actual}) == len(actual), query.name + "/duplicate ID")
    candidates = {r.id: r for r in selected(records, query)}
    for hit in actual:
        require(hit.id in candidates, query.name + "/filter or deleted ID")
        record = candidates[hit.id]
        require((hit.revision, hit.profile, hit.group) ==
                (record.revision, record.profile, record.group), query.name + "/stale metadata")
        score = sum(x * y for x, y in zip(query.vector, record.vector))
        require(math.isclose(hit.score, score, rel_tol=1e-6, abs_tol=1e-6),
                query.name + "/score or distance convention")
    if not expected:
        return 1.0
    cutoff = expected[-1].score
    mandatory = {h.id for h in expected if h.score > cutoff}
    eligible = {r.id for r in selected(records, query)
                if sum(x * y for x, y in zip(query.vector, r.vector)) >= cutoff}
    found = {h.id for h in actual}
    recall = (sum(h.id in mandatory for h in actual) +
              min(len(found & (eligible - mandatory)), len(expected) - len(mandatory))) / len(expected)
    if not approximate:
        require(found == {h.id for h in expected}, query.name + "/top-k or tie policy")
    return recall


def parse_sexpr(text):
    tokens = iter(re.findall(r"[()]|[^\s()]+", text))

    def term(token):
        if token != "(":
            require(token != ")", "Unbalanced result")
            return token
        result = []
        for token in tokens:
            if token == ")":
                return result
            result.append(term(token))
        raise ValueError("Unterminated result")

    result = term(next(tokens))
    require(next(tokens, None) is None, "Trailing result tokens")
    return result


class ChromaComparison:
    def __init__(self):
        # Optional dependency; no embedding function or model download.
        import chromadb
        from chromadb.config import Settings
        self.version = chromadb.__version__
        self.client = chromadb.EphemeralClient(Settings(anonymized_telemetry=False))
        self.index_profile = {"space": "ip", "num_threads": 1,
                              "ef_search": 128, "batch_size": 2}
        self.collection = self.client.create_collection(
            "cetta_tensor_bench_" + uuid.uuid4().hex, embedding_function=None,
            configuration={"hnsw": self.index_profile})

    def upsert(self, records):
        if records:
            self.collection.upsert(ids=[r.id for r in records],
                                   embeddings=[list(r.vector) for r in records],
                                   metadatas=[r.metadata() for r in records])

    def delete(self, ids):
        if ids:
            self.collection.delete(ids=ids)

    def query(self, query):
        # CeTTa's empty top-k is handled by this adapter, not by n_results=0.
        if query.k == 0:
            return []
        where = {"profile": query.profile}
        if query.group is not None:
            where = {"$and": [where, {"group": query.group}]}
        result = self.collection.query(query_embeddings=[list(query.vector)],
                                       n_results=query.k, where=where,
                                       include=["distances", "metadatas"])
        return [Hit(1.0 - distance, id_, metadata["revision"],
                    metadata["profile"], metadata["group"])
                for id_, distance, metadata in zip(result["ids"][0],
                                                   result["distances"][0],
                                                   result["metadatas"][0])]

    def close(self):
        self.client.delete_collection(self.collection.name)


def retrieval_checks(runtime, rng, count, dimension, chroma_mode, min_recall=1.0):
    records = [Record(f"r{i:06d}", 1, "model-a" if i % 4 else "model-b",
                      tuple(rng.randrange(-127, 128) / 128.0 for _ in range(dimension)),
                      f"g{i % 2}") for i in range(count)]
    # Equal values with distinct occurrence IDs deliberately exercise ties.
    shared = tuple([0.5] * dimension)
    records += [Record("tie-a", 1, "model-a", shared, "g-tie"),
                Record("tie-b", 1, "model-a", shared, "g-tie")]
    query_vector = tuple(rng.randrange(-127, 128) / 128.0 for _ in range(dimension))
    sources = ["!(import! &self (library lib_retrieval))", "!(bind! &bench (new-space))"]
    sources += [r.command("add-atom") for r in records]
    observations = []
    chroma = None
    if chroma_mode == "required":
        chroma = ChromaComparison()
    chroma_recalls = []
    chroma_recall_details = []
    chroma_times = []
    chroma_queries = 0

    def observe(stage):
        nonlocal chroma_queries
        for profile, group in (("model-a", None), ("model-a", "g0"),
                               ("model-a", "g1"), ("model-a", "missing"),
                               ("model-a", "g-tie"), ("model-a", "g-updated"),
                               ("model-a", "g-new"),
                               ("model-b", None), ("missing", None)):
            for k in (0, 1, 5):
                query = Query(f"{stage}-q{len(observations)}", profile, group, query_vector, k)
                pattern_group = "$group" if group is None else group
                pattern = f"(Embedding $id $rev {profile} $v (Group {pattern_group}))"
                search = (f"(let $records (collapse (match &bench {pattern} {pattern})) "
                          f"(retrieval:exact-dot {tensor('f64', (dimension,), query_vector)} $records {k}))")
                sources.append(f"!(let $hits {search} (BenchQuery {query.name} $hits))")
                observations.append((query, tuple(records)))
                if chroma is not None:
                    start = time.perf_counter()
                    hits = chroma.query(query)
                    latency = time.perf_counter() - start
                    recall = validate_hits(hits, records, query, approximate=True)
                    chroma_queries += 1
                    if query.k:
                        chroma_times.append(latency)
                    if reference_hits(records, query):
                        chroma_recalls.append(recall)
                        chroma_recall_details.append({"query": query.name, "k": query.k,
                                                      "eligible_records": len(selected(records, query)),
                                                      "recall": recall})

    try:
        if chroma is not None:
            chroma.upsert(records)
        observe("initial")
        old = records[1]
        changed = replace(old, revision=2, vector=tuple(x * 4.0 for x in query_vector), group="g-updated")
        records.remove(old)
        records.append(changed)
        inserted = Record("inserted", 1, "model-a", tuple([-4.0] * dimension), "g-new")
        records.append(inserted)
        sources += [old.command("remove-atom"), changed.command("add-atom"), inserted.command("add-atom")]
        if chroma is not None:
            chroma.upsert([changed, inserted])
        observe("upsert")
        removed = [r for r in records if r.id in (changed.id, "tie-a")]
        for record in removed:
            sources.append(record.command("remove-atom"))
            records.remove(record)
        if chroma is not None:
            chroma.delete([r.id for r in removed])
        observe("delete")
        removed = [r for r in records if r.profile == "model-a" and r.group in ("g1", "g-new")]
        for record in removed:
            sources.append(record.command("remove-atom"))
            records.remove(record)
        if chroma is not None:
            chroma.collection.delete(where={"$and": [{"profile": "model-a"},
                                                       {"group": {"$in": ["g1", "g-new"]}}]})
        observe("filtered-delete")
        output, measurement = runtime.run("\n".join(sources))
        results = {}
        for line in output:
            if line.startswith("(BenchQuery "):
                tag, name, rows = parse_sexpr(line)
                require(name not in results, "Duplicate native query receipt")
                require(isinstance(rows, list) and all(isinstance(row, list) and len(row) == 2
                                                      for row in rows),
                        f"{name}/retrieval failure: {rows}")
                hits = []
                for score, row in rows:
                    kind, id_, revision, profile, metadata = row
                    require(kind == "RetrievalHit" and metadata[0] == "Group", "Malformed hit")
                    hits.append(Hit(float(score), id_, int(revision), profile, metadata[1]))
                results[name] = hits
        require(len(results) == len(observations), "Missing native query receipts")
        for query, snapshot in observations:
            validate_hits(results[query.name], snapshot, query)
        chroma_passed = not chroma_recalls or min(chroma_recalls) >= min_recall
        return {"status": "pass" if chroma_passed else "fail", "native_status": "pass",
                "public_queries": len(observations),
                "records_initial": count + 2, "dimension": dimension,
                "chroma_status": ("pass" if chroma_passed else "fail") if chroma is not None else "not-run",
                "chroma_version": chroma.version if chroma is not None else None,
                "chroma_index_profile": chroma.index_profile if chroma is not None else None,
                "chroma_queries": chroma_queries,
                "chroma_recall_queries": len(chroma_recalls),
                "chroma_min_recall_required": min_recall,
                "chroma_min_tie_aware_recall": min(chroma_recalls, default=None),
                "chroma_mean_tie_aware_recall": statistics.mean(chroma_recalls) if chroma_recalls else None,
                "chroma_recall_details": chroma_recall_details,
                "chroma_query_latency_seconds": chroma_times,
                "chroma_timing_scope": "in-process SDK queries; k=0 local adapter results excluded",
                "chroma_reference": "generated f64 dot, Chroma ip distance converted to score",
                **measurement}
    finally:
        if chroma is not None:
            chroma.close()


def checker_controls():
    records = (Record("best", 2, "p", (2.0,), "g0"),
               Record("other", 1, "p", (1.0,), "g0"))
    query = Query("control", "p", None, (1.0,), 1)
    good = reference_hits(records, query)
    validate_hits(good, records, query)
    bad = ([replace(good[0], score=99.0)], [replace(good[0], revision=1)],
           [replace(good[0], profile="wrong")], [replace(good[0], id="deleted")],
           [Hit(1.0, "other", 1, "p", "g0")], [], good + good)
    for hits in bad:
        try:
            validate_hits(hits, records, query)
        except ValueError:
            continue
        raise ValueError("Broken retrieval result accepted by checker")
    return {"status": "pass", "rejected_results": len(bad)}


def bulk_measurements(runtime, lengths, repeats):
    rows = []
    for length in lengths:
        # Both operations use whole native buffers; stdout remains constant size.
        source = ("!(import! &self (library lib_tensor))\n"
                  f"!(tensor:sum (tensor:full f64 ({length}) 1))\n"
                  f"!(let $v (tensor:full f64 ({length}) 1) (tensor:dot $v $v))")
        samples = []
        for _ in range(repeats):
            actual, measurement = runtime.run(source)
            require(len(actual) == 3 and actual[0] == "true" and
                    all(float(x) == length for x in actual[1:]), "Bulk result disagreement")
            samples.append(measurement)
        rows.append({"elements": length, "payload_bytes_per_tensor": length * 8,
                     "median_wall_seconds": statistics.median(s["wall_seconds"] for s in samples),
                     "samples": samples})
    return {"status": "pass", "timing_scope": "whole process, import, fills, sum and dot",
            "rows": rows}


def write_report(report, output):
    encoded = json.dumps(report, indent=2, allow_nan=False) + "\n"
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(mode="w", dir=output.parent,
                                         prefix=output.name + ".", delete=False) as file:
            file.write(encoded)
            temporary = Path(file.name)
        try:
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)
    else:
        sys.stdout.write(encoded)
    return len(encoded.encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path(os.environ.get("CETTA_BIN", ROOT / "cetta")))
    parser.add_argument("--unit-binary", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--seed", type=int, default=7717)
    parser.add_argument("--records", type=int, default=32)
    parser.add_argument("--dimension", type=int, default=16)
    parser.add_argument("--lengths", type=int, nargs="+", default=[1000, 100000])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--numpy", choices=("auto", "off", "required"), default="auto")
    parser.add_argument("--chroma", choices=("off", "required"), default="off")
    parser.add_argument("--min-chroma-recall", type=float, default=1.0)
    options = parser.parse_args()
    # A failed rerun cannot leave an older passing receipt at this output path.
    if options.output:
        write_report({"schema": "cetta-tensor-benchmark-v1", "status": "running"}, options.output)
    try:
        run_benchmark(options)
    except Exception as error:
        if options.output:
            saved = json.loads(options.output.read_text())
            if saved.get("status") != "fail":
                write_report({"schema": "cetta-tensor-benchmark-v1", "status": "fail",
                              "failure": str(error), "programme_complete": False}, options.output)
        raise


def run_benchmark(options):
    require(options.records >= 2 and options.dimension > 0 and options.repeats > 0 and
            options.threads > 0 and all(x >= 0 for x in options.lengths) and
            0 <= options.min_chroma_recall <= 1, "Invalid workload dimensions or recall threshold")
    binary = options.binary.resolve()
    require(binary.is_file(), "A tensor-enabled CeTTa binary is required")
    rng = random.Random(options.seed)
    report = {"schema": "cetta-tensor-benchmark-v1", "status": "running", "seed": options.seed,
              "binary_sha256": digest(binary), "threads": options.threads,
              "environment": {"python": platform.python_version(), "system": platform.system(),
                              "architecture": platform.machine()},
              "benchmark_sha256": digest(Path(__file__)),
              "source_sha256": {name: digest(ROOT / name) for name in
                                ("src/tensor.c", "src/tensor.h", "lib/lib_tensor.metta",
                                 "lib/petta/lib_retrieval.metta", "tests/support/test_tensor_native.py")},
              "coverage": json.loads(Path(__file__).with_name("coverage.json").read_text()),
              "programme_complete": False}
    if options.unit_binary:
        unit = options.unit_binary.resolve()
        subprocess.run([str(unit)], cwd=ROOT, check=True, stdout=subprocess.DEVNULL, timeout=120)
        report["native_unit"] = {"status": "pass", "binary_sha256": digest(unit)}
    else:
        report["native_unit"] = {"status": "not-run", "reason": "Supply --unit-binary or use the Make target"}
    fixtures = subprocess.run([sys.executable, str(ROOT / "tests/support/test_tensor_native.py")],
                              cwd=ROOT, env={**os.environ, "CETTA_BIN": str(binary)},
                              capture_output=True, text=True, timeout=120)
    require(fixtures.returncode == 0, "Dialect fixtures failed: " + fixtures.stdout[-1000:] + fixtures.stderr[-1000:])
    count = re.search(r"fixtures \((\d+) results\)", fixtures.stdout)
    require(count is not None, "Missing dialect fixture receipt")
    report["dialect_fixtures"] = {"status": "pass", "fixture_outputs": int(count[1]),
                                 "actual_parallel_threads": [1, 4]}
    with tempfile.TemporaryDirectory(prefix="cetta-tensor-bench-", dir=options.work_dir) as scratch:
        runtime = Runtime(binary, Path(scratch), options.threads)
        report["numerical"] = numerical_checks(runtime, rng, options.numpy)
        report["checker_controls"] = checker_controls()
        report["retrieval"] = retrieval_checks(runtime, rng, options.records, options.dimension,
                                               options.chroma, options.min_chroma_recall)
        report["bulk"] = bulk_measurements(runtime, options.lengths, options.repeats)
        report["resources"] = {"largest_transient_input_bytes": runtime.largest_input,
                               "native_processes": runtime.processes,
                               "retained_dataset_bytes": 0,
                               "rss_units": "KiB on Linux; parent includes optional clients",
                               "rss_methods": {"parent": "current process image VmHWM",
                                               "native": "GNU time per child"},
                               "parent_max_rss_kib": current_image_peak_rss_kib()}
    report["status"] = report["retrieval"]["status"]
    if report["status"] != "pass":
        report["failure"] = "Chroma recall below requested threshold"
    size = write_report(report, options.output)
    require(report["status"] == "pass", report.get("failure", "Qualification failed"))
    if options.output:
        print(f"PASS: {report['numerical']['public_checks']} numerical checks; "
              f"{report['retrieval']['public_queries']} retrieval queries; "
              f"Chroma {report['retrieval']['chroma_status']}; report {size} bytes")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, ImportError, OSError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
