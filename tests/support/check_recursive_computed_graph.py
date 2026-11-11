#!/usr/bin/env python3
"""Memoized recursion over immutable rows projected from a captured Space."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S


def counters(answer):
    return {key.name: value for key, value in answer}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="recursive-computed-") as temporary:
        path = str(Path(temporary) / "store")
        worker = Worker(args.worker.resolve(), ROOT / "examples/kb/space_recursive_curriculum.metta",
                        "space-recursive:run", language="petta")
        try:
            installed = worker.call(E("Install", path))
            assert installed[0] == S("db:committed"), installed
            edges = worker.call(E("Edges", path))
            assert len(edges) == 6
            worker.call(S("ClearStats"))
            first = worker.call(E("Reach", edges, S("a")))
            first_stats = counters(worker.call(S("Stats")))
            second = worker.call(E("Reach", edges, S("a")))
            second_stats = counters(worker.call(S("Stats")))
            assert set(first) == {S("b"), S("c"), S("d"), S("e")}, first
            assert second == first
            assert second_stats.get("cache_hit", 0) > first_stats.get("cache_hit", 0)
            assert second_stats.get("cache_miss", 0) == first_stats.get("cache_miss", 0)

            dag = (E("Edge", S("a"), S("b")), E("Edge", S("a"), S("c")),
                   E("Edge", S("b"), S("d")), E("Edge", S("c"), S("d")),
                   E("Edge", S("d"), S("e")))
            worker.call(S("ClearStats"))
            assert set(worker.call(E("Dag", dag, S("a")))) == {
                S("b"), S("c"), S("d"), S("e")}
            dag_stats = counters(worker.call(S("Stats")))
            assert dag_stats.get("cache_hit", 0) > 0 and dag_stats.get("cache_miss", 0) > 0, dag_stats

            assert worker.call(E("Add", path, S("e"), S("f"), "add-f"))[0] == S("db:committed")
            changed = worker.call(E("Edges", path))
            assert set(worker.call(E("Reach", edges, S("a")))) == set(first)
            assert set(worker.call(E("Reach", changed, S("a")))) == {
                S("b"), S("c"), S("d"), S("e"), S("f")}
            changed_stats = counters(worker.call(S("Stats")))
            assert changed_stats.get("cache_miss", 0) > dag_stats.get("cache_miss", 0)

            assert worker.call(E("Reject", S("not-an-edge-list"))) == S("ComputedGraphRefusedV1")
        finally:
            worker.close()
        report = dict(
            first_reach=sorted(value.name for value in first),
            repeated_reach=sorted(value.name for value in second),
            first_stats=first_stats, repeated_stats=second_stats,
            dag_stats=dag_stats, changed_stats=changed_stats,
            immutable_projection_keeps_old_answer=True,
            changed_projection_is_new_key=True,
            resource_argument_refused=True)
        if args.output:
            args.output.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: captured immutable graph projection, recursive memo hits and changed-key miss")


if __name__ == "__main__":
    main()
