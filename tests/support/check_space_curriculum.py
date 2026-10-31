"""The claims/dependency curriculum uses one ordinary Space in both modes."""
import argparse
from pathlib import Path
import json
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S, encode
from check_native_kb_service import Server, Client, await_resources
from check_space_query import rows, bag, open_query, drain
from check_space_graph import service_rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="space-curriculum-") as directory:
        root = Path(directory); path = root / "store"; path.mkdir(mode=0o700)
        module = ROOT / "examples/kb/space_curriculum.metta"
        worker = Worker(args.worker.resolve(), module, "space-curriculum:run", language="petta")
        server = Server(args.worker.resolve(), root, module=module, entry="space-curriculum:configuration")
        client = None
        try:
            assert worker.call(E("Install", str(path)))[0] == S("KnowledgeCommittedV1")
            plans = worker.call(S("Plans"))
            assert plans[0] == S("CurriculumPlansV1"), plans
            expected = [rows(worker.call(E("Query", str(path), plan, 2, 3))) for plan in plans[1:]]
            supports, optional, two_hops, groups, independent = expected
            assert len(supports) == 4
            assert {row[1][2] for row in supports} == {S(f"evidence{i}") for i in range(1, 5)}
            assert all(len(row[1][7]) == 2 and len(row[2]) == 1 for row in supports), supports
            assert len({encode(row[1][7][1]) for row in supports}) == 4
            assert len(optional) == 5
            assert sum(row[0] == S("EvidenceAbsentV1") for row in optional) == 1
            assert any(row[0] == S("EvidenceAbsentV1") and row[2:] == (S("judgement4"), S("claim2")) for row in optional)
            assert len(two_hops) == 5
            assert {(row[1][1].name, row[1][2].name) for row in two_hops} == {
                ("dep3", "dep2"), ("dep4", "dep1"), ("dep4", "dep5"), ("dep6", "dep1"), ("dep6", "dep5")}
            assert bag(groups) == bag([E("RelGroup", S("origin1"), 3), E("RelGroup", S("origin2"), 1)])
            assert independent == [2], independent
            # Fresh process: durable membership and receipts, not interpreter
            # heap addresses or generated symbol allocation, restore the data.
            worker.close()
            worker = Worker(args.worker.resolve(), module, "space-curriculum:run", language="petta")
            assert worker.call(E("Install", str(path)))[0] == S("KnowledgeReplayedV1")
            server.start(); client = Client(server.path)
            for plan, wanted in zip(plans[1:], expected):
                assert bag(rows(worker.call(E("Query", str(path), plan, 1, 1)))) == bag(wanted)
                view, _ = client.snapshot(); token = open_query(client, view, plan); client.drop(view)
                assert bag(drain(client, token, 2, 3, replay=True)[0]) == bag(wanted)
                await_resources(client, 0)
            # Graph traversal and relational plans share this same ordinary
            # collection, captured views, occurrence references and service.
            view, _ = client.snapshot()
            reach = E("KgReach", E("KgRelation", S("dependency"), 1, 2), S("emit"), S("out"))
            opened = client.route("GraphOpen", E("GraphOpenV1", S("curriculum"), 4, reach, 1000), view)
            assert opened[0] == S("ServiceResourceV1"), opened
            client.drop(view)
            reached, _ = service_rows(client, opened[1], 1, 3)
            assert bag(reached) == bag([S("parse"), S("check"), S("lex")]), reached
            await_resources(client, 0)
            # Retract just one supporting occurrence. Its logical judgement
            # and other supporting/source memberships must remain present.
            removed = supports[0][1][7][1]
            view, _ = client.snapshot()
            changed = client.route("Commit", E("CommitV1", S("curriculum"), "retract-support", (E("SpaceDeleteV1", removed),)), view)
            client.drop(view)
            assert changed[0] == S("KnowledgeCommittedV1"), changed
            remaining = rows(worker.call(E("Query", str(path), plans[1], 2, 3)))
            assert len(remaining) == 3 and all(row[1][7][1] != removed for row in remaining)
            assert client.route("Compact")[0] == S("StoreCompacted")
            server.stop(crash=True); client.close(); server.start(); client = Client(server.path)
            view, _ = client.snapshot(); token = open_query(client, view, plans[1]); client.drop(view)
            assert bag(drain(client, token)[0]) == bag(remaining)
            report = {"ordinary_space": "curriculum", "supporting_occurrences": 4,
                      "independent_source_origins": 2, "optional_rows": 5, "two_hop_paths": 5,
                      "group_counts": {"origin1": 3, "origin2": 1}, "relational_plans": 5,
                      "embedded_service_and_fresh_process_equivalence": True,
                      "graph_and_relational_same_collection": True,
                      "single_occurrence_retraction_compaction_restart": True,
                      "numerical_evidence_revision": "not asserted; source counts are not independent weights"}
            if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2))
        finally:
            if client: client.close()
            server.stop(); worker.close()


if __name__ == "__main__":
    main()
