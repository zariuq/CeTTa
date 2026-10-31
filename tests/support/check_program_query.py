"""Captured data plus owned program watches for derived query decisions."""
import argparse
import json
from pathlib import Path
import sys
import tempfile
import socket

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S
from check_space_query import rows
from check_native_kb_service import Server, Client, await_resources
from check_space_query import use


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--rounds", type=int, default=100)
    args = parser.parse_args()
    assert args.rounds > 0
    cases = []
    with tempfile.TemporaryDirectory(prefix="program-query-") as directory:
        root = Path(directory)
        def call(command):
            worker = Worker(args.worker.resolve(), ROOT / "tests/support/program_query_fixture.metta", "program-test:run", language="petta")
            try:
                return worker.call(command)
            finally:
                worker.close()
        for kind, callback in [("equation", "program-test:value"), ("transitive", "program-test:outer"),
                               ("declaration", "program-test:typed"), ("future", "program-test:future"),
                               ("data", "program-test:value"), ("import", "program-test:value")]:
            path = root / kind; path.mkdir(mode=0o700)
            answer = call(E("Live", str(path), S(kind), S(callback)))
            assert answer[0] == S("ProgramLiveV1") and answer[4][2] == 1, answer
            if kind == "data":
                assert answer[2][0] == S("KqFirst") and answer[2][1] == (E("Old", 2),), answer
            else:
                assert answer[2][0] == S("KqFirstFailure") and answer[2][1] == (), answer
                assert answer[2][2] == E("StoreRefused", S("program-invalidated")), answer
            if kind == "equation":
                assert answer[1] == (E("Old", 1),) and answer[3] == E("New", 9), answer
            cases.append(kind)
        for kind in ["unchanged", "code", "data", "retry", "closed"]:
            path = root / f"commit-{kind}"; path.mkdir(mode=0o700)
            answer = call(E("Commit", str(path), S(kind)))
            assert answer[0] == S("ProgramCommitV1") and rows(answer[1]) == [S("allowed")], answer
            outcome = answer[2]
            if kind in ["unchanged", "retry"]:
                assert outcome[0] == S("KnowledgeCommittedV1") and answer[6] == answer[5] + 1, answer
            else:
                reason = {"code": "program-invalidated", "data": "conflict", "closed": "closed-program-watch"}[kind]
                assert outcome == E("StoreRefused", S(reason)), answer
                assert answer[6] == answer[5] + (kind == "data"), answer
            if kind == "retry":
                assert answer[3][0] == S("KnowledgeReplayedV1"), answer
                assert answer[4] == E("KnowledgeRefusedV1", S("receipt-collision")), answer
            cases.append(f"commit-{kind}")
        path = root / "during"; path.mkdir(mode=0o700)
        during = call(E("During", str(path)))
        assert during[0] == S("KqFirstFailure") and during[1] == (), during
        assert during[2] == E("StoreRefused", S("program-invalidated")), during
        assert during[3][2] > 0 and during[3][3] > 0, during
        for kind in ["data", "other-equation"]:
            path = root / f"graph-{kind}"; path.mkdir(mode=0o700)
            result = call(E("Graph", str(path), S(kind)))
            assert result[0] == S("ProgramGraphV1") and len(result[1]) == 1, result
            if kind == "data":
                assert result[2][2][0] in (S("KgMore"), S("KgComplete")) and result[3][0] == S("KnowledgeCommittedV1"), result
            else:
                assert result[2][2][0] == S("KgFailure") and result[3] == E("StoreRefused", S("program-invalidated")), result
        errors = call(S("WatchErrors"))
        assert errors == E("ProgramWatchErrorsV1", E("StoreRefused", S("closed-program-watch")),
                           S("StoreProgramCurrent"), E("StoreRefused", S("closed-program-watch")),
                           E("StoreRefused", S("program-invalidated"))), errors
        service = root / "service"; service.mkdir(mode=0o700); (service / "store").mkdir(mode=0o700)
        server = Server(args.worker.resolve(), service, module=ROOT / "tests/support/program_query_server.metta", entry="program-server:configuration")
        client = other = None
        try:
            server.start(); client, other = Client(server.path), Client(server.path)
            assert client.route("Create", E("CreateV1", S("sample"), "create"))[0] == S("KnowledgeCommittedV1")
            plan = E("KqMap", E("KqRows", (1, 2)), S("sq-test:identity"))
            def open_read(peer):
                view, _ = peer.snapshot()
                answer = peer.route("RelOpenRead", E("RelOpenV1", plan, 131072), view); peer.drop(view)
                assert answer[0] == S("ServiceResourceV1"), answer
                return answer[1]
            def complete(peer, token):
                values = []
                for _ in range(100):
                    answer = use(peer, token, "RelReadPage", E("RelPageV1", 1, 8))
                    assert answer[0] == S("ServiceResourceV1"), answer
                    token, page = answer[1:]; values.extend(page[1])
                    if isinstance(page[2], tuple) and page[2][0] == S("Complete"):
                        assert values == [1, 2], values
                        return token
                raise AssertionError("read session did not complete")
            def command(identity):
                return E("RelReadCommitV1", S("sample"), identity, E("DecisionV1", identity),
                         (E("SpaceInsertV1", 0, E("decision", identity)),))
            premature = open_read(client)
            assert use(client, premature, "RelReadCommit", command("premature")) == E("KbRefused", S("query-read-phase"))
            await_resources(client, 0)
            token = complete(client, open_read(client))
            assert other.route("EditProgram") == S("ProgramEditedV1")
            assert use(client, token, "RelReadCommit", command("stale-code")) == E("StoreRefused", S("program-invalidated"))
            await_resources(client, 0)
            token = complete(client, open_read(client))
            view, _ = other.snapshot()
            assert other.route("Commit", E("CommitV1", S("sample"), "competing", (E("SpaceInsertV1", 0, E("other", "write")),)), view)[0] == S("KnowledgeCommittedV1")
            other.drop(view)
            assert use(client, token, "RelReadCommit", command("stale-data")) == E("StoreRefused", S("conflict"))
            await_resources(client, 0)
            readonly = Client(server.path)
            try:
                token = complete(readonly, open_read(readonly))
                assert readonly.call(E("ReadOnlyV1")) == S("ServiceReadOnlyV1")
                assert use(readonly, token, "RelReadCommit", command("readonly"), ok=False) == E("ServiceRefusedV1", S("read-only"))
                assert use(readonly, token, "RelReadCancel") == S("RelReadClosedV1")
            finally:
                readonly.close()
            token = complete(client, open_read(client))
            client.send(E("UseValueV1", token, S("RelReadCommit"), command("lost-reply")))
            client.socket.shutdown(socket.SHUT_WR)
            await_resources(other, 0)
            settled = other.route("Resolve", E("ResolveV1", "lost-reply", E("DecisionV1", "lost-reply")))
            assert settled[0] == S("KnowledgeResolvedV1"), settled
            server.stop(crash=True); client.close(); other.close()
            server.start(); client, other = Client(server.path), Client(server.path)
            assert client.route("Resolve", E("ResolveV1", "lost-reply", E("DecisionV1", "lost-reply"))) == settled
            token = complete(client, open_read(client))
            assert use(client, token, "RelReadCommit", command("lost-reply"))[0] == S("KnowledgeReplayedV1")
            samples = []
            for index in range(args.rounds + 32):
                if index % 5 == 0: other.call(E("StatsV1"))
                if index % 3 == 0:
                    token = complete(client, open_read(client))
                    assert use(client, token, "RelReadCancel") == S("RelReadClosedV1")
                else:
                    abandoned = Client(server.path)
                    token = open_read(abandoned)
                    if index % 3 == 1: complete(abandoned, token)
                    abandoned.close()
                await_resources(client, 0)
                if index >= 31 and ((index - 31) % 25 == 0 or index == args.rounds + 31):
                    samples.append(server.sample(client, index - 31))
            assert len({s["descriptors"] for s in samples}) == 1, samples
            assert len({s["stats"][2] for s in samples}) == 1, samples
            await_resources(client, 0)
        finally:
            if client: client.close()
            if other: other.close()
            server.stop()
        report = {"cases": cases, "closed_forged_stale_clone_controls": True,
                  "ordinary_live_declarations_preserved": True,
                  "mixed_code_pages_refused": True, "data_only_program_mutation_allowed": True,
                  "program_and_data_dependencies_checked_for_commit": True,
                  "receipt_precedes_stale_program_on_retry": True,
                  "during_callback_change_discards_row_and_keeps_work_counts": True,
                  "graph_program_and_commit_context": True,
                  "service_completion_rights_code_data_lost_reply_restart": True,
                  "post_warmup_read_context_rounds": args.rounds, "lifetime_samples": samples}
        if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
