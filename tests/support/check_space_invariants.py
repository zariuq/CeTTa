"""Ordinary captured Space invariant policies versus a SQLite transition oracle."""
import argparse
from collections import Counter
import json
from pathlib import Path
import random
import socket
import sqlite3
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, encode, decode
from value_worker_client import Worker
from check_native_kb_service import Server, Client
from check_persistent_query_worker import sample


def outcome(value):
    if value[0] == S("KnowledgeCommittedV1"):
        return "committed"
    if value[0] == S("KnowledgeReplayedV1"):
        return "replayed"
    assert value[0] in [S("InvariantRefusedV1"), S("KnowledgeRefusedV1"), S("StoreRefused")], value
    return value[1].name


class Oracle:
    def __init__(self):
        self.db = sqlite3.connect(":memory:")
        self.db.executescript("""
            PRAGMA foreign_keys=ON;
            CREATE TABLE programs (name BLOB PRIMARY KEY);
            CREATE TABLE dependencies (source BLOB REFERENCES programs(name),
                                       target BLOB REFERENCES programs(name));
            CREATE TABLE jobs (id BLOB PRIMARY KEY, payload BLOB NOT NULL,
                               worker BLOB);
            CREATE TABLE receipts (id TEXT PRIMARY KEY, command BLOB NOT NULL);
        """)

    def apply(self, request, command):
        before = self.db.execute("SELECT command FROM receipts WHERE id=?", (request,)).fetchone()
        if before:
            return "replayed" if before[0] == encode(command) else "receipt-collision"
        kind, *args = command
        with self.db:
            if kind == S("AddProgramV1"):
                if self.db.execute("SELECT 1 FROM programs WHERE name=?", (encode(args[0]),)).fetchone():
                    return "duplicate-program"
                self.db.execute("INSERT INTO programs VALUES (?)", (encode(args[0]),))
            elif kind == S("AddDependencyV1"):
                source, target = map(encode, args)
                if not all(self.db.execute("SELECT 1 FROM programs WHERE name=?", (name,)).fetchone()
                           for name in [source, target]):
                    return "missing-program"
                self.db.execute("INSERT INTO dependencies VALUES (?,?)", (source, target))
            elif kind == S("RemoveProgramV1"):
                name = encode(args[0])
                if not self.db.execute("SELECT 1 FROM programs WHERE name=?", (name,)).fetchone():
                    return "missing-program"
                if self.db.execute("SELECT 1 FROM dependencies WHERE source=? OR target=?", (name, name)).fetchone():
                    return "referenced-program"
                self.db.execute("DELETE FROM programs WHERE name=?", (name,))
            elif kind == S("EnqueueV1"):
                job, payload = map(encode, args)
                if self.db.execute("SELECT 1 FROM jobs WHERE id=?", (job,)).fetchone():
                    return "duplicate-job"
                self.db.execute("INSERT INTO jobs VALUES (?,?,NULL)", (job, payload))
            elif kind == S("ClaimV1"):
                job, worker = map(encode, args)
                result = self.db.execute("UPDATE jobs SET worker=? WHERE id=? AND worker IS NULL", (worker, job))
                if result.rowcount != 1:
                    return "not-queued"
            else:
                return "command"
            self.db.execute("INSERT INTO receipts VALUES (?,?)", (request, encode(command)))
        return "committed"

    def rows(self):
        rows = [E("ProgramV1", decode(name)) for name, in self.db.execute("SELECT name FROM programs")]
        rows += [E("DependsV1", decode(source), decode(target))
                 for source, target in self.db.execute("SELECT source,target FROM dependencies")]
        rows += [E("JobV1", decode(job), S("QueuedV1") if worker is None else E("ClaimedV1", decode(worker)), decode(payload))
                 for job, payload, worker in self.db.execute("SELECT id,payload,worker FROM jobs")]
        return rows


def bag(rows):
    return Counter(encode(row) for row in rows)


def rows(client):
    view, _ = client.snapshot()
    query = client.query(view, Var(0), Var(0))
    try:
        return client.drain(query)
    finally:
        client.drop(query); client.drop(view)


def apply(client, request, command, view=None):
    if view is not None:
        return client.route("Apply", E("InvariantV1", S("sample"), request, command), view)
    view, _ = client.snapshot()
    try:
        return apply(client, request, command, view)
    finally:
        client.drop(view)


