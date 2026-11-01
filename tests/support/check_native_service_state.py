"""Linear, connection-owned MeTTa continuations with native resource leaves."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, encode
from check_native_kb_service import Server, Client, await_resources


def use(client, token, operation, argument=(), **kwargs):
    return client.call(E("UseValueV1", token, S(operation), argument), **kwargs)


def open_state(client, payload):
    view, metadata = client.snapshot()
    try:
        answer = client.route("OpenState", E("StateOpenV1", S("sample"), payload), view)
        assert answer[0] == S("ServiceResourceV1") and answer[2] == E("StateOpenedV1", *metadata[1:]), answer
        return answer[1]
    finally:
        client.drop(view)  # State owns an independent clone, not this handle.


def drain(client, token, payload, *, replay=False):
    rows = []
    for index in range(1000):
        request = f"state-step-{index}"
        previous = token
        answer = use(client, token, "StepState", identifier=request)
        if replay:
            assert use(client, previous, "StepState", identifier=request) == answer
        assert use(client, previous, "StepState", ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
        if answer[0] == S("StateDoneV1"):
            assert answer == E("StateDoneV1", len(rows), payload), answer
            return rows
        assert answer[0] == S("ServiceResourceV1") and answer[1] != previous, answer
        token, page = answer[1:]
        if page == S("StatePausedV1"):
            continue
        assert page[0] == S("StateRowV1") and len(page) == 4
        assert encode(page[3]) == encode(payload), page
        rows.append(page[1:3])
    raise AssertionError("state did not finish within the test bound")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assert args.rounds > 0
    with tempfile.TemporaryDirectory(prefix="native-service-state-") as directory:
        root = Path(directory); (root / "store").mkdir(mode=0o700)
        fixture = ROOT / "tests/support/native_service_state_fixture.metta"
        server = Server(args.worker.resolve(), root, module=fixture, entry="state-server:configuration")
        client = second = None
        try:
            server.start(); client, second = Client(server.path), Client(server.path)
            assert client.route("Create", E("CreateV1", S("sample"), "create"))[0] == S("KnowledgeCommittedV1")
            values = [E("edge", "a", "b"), E("edge", "a", "b"), E("edge", "b", "c"),
                      E("rel", Var(0), Var(0)), E("danger"),
                      E("=", E("inert"), E("println!", S("ACCIDENTAL_EXECUTION"))),
                      E("Error", S("source"), S("datum"))]
            view, _ = client.snapshot()
            seeded = client.route("Commit", E("CommitV1", S("sample"), "seed",
                tuple(E("SpaceInsertV1", i, atom) for i, atom in enumerate(values))), view)
            assert seeded[0] == S("KnowledgeCommittedV1")
            client.drop(view)
            payload = E("payload", E("danger"), E("pair", Var(0), Var(0)))
            token = open_state(client, payload)
            assert use(second, token, "StepState", ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            assert client.route("StepState", (), token, ok=False) == E("ServiceRefusedV1", S("resource-mode"))
            assert use(client, token, "unknown", ok=False) == E("ServiceRefusedV1", S("unknown-operation"))
            assert second.route("Compact")[0] == S("StoreCompacted")
            original = drain(client, token, payload, replay=True)
            assert Counter(encode(row[0]) for row in original) == Counter(encode(value) for value in values)
            assert len({encode(row[1][0]) for row in original}) == len(values)
            await_resources(client, 0)
            # Pure data sharing and callable/error-looking values remain inert.
            for value in [E("pair", Var(0), Var(0)), E("pair", Var(0), Var(1)),
                          E("danger"), E("Error", S("source"), S("datum"))]:
                answer = client.route("PureState", value)
                assert answer[0] == S("ServiceResourceV1"), answer
                answer = use(client, answer[1], "EchoState")
                assert answer[0] == S("ServiceResourceV1") and encode(answer[2]) == encode(value), answer
                client.drop(answer[1])
            view, _ = client.snapshot()
            assert client.route("AliasState", (), view, ok=False) == E("ServiceFailedV1", S("state-already-owned"))
            # Printed handle syntax remains ordinary data. Retaining that
            # syntax cannot turn its reconstructed integer into a capability.
            literal = client.route("ForgeState", (), view)
            assert literal[0] == S("ServiceResourceV1"), literal
            assert use(client, literal[1], "InspectState") == E("StoreRefused", S("closed-view"))
            assert client.route("ClosedState", (), view, ok=False) == E("ServiceFailedV1", S("state-unowned-handle"))
            assert client.route("HeapState", ok=False) == E("ServiceFailedV1", S("state-format"))
            assert client.route("DeepState", ok=False) == E("ServiceFailedV1", S("state-limit"))
            client.drop(view)
            # A failure after invocation consumes the state. It never leaves
            # a resumable token whose hidden cursor may already have advanced.
            token = open_state(client, payload)
            assert use(client, token, "FailState", ok=False) == E("ServiceFailedV1", S("handler-nonsingle-result"))
            assert use(client, token, "StepState", ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            token = open_state(client, payload)
            assert use(client, token, "BadReplyState", "x" * 150000, ok=False) == E("ServiceFailedV1", S("reply-limit"))
            assert use(client, token, "StepState", ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            await_resources(client, 0)
            readonly = Client(server.path)
            try:
                token = open_state(readonly, payload)
                assert readonly.call(E("ReadOnlyV1")) == S("ServiceReadOnlyV1")
                assert use(readonly, token, "Commit", ok=False) == E("ServiceRefusedV1", S("read-only"))
                assert use(readonly, token, "CancelState") == S("StateCancelledV1")
            finally: readonly.close()
            # The same fixed graph is repeatedly completed/cancelled/abandoned.
            samples = []
            for index in range(64 + args.rounds):
                token = open_state(client, payload)
                if index % 2:
                    assert drain(client, token, payload) == original
                else:
                    assert use(client, token, "CancelState") == S("StateCancelledV1")
                if index % 5 == 0:
                    abandoned = Client(server.path)
                    try: open_state(abandoned, payload)
                    finally: abandoned.close()
                    await_resources(client, 0)
                if index == 63 or (index >= 64 and ((index - 63) % 50 == 0 or index == args.rounds + 63)):
                    samples.append(server.sample(client, index - 63))
            assert len({item["descriptors"] for item in samples}) == 1, samples
            assert len({item["stats"][2] for item in samples}) == 1, samples
            token = open_state(client, payload)
            client.close(); client = None; second.close(); second = None
            server.stop(crash=True); server.start(); client = Client(server.path)
            assert use(client, token, "StepState", ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            assert drain(client, open_state(client, payload), payload) == original
            report = {"occurrences": len(values), "post_warmup_rounds": args.rounds, "samples": samples,
                      "exact_request_replay_before_consumed_token_lookup": True,
                      "independent_captured_view_and_compaction": True,
                      "inert_data_and_variable_sharing": True, "failures_retire_state": True,
                      "disconnect_and_restart_cleanup": True}
        finally:
            if client: client.close()
            if second: second.close()
            server.stop()
        limited = Server(args.worker.resolve(), root, module=fixture, entry="state-server:limited-configuration")
        client = None
        try:
            limited.start(); client = Client(limited.path)
            assert client.route("PureState", "x" * 200000, ok=False) == E("ServiceFailedV1", S("resource-budget"))
            assert client.call(E("StatsV1"))[2] == 0
            tokens = [client.route("PureState", E("small", i))[1] for i in range(3)]
            assert client.route("PureState", S("extra"), ok=False) == E("ServiceFailedV1", S("resource-limit"))
            for token in tokens: client.drop(token)
            report["state_resource_and_byte_budgets"] = True
        finally:
            if client: client.close()
            limited.stop()
        if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
