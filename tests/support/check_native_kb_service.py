"""Durable Space operations through a persistent, connection-owned CSP1 service."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, encode, decode
from value_worker_client import Worker


class Client:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(10)
        self.socket.connect(str(path))

    def send(self, body, identifier=None):
        identifier = identifier or uuid.uuid4().hex
        data = encode(E("request", identifier, body))
        self.socket.sendall(b"CSP1" + struct.pack(">I", len(data)) + data)
        return identifier

    def read(self, size):
        result = b""
        while len(result) < size:
            part = self.socket.recv(size - len(result))
            assert part, "disconnected before a complete reply"
            result += part
        return result

    def receive(self, identifier, ok=True):
        header = self.read(8)
        assert header[:4] == b"CSP1", header
        size = struct.unpack(">I", header[4:])[0]
        assert 4 <= size <= 262144, size
        data = self.read(size)
        value = decode(data)
        assert value[:3] == (S("reply"), identifier, S("ok" if ok else "error")), value
        return value[3]

    def call(self, body, identifier=None, ok=True):
        return self.receive(self.send(body, identifier), ok=ok)

    def route(self, operation, argument=(), token=None, **kwargs):
        body = E("CallV1", S(operation), argument) if token is None else E("UseV1", token, S(operation), argument)
        return self.call(body, **kwargs)

    def snapshot(self):
        value = self.route("Snapshot")
        assert value[0] == S("ServiceResourceV1"), value
        return value[1], value[2]

    def query(self, view, pattern, template):
        value = self.route("Query", E("QueryV1", S("sample"), pattern, template), view)
        assert value[0] == S("ServiceResourceV1"), value
        return value[1]

    def drain(self, cursor):
        rows, sequence, work = [], 0, 0
        while True:
            answer = self.route("Next", E("NextV1", sequence), cursor)
            work += 1
            assert work < 10000, "bounded test failed to finish"
            if answer == S("StoreQueryPaused"):
                continue
            if answer == S("StoreQueryEnd"):
                break
            assert answer[0:2] == (S("StoreQueryRow"), sequence), answer
            # A second transport request must replay the same cursor position.
            assert self.route("Next", E("NextV1", sequence), cursor) == answer
            rows.append(answer[2:])
            sequence += 1
        return rows

    def drop(self, token):
        assert self.call(E("DropV1", token)) == S("ServiceClosedV1")

    def close(self):
        self.socket.close()


class Server:
    def __init__(self, worker, directory, *, module=None, entry="kb-server:configuration"):
        self.directory = directory
        self.path = directory / "service.sock"
        self.command = [str(worker), "--serve", "--lang", "petta",
                        str(module or ROOT / "examples/native_kb_server.metta"), entry]
        self.environment = dict(os.environ, CETTA_KB_DIRECTORY=str(directory / "store"), CETTA_KB_SOCKET=str(self.path))
        self.logs = []
        self.process = None

    def start(self):
        self.process = subprocess.Popen(self.command, env=self.environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise AssertionError(self.process.communicate())
            try:
                client = Client(self.path)
                client.close()
                return
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(.01)
        raise AssertionError("service startup timeout")

    def stop(self, crash=False):
        if not self.process:
            return
        if self.process.poll() is None:
            self.process.kill() if crash else self.process.terminate()
        out, err = self.process.communicate(timeout=10)
        self.logs.append((out, err))
        assert self.process.returncode == (-9 if crash else 0), (self.process.returncode, out, err)
        assert b"ACCIDENTAL_EXECUTION" not in out, out
        self.process = None

    def sample(self, client, round_number):
        stats = client.call(E("StatsV1"))
        process = Path("/proc") / str(self.process.pid)
        status = dict(line.split(":", 1) for line in (process / "status").read_text().splitlines())
        return {"round": round_number, "stats": stats[1:],
                "descriptors": len(list((process / "fd").iterdir())),
                "rss_kib": int(status["VmRSS"].split()[0])}


def await_resources(client, count):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        stats = client.call(E("StatsV1"))
        if stats[2] == count:
            return
        time.sleep(.005)
    raise AssertionError((count, stats))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--rounds", type=int, default=250)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assert args.rounds > 0
    with tempfile.TemporaryDirectory(prefix="native-kb-service-") as temporary:
        root = Path(temporary)
        (root / "store").mkdir(mode=0o700)
        server = Server(args.worker.resolve(), root)
        first = second = None
        try:
            server.start()
            first, second = Client(server.path), Client(server.path)
            created = first.route("Create", E("CreateV1", S("sample"), "create"))
            assert created[0] == S("KnowledgeCommittedV1"), created
            view, metadata = first.snapshot()
            changes = tuple(E("SpaceInsertV1", index, atom) for index, atom in enumerate([
                E("edge", S("alice"), S("bob")), E("edge", S("alice"), S("bob")),
                E("edge", S("bob"), S("carol")), E("rel", Var(0), Var(0)),
                E("=", E("inert-rule"), E("println!", S("ACCIDENTAL_EXECUTION"))) ]))
            command = E("CommitV1", S("sample"), "seed", changes)
            commit = first.route("Commit", command, view, identifier="seed-wire")
            assert commit[0] == S("KnowledgeCommittedV1"), commit
            # Exact transport retry is byte-identical; changed input under its
            # last id is refused without rerunning a handler.
            assert first.route("Commit", command, view, identifier="seed-wire") == commit
            assert first.call(E("StatsV1"), "seed-wire", ok=False) == E("ServiceRefusedV1", S("request-id-collision"))
            assert second.route("Query", E("QueryV1", S("sample"), Var(0), Var(0)), view, ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            old_cursor = first.query(view, Var(0), Var(0))
            assert first.drain(old_cursor) == []
            first.drop(old_cursor)
            stale = first.route("Commit", E("CommitV1", S("sample"), "stale", (E("SpaceInsertV1", 0, E("wrong")),)), view)
            assert stale == E("StoreRefused", S("conflict")), stale
            first.drop(view)
            view, _ = first.snapshot()
            cursor = first.query(view, Var(0), Var(0))
            rows = first.drain(cursor)
            assert len(rows) == 5 and len({encode(row[1][0]) for row in rows}) == 5, rows
            assert Counter(encode(row[0]) for row in rows) == Counter(encode(change[2]) for change in changes)
            first.drop(cursor)
            # A captured view survives publication, compaction and collection.
            compact = second.route("Compact")
            assert compact[0] == S("StoreCompacted"), compact
            cursor = first.query(view, Var(0), Var(0))
            assert first.drain(cursor) == rows
            first.drop(cursor)
            first.drop(view)
            # Dropping the connection releases both an unconsumed query and
            # its informing view. Tokens cannot be reused on another client.
            abandoned, _ = second.snapshot()
            second.query(abandoned, Var(0), Var(0))
            second.close(); second = None
            await_resources(first, 0)
            assert first.route("Next", E("NextV1", 0), abandoned, ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            readonly = Client(server.path)
            assert readonly.call(E("ReadOnlyV1")) == S("ServiceReadOnlyV1")
            assert readonly.route("Create", E("CreateV1", S("forbidden"), "forbidden"), ok=False) == E("ServiceRefusedV1", S("read-only"))
            assert readonly.call(E("ReadOnlyV1")) == S("ServiceReadOnlyV1")
            readonly.close()
            # The native MeTTa client uses the very same framing and socket;
            # its protected connection owns query lifetimes across calls.
            worker = Worker(args.worker.resolve(), ROOT / "tests/support/native_kb_client_fixture.metta",
                            "kb-client:run", language="petta", profile=True)
            try:
                assert worker.call(E("Guards", str(server.path))) == S("ClientGuardsPassed")
                for _ in range(10):
                    answer = worker.call(E("Inspect", str(server.path)))
                    assert answer[0] == S("ClientRows") and len(answer[1]) == 2, answer
                    assert all(row[0:2] == (S("Row"), S("bob")) for row in answer[1]), answer
                    assert worker.call(E("Abandon", str(server.path))) == S("ClientAbandoned")
                    assert worker.call(S("Echo")) == S("Echo")
                    await_resources(first, 0)
                for index, response in enumerate([None,
                        encode(E("reply", "wrong-id", S("ok"), S("value"))),
                        encode(E("reply", "probe", S("invalid-status"), S("value"))),
                        b"not-CAV1"]):
                    mock_path = root / "mock.sock"
                    if mock_path.exists(): mock_path.unlink()
                    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    listener.bind(str(mock_path)); listener.listen(1); listener.settimeout(10)
                    failures = []
                    def mock_reply():
                        try:
                            peer, _ = listener.accept()
                            with peer:
                                peer.settimeout(5)
                                def read(size):
                                    data = b""
                                    while len(data) < size:
                                        part = peer.recv(size - len(data)); assert part
                                        data += part
                                    return data
                                header = read(8)
                                request = decode(read(struct.unpack(">I", header[4:])[0]))
                                assert request[:2] == (S("request"), "probe"), request
                                if response is not None:
                                    peer.sendall(b"CSP1" + struct.pack(">I", len(response)) + response)
                        except Exception as error:
                            failures.append(error)
                    thread = threading.Thread(target=mock_reply)
                    thread.start()
                    try:
                        answer = worker.call(E("FailedExchange", str(mock_path)))
                    finally:
                        thread.join(timeout=10); listener.close()
                    assert not thread.is_alive() and not failures, failures
                    assert answer[0] == S("ClientFailedExchange"), answer
                    assert answer[1][0:2] == (S("space:uncertain"), "probe"), answer
                    assert answer[2] == E("space:unavailable", "after", S("closed-or-busy-connection")), answer
            finally:
                worker.close()
            samples = []
            for index in range(64 + args.rounds):
                view, _ = first.snapshot()
                cursor = first.query(view, E("edge", S("alice"), Var(0)), Var(0))
                if index % 2:
                    assert len(first.drain(cursor)) == 2
                else:
                    assert first.route("Cancel", (), cursor) == S("StoreQueryCancelled")
                    assert first.route("Next", E("NextV1", 0), cursor) == S("StoreQueryCancelled")
                first.drop(cursor); first.drop(view)
                if index == 63 or (index >= 64 and ((index - 63) % 50 == 0 or index == args.rounds + 63)):
                    samples.append(server.sample(first, index - 63))
            assert len({sample["descriptors"] for sample in samples}) == 1, samples
            assert len({sample["stats"][2] for sample in samples}) == 1, samples
            # Malformed/oversized frames lose only their own connection.
            for data in [b"BAD!\0\0\0\x08", b"CSP1\0\x10\0\x01", b"CSP1\0\0\0\x04nope"]:
                bad = Client(server.path); bad.socket.sendall(data)
                assert bad.socket.recv(8) == b""
                bad.close()
                assert first.call(E("StatsV1"))[0] == S("ServiceStatsV1")
            # Losing the reply does not lose the durable receipt. A half-close
            # ensures a complete request was sent; the witness waits for commit
            # publication through an independent snapshot before crashing.
            second = Client(server.path)
            lost_view, lost_metadata = second.snapshot()
            lost_changes = (E("SpaceInsertV1", 0, E("edge", S("carol"), S("dave"))),)
            second.send(E("UseV1", lost_view, S("Commit"), E("CommitV1", S("sample"), "lost-reply", lost_changes)))
            second.socket.shutdown(socket.SHUT_WR)
            deadline = time.monotonic() + 5
            while True:
                current, meta = first.snapshot(); first.drop(current)
                if meta[2] > lost_metadata[2]:
                    break
                assert time.monotonic() < deadline
                time.sleep(.005)
            second.close(); second = None
            first.close(); first = None
            server.stop(crash=True)
            assert server.path.exists(), "SIGKILL should leave the stale socket witness"
            server.start()
            first = Client(server.path)
            view, _ = first.snapshot()
            cursor = first.query(view, Var(0), Var(0))
            recovered = first.drain(cursor)
            assert len(recovered) == 6, recovered
            assert set(encode(row) for row in rows).issubset(set(encode(row) for row in recovered)), recovered
            resolved = first.route("Resolve", E("ResolveV1", "lost-reply", E("SpaceCommitV1", S("sample"), lost_changes)))
            assert resolved[0] == S("KnowledgeResolvedV1"), resolved
            assert first.route("Next", E("NextV1", 0), lost_view, ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            first.drop(cursor); first.drop(view)
            # Duplicate owner startup must not remove the active socket.
            duplicate = subprocess.run(server.command, env=server.environment, capture_output=True, timeout=10)
            assert duplicate.returncode != 0 and server.path.exists(), duplicate
            assert first.call(E("StatsV1"))[0] == S("ServiceStatsV1")
            report = {"post_warmup_rounds": args.rounds, "samples": samples,
                      "recovered_occurrences": len(recovered), "crash_restart": True,
                      "scope": "same-UID local service; captured views, ordinary Space matching, connection-owned resources and durable receipts"}
        finally:
            if first: first.close()
            if second: second.close()
            server.stop()
        limited = Server(args.worker.resolve(), root, module=ROOT / "tests/support/native_service_limits_fixture.metta",
                         entry="service-limits:configuration")
        client = idle = extra = None
        try:
            limited.start()
            client = Client(limited.path)
            tokens = [client.snapshot()[0] for _ in range(3)]
            assert client.route("Snapshot", ok=False) == E("ServiceFailedV1", S("resource-limit"))
            assert client.call(E("StatsV1"))[2] == 3
            for token in tokens: client.drop(token)
            idle = Client(limited.path)
            abandoned, _ = idle.snapshot()
            extra = Client(limited.path)
            try:
                extra.send(E("StatsV1"))
                assert extra.socket.recv(8) == b"", "connection quota not enforced"
            except (BrokenPipeError, ConnectionResetError):
                pass  # Accept-and-close can precede the client's first send.
            extra.close(); extra = None
            await_resources(client, 0)
            assert idle.socket.recv(8) == b"", "idle owner not disconnected"
            idle.close(); idle = None
            partial = Client(limited.path)
            partial.socket.sendall(b"CSP1\0")
            deadline = time.monotonic() + 2
            # Another client remains serviceable while a frame is unfinished.
            while time.monotonic() < deadline:
                assert client.call(E("StatsV1"))[0] == S("ServiceStatsV1")
                partial.socket.settimeout(.025)
                try:
                    assert partial.socket.recv(8) == b""
                    break
                except socket.timeout:
                    pass
            else:
                raise AssertionError("unfinished frame bypassed idle deadline")
            partial.close()
        finally:
            if extra: extra.close()
            if idle: idle.close()
            if client: client.close()
            limited.stop()
        limited = Server(args.worker.resolve(), root, module=ROOT / "tests/support/native_service_limits_fixture.metta",
                         entry="service-limits:budget")
        client = None
        try:
            limited.start(); client = Client(limited.path)
            assert client.route("Snapshot", ok=False) == E("ServiceFailedV1", S("resource-budget"))
            assert client.call(E("StatsV1"))[2] == 0
        finally:
            if client: client.close()
            limited.stop()
        limited = Server(args.worker.resolve(), root, module=ROOT / "tests/support/native_service_limits_fixture.metta",
                         entry="service-limits:failure")
        client = None
        try:
            limited.start(); client = Client(limited.path)
            assert client.route("Exhaust", ok=False) == E("ServiceFailedV1", S("handler-fuel"))
            for operation in ["Multiple", "Empty"]:
                assert client.route(operation, ok=False) == E("ServiceFailedV1", S("handler-nonsingle-result"))
            assert client.call(E("StatsV1"))[0] == S("ServiceStatsV1")
        finally:
            if client: client.close()
            limited.stop()
        report["bounded_connections_resources_idle_and_partial_frames"] = True
        if args.output:
            args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