def race(first, second, commands):
    captured = [client.snapshot() for client in [first, second]]
    assert captured[0][1] == captured[1][1]
    barrier = threading.Barrier(2)
    results, errors = [None, None], []
    def execute(index, client):
        try:
            barrier.wait(timeout=5)
            request, command = commands[index]
            results[index] = apply(client, request, command, captured[index][0])
        except Exception as error:
            errors.append(error)
    threads = [threading.Thread(target=execute, args=(i, client)) for i, client in enumerate([first, second])]
    for thread in threads: thread.start()
    for thread in threads: thread.join(timeout=10)
    assert not errors and not any(thread.is_alive() for thread in threads), errors
    for client, (view, _) in zip([first, second], captured): client.drop(view)
    assert sorted(map(outcome, results)) == ["committed", "conflict"], results
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--steps", type=int, default=120)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--rounds", type=int, default=100)
    args = parser.parse_args()
    assert args.steps > 0 and args.rounds > 0
    fixture = ROOT / "tests/support/space_invariants_fixture.metta"
    with tempfile.TemporaryDirectory(prefix="space-invariants-") as directory:
        root = Path(directory)
        (root / "store").mkdir(mode=0o700)
        server = Server(args.worker.resolve(), root, module=fixture, entry="invariant-server:configuration")
        first = second = lost = embedded = None
        oracle = Oracle()
        checks = 0
        try:
            server.start(); first, second = Client(server.path), Client(server.path)
            specs = (E("SpaceIndexV1", 2, 1), E("SpaceIndexV1", 3, 1),
                     E("SpaceIndexV1", 3, 2), E("SpaceIndexV1", 4, 1))
            assert first.route("Create", E("CreateIndexedV1", S("sample"), "create", specs))[0] == S("KnowledgeCommittedV1")
            # This service offers the trusted policy, not arbitrary fact edits.
            assert first.route("Commit", (), ok=False) == E("ServiceRefusedV1", S("unknown-operation"))
            def compare(request, command, client=None):
                nonlocal checks
                expected = oracle.apply(request, command)
                actual = apply(client or first, request, command)
                assert outcome(actual) == expected, (request, command, expected, actual)
                assert bag(row[0] for row in rows(first)) == bag(oracle.rows())
                checks += 1
                return actual
            scripted = [E("AddProgramV1", "a"), E("AddProgramV1", "a"),
                        E("AddDependencyV1", "a", "b"), E("AddProgramV1", "b"),
                        E("AddDependencyV1", "a", "b"), E("AddDependencyV1", "a", "b"),
                        E("RemoveProgramV1", "a"), E("RemoveProgramV1", "b"),
                        E("AddProgramV1", "isolated"), E("RemoveProgramV1", "isolated"),
                        E("AddProgramV1", E("danger")), E("EnqueueV1", "inert", E("danger")),
                        E("ClaimV1", "inert", "worker-a"),
                        E("EnqueueV1", "error-data", E("Error", S("source"), S("datum"))),
                        E("ClaimV1", "error-data", "worker-a"),
                        E("EnqueueV1", "shared-data", E("pair", Var(0), Var(0))),
                        E("ClaimV1", "shared-data", "worker-a")]
            for index, command in enumerate(scripted): compare(f"script-{index}", command)
            original = compare("retry-enqueue", E("EnqueueV1", "retry-job", E("payload", "saved")))
            compare("retry-claim", E("ClaimV1", "retry-job", "worker"))
            replay = compare("retry-enqueue", E("EnqueueV1", "retry-job", E("payload", "saved")))
            assert replay[1] == original[1], (original, replay)
            compare("retry-claim", E("ClaimV1", "retry-job", "worker"))
            compare("retry-claim", E("ClaimV1", "retry-job", "other-worker"))
            rng = random.Random(91823)
            for index in range(args.steps):
                a, b = rng.choice(["a", "b", "c", "d", "e"]), rng.choice(["a", "b", "c", "d", "e"])
                job = f"generated-job-{rng.randrange(12)}"
                commands = [E("AddProgramV1", a), E("RemoveProgramV1", a),
                            E("AddDependencyV1", a, b), E("EnqueueV1", job, E("payload", a, b)),
                            E("ClaimV1", job, f"worker-{rng.randrange(3)}")]
                compare(f"generated-{index}", rng.choice(commands))
            for label, commands in [
                ("unique", [("unique-0", E("EnqueueV1", "racing-job", S("value"))),
                            ("unique-1", E("EnqueueV1", "racing-job", S("value")))]),
                ("claim", [("claim-0", E("ClaimV1", "racing-job", "left")),
                           ("claim-1", E("ClaimV1", "racing-job", "right"))])]:
                results = race(first, second, commands)
                winner = next(i for i, value in enumerate(results) if outcome(value) == "committed")
                assert oracle.apply(*commands[winner]) == "committed"
                loser = 1 - winner
                compare(*commands[loser], client=second)
            # A negative dependency check from an older view cannot justify
            # deleting a program after another writer adds an incoming edge.
            compare("phantom-a", E("AddProgramV1", "phantom-a"))
            compare("phantom-b", E("AddProgramV1", "phantom-b"))
            stale, _ = first.snapshot()
            compare("phantom-edge", E("AddDependencyV1", "phantom-a", "phantom-b"), client=second)
            assert outcome(apply(first, "phantom-delete", E("RemoveProgramV1", "phantom-b"), stale)) == "conflict"
            first.drop(stale)
            compare("phantom-delete", E("RemoveProgramV1", "phantom-b"))
            # Embedded programs invoke exactly the same policy and probe. A
            # budgeted unfinished search must not be interpreted as absence.
            embedded = Worker(args.worker.resolve(), fixture, "invariants-fixture:run", language="petta", profile=True)
            assert embedded.call(E("Probe", str(root / "store"), E("ProgramV1", "a"), (), 0)) == S("StoreQueryPaused")
            assert embedded.call(E("Probe", str(root / "store"), E("ProgramV1", "a"), (), 4096))[0] == S("SpaceProbeFoundV1")
            assert embedded.call(E("Probe", str(root / "store"), E("ProgramV1", "never-seen"), (), 4096)) == S("SpaceProbeAbsentV1")
            command = E("EnqueueV1", "embedded", E("payload", "embedded"))
            assert outcome(embedded.call(E("Apply", str(root / "store"), "embedded", command))) == oracle.apply("embedded", command)
            assert bag(row[0] for row in rows(first)) == bag(oracle.rows())
            # An unbound identity is refused; callable-looking command data is
            # neither executed nor accepted as a policy operation.
            assert outcome(apply(first, "bad-id", E("AddProgramV1", Var(0)))) == "program-identity"
            assert outcome(apply(first, "bad-command", E("danger"))) == "command"
            samples = []
            for index in range(64 + args.rounds):
                assert embedded.call(E("Probe", str(root / "store"), E("ProgramV1", "a"), (), 0)) == S("StoreQueryPaused")
                assert embedded.call(E("Probe", str(root / "store"), E("ProgramV1", "a"), (), 4096))[0] == S("SpaceProbeFoundV1")
                assert embedded.call(E("Probe", str(root / "store"), E("ProgramV1", "never-seen"), (), 4096)) == S("SpaceProbeAbsentV1")
                assert embedded.call(S("Echo")) == S("Echo")
                if index == 63 or (index >= 64 and ((index - 63) % 50 == 0 or index == args.rounds + 63)):
                    samples.append(sample(embedded, index - 63))
            assert len({item["descriptors"] for item in samples}) == 1, samples
            assert len({item["metrics"]["permanent_bytes"] for item in samples}) == 1, samples
            # Save the occurrence identity, then lose a claim's transport reply.
            compare("lost-enqueue", E("EnqueueV1", "lost-job", E("payload", "original")))
            original_id = next(row[1] for row in rows(first) if row[0][0] == S("JobV1") and row[0][1] == "lost-job")
            lost = Client(server.path)
            view, before = lost.snapshot()
            command = E("ClaimV1", "lost-job", "lost-worker")
            lost.send(E("UseV1", view, S("Apply"), E("InvariantV1", S("sample"), "lost-claim", command)))
            lost.socket.shutdown(socket.SHUT_WR)
            deadline = time.monotonic() + 5
            while True:
                current, metadata = first.snapshot(); first.drop(current)
                if metadata[2] > before[2]: break
                assert time.monotonic() < deadline
                time.sleep(.005)
            assert oracle.apply("lost-claim", command) == "committed"
            lost.close(); lost = None
            first.close(); first = None
            second.close(); second = None
            server.stop(crash=True); server.start(); first = Client(server.path)
            recovered = rows(first)
            assert bag(row[0] for row in recovered) == bag(oracle.rows())
            assert next(row[1] for row in recovered if row[0][0] == S("JobV1") and row[0][1] == "lost-job") == original_id
            compare("lost-claim", command)
            resolved = first.route("Resolve", E("ResolveV1", "lost-claim", E("SpaceInvariantCommandV1", S("sample"), command)))
            assert resolved[0] == S("KnowledgeResolvedV1"), resolved
            report = {"sqlite_transition_comparisons": checks, "seed": 91823,
                      "generated_steps": args.steps, "competing_unique_and_claim_checks": True,
                      "absent_reference_phantom_conflict": True, "bounded_probe_preserves_incomplete": True,
                      "same_policy_embedded_and_service": True, "lost_claim_receipt_survives_restart": True,
                      "probe_post_warmup_rounds": args.rounds, "probe_lifetime_samples": samples,
                      "scope": "example MeTTa policies; string/constructor identities; no live gateway replacement or external action replay"}
            if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2))
        finally:
            if embedded: embedded.close()
            if lost: lost.close()
            if first: first.close()
            if second: second.close()
            server.stop()
            oracle.db.close()


if __name__ == "__main__":
    main()
