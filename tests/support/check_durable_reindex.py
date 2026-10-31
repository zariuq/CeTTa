"""Paged index repair publishes once, preserving ordinary Space data and IDs."""
import argparse
from collections import Counter
import json
from pathlib import Path
import socket
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, encode
from value_worker_client import Worker
from check_native_kb_service import Server, Client, await_resources


def bag(rows):
    return Counter(encode(row) for row in rows)


def use(client, token, route, argument=(), **kwargs):
    return client.call(E("UseValueV1", token, S(route), argument), **kwargs)


def opened(client, request, maximum=16 * 1024 * 1024):
    view, coordinates = client.snapshot()
    try:
        answer = client.route("ReindexOpen", E("ReindexOpenV1", S("sample"), request, maximum), view)
    finally:
        client.drop(view)
    return answer, coordinates


def drain(client, answer, *, steps=8, observe=None, replay=False):
    progress = []
    for i in range(20000):
        if answer[0] != S("ServiceResourceV1"):
            return answer, progress
        token, detail = answer[1:]
        assert detail[0] == S("SpaceReindexProgressV1"), detail
        progress.append(detail[1:])
        if observe:
            observe(detail)
        identifier = f"reindex-page-{i}" if replay else None
        answer = use(client, token, "ReindexPage", E("ReindexPageV1", steps), identifier=identifier)
        if replay:
            assert use(client, token, "ReindexPage", E("ReindexPageV1", steps), identifier=identifier) == answer
            assert use(client, token, "ReindexPage", E("ReindexPageV1", steps), ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
    raise AssertionError("reindex did not complete within the test bound")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assert args.rounds > 0
    with tempfile.TemporaryDirectory(prefix="durable-reindex-") as directory:
        root = Path(directory)
        path = root / "store"; path.mkdir(mode=0o700)
        worker = Worker(args.worker.resolve(), ROOT / "tests/support/indexed_space_fixture.metta", "indexed-fixture:run", language="petta")
        maintenance = Worker(args.worker.resolve(), ROOT / "tests/support/durable_reindex_fixture.metta", "reindex-fixture:run", language="petta")
        server = Server(args.worker.resolve(), root)
        client = other = None
        try:
            def call(command):
                return worker.call(E("At", str(path), command))
            specs = (E("SpaceIndexV1", 3, 0), E("SpaceIndexV1", 3, 1), E("SpaceIndexV1", 4, 3))
            assert worker.call(E("Create", str(path), specs))[0] == S("KnowledgeCommittedV1")
            values = [E("edge", S("a"), S("b")), E("edge", S("a"), S("b")),
                      E("edge", True, S("native-true")), E("edge", False, S("native-false")),
                      E("edge", S("True"), S("symbol-True")), E("edge", S("False"), S("symbol-False")),
                      E("edge", S("true"), S("symbol-true")), E("edge", S("false"), S("symbol-false")),
                      E("rel", Var(0), Var(0)), E("rel", Var(0), Var(1)), Var(0),
                      E("danger"), E("=", E("stored"), E("println!", S("ACCIDENTAL_EXECUTION")))]
            # The posting corpus exceeds one CAV1 message; seed messages stay
            # bounded. Maintenance must not collect it in one result/ops list.
            values += [E("property", S(f"node-{i}"), S("description"), f"{i:08d}:" + "x" * 8192) for i in range(128)]
            references = []
            for start in range(0, len(values), 16):
                answer = call(E("Commit", f"seed-{start}", tuple(E("SpaceInsertV1", i, value) for i, value in enumerate(values[start:start + 16]))))
                assert answer[0] == S("KnowledgeCommittedV1"), answer
                references.extend(answer[1][1])
            server.start(); client, other = Client(server.path), Client(server.path)
            def all_rows():
                view, _ = client.snapshot()
                query = client.query(view, Var(0), Var(0))
                try:
                    return client.drain(query)
                finally:
                    client.drop(query); client.drop(view)
            expected = bag(all_rows())
            assert len(expected) == len(values) and len(set(map(encode, references))) == len(values)
            assert call(E("LegacyMissingV2"))[0] == S("LegacyMissingV2")
            revision = call(E("Info"))
            legacy = call(E("Query", E("edge", S("a"), Var(0)), Var(0)))
            assert legacy[2][1] > 0 and legacy[2][2] == 0, legacy
            assert call(E("Commit", "refused-before-upgrade", (E("SpaceInsertV1", 0, E("not-published")),))) == E("SpaceRefusedV1", S("index-upgrade-required"))

            # Cancellation, read-only attenuation and disconnect release the
            # private spool, without publishing metadata or a success receipt.
            for mode in ("cancel", "cancel-filling", "readonly", "disconnect", "kill"):
                abandoned = Client(server.path)
                state, _ = opened(abandoned, f"abandon-{mode}")
                state = use(abandoned, state[1], "ReindexPage", E("ReindexPageV1", 16))
                assert state[0] == S("ServiceResourceV1"), state
                if mode == "cancel-filling":
                    while state[2][1] != S("Filling"):
                        state = use(abandoned, state[1], "ReindexPage", E("ReindexPageV1", 1))
                        assert state[0] == S("ServiceResourceV1"), state
                    state = use(abandoned, state[1], "ReindexPage", E("ReindexPageV1", 2))
                    assert state[2][1] == S("Filling") and state[2][2] > 0, state
                if mode in ("cancel", "cancel-filling", "readonly"):
                    if mode == "readonly":
                        assert abandoned.call(E("ReadOnlyV1")) == S("ServiceReadOnlyV1")
                        assert use(abandoned, state[1], "ReindexPage", E("ReindexPageV1", 1), ok=False) == E("ServiceRefusedV1", S("read-only"))
                    assert use(abandoned, state[1], "ReindexCancel") == S("ReindexClosedV1")
                if mode == "kill":
                    server.stop(crash=True)
                abandoned.close()
                if mode == "kill":
                    client.close(); other.close(); server.start()
                    client, other = Client(server.path), Client(server.path)
                await_resources(client, 0)
                assert call(E("Info")) == revision
                assert not list(path.glob(".batch-*"))

            # A quota failure is explicit, and even a partially staged prefix
            # cannot publish. The state is retired on the terminal response.
            state, _ = opened(client, "quota", 4096)
            refused, _ = drain(client, state)
            assert refused[0] == S("StoreRefused"), refused
            assert call(E("Info")) == revision
            await_resources(client, 0)
            readonly = Client(server.path)
            try:
                assert readonly.call(E("ReadOnlyV1")) == S("ServiceReadOnlyV1")
                view, _ = readonly.snapshot()
                assert readonly.route("ReindexOpen", E("ReindexOpenV1", S("sample"), "readonly-open", 4096), view, ok=False) == E("ServiceRefusedV1", S("read-only"))
                readonly.drop(view)
            finally:
                readonly.close()
            state, _ = opened(client, "invalid-page")
            assert use(other, state[1], "ReindexPage", E("ReindexPageV1", 1), ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            assert use(client, state[1], "ReindexPage", E("ReindexPageV1", 129)) == E("SpaceRefusedV1", S("reindex-page-limit"))
            assert use(client, state[1], "ReindexCancel", ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            await_resources(client, 0)

            # A separate writer changes an unrelated collection while repair
            # uses an old view: conservative revision validation must conflict.
            state, _ = opened(client, "competing")
            assert other.route("Create", E("CreateV1", S("other"), "other-create"))[0] == S("KnowledgeCommittedV1")
            revision = call(E("Info"))
            conflict, _ = drain(client, state, steps=64)
            assert conflict == E("StoreRefused", S("conflict")), conflict
            assert call(E("Info")) == revision
            unresolved = other.route("Resolve", E("ResolveV1", "competing", E("SpaceReindexV1", S("sample"))))
            assert unresolved == E("KnowledgeUnrecordedV1", *revision[1:]), unresolved

            # Capture old readers, then build page by page. Every intermediate
            # page observes exactly the old public revision. The final call
            # writes metadata, all postings and the receipt in one publication.
            old_view, _ = other.snapshot()
            old_query = other.query(old_view, Var(0), Var(0))
            state, _ = opened(client, "upgrade")
            def unchanged(_):
                assert call(E("Info")) == revision
            repaired, progress = drain(client, state, steps=8, observe=unchanged, replay=True)
            assert repaired[0] == S("KnowledgeCommittedV1"), repaired
            assert repaired[1] == E("SpaceReindexedV1", S("sample"), len(values)), repaired
            assert repaired[3] == revision[2] + 1
            assert max(row[3] for row in progress) > 1048576, progress[-1]
            assert bag(other.drain(old_query)) == expected
            other.drop(old_query); other.drop(old_view)
            assert bag(all_rows()) == expected
            for value in [True, False, S("True"), S("False"), S("true"), S("false"), S("a")]:
                indexed = call(E("Query", E("edge", value, Var(0)), Var(0)))
                reference = call(E("Reference", E("edge", value, Var(0)), Var(0)))
                assert Counter(encode(row[1]) for row in indexed[1]) == Counter(map(encode, reference[1])), (value, indexed, reference)
                assert indexed[2][2] > 0, indexed
            replayed, _ = opened(client, "upgrade")
            assert replayed[0] == S("KnowledgeReplayedV1"), replayed
            assert replayed[1] == repaired[1]
            assert call(E("Info"))[2] == repaired[3]
            await_resources(client, 0)
            assert other.route("Compact")[0] == S("StoreCompacted")
            assert bag(all_rows()) == expected

            # Stage until the next step is publication. Lose that reply, then
            # kill/reopen. A durable receipt settles the retry, without doing
            # another rebuild or advancing the revision again.
            state, _ = opened(client, "lost-reply")
            while state[2][1] != S("Ready"):
                state = use(client, state[1], "ReindexPage", E("ReindexPageV1", 1))
                assert state[0] == S("ServiceResourceV1"), state
            client.send(E("UseValueV1", state[1], S("ReindexPage"), E("ReindexPageV1", 1)))
            client.socket.shutdown(socket.SHUT_WR)
            await_resources(other, 0)  # Handler returned; final reply unread.
            server.stop(crash=True); client.close(); other.close()
            server.start(); client, other = Client(server.path), Client(server.path)
            resolved = client.route("Resolve", E("ResolveV1", "lost-reply", E("SpaceReindexV1", S("sample"))))
            assert resolved[0] == S("KnowledgeResolvedV1"), resolved
            lost_revision = call(E("Info"))
            again, _ = opened(client, "lost-reply")
            assert again[0] == S("KnowledgeReplayedV1"), again
            assert call(E("Info")) == lost_revision
            assert bag(all_rows()) == expected
            view, _ = client.snapshot()
            changed = client.route("Commit", E("CommitV1", S("sample"), "lost-reply", ()), view)
            assert changed == E("KnowledgeRefusedV1", S("receipt-collision")), changed
            client.drop(view)
            assert call(E("Commit", "editable-again", (E("SpaceUpdateV1", references[0], E("edge", S("updated"), S("b"))),)))[0] == S("KnowledgeCommittedV1")

            samples = []
            for i in range(args.rounds + 32):
                state, _ = opened(client, "cancel-fixed")
                state = use(client, state[1], "ReindexPage", E("ReindexPageV1", 16))
                assert state[0] == S("ServiceResourceV1"), state
                assert use(client, state[1], "ReindexCancel") == S("ReindexClosedV1")
                await_resources(client, 0)
                if i >= 31 and ((i - 31) % 25 == 0 or i == args.rounds + 31):
                    samples.append(server.sample(client, i - 31))
            assert len({s["descriptors"] for s in samples}) == 1, samples
            assert len({s["stats"][2] for s in samples}) == 1, samples

            # Embedded maintenance shares the same library and owner. Include
            # an empty plain Space, which has no posting records to clear.
            empty = root / "empty"; empty.mkdir(mode=0o700)
            assert worker.call(E("CreatePlain", str(empty)))[0] == S("KnowledgeCommittedV1")
            errors = maintenance.call(E("At", str(empty), E("BatchErrors")))
            assert errors == E("BatchErrorsV1", *([E("StoreRefused", S("closed-or-busy-batch"))] * 3)), errors
            for index, request in enumerate(("plain", "plain")):
                result = maintenance.call(E("At", str(empty), E("Reindex", request, 1048576, 2)))
                assert result[0] == S("KnowledgeCommittedV1" if index == 0 else "KnowledgeReplayedV1"), result
                assert result[1] == E("SpaceReindexedV1", S("sample"), 0), result
            plain_values = (E("edge", S("False"), S("same")), E("edge", S("False"), S("same")))
            inserted = worker.call(E("At", str(empty), E("Commit", "plain-insert", tuple(E("SpaceInsertV1", i, v) for i, v in enumerate(plain_values)))))
            assert inserted[0] == S("KnowledgeCommittedV1"), inserted
            plain_before = worker.call(E("At", str(empty), E("Query", Var(0), Var(0))))
            result = maintenance.call(E("At", str(empty), E("Reindex", "plain-filled", 1048576, 1)))
            assert result[1] == E("SpaceReindexedV1", S("sample"), 2), result
            assert worker.call(E("At", str(empty), E("Query", Var(0), Var(0)))) == plain_before
            empty_indexed = root / "empty-indexed"; empty_indexed.mkdir(mode=0o700)
            assert worker.call(E("Create", str(empty_indexed), specs))[0] == S("KnowledgeCommittedV1")
            result = maintenance.call(E("At", str(empty_indexed), E("Reindex", "empty-indexed", 1048576, 1)))
            assert result[1] == E("SpaceReindexedV1", S("sample"), 0), result
            report = {"facts": len(values), "paged_rebuild_pages": len(progress),
                      "maximum_retained_spool_bytes": max(row[3] for row in progress),
                      "one_publication": True, "unchanged_payloads_and_occurrence_ids": True,
                      "legacy_missing_posting_repaired": True, "competing_write_conflicts": True,
                      "cancel_disconnect_kill_before_publication": True, "quota_refuses_without_publication": True,
                      "lost_reply_restart_receipt": True, "indexed_edits_restored": True,
                      "empty_and_nonempty_unindexed": True, "empty_indexed": True,
                      "post_warmup_cancellation_rounds": args.rounds, "lifetime_samples": samples}
            if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2))
        finally:
            if client: client.close()
            if other: other.close()
            server.stop(); maintenance.close(); worker.close()


if __name__ == "__main__":
    main()
