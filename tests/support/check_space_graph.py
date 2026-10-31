"""MeTTa graph traversal over ordinary durable Space cursors and local service."""
import argparse
from collections import Counter, deque
import json
from pathlib import Path
import random
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, FloatBits, encode
from value_worker_client import Worker
from check_native_kb_service import Server, Client, await_resources


def flatten(answer):
    assert answer[0] == S("GraphCompleteV1"), answer
    return [row for page in answer[1] for row in page]


def bag(values):
    return Counter(encode(value) for value in values)


def walk_oracle(edges, start, minimum, maximum, direction="out"):
    result, pending = [], deque([((start,), ())])
    while pending:
        nodes, identities = pending.popleft()
        if len(identities) >= minimum:
            result.append(E("KgPath", nodes, identities, len(identities)))
        if len(identities) == maximum:
            continue
        for identity, source, target in edges:
            here, there = (source, target) if direction == "out" else (target, source)
            if here == nodes[-1]: pending.append((nodes + (there,), identities + (identity,)))
    return result


def oracle(edges, plan):
    kind, spec, start, *arguments = plan
    direction = arguments[-1].name
    if kind == S("KgNeighbours"):
        return [E("KgEdge", identity, source, target) for identity, source, target in edges
                if (source if direction == "out" else target) == start]
    if kind == S("KgPaths"):
        return walk_oracle(edges, start, arguments[0], arguments[1], direction)
    if kind == S("KgTriangles"):
        return [path for path in walk_oracle(edges, start, 3, 3, direction)
                if path[1][-1] == start and len(set(path[1][:-1])) == 3]
    if kind == S("KgReach"):
        reached, pending = set(), deque([start])
        while pending:
            node = pending.popleft()
            for _, source, target in edges:
                here, there = (source, target) if direction == "out" else (target, source)
                if here == node and there not in reached:
                    reached.add(there); pending.append(there)
        return list(reached)
    assert kind == S("KgShortest"), plan
    goal, ties, _ = arguments
    if start == goal:
        return [E("KgPath", (start,), (), 0)]
    # Exhaustive bounded walks are intentionally independent of the MeTTa
    # walker's visited/distance policy. A finite shortest path is simple.
    vertices = {node for _, source, target in edges for node in [source, target]}
    paths = [path for path in walk_oracle(edges, start, 1, len(vertices), direction) if path[1][-1] == goal]
    if not paths: return []
    distance = min(path[3] for path in paths)
    return [path for path in paths if path[3] == distance]


def open_graph(client, view, plan, arity=3, states=10000):
    answer = client.route("GraphOpen", E("GraphOpenV1", S("sample"), arity, plan, states), view)
    assert answer[0] == S("ServiceResourceV1"), answer
    return answer[1]


