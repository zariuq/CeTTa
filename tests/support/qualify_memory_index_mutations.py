#!/usr/bin/env python3
"""Check the incremental collection and reject valid, deliberately wrong policies."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from memory_collection import plain
from space_service_client import E, S, Tensor
from value_worker_client import Worker


def exercise(binary, module):
    worker = Worker(binary, module, "memory:dispatch")
    current = {}
    checks = []

    def record(item_id, revision, vector, group):
        row = E("Memory", item_id, revision, "fixture", Tensor.vector(vector),
                "document-" + str(revision), (("group", group),))
        current[item_id] = row
        return row

    def query(stage, group=None):
        result = plain(worker.call(E("query", "fixture", Tensor.vector([1., 0.]), 3,
                                     S("All") if group is None else E("Eq", "group", group))))
        expected = {item_id: row for item_id, row in current.items()
                    if group is None or dict(row[-1])["group"] == group}
        ids = [row[1][1] for row in result]
        if len(ids) != len(set(ids)) or set(ids) != set(expected):
            raise AssertionError(stage + ": current distinct IDs")
        for score, brief in result:
            row = expected[brief[1]]
            if brief[2:] != [row[2], row[5], plain(row[6])]:
                raise AssertionError(stage + ": current version")
            vector = struct.unpack("<2d", row[4].encoded[-16:])
            wanted = vector[0] / sum(x*x for x in vector)**.5
            if not math.isfinite(score) or abs(score - wanted) > 1e-12:
                raise AssertionError(stage + ": exact score")
        checks.append(stage)

    try:
        a = record("a", 1, [1., 0.], "old")
        b = record("b", 1, [0., 1.], "old")
        c = record("c", 1, [1., 1.], "old")
        if worker.call(E("load", "fixture", 2, (a,b,c))) != E("Loaded", 3):
            raise RuntimeError("fixture did not load")
        query("base")
        for revision, vector in ((2, [0., -1.]), (3, [2., 1.])):
            old = current["a"]
            new = record("a", revision, vector, "new")
            if worker.call(E("apply", (E("add",new), E("remove",old)))) != E("Applied", 2):
                raise RuntimeError("fixture write did not apply")
            query("updated-" + str(revision))
            query("updated-filter-" + str(revision), "new")
            query("old-filter-" + str(revision), "old")
        old = current.pop("a")
        if worker.call(E("apply", (E("remove",old),))) != E("Applied", 1):
            raise RuntimeError("fixture delete did not apply")
        query("deleted")
        return checks
    finally:
        worker.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--worker", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--work-dir", type=Path)
    args = p.parse_args()
    source = ROOT / "lib/petta/lib_memory_collection.metta"
    text = source.read_text()
    mutations = {
        "no-retirement": ("(tensor:mask-and $live (index:mask $stamp $projection $filter))",
                           "(index:mask $stamp $projection $filter)"),
        "drop-delta": ("(list:append $hits $recent)", "$hits"),
        "retain-stale-delta": (
            "($version (change-state! &memory-view $next))\n          ($delta (change-state! &memory-delta-index Missing))",
            "($version (change-state! &memory-view $next))\n          ($delta ())"),
    }
    receipt = {"status": "failed", "controls": [], "programme_complete": False,
               "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "worker_sha256": hashlib.sha256(args.worker.read_bytes()).hexdigest()}
    try:
        receipt["baseline_checks"] = exercise(args.worker.resolve(), source)
        with tempfile.TemporaryDirectory(prefix="cetta-index-controls-", dir=args.work_dir) as directory:
            for name, (old,new) in mutations.items():
                if text.count(old) != 1:
                    raise RuntimeError("mutation anchor mismatch: " + name)
                module = Path(directory) / (name + ".metta")
                module.write_text(text.replace(old,new))
                try:
                    exercise(args.worker.resolve(), module)
                except AssertionError as error:
                    receipt["controls"].append({"name": name, "rejected": True, "reason": str(error)})
                else:
                    raise AssertionError("incorrect policy passed: " + name)
        receipt["status"] = "passed"
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(receipt,indent=2)+"\n")
    print("PASS: incremental index baseline and", len(mutations), "incorrect policy controls")


if __name__ == "__main__":
    main()
