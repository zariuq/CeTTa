"""Completed graph decisions retain their original data/program dependencies."""
import argparse
import json
from pathlib import Path
import socket
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var
from check_native_kb_service import Server, Client, await_resources
from check_space_query import use


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--module", type=Path, default=ROOT / "tests/support/graph_read_server.metta")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assert args.rounds > 0
    scenarios = []
    relation = E("KgRelation", S("edge"), 0, 1)
    reach = E("KgReach", relation, S("a"), S("out"))
    with tempfile.TemporaryDirectory(prefix="graph-read-") as directory:
        root = Path(directory); (root / "store").mkdir(mode=0o700)
        server = Server(args.worker.resolve(), root, module=args.module.resolve(),
                        entry="graph-read-test:configuration")
        peers = []
        def connect():
            client = Client(server.path); peers.append(client); return client
        def write(client, request, *atoms):
            view, _ = client.snapshot()
            try:
                changes = tuple(E("SpaceInsertV1", n, atom) for n, atom in enumerate(atoms))
                answer = client.route("Commit", E("CommitV1", S("sample"), request, changes), view)
                assert answer[0] == S("KnowledgeCommittedV1"), answer
                return answer
            finally: client.drop(view)
        def open_read(client, plan=reach, family="Graph", states=4096):
            view, _ = client.snapshot()
            try:
                argument = (E("GraphOpenV1", S("sample"), 3, plan, states) if family == "Graph"
                            else E("RelOpenV1", plan, 131072))
                answer = client.route(family + "OpenRead", argument, view)
                assert answer[0] == S("ServiceResourceV1"), answer
                return answer[1]
            finally: client.drop(view)
        def complete(client, token, family="Graph", count=1, fuel=8):
            values = []
            for _ in range(10000):
                answer = use(client, token, family + "ReadPage", E(family + "PageV1", count, fuel))
                assert answer[0] == S("ServiceResourceV1"), answer
                token, page = answer[1:]
                assert page[0] == S(family + "PageV1"), page
                values.extend(page[1])
                if isinstance(page[2], tuple) and page[2][0] == S("Complete"):
                    return token, values
                assert page[2] in (S("More"), S("Paused")), page
            raise AssertionError("finite fixture did not complete")
        def decision(request, family="Graph", changes=None, command=None):
            changes = ((E("SpaceInsertV1", 0, E("decision", request)),) if changes is None else changes)
            return E(family + "ReadCommitV1", S("sample"), request,
                     E("DecisionV1", request) if command is None else command, changes)
        def commit(client, token, request, family="Graph", **kwargs):
            return use(client, token, family + "ReadCommit", decision(request, family, **kwargs))
        def cancel(client, token, family="Graph"):
            answer = use(client, token, family + "ReadCancel")
            assert answer == S(family + "ReadClosedV1"), answer

        try:
            server.start(); first, other = connect(), connect()
            specs = (E("SpaceIndexV1", 3, 0), E("SpaceIndexV1", 3, 1))
            assert first.route("Create", E("CreateIndexedV1", S("sample"), "create", specs))[0] == S("KnowledgeCommittedV1")
            write(first, "seed", E("edge", S("a"), S("b")), E("edge", S("b"), S("c")))
            token = open_read(first)
            assert commit(first, token, "premature") == E("KbRefused", S("graph-read-phase"))
            await_resources(first, 0); scenarios.append("premature-commit-refused")
            assert first.route("GraphReadCommit", decision("forged")) == E("KbRefused", S("graph-read-phase"))

            token = open_read(first)
            paused = use(first, token, "GraphReadPage", E("GraphPageV1", 1, 0))
            assert paused[0] == S("ServiceResourceV1") and paused[2] == E("GraphPageV1", (), S("Paused")), paused
            assert commit(first, paused[1], "paused") == E("KbRefused", S("graph-read-phase"))
            await_resources(first, 0); scenarios.append("paused-is-not-complete")

            token, answers = complete(first, open_read(first))
            assert set(answers) == {S("b"), S("c")}, answers
            assert commit(first, token, "unchanged")[0] == S("KnowledgeCommittedV1")
            await_resources(first, 0); scenarios.append("completed-graph-commit")

            absent_plan = E("KgReach", relation, S("isolated"), S("out"))
            token, before = complete(first, open_read(first, absent_plan))
            assert before == [], before
            write(other, "negative-insertion", E("edge", S("isolated"), S("new")))
            outcome = commit(first, token, "negative-decision")
            assert outcome == E("StoreRefused", S("conflict")), outcome
            fresh, after = complete(first, open_read(first, absent_plan)); cancel(first, fresh)
            assert after == [S("new")], after
            scenarios.append("completed-negative-reachability-phantom")

            shortest = E("KgShortest", relation, S("a"), S("c"), S("all"), S("out"))
            token = open_read(first, shortest)
            paused = use(first, token, "GraphReadPage", E("GraphPageV1", 1, 0))
            write(other, "shorter-path", E("edge", S("a"), S("c")))
            token, old_paths = complete(first, paused[1])
            assert [p[3] for p in old_paths] == [2], old_paths
            assert commit(first, token, "path-decision") == E("StoreRefused", S("conflict"))
            fresh, new_paths = complete(first, open_read(first, shortest)); cancel(first, fresh)
            assert [p[3] for p in new_paths] == [1], new_paths
            scenarios.append("path-insertion-during-captured-query")

            token, one_path = complete(first, open_read(first, shortest))
            write(other, "parallel-shortest", E("edge", S("a"), S("c")))
            assert commit(first, token, "ties-decision") == E("StoreRefused", S("conflict"))
            fresh, tied_paths = complete(first, open_read(first, shortest)); cancel(first, fresh)
            assert len(one_path) == 1 and len(tied_paths) == 2
            assert [p[3] for p in tied_paths] == [1, 1]
            assert len({p[2] for p in tied_paths}) == 2, tied_paths
            scenarios.append("parallel-occurrences-change-all-shortest-ties")

            constrained = open_read(first, states=2)
            failure = use(first, constrained, "GraphReadPage", E("GraphPageV1", 1, 64))
            assert failure[0] == S("GraphPageV1") and failure[2][0] == S("Failed"), failure
            await_resources(first, 0)
            assert use(first, constrained, "GraphReadCommit", decision("failed-query"), ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            scenarios.append("resource-exhaustion-is-not-empty-completion")
            unfinished = open_read(first); cancel(first, unfinished)
            completed, _ = complete(first, open_read(first))
            wrong = use(first, completed, "RelReadCommit", decision("wrong-family", "Rel"))
            assert wrong == E("KbRefused", S("query-read-phase")), wrong
            await_resources(first, 0)
            scenarios.append("wrong-family-consumes-and-closes-context")

            for operation in ("GraphReadPage", "GraphReadCommit", "GraphReadCancel"):
                view, _ = first.snapshot()
                plain = first.route("GraphOpen", E("GraphOpenV1", S("sample"), 3, reach, 4096), view)
                first.drop(view)
                assert plain[0] == S("ServiceResourceV1"), plain
                argument = (E("GraphPageV1", 1, 8) if operation == "GraphReadPage"
                            else decision("plain-cursor"))
                refused = use(first, plain[1], operation, argument)
                expected = (S("GraphReadClosedV1") if operation == "GraphReadCancel"
                            else E("KbRefused", S("graph-read-phase")))
                assert refused == expected, refused
                await_resources(first, 0)
            for operation in ("GraphReadPage", "RelReadPage"):
                completed, _ = complete(first, open_read(first))
                prefix = "Graph" if operation.startswith("Graph") else "Rel"
                refused = use(first, completed, operation, E(prefix + "PageV1", 1, 8))
                assert refused == E("KbRefused", S("graph-read-phase" if prefix == "Graph" else "query-read-phase")), refused
                await_resources(first, 0)
            running = open_read(first)
            refused = use(first, running, "GraphPage", E("GraphPageV1", 1, 8))
            assert refused == E("KbRefused", S("graph-cursor")), refused
            await_resources(first, 0)
            running = open_read(first)
            assert use(first, running, "GraphCancel") == S("GraphCancelledV1")
            await_resources(first, 0)
            running = open_read(first)
            inspected = use(first, running, "GraphInspect")
            assert inspected[0] == S("ServiceResourceV1"), inspected
            assert inspected[2] == E("KbRefused", S("graph-cursor")), inspected
            await_resources(first, 1); cancel(first, inspected[1])
            await_resources(first, 0)
            scenarios.append("old-and-relational-routes-cannot-consume-graph-phases-as-cursors")

            view, _ = first.snapshot()
            malformed = first.route("GraphOpenRead", E("GraphOpenV1", S("sample")), view)
            first.drop(view)
            assert malformed == E("KbRefused", S("graph-read-shape")), malformed
            for operation, argument in [("GraphReadPage", E("GraphPageV1", 1)),
                                        ("GraphReadCommit", E("GraphReadCommitV1", S("sample")))]:
                running = open_read(first)
                assert use(first, running, operation, argument) == E("KbRefused", S("graph-read-shape"))
                await_resources(first, 0)
            completed, _ = complete(first, open_read(first))
            assert use(first, completed, "GraphReadCommit", E("GraphReadCommitV1")) == E("KbRefused", S("graph-read-shape"))
            await_resources(first, 0)
            scenarios.append("malformed-arguments-refuse-without-echoing-protected-state")

            missing = E("KqOptional", E("KqRows", (S("c1"),)), Var(0),
                        E("KqSpace", S("sample"), E("rating", Var(0), Var(1)), Var(1)), S("NoRating"))
            anti = E("KqAnti", E("KqRows", (S("allowed"),)), Var(0),
                     E("KqSpace", S("sample"), E("blocked", Var(1)), Var(1)))
            aggregate = E("KqFold", E("KqSpace", S("sample"), E("edge", Var(0), Var(1)), Var(1)),
                          0, S("graph-read-test:count"), 131072)
            for label, plan, inserted in [
                ("optional", missing, E("rating", S("c1"), 7)),
                ("anti", anti, E("blocked", S("witness"))),
                ("aggregate", aggregate, E("edge", S("d"), S("e")))]:
                token, before = complete(first, open_read(first, plan, "Rel"), "Rel")
                write(other, label + "-insertion", inserted)
                assert commit(first, token, label + "-decision", "Rel") == E("StoreRefused", S("conflict"))
                fresh, after = complete(first, open_read(first, plan, "Rel"), "Rel"); cancel(first, fresh, "Rel")
                assert before != after, (label, before, after)
                if label == "anti": assert before == [S("allowed")] and after == []
                if label == "aggregate": assert after == [before[0] + 1]
                scenarios.append(label + "-insertion-changes-answer-and-refuses-write")

            token, _ = complete(first, open_read(first))
            assert other.route("EditProgram") == S("ProgramEditedV1")
            assert commit(first, token, "stale-program") == E("StoreRefused", S("program-invalidated"))
            scenarios.append("completed-program-watch-invalidated")
            token = open_read(first)
            assert other.route("EditProgram") == S("ProgramEditedV1")
            page = use(first, token, "GraphReadPage", E("GraphPageV1", 1, 8))
            assert page[0] == S("GraphPageV1") and page[2][0] == S("Failed"), page
            await_resources(first, 0); scenarios.append("failure-closes-context")

            readonly = connect(); token, _ = complete(readonly, open_read(readonly))
            readonly.call(E("ReadOnlyV1"))
            assert use(readonly, token, "GraphReadCommit", decision("readonly"), ok=False) == E("ServiceRefusedV1", S("read-only"))
            cancel(readonly, token); scenarios.append("context-does-not-grant-write-rights")
            token = open_read(first)
            assert use(other, token, "GraphReadCancel", ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            cancel(first, token); scenarios.append("foreign-token-refused")

            token, _ = complete(first, open_read(first))
            changes = tuple(E("SpaceInsertV1", n, E("bulk-decision", n)) for n in range(129))
            assert commit(first, token, "over-128-edits", changes=changes)[0] == S("KnowledgeCommittedV1")
            scenarios.append("frame-bounded-write-not-fixed-edit-count")
            token, _ = complete(first, open_read(first))
            first.send(E("UseValueV1", token, S("GraphReadCommit"), decision("lost-reply")))
            first.socket.shutdown(socket.SHUT_WR); await_resources(other, 0)
            settled = other.route("Resolve", E("ResolveV1", "lost-reply", E("DecisionV1", "lost-reply")))
            assert settled[0] == S("KnowledgeResolvedV1"), settled
            for peer in peers: peer.close()
            peers.clear(); server.stop(crash=True); server.start(); first, other = connect(), connect()
            assert first.route("Resolve", E("ResolveV1", "lost-reply", E("DecisionV1", "lost-reply"))) == settled
            token, _ = complete(first, open_read(first))
            other.route("EditProgram")
            assert commit(first, token, "lost-reply")[0] == S("KnowledgeReplayedV1")
            token, _ = complete(first, open_read(first))
            assert commit(first, token, "lost-reply", command=E("DifferentDecisionV1")) == E("KnowledgeRefusedV1", S("receipt-collision"))
            scenarios.append("lost-reply-restart-receipt-before-stale-program")

            # Sample a fixed connection population. An idle second client can
            # expire under instrumentation, and zero resources alone does not
            # mean the host has consumed an abandoned socket's disconnect.
            other.close(); peers.remove(other)
            def settled():
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    stats = first.call(E("StatsV1"))
                    if stats[1:3] == (1, 0):
                        return
                    time.sleep(.005)
                raise AssertionError(("connections/resources did not settle", stats))

            samples = []
            for index in range(args.rounds + 24):
                if index % 3 == 0:
                    token, _ = complete(first, open_read(first)); cancel(first, token)
                else:
                    abandoned = Client(server.path); token = open_read(abandoned)
                    if index % 3 == 1: complete(abandoned, token)
                    abandoned.close()
                settled()
                if index >= 23 and ((index - 23) % 25 == 0 or index == args.rounds + 23):
                    samples.append(server.sample(first, index - 23))
            assert len({p["descriptors"] for p in samples}) == 1, samples
            assert len({p["stats"][2] for p in samples}) == 1, samples
            report = dict(scenarios=scenarios, rounds=args.rounds, lifetime_samples=samples,
                          dependency_policy="captured whole data revision and owned program watch")
            if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2))
        finally:
            for peer in peers: peer.close()
            server.stop()

if __name__ == "__main__": main()
