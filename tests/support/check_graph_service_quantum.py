"""Large star traversal yields bounded service turns without losing its path."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S
from value_worker_client import Worker
from check_native_kb_service import Server, Client, await_resources


def work(value):
    if isinstance(value, tuple):
        if value and value[0] == S("KgWork"):
            return value
        for child in value:
            found = work(child)
            if found is not None:
                return found
    return None


def use(client, token, operation, value=()):
    return client.call(E("UseValueV1", token, S(operation), value))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="graph-turn-") as directory:
        root = Path(directory); path = root / "store"; path.mkdir(mode=0o700)
        writer = Worker(args.worker.resolve(), ROOT / "tests/support/indexed_space_fixture.metta", "indexed-fixture:run", language="petta")
        server = Server(args.worker.resolve(), root, module=ROOT / "examples/native_space_graph_server.metta", entry="graph-server:configuration")
        client = None
        try:
            specs = tuple(E("SpaceIndexV1", 3, i) for i in [0, 1, 2])
            assert writer.call(E("Create", str(path), specs))[0] == S("KnowledgeCommittedV1")
            references = {}
            for start in range(1, 512, 64):
                nodes = list(range(start, min(start + 64, 512)))
                answer = writer.call(E("At", str(path), E("Commit", f"seed-{start}",
                    tuple(E("SpaceInsertV1", n, E("edge", "root", f"node-{n:08d}")) for n in nodes))))
                assert answer[0] == S("KnowledgeCommittedV1"), answer
                references.update(zip(nodes, answer[1][1]))
            server.start(); client = Client(server.path)
            def opened():
                view, _ = client.snapshot()
                plan = E("KgShortest", E("KgRelation", S("edge"), 0, 1), "root", "node-00000511", S("one"), S("out"))
                answer = client.route("GraphOpen", E("GraphOpenV1", S("sample"), 3, plan, 4096), view)
                client.drop(view)
                assert answer[0] == S("ServiceResourceV1"), answer
                return answer[1]
            # A zero budget performs no transition and remains resumable.
            token = opened()
            paused = use(client, token, "GraphPage", E("GraphPageV1", 1, 0))
            assert paused[0] == S("ServiceResourceV1") and paused[2][2] == S("Paused"), paused
            inspected = use(client, paused[1], "GraphInspect")
            assert work(inspected[2])[1] == 0, inspected
            assert use(client, inspected[1], "GraphCancel") == S("GraphCancelledV1")
            # A caller's small upper bound stays small.
            token = opened()
            paused = use(client, token, "GraphPage", E("GraphPageV1", 1, 1))
            inspected = use(client, paused[1], "GraphInspect")
            assert work(inspected[2])[1] == 1, inspected
            assert use(client, inspected[1], "GraphCancel") == S("GraphCancelledV1")
            # The originally failing request shape: large caller budget,
            # delayed first answer and a retained frontier of hundreds of rows.
            token = opened(); steps = 0; rows = []; deltas = []
            for page in range(10000):
                answer = use(client, token, "GraphPage", E("GraphPageV1", 1, 256))
                if answer[0] != S("ServiceResourceV1"):
                    assert answer[0] == S("GraphPageV1") and answer[2][0] == S("Complete"), answer
                    rows.extend(answer[1]); final_work = answer[2][1]; deltas.append(final_work[1] - steps)
                    break
                token, detail = answer[1:]; rows.extend(detail[1])
                inspected = use(client, token, "GraphInspect")
                assert inspected[0] == S("ServiceResourceV1"), inspected
                token = inspected[1]; current = work(inspected[2]); assert current is not None, inspected
                deltas.append(current[1] - steps); steps = current[1]
                assert 0 < deltas[-1] <= 64, deltas
            else:
                raise AssertionError("query failed to complete within fixture bound")
            assert all(0 < delta <= 64 for delta in deltas), deltas
            assert rows == [E("KgPath", ("root", "node-00000511"), (references[511],), 1)], rows
            await_resources(client, 0)
            report = {"vertices": 512, "edges": 511, "caller_steps": 256, "turn_max_steps": 64,
                      "pages": page + 1, "actual_step_deltas": deltas, "rows": len(rows),
                      "candidate_visits": final_work[2], "zero_and_one_budgets_preserved": True}
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(json.dumps(report, indent=2) + "\n")
            print("PASS:512-node star shortest path completes in capped service turns;zero/small budgets and occurrence identity preserved")
        finally:
            if client: client.close()
            server.stop(); writer.close()


if __name__ == "__main__": main()