def service_rows(client, token, count, fuel, *, replay=False):
    result = []
    for index in range(20000):
        request = f"graph-page-{index}"
        previous = token
        body = E("UseValueV1", token, S("GraphPage"), E("GraphPageV1", count, fuel))
        answer = client.call(body, request)
        if replay: assert client.call(body, request) == answer
        if answer[0] == S("ServiceResourceV1"):
            token, page = answer[1:]
            assert token != previous
            assert page[0] == S("GraphPageV1") and page[2] in [S("More"), S("Paused")], page
        else:
            page = answer
            assert page[0] == S("GraphPageV1") and page[2][0] == S("Complete"), page
            result.extend(page[1]);return result, page[2][1]
        result.extend(page[1])
    raise AssertionError("graph did not finish within test bound")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--generated", type=int, default=6)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--warmup", type=int, default=64)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assert args.generated > 0 and args.rounds > 0 and args.warmup >= 0
    binary = args.worker.resolve()
    with tempfile.TemporaryDirectory(prefix="space-graph-") as directory:
        root = Path(directory); (root / "store").mkdir(mode=0o700)
        seed = Worker(binary, ROOT / "tests/support/indexed_space_fixture.metta", "indexed-fixture:run", language="petta")
        embedded = Worker(binary, ROOT / "tests/support/space_graph_fixture.metta", "space-graph:run", language="petta", profile=True)
        server = Server(binary, root, module=ROOT / "tests/support/space_graph_fixture.metta", entry="graph-server:configuration")
        client = None
        checks, generated_checks = 0, 0
        try:
            specs = tuple(E("SpaceIndexV1", arity, position) for arity in [3, 4] for position in [0, 1, 2])
            assert seed.call(E("Create", str(root / "store"), specs))[0] == S("KnowledgeCommittedV1")
            pairs = [("a", "b"), ("a", "b"), ("a", "c"), ("b", "d"), ("c", "d"),
                     ("d", "a"), ("c", "c"), ("x", "y")]
            values = [E("edge", source, target) for source, target in pairs]
            # These remain valid ordinary Space data. They are not declared
            # ground graph edges, even when unification instantiates a field.
            values += [E("edge", Var(0), "invented"), E("edge", "a", Var(0)), Var(0),
                       E("=", E("inert"), E("println!", S("ACCIDENTAL_EXECUTION"))),
                       E("rich", "a", "b", "property-distinct-from-b"), E("rich", "b", "c", "different"),
                       E(",", "a", "comma-node"), E("|", "a", "pipe-node"),
                       E("code", "a", E("danger")), E("code", E("danger"), "target"),
                       E("rich2", "a", "b", E("danger")), (E("danger"), "a", "relation-target")]
            numeric_ids = [(1, "integer"), (FloatBits.of(1.0), "float"), (FloatBits.of(0.0), "positive-zero"),
                           (FloatBits.of(-0.0), "negative-zero"), (FloatBits(0x7ff8000000000001), "nan"),
                           (True, "boolean"), (False, "false-boolean"), (S("False"), "false-symbol")]
            values += [E("numeric", identity, label) for identity, label in numeric_ids]
            values += [(1, "a", "numeric-relation")]
            applied = seed.call(E("At", str(root / "store"), E("Commit", "seed",
                tuple(E("SpaceInsertV1", i, atom) for i, atom in enumerate(values)))))
            assert applied[0] == S("KnowledgeCommittedV1"), applied
            identities = applied[1][1]
            edges = [(identities[i], source, target) for i, (source, target) in enumerate(pairs)]
            spec = E("KgRelation", S("edge"), 0, 1)
            plans = [E("KgNeighbours", spec, "a", S("out")), E("KgNeighbours", spec, "d", S("in")),
                     E("KgReach", spec, "a", S("out")), E("KgReach", spec, "d", S("in")),
                     E("KgReach", spec, "absent", S("out")), E("KgPaths", spec, "a", 0, 2, S("out")),
                     E("KgTriangles", spec, "a", S("out")), E("KgShortest", spec, "a", "d", S("all"), S("out")),
                     E("KgShortest", spec, "a", "d", S("one"), S("out")),
                     E("KgShortest", spec, "a", "a", S("all"), S("out")),
                     E("KgShortest", spec, "a", "x", S("all"), S("out"))]
            server.start();client = Client(server.path)
            for plan in plans:
                expected = oracle(edges, plan)
                for count, fuel in [(1, 1), (20, 1000)]:
                    answer = embedded.call(E("Query", str(root / "store"), S("sample"), 3, plan, count, fuel, 10000))
                    actual = flatten(answer)
                    view, metadata = client.snapshot()
                    token = open_graph(client, view, plan);client.drop(view)
                    remote, work = service_rows(client, token, count, fuel, replay=True)
                    if plan[0] == S("KgShortest") and plan[-2] == S("one"):
                        assert len(actual) == len(remote) == min(1, len(expected))
                        assert all(encode(row) in bag(expected) for row in actual + remote)
                    else:
                        assert bag(actual) == bag(remote) == bag(expected), (plan, actual, remote, expected)
                    checks += 1
            # Different fields must use independent pattern variables. Literal
            # connective heads retain their data meaning through a scan.
            for relation, arity, expected in [(S("rich"), 4, ["b", "c"]), (S(","), 3, ["comma-node"]),
                                             (S("|"), 3, ["pipe-node"]), (S("code"), 3, [E("danger"), "target"]),
                                             (S("rich2"), 4, ["b"]), (E("danger"), 3, ["relation-target"])]:
                plan = E("KgReach", E("KgRelation", relation, 0, 1), "a", S("out"))
                assert bag(flatten(embedded.call(E("Query", str(root / "store"), S("sample"), arity, plan, 1, 2, 10000)))) == bag(expected)
                view, _ = client.snapshot();token = open_graph(client, view, plan, arity);client.drop(view)
                assert bag(service_rows(client, token, 1, 2)[0]) == bag(expected)
                checks += 1
            # Canonical ground identities are stricter than arithmetic numeric
            # equality. Broad candidates prevent both promotion and NaN from
            # changing the graph's identity relation.
            for identity, label in numeric_ids:
                plan = E("KgReach", E("KgRelation", S("numeric"), 0, 1), identity, S("out"))
                actual = flatten(embedded.call(E("Query", str(root / "store"), S("sample"), 3, plan, 1, 4, 10000)))
                assert actual == [label], (identity, label, actual)
                view, _ = client.snapshot();token = open_graph(client, view, plan);client.drop(view)
                assert service_rows(client, token, 1, 4)[0] == [label]
                checks += 1
            plan = E("KgReach", E("KgRelation", 1, 0, 1), "a", S("out"))
            assert flatten(embedded.call(E("Query", str(root / "store"), S("sample"), 3, plan, 1, 4, 10000))) == ["numeric-relation"]
            checks += 1
            # Captured roots and actual occurrence reads survive retraction,
            # publication, compaction and closing the exported informing view.
            view, _ = client.snapshot()
            old_token = open_graph(client, view, plans[0])
            original = client.route("ReadOccurrence", E("OccurrenceReadV1", S("sample"), identities[0], 131072), view)
            assert original[0] == S("SpaceOccurrenceReadV1") and original[1] == values[0], original
            changed = seed.call(E("At", str(root / "store"), E("Commit", "retract", (E("SpaceDeleteV1", identities[0]),))))
            assert changed[0] == S("KnowledgeCommittedV1")
            assert client.route("Compact")[0] == S("StoreCompacted")
            assert client.route("ReadOccurrence", E("OccurrenceReadV1", S("sample"), identities[0], 131072), view) == original
            client.drop(view)
            assert bag(service_rows(client, old_token, 1, 2)[0]) == bag(oracle(edges, plans[0]))
            view, _ = client.snapshot()
            assert client.route("ReadOccurrence", E("OccurrenceReadV1", S("sample"), identities[0], 131072), view) == S("SpaceOccurrenceAbsentV1")
            assert client.route("ReadOccurrence", E("OccurrenceReadV1", S("other"), identities[1], 131072), view) == E("SpaceRefusedV1", S("occurrence-collection"))
            token = open_graph(client, view, plans[0]);client.drop(view)
            assert bag(service_rows(client, token, 1, 2)[0]) == bag(oracle(edges[1:], plans[0]))
            # Budget exhaustion is an explicit graph failure, never complete
            # empty reachability. Zero page fuel is resumable, not END.
            view, _ = client.snapshot();token = open_graph(client, view, plans[2]);client.drop(view)
            inspected = client.call(E("UseValueV1", token, S("GraphInspect"), ()))
            assert inspected[0] == S("ServiceResourceV1") and inspected[2][0] == S("KgSpaceInspectionV1"), inspected
            token = inspected[1]
            answer = client.call(E("UseValueV1", token, S("GraphPage"), E("GraphPageV1", 1, 0)))
            assert answer[0] == S("ServiceResourceV1") and answer[2] == E("GraphPageV1", (), S("Paused")), answer
            assert client.call(E("UseValueV1", answer[1], S("GraphCancel"), ())) == S("GraphCancelledV1")
            limited = embedded.call(E("Query", str(root / "store"), S("sample"), 3, plans[2], 1, 20, 2))
            assert limited[0] == S("GraphFailedV1"), limited
            large = seed.call(E("At", str(root / "store"), E("Commit", "large-property",
                (E("SpaceInsertV1", 0, E("large", "a", "b", "x" * 140000)),))))
            assert large[0] == S("KnowledgeCommittedV1"), large
            oversized = embedded.call(E("Query", str(root / "store"), S("sample"), 4,
                E("KgReach", E("KgRelation", S("large"), 0, 1), "a", S("out")), 1, 20, 10000))
            assert oversized[0] == S("GraphFailedV1") and oversized[1][0] == S("StoreTooLarge"), oversized
            assert oversized[2][2] > 0 and oversized[2][3] > 140000, oversized
            samples = []
            for i in range(args.warmup + args.rounds):
                view, _ = client.snapshot();token = open_graph(client, view, plans[2]);client.drop(view)
                if i % 2:
                    assert bag(service_rows(client, token, 2, 8)[0]) == bag(oracle(edges[1:], plans[2]))
                else:
                    assert client.call(E("UseValueV1", token, S("GraphCancel"), ())) == S("GraphCancelledV1")
                if i == args.warmup - 1 or (i >= args.warmup and ((i - args.warmup + 1) % 50 == 0 or i == args.rounds + args.warmup - 1)):
                    samples.append(server.sample(client, i - args.warmup + 1))
            assert len({item["descriptors"] for item in samples}) == 1, samples
            assert len({item["stats"][2] for item in samples}) == 1, samples
            view, _ = client.snapshot();token = open_graph(client, view, plans[2]);client.drop(view)
            client.close();client = None
            server.stop(crash=True);server.start();client = Client(server.path)
            assert client.call(E("UseValueV1", token, S("GraphPage"), E("GraphPageV1", 1, 2)), ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            view, _ = client.snapshot();token = open_graph(client, view, plans[2]);client.drop(view)
            assert bag(service_rows(client, token, 1, 2)[0]) == bag(oracle(edges[1:], plans[2]))
            # Independent generated finite graphs, with duplicate edges and
            # cycles. Keep them small for the exhaustive shortest-walk oracle.
            rng = random.Random(29117)
            for graph_number in range(args.generated):
                generated_root = root / f"generated-{graph_number}";generated_root.mkdir(mode=0o700)
                path = generated_root / "store";path.mkdir(mode=0o700)
                assert seed.call(E("Create", str(path), specs))[0] == S("KnowledgeCommittedV1")
                pairs2 = [(str(rng.randrange(4)), str(rng.randrange(4))) for _ in range(7)]
                result = seed.call(E("At", str(path), E("Commit", "seed", tuple(E("SpaceInsertV1", i, E("edge", a, b)) for i, (a, b) in enumerate(pairs2)))))
                refs2 = result[1][1];edges2 = [(refs2[i], a, b) for i, (a, b) in enumerate(pairs2)]
                generated_server = Server(binary, generated_root, module=ROOT / "examples/native_space_graph_server.metta", entry="graph-server:configuration")
                generated_client = None
                try:
                    generated_server.start();generated_client = Client(generated_server.path)
                    for kind in ["KgReach", "KgTriangles", "KgPaths", "KgShortest"]:
                        plan = {"KgReach": E(kind, spec, "0", S("out")), "KgTriangles": E(kind, spec, "0", S("out")),
                                "KgPaths": E(kind, spec, "0", 0, 2, S("out")), "KgShortest": E(kind, spec, "0", "3", S("all"), S("out"))}[kind]
                        actual = flatten(embedded.call(E("Query", str(path), S("sample"), 3, plan, 2, 4, 10000)))
                        view, _ = generated_client.snapshot();token = open_graph(generated_client, view, plan);generated_client.drop(view)
                        remote, _ = service_rows(generated_client, token, 2, 4)
                        assert bag(actual) == bag(remote) == bag(oracle(edges2, plan)), (graph_number, plan, actual, remote)
                        generated_checks += 1
                finally:
                    if generated_client: generated_client.close()
                    generated_server.stop()
            report = {"embedded_service_oracle_comparisons": checks, "generated_oracle_comparisons": generated_checks,
                      "generated_seed": 29117, "warmup_rounds": args.warmup, "post_warmup_rounds": args.rounds, "samples": samples,
                      "ground_occurrence_projection": True, "captured_retraction_compaction": True,
                      "restart_preserves_graph_occurrences": True,
                      "source": "existing MeTTa BFS over shared ordinary Space cursors; no graph-native matcher"}
            if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2))
        finally:
            if client: client.close()
            server.stop();embedded.close();seed.close()


if __name__ == "__main__":
    main()
