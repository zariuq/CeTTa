#!/usr/bin/env python3
"""Cross-process storage conformance, using an independent CSP1 codec."""
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction
import grp
import hashlib
import importlib.util
import json
from pathlib import Path
import os
import pwd
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("space_service_client", ROOT / "tools/space_service_client.py")
csp = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = csp
spec.loader.exec_module(csp)
Client, S, E, Var = csp.Client, csp.S, csp.E, csp.Var
SERVICE = str(Path(sys.argv[1]).resolve())
CETTA = str(Path(sys.argv[2]).resolve()) if len(sys.argv) > 2 else None
WORKER = str(Path(sys.argv[3]).resolve()) if len(sys.argv) > 3 else None
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker, WorkerUnavailable
TRACE = []
BACKENDS = tuple(os.environ.get("CETTA_SPACE_TEST_BACKENDS", "native,pathmap,mork").split(","))
if not BACKENDS or any(x not in ("native", "native-ordered", "pathmap", "mork") for x in BACKENDS):
    raise ValueError("invalid test backend selection")


class Fixture:
    def __init__(self, shared=False, **options):
        self.temp = tempfile.TemporaryDirectory(prefix="cetta-space-")
        self.root = Path(self.temp.name)
        self.socket = str(self.root / "s.sock")
        self.shared_socket = str(self.root / "shared.sock") if shared else None
        self.data = self.root / "data"
        self.options = options
        self.process = None
        self.error = self.root / "stderr"
        self.start()

    def start(self, success=True):
        self.log = self.error.open("ab")
        arguments = [SERVICE, "--socket", self.socket, "--data", str(self.data)]
        if self.shared_socket:
            arguments += ["--shared-socket", self.shared_socket,
                          "--shared-group", grp.getgrgid(os.getgid()).gr_name]
        for name, value in self.options.items():
            arguments += ["--" + name.replace("_", "-"), str(value)]
        self.process = subprocess.Popen(arguments, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=self.log)
        until = time.monotonic() + 20
        while time.monotonic() < until:
            if self.process.poll() is not None:
                if not success:
                    self.log.close()
                    return
                raise AssertionError(self.error.read_text())
            if Path(self.socket).exists():
                try:
                    with Client(self.socket) as client:
                        client.hello()
                    if success:
                        return
                except (OSError, csp.Uncertain):
                    pass
            time.sleep(.01)
        raise AssertionError("service startup timed out: " + self.error.read_text())

    def stop(self, crash=False):
        if not self.process:
            return
        if self.process and self.process.poll() is None:
            self.process.kill() if crash else self.process.terminate()
            self.process.wait(timeout=20)
        self.log.close()
        if not crash and self.process.returncode:
            raise AssertionError(self.error.read_text())

    def restart(self, crash=False):
        self.stop(crash)
        self.start()

    def close(self):
        self.stop()
        self.temp.cleanup()


class ServiceTests(unittest.TestCase):
    def test_idle_shutdown_does_not_wait_for_the_poll_deadline(self):
        # Close the last connection, then stop while the service is preparing
        # or entering its idle wait, reopening the same durable directory.
        self.client.close()
        for _ in range(12):
            self.fixture.process.terminate()
            self.fixture.process.wait(timeout=2)
            self.fixture.stop()
            self.fixture.start()

    def setUp(self):
        self.fixture = Fixture()
        self.addCleanup(self.fixture.close)
        self.client = Client(self.fixture.socket)
        self.addCleanup(self.client.close)

    def space(self, backend="native"):
        return self.client.open("test-" + backend, backend)

    def test_f32_capability_and_durable_bits(self):
        from array import array
        self.assertIn(S("tensor-f32-native"), self.client.hello()[5])
        compact = csp.Tensor.from_buffer(array("f", [1., -0., .125]))
        space = self.space()
        space.add(E("Compact", compact))
        self.assertEqual(space.match(E("Compact", Var(0)), Var(0)), [compact])
        self.fixture.restart(crash=True)
        self.client.close()
        self.client = Client(self.fixture.socket)
        self.addCleanup(self.client.close)
        space = self.space()
        self.assertEqual(space.match(E("Compact", Var(0)), Var(0)), [compact])

    def denied(self, reason, function, *args, **kwargs):
        with self.assertRaises(csp.ServiceError) as caught:
            function(*args, **kwargs)
        self.assertEqual(caught.exception.reason, S(reason))

    def test_backend_bag_or_set_and_remove_one(self):
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                space = self.space(backend)
                space.batch(E("add", E("p", 7)), E("add", E("p", 7)))
                expected = 1 if backend == "mork" else 2
                self.assertEqual(space.describe()[7], expected)
                self.assertEqual(space.match(E("p", Var(0)), Var(0)), [7] * expected)
                self.assertEqual(space.match_count(E("p", Var(0))), expected)
                space.remove(E("p", 7))
                self.assertEqual(space.match_count(E("p", Var(0))), expected - 1)
                TRACE.append({"kind": "multiplicity", "backend": backend, "initial": expected,
                              "remaining": expected-1, "revision": space.revision})

    def test_ordered_native_bag_retains_survivor_order_through_recovery(self):
        self.assertIn(S("native-ordered"), self.client.hello()[5])
        space = self.client.open("ordered", "native-ordered")
        space.batch(*(E("add", E("row", n)) for n in (1, 2, 3, 2, 4)))
        old = space.query(E("row", Var(0)), Var(0))
        space.batch(E("add", E("row", 5)), E("remove", E("row", 3)), E("remove", E("row", 2)))
        self.assertEqual(space.match(E("row", Var(0)), Var(0)), [1, 2, 4, 5])
        self.assertEqual(space.fetch(old)[3][1:], (1, 2, 3, 2, 4))
        space.close_query(old)
        self.denied("backend-mismatch", self.client.open, "ordered", "native")
        self.client.close()
        self.fixture.restart(crash=True)
        self.client = Client(self.fixture.socket)
        self.addCleanup(self.client.close)
        space = self.client.open("ordered", "native-ordered")
        self.assertEqual(space.match(E("row", Var(0)), Var(0)), [1, 2, 4, 5])
        self.client.call("compact", space.uuid)
        self.client.close()
        self.fixture.restart(crash=True)
        self.client = Client(self.fixture.socket)
        self.addCleanup(self.client.close)
        space = self.client.open("ordered", "native-ordered")
        space.remove(E("row", 1))
        self.assertEqual(space.match(E("row", Var(0)), Var(0)), [2, 4, 5])

    def test_scoped_independent_channels_do_not_conflict(self):
        space = self.client.open("channels", "native-ordered")
        a, b = E("Message", "a", Var(0)), E("Message", "b", Var(0))
        oa, ob = space.observe((a,), (a,)), space.observe((b,), (b,))
        self.assertEqual(space.observe_fetch(oa, 0)[3], E("rows"))
        first = space.batch_observed(oa, E("add", E("Message", "a", 1)), request_id="scope-a")
        space.batch_observed(ob, E("add", E("Message", "b", 2)), request_id="scope-b")
        self.assertEqual(space.match(E("Message", Var(0), Var(1)), E("pair", Var(0), Var(1))),
                         [E("pair", "a", 1), E("pair", "b", 2)])
        self.assertEqual(space.batch_observed(oa, E("add", E("Message", "a", 1)), request_id="scope-a"), first)
        self.denied("expired-or-closed", space.observe_fetch, oa, 0)

    def test_scoped_eight_simultaneous_channels_all_commit(self):
        space = self.client.open("parallel-channels", "native-ordered")
        barrier = threading.Barrier(8)
        def publish(number):
            with Client(self.fixture.socket) as peer:
                channel = peer.open(space.name, "native-ordered")
                pattern = E("Message", number, Var(0))
                observed = channel.observe((pattern,), (pattern,))
                barrier.wait(timeout=10)
                return channel.batch_observed(observed, E("add", E("Message", number, "data")))
        with ThreadPoolExecutor(max_workers=8) as pool:
            answers = list(pool.map(publish, range(8)))
        self.assertTrue(all(answer[0] == S("committed") for answer in answers))
        self.assertEqual(space.match_count(E("Message", Var(0), Var(1))), 8)

    def test_scoped_eight_spenders_never_overspend_or_lose_a_receipt(self):
        space = self.client.open("spenders", "native-ordered")
        space.add(E("Budget", 4))
        barrier = threading.Barrier(8)
        def spend(number):
            with Client(self.fixture.socket) as peer:
                ledger = peer.open(space.name, "native-ordered")
                for attempt in range(9):  # At most eight competing successful mutations.
                    budget = E("Budget", Var(0))
                    receipt = E("Spent", number)
                    observed = ledger.observe((budget,), (budget, receipt))
                    rows = ledger.observe_fetch(observed, 0)[3][1:]
                    self.assertEqual(len(rows), 1)
                    balance = rows[0][1]
                    if attempt == 0:
                        barrier.wait(timeout=10)
                    if balance == 0:
                        ledger.observe_close(observed)
                        return False
                    try:
                        ledger.batch_observed(observed, E("remove", E("Budget", balance)),
                            E("add", E("Budget", balance-1)), E("add", receipt))
                        return True
                    except csp.ServiceError as failure:
                        self.assertEqual(failure.reason, S("scope-conflict"))
                self.fail("contention did not settle after every possible competing spend")
        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(spend, range(8)))
        self.assertEqual(sum(results), 4)
        self.assertEqual(space.match(E("Budget", Var(0)), Var(0)), [0])
        receipts = space.match(E("Spent", Var(0)), Var(0))
        self.assertEqual(len(receipts), 4)
        self.assertEqual(len(set(receipts)), 4)

    def test_scoped_competing_claim_and_negative_dependency(self):
        space = self.client.open("claims", "native-ordered")
        item = E("Pending", 7)
        space.add(item)
        first, second = space.observe((item,), (item,)), space.observe((item,), (item,))
        space.batch_observed(first, E("remove", item))
        self.denied("scope-conflict", space.batch_observed, second, E("remove", item))
        self.assertEqual(space.match_count(item), 0)
        absent = E("Missing", Var(0))
        negative = space.observe((absent,), (item,))
        space.add(E("Missing", 1))
        self.denied("scope-conflict", space.batch_observed, negative, E("add", item))
        self.assertEqual(space.match_count(item), 0)

    def test_scoped_views_keep_full_records_not_lossy_projections(self):
        space = self.client.open("scope-records", "native-ordered")
        original = E("Fact", Var(7))
        space.add(original)
        pattern = E("Fact", 1)
        observation = space.observe((pattern,), (E("Changed", Var(0)),))
        self.assertEqual(space.observe_fetch(observation, 0)[3], E("rows", E("Fact", Var(0))))
        space.batch(E("remove", original), E("add", E("Fact", 1)))
        # The projected answer is still 1, but the original record changed.
        self.denied("scope-conflict", space.batch_observed, observation, E("add", E("Changed", 1)))
        numeric = E("Numeric", Var(0))
        space.add(E("Numeric", 1))
        observation = space.observe((numeric,), (numeric,))
        space.batch(E("remove", E("Numeric", 1)), E("add", E("Numeric", 1.0)))
        self.denied("scope-conflict", space.batch_observed, observation, E("add", E("Numeric", 2)))

    def test_scoped_noop_cannot_reapply_after_reply_cache_retirement(self):
        space = self.client.open("scoped-noop", "native-ordered")
        observation = space.observe()
        space.batch_observed(observation, request_id="scoped-noop")
        for _ in range(260):
            space.batch()
        before = space.describe()
        # The durable decision outlives its retired reply, as for every request.
        self.denied("unknown-receipt", space.batch_observed, observation, request_id="scoped-noop")
        self.assertEqual(space.describe(), before)

    def test_scoped_commit_recovers_without_its_ephemeral_observation(self):
        space = self.client.open("scoped-recovery", "native-ordered")
        pattern = E("Value", Var(0))
        observation = space.observe((pattern,), (pattern,))
        operations = (E("add", E("Value", 7)), E("add", E("Value", 7)))
        answer = space.batch_observed(observation, *operations, request_id="scoped-recover")
        self.client.close()
        self.fixture.restart(crash=True)
        self.client = Client(self.fixture.socket)
        self.addCleanup(self.client.close)
        space = self.client.open("scoped-recovery", "native-ordered")
        self.assertEqual(space.match(pattern, Var(0)), [7, 7])
        self.assertEqual(space.batch_observed(observation, *operations, request_id="scoped-recover"), answer)
        self.client.call("compact", space.uuid)
        self.client.close()
        self.fixture.restart(crash=True)
        self.client = Client(self.fixture.socket)
        self.addCleanup(self.client.close)
        space = self.client.open("scoped-recovery", "native-ordered")
        self.assertEqual(space.batch_observed(observation, *operations, request_id="scoped-recover"), answer)
        self.assertEqual(space.match(pattern, Var(0)), [7, 7])

    def test_scoped_rejection_and_close_release_owned_buffers(self):
        space = self.client.open("scope-cleanup", "native-ordered")
        for _ in range(70):
            observation = space.observe((), (E("Allowed", Var(0)),))
            self.denied("undeclared-write", space.batch_observed, observation, E("add", E("Other", 1)))
        for _ in range(70):
            observation = space.observe((), (E("Allowed", Var(0)),))
            self.assertEqual(space.observe_close(observation), S("closed"))
        observation = space.observe((), (), lease_ms=1)
        time.sleep(.01)
        self.denied("expired-or-closed", space.batch_observed, observation)
        self.assertEqual(space.match_count(Var(0)), 0)

    def test_scoped_watch_ignores_other_channels_and_has_no_registration_gap(self):
        space = self.client.open("scope-watch", "native-ordered")
        pattern = E("Message", "a", Var(0))
        observation = space.observe((pattern,), (), lease_ms=1000)
        with Client(self.fixture.socket, timeout=5) as listener:
            watched = listener.open(space.name, "native-ordered")
            with ThreadPoolExecutor(max_workers=1) as pool:
                future = pool.submit(watched.observe_watch, observation, 4000)
                # Ownership transfer is observable: the old handle is gone.
                until = time.monotonic()+2
                while True:
                    try:
                        space.observe_fetch(observation, 0)
                    except csp.ServiceError as error:
                        self.assertEqual(error.reason, S("expired-or-closed"))
                        break
                    self.assertLess(time.monotonic(), until)
                    time.sleep(.005)
                for number in range(20):
                    space.add(E("Message", "b", number))
                time.sleep(1.1)  # The wait, not the old lease, now owns its view.
                self.assertFalse(future.done())
                start = time.monotonic()
                space.add(E("Message", "a", "arrived"))
                result = future.result(timeout=2)
                self.assertEqual(result[0], S("changed"))
                self.assertLess(time.monotonic()-start, 2)
        observation = space.observe((E("Later", Var(0)),), ())
        space.add(E("Later", 1))
        # A relevant write before registration answers immediately.
        self.assertEqual(space.observe_watch(observation, 1000)[0], S("changed"))
        observation = space.observe((E("Never", Var(0)),), ())
        self.assertEqual(space.observe_watch(observation, 10)[0], S("unchanged"))

    def test_cancelled_scoped_watches_release_connection_owned_views(self):
        space = self.client.open("scope-cancel", "native-ordered")
        for number in range(24):
            observation = space.observe((E("Message", number),), ())
            stream = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                stream.connect(self.fixture.socket)
                stream.sendall(csp.frame(E("request", "watch-"+str(number), S("observe-watch"),
                    space.uuid, space.id, observation[1], observation[2], 60000)))
                until = time.monotonic()+2
                while True:
                    try:
                        space.observe_fetch(observation, 0)
                    except csp.ServiceError as error:
                        self.assertEqual(error.reason, S("expired-or-closed"))
                        break
                    self.assertLess(time.monotonic(), until)
                    time.sleep(.005)
                stream.shutdown(socket.SHUT_RDWR)
            finally:
                stream.close()
            # A round trip lets the owner observe the disconnected waiter.
            self.client.hello()
        observation = space.observe((E("Message", 0),), ())
        self.assertEqual(space.observe_watch(observation, 0)[0], S("unchanged"))

    def test_noop_advances_revision_and_retry_is_correlated(self):
        space = self.space()
        initial = space.revision
        answer = space.batch(request_id="noop")
        self.assertEqual(space.revision, initial+1)
        retried = self.client.call("batch", space.uuid, space.id, initial, E("ops"), request_id="noop")
        self.assertEqual(retried, answer)
        self.denied("stale-revision", self.client.call, "batch", space.uuid, space.id, initial, E("ops"))
        self.denied("reused-id", self.client.call, "batch", space.uuid, space.id, initial,
                    E("ops", E("add", S("different"))), request_id="noop")
        receipt = self.client.call("receipt", space.uuid, "noop")
        self.assertEqual(receipt, E("reply", "noop", S("ok"), answer))
        self.denied("service-identity", self.client.call, "receipt", "0" * 32, "noop")
        TRACE.append({"kind": "conditional", "before": initial, "after": space.revision,
                      "stale": True, "correlated": True})

    def test_backend_key_projection_verification(self):
        for backend in BACKENDS:
            space=self.space(backend)
            values=[E("duplicate",7),E("duplicate",7),E("binary",b"\xff\x00"*10000),
                    E("open",Var(70),Var(70)),E("wide",*range(100))]
            space.batch(*(E("add",value) for value in values))
            verified=space.verify()
            self.assertEqual(verified,E("verified",space.uuid,space.id,space.revision,
                                        4 if backend=="mork" else 5))
            space.remove(E("duplicate",7))
            self.client.call("compact",space.uuid)
            self.assertEqual(space.verify()[4],3 if backend=="mork" else 4)

    def test_invalid_batch_does_not_publish_prefix(self):
        space = self.space()
        revision = space.revision
        self.denied("invalid-batch", space.batch, E("add", S("prefix")), E("execute", S("program")))
        self.assertEqual(space.describe()[6:], (revision, 0))
        self.assertEqual(space.match_count(S("prefix")), 0)

    def test_transport_valid_batch_too_large_for_journal_has_no_effect(self):
        space = self.space()
        request_id = "large-preflight"
        empty = E("ops", E("add", E("chunk", b"")), E("add", E("chunk", b"")))
        request = E("request", request_id, S("batch"), space.uuid, space.id, space.revision, empty)
        available = csp.MAX_BYTES - len(csp.encode(request)) - 32
        chunks = (E("chunk", b"x" * (available // 2)),
                  E("chunk", b"y" * (available - available // 2)))
        self.assertTrue(all(len(csp.encode(chunk)) < 512 * 1024 for chunk in chunks))
        ops = E("ops", *(E("add", chunk) for chunk in chunks))
        request = E("request", request_id, S("batch"), space.uuid, space.id, space.revision, ops)
        self.assertEqual(len(csp.encode(request)), csp.MAX_BYTES - 32)
        journal = self.fixture.data / "journal.csl"
        before = journal.read_bytes()
        self.denied("limit", self.client.call, "batch", *request[3:], request_id=request_id)
        self.assertEqual(journal.read_bytes(), before)
        self.assertEqual(space.describe()[6:], (0, 0))
        self.assertEqual(self.client.call("receipt", space.uuid, request_id), S("unknown-receipt"))
        space.add(S("after-refusal"))
        self.assertEqual(space.match_count(S("after-refusal")), 1)

    def test_snapshot_survives_mutation_and_compaction(self):
        space = self.space()
        space.batch(E("add", E("p", 1)), E("add", E("p", 2)))
        snapshot = space.query(E("p", Var(0)), Var(0))
        before = space.fetch(snapshot)
        space.batch(E("remove", E("p", 1)), E("add", E("p", 3)))
        self.client.call("compact", space.uuid)
        self.assertEqual(space.fetch(snapshot), before)
        self.assertCountEqual(space.match(E("p", Var(0)), Var(0)), [2, 3])
        self.assertEqual(before[1], snapshot[4])
        TRACE.append({"kind": "snapshot", "captured": before[1], "current": space.revision,
                      "rows": list(before[3][1:]), "stable": True})

    def test_exhaustion_expiry_and_close_are_distinct(self):
        space = self.space()
        empty = space.query(S("absent"), S("absent"), lease_ms=500)
        page = space.fetch(empty)
        self.assertEqual(page[3], E("rows"))
        self.assertIs(page[5], True)
        self.denied("page-limit", space.fetch, empty, 1)
        space.close_query(empty)
        self.denied("expired-or-closed", space.fetch, empty)
        short = space.query(S("absent"), S("absent"), lease_ms=1)
        time.sleep(.02)
        self.denied("expired-or-closed", space.fetch, short)

    def test_pages_are_offset_addressed(self):
        space = self.space()
        space.batch(*(E("add", E("p", i)) for i in range(7)))
        snapshot = space.query(E("p", Var(0)), Var(0))
        first = space.fetch(snapshot, 0, 3)
        self.assertEqual(space.fetch(snapshot, 0, 3), first)
        second = space.fetch(snapshot, 3, 3)
        last = space.fetch(snapshot, 6, 3)
        self.assertEqual(len(first[3][1:]), 3)
        self.assertEqual(len(second[3][1:]), 3)
        self.assertEqual(len(last[3][1:]), 1)
        self.assertFalse(first[5])
        self.assertTrue(last[5])

    def test_query_row_limit_never_looks_like_exhaustion(self):
        space = self.space()
        space.batch(E("add", E("p", 1)), E("add", E("p", 2)))
        self.denied("query-row-limit", space.query, E("p", Var(0)), Var(0), 1)
        self.assertEqual(space.match_count(E("p", Var(0))), 2)

    def test_variable_coreference_and_open_facts(self):
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                space = self.space(backend)
                space.batch(E("add", E("same", 1, 1)), E("add", E("same", 1, 2)),
                            E("add", E("open", Var(70), Var(70))),
                            E("add", E("open", Var(80), Var(80))))
                self.assertEqual(space.match(E("same", Var(2), Var(2)), Var(2)), [1])
                self.assertEqual(space.match_count(E("open", 1, 2)), 0)
                expected = 1 if backend == "mork" else 2
                self.assertEqual(space.match_count(E("open", 3, 3)), expected)
                space.remove(E("open", Var(500), Var(500)))
                self.assertEqual(space.match_count(E("open", 3, 3)), expected-1)

    def test_typed_values_round_trip(self):
        values = [S("7"), 7, True, "7", "a\x00b", b"\xff\x00", csp.FloatBits.of(-0.0),
                  csp.FloatBits(0x7ff8000000000042), csp.BigInt(str(2**90)), csp.Rational("123/7")]
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                space = self.space(backend)
                space.batch(*(E("add", E("payload", i, value)) for i, value in enumerate(values)))
                for i, expected in enumerate(values):
                    rows = space.match(E("payload", i, Var(0)), Var(0))
                    self.assertEqual(len(rows), 1, (backend, i, rows))
                    if isinstance(expected, csp.Rational):
                        self.assertEqual(Fraction(rows[0].spelling.replace("r", "/")), Fraction(expected.spelling))
                    else:
                        self.assertEqual(rows[0], expected, (backend, i))
                    space.verify()
                    space.remove(E("payload",i,expected))
                    self.assertEqual(space.match_count(E("payload",i,Var(0))),0,(backend,i,expected))

    def test_large_values_survive_compaction_and_recovery(self):
        value = E("persisted", "雪\x00"*16384, E("wide", *range(100)), Var(91), Var(91))
        for backend in BACKENDS:
            space = self.space(backend)
            space.add(value)
            self.client.call("compact",space.uuid)
        self.client.close()
        self.fixture.restart(crash=True)
        for backend in BACKENDS:
            space = self.space(backend)
            self.assertEqual(space.match(Var(0),Var(0)),[E("persisted", "雪\x00"*16384,
                             E("wide",*range(100)),Var(0),Var(0))])
            space.remove(value)
            self.assertEqual(space.match_count(Var(0)),0)

    def test_concurrent_readers_never_observe_a_batch_prefix(self):
        space = self.space()
        finished = threading.Event()
        def writer():
            try:
                with Client(self.fixture.socket) as client:
                    owned = client.open(space.name)
                    for i in range(40):
                        owned.batch(E("add",E("pair",i,0)),E("add",E("pair",i,1)))
            finally:
                finished.set()
        def reader():
            with Client(self.fixture.socket) as client:
                owned = client.open(space.name)
                counts=[]
                while not finished.is_set() or not counts:
                    counts.append(owned.match_count(E("pair",Var(0),Var(1))))
                    self.assertEqual(counts[-1]%2,0)
                return len(counts)
        with ThreadPoolExecutor(max_workers=3) as pool:
            writers=pool.submit(writer)
            readers=[pool.submit(reader) for _ in range(2)]
            writers.result(timeout=20)
            self.assertTrue(all(future.result(timeout=20)>0 for future in readers))
        self.assertEqual(space.match_count(E("pair",Var(0),Var(1))),80)

    def test_named_space_and_service_isolation(self):
        a, b = self.client.open("a"), self.client.open("b")
        a.add(S("fact"))
        self.assertEqual(b.match_count(S("fact")), 0)
        self.denied("service-identity", self.client.call, "describe", "0"*32, a.id)
        self.denied("backend-mismatch", self.client.open, "a", "mork")
        listed=self.client.list_spaces()
        self.assertCountEqual([descriptor[3] for descriptor in listed],["a","b"])
        self.assertEqual(listed,[a.describe(),b.describe()])

    def test_large_binary_values_and_wide_syntax(self):
        values = ["雪\x00"*32768, b"\xff\x00"*32768, csp.Symbol(b"n\x00s"),
                  E("wide", *range(100)), E("variables", *(Var(i) for i in range(70)))]
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                space = self.space(backend)
                for i, value in enumerate(values):
                    space.add(E("large", i, value))
                    rows = space.match(E("large", i, Var(200)), Var(200))
                    self.assertEqual(len(rows),1)
                    if isinstance(value, csp.Symbol) and isinstance(value.name, bytes):
                        self.assertEqual(rows[0].name.encode("utf-8"), value.name)
                    else:
                        self.assertEqual(rows[0],value)
                    space.verify()
                    space.remove(E("large",i,value))
                    self.assertEqual(space.match_count(E("large",i,Var(200))),0)

    def test_drop_recreate_rejects_old_identity(self):
        old = self.space()
        catalog = self.client.hello()[4]
        self.client.call("drop", old.uuid, old.id, old.revision, catalog)
        new = self.space()
        self.assertNotEqual(old.id, new.id)
        self.denied("missing-space", old.describe)

    def test_competing_conditional_writers_have_one_winner(self):
        space = self.space()
        def writer(i):
            with Client(self.fixture.socket) as client:
                try:
                    client.call("batch", space.uuid, space.id, 0, E("ops", E("add", E("writer", i))))
                    return True
                except csp.ServiceError as error:
                    self.assertEqual(error.reason, S("stale-revision"))
                    return False
        with ThreadPoolExecutor(max_workers=8) as pool:
            self.assertEqual(sum(pool.map(writer, range(8))), 1)
        self.assertEqual(space.describe()[6:], (1, 1))

    def test_acknowledged_writes_and_receipts_survive_kill(self):
        descriptors = []
        for backend in BACKENDS:
            space = self.space(backend)
            space.add(E("saved", backend), request_id="save-"+backend)
            descriptors.append(space.describe())
        uuid = descriptors[0][1]
        self.client.close()
        self.fixture.restart(crash=True)
        self.assertEqual(self.client.hello()[2], uuid)
        for descriptor in descriptors:
            space = self.space(descriptor[4].name)
            self.assertEqual(space.describe(), descriptor)
            self.assertEqual(space.match(E("saved", Var(0)), Var(0)), [descriptor[4].name])
            self.assertEqual(self.client.call("receipt", uuid, "save-"+descriptor[4].name)[2], S("ok"))
        TRACE.append({"kind": "recovery", "backends": 3, "same_identity": True,
                      "acknowledged": True})

    def test_lost_reply_is_recoverable_from_receipt(self):
        space = self.space()
        request = E("request", "lost", S("batch"), space.uuid, space.id, space.revision,
                    E("ops", E("add", S("once"))))
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.connect(self.fixture.socket)
            connection.sendall(csp.frame(request))
        until = time.monotonic() + 5
        while self.client.call("receipt", space.uuid, "lost") == S("unknown-receipt"):
            self.assertLess(time.monotonic(), until)
            time.sleep(.01)
        self.assertEqual(space.match_count(S("once")), 1)

        answer = self.client.call("batch", *request[3:], request_id="lost")
        self.assertEqual(answer[3], 1)
        self.assertEqual(space.match_count(S("once")), 1)

    def test_exact_request_bytes_survive_journal_and_checkpoint(self):
        saved=[]
        for backend in BACKENDS:
            space=self.space(backend)
            value=E("spelling",csp.BigInt("00042"),csp.Rational("0006/0004"),Var(9),Var(9))
            args=(space.uuid,space.id,space.revision,E("ops",E("add",value)))
            request_id="exact-"+backend
            answer=self.client.call("batch",*args,request_id=request_id)
            saved.append((args,request_id,answer))
        for checkpoint in (False,True):
            if checkpoint:
                self.client.call("compact",saved[0][0][0])
            self.client.close()
            self.fixture.restart(crash=True)
            for args,request_id,answer in saved:
                self.assertEqual(self.client.call("batch",*args,request_id=request_id),answer)
                changed=args[:3]+(E("ops",E("add",E("spelling",csp.BigInt("42"),
                                 csp.Rational("3/2"),Var(9),Var(9)))),)
                self.denied("reused-id",self.client.call,"batch",*changed,request_id=request_id)
            for backend in BACKENDS:
                self.assertEqual(self.space(backend).match_count(Var(0)),1)

    def test_retired_receipt_cannot_make_old_write_apply_twice(self):
        space = self.space()
        before = space.revision
        ops = E("ops", E("add", S("once")))
        space.add(S("once"), request_id="old")
        # Cross both the 256-entry reply-cache boundary and the durable
        # decision index's initial load threshold.
        for i in range(385):
            space.batch(request_id=f"retire-{i}")
        self.assertEqual(self.client.call("receipt", space.uuid, "old"), S("unknown-receipt"))
        self.denied("unknown-receipt", self.client.call, "batch", space.uuid, space.id, before, ops, request_id="old")
        self.assertEqual(space.match_count(S("once")), 1)
        self.client.call("compact", space.uuid)
        self.client.close()
        self.fixture.restart(crash=True)
        self.client = Client(self.fixture.socket)
        self.addCleanup(self.client.close)
        space = self.space()
        self.assertEqual(self.client.call("receipt", space.uuid, "old"), S("unknown-receipt"))
        self.denied("unknown-receipt", self.client.call, "batch", space.uuid, space.id,
                    before, ops, request_id="old")
        self.assertEqual(space.match_count(S("once")), 1)

    def test_restart_rejects_old_cursor(self):
        space = self.space()
        old = space.query(Var(0), Var(0))
        self.client.close()
        self.fixture.restart()
        fresh = self.space().query(Var(0),Var(0))
        self.assertEqual(fresh[2],old[2])
        self.assertNotEqual(fresh[1],old[1])
        self.denied("cursor-identity", space.fetch, old)

    def test_torn_journal_tail_recovers_prefix(self):
        space = self.space()
        space.add(S("saved"))
        self.client.close()
        self.fixture.stop()
        journal = self.fixture.data / "journal.csl"
        size = journal.stat().st_size
        with journal.open("ab") as output:
            output.write(b"CSL1\x00")
        self.fixture.start()
        self.assertEqual(journal.stat().st_size, size)
        self.assertEqual(self.space().match_count(S("saved")), 1)

    def test_complete_corrupt_record_is_not_ignored(self):
        self.space().add(S("saved"))
        self.client.close()
        self.fixture.stop()
        journal = self.fixture.data / "journal.csl"
        data = bytearray(journal.read_bytes())
        data[-1] ^= 1
        journal.write_bytes(data)
        self.fixture.start(success=False)
        self.assertNotEqual(self.fixture.process.returncode, 0)
        self.fixture.process = None

    def test_sealed_checkpoint_replays_and_rejects_truncation(self):
        space = self.space()
        space.batch(*(E("add", E("saved", i)) for i in range(300)))
        self.client.call("compact", space.uuid)
        self.client.close()
        self.fixture.restart()
        self.assertEqual(self.space().match_count(E("saved", Var(0))), 300)
        self.client.close()
        self.fixture.stop()
        journal = self.fixture.data / "journal.csl"
        data = journal.read_bytes()
        journal.write_bytes(data[:-8])
        self.fixture.start(success=False)
        self.assertNotEqual(self.fixture.process.returncode, 0)
        self.fixture.process = None

    def test_fragmented_slow_peer_does_not_block_another_client(self):
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(connection.close)
        connection.connect(self.fixture.socket)
        connection.sendall(b"CSP")
        start = time.monotonic()
        self.client.hello()
        self.assertLess(time.monotonic() - start, 2)
        packet = csp.frame(E("request", "fragment", S("hello")))
        connection.sendall(packet[3:])
        connection.settimeout(5)
        header = connection.recv(8, socket.MSG_WAITALL)
        self.assertEqual(header[:4], b"CSP1")
        answer = csp.decode(connection.recv(struct.unpack(">I", header[4:])[0], socket.MSG_WAITALL))
        self.assertEqual(answer[1], "fragment")

    def test_malformed_and_oversize_frames_do_not_break_service(self):
        packets = [b"NOPE"+struct.pack(">I",4)+b"CAV1",
                   b"CSP1"+struct.pack(">I",csp.MAX_BYTES+1),
                   b"CSP1"+struct.pack(">I",9)+b"CAV1V"+struct.pack("<I",99)]
        for packet in packets:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                connection.settimeout(5)
                connection.connect(self.fixture.socket)
                connection.sendall(packet)
                try:
                    self.assertEqual(connection.recv(1), b"")
                except ConnectionResetError:
                    pass
            self.client.hello()

    def test_socket_and_directory_are_private(self):
        self.assertEqual(Path(self.fixture.socket).stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.fixture.data.stat().st_mode & 0o777, 0o700)
        self.assertEqual((self.fixture.data / "journal.csl").stat().st_mode & 0o777, 0o600)

    def test_second_writer_is_refused(self):
        other = subprocess.run([SERVICE, "--socket", str(self.fixture.root / "other.sock"),
                                "--data", str(self.fixture.data)], cwd=ROOT, capture_output=True, timeout=10)
        self.assertNotEqual(other.returncode, 0)
        self.client.hello()

    @unittest.skipUnless(CETTA, "provide CeTTa binary for native client qualification")
    def test_cetta_processes_in_he_petta_and_prime(self):
        space = self.space()
        for mode in ("he", "petta", "prime"):
            path = self.fixture.root / (mode + ".metta")
            path.write_text(f'''!(import! &self space_service)
!(space:call "{self.fixture.socket}" (request "native-{mode}" batch "{space.uuid}" {space.id} {space.revision} (ops (add (from-{mode} 7)))) 5000)
''')
            process = subprocess.run([CETTA,"--lang",mode,"--quiet",str(path)], cwd=ROOT,
                                     text=True,capture_output=True,timeout=30)
            self.assertEqual(process.returncode,0,process.stderr)
            self.assertIn("committed",process.stdout)
            space.describe()
            self.assertEqual(space.match_count(E("from-"+mode,Var(0))),1)

    @unittest.skipUnless(CETTA, "provide CeTTa binary for native client qualification")
    def test_metta_helpers_and_stale_descriptor(self):
        for mode in ("he","petta","prime"):
            space = self.client.open("helper-"+mode)
            descriptor = f'(space "{space.uuid}" {space.id} "{space.name}" native bag 0 0)'
            path = self.fixture.root / ("helper-"+mode+".metta")
            path.write_text(f'''!(import! &self space_service)
!(space:open "{self.fixture.socket}" "{space.name}" native)
!(space:list "{self.fixture.socket}")
!(space:principal-ref "{self.fixture.socket}")
!(space:rotate-principal "{self.fixture.socket}" (account {os.getuid()}))
!(space:describe "{self.fixture.socket}" {descriptor})
!(space:add "{self.fixture.socket}" {descriptor} (helper 17))
!(space:count "{self.fixture.socket}" {descriptor} (helper $x))
!(space:add "{self.fixture.socket}" {descriptor} (helper 18))
''')
            process = subprocess.run([CETTA,"--lang",mode,"--quiet",str(path)],cwd=ROOT,
                                     text=True,capture_output=True,timeout=30)
            self.assertEqual(process.returncode,0,process.stderr)
            self.assertIn("committed",process.stdout)
            self.assertIn("stale-revision",process.stdout)
            self.assertIn("principal-ref",process.stdout)
            self.assertIn("rotated-principal",process.stdout)
            self.assertIn(f'"{space.name}" native bag',process.stdout)
            self.assertEqual(space.match(E("helper",Var(0)),Var(0)),[17])

    def transaction_worker(self):
        module = self.fixture.root / "transactions.metta"
        module.write_text('''!(import! &self (library space_transaction))
!(import! &self (library io))
!(import! &self (library str))
(: transaction-test (-> %Undefined% %Undefined%))
(= (transaction-test (observe $socket $space $reads $writes $limit))
   (space:observe $socket $space $reads $writes $limit 60000))
(= (transaction-test (scoped-observe $socket $space $reads $writes))
   (space:observe-scoped $socket $space $reads $writes 4096 60000))
(= (transaction-test (abort $socket $observation)) (space:abort $socket $observation))
(= (transaction-test (commit $socket $observation $id $operations))
   (space:commit $socket $observation $id $operations))
(= (transaction-test (begin $socket $space $key $fingerprint $reads $writes))
   (space:begin $socket $space $key $fingerprint $reads $writes 4096 60000))
(= (transaction-test (scoped-begin $socket $space $key $fingerprint $reads $writes))
   (space:begin-scoped $socket $space $key $fingerprint $reads $writes 4096 60000))
(= (transaction-test (watch $socket $observation $duration))
   (space:wait-start $socket $observation $duration))
(= (transaction-test (watch-poll $socket $waiting)) (space:wait-poll $socket $waiting))
(= (transaction-test (watch-cancel $socket $waiting)) (space:wait-cancel $socket $waiting))
(= (transaction-test (poll $id)) (io:poll $id))
(= (transaction-test (cancel $id)) (io:cancel $id))
(= (transaction-test (publish $socket $transaction $id $result $operations))
   (space:publish $socket $transaction $id $result $operations))
(= (transaction-test (large-read $socket $space $scoped))
   (let $answer (if $scoped
       (space:observe-scoped $socket $space ((scope "large" (Large $id $body))) () 8 60000)
       (space:observe $socket $space ((scope "large" (Large $id $body) (Large $id $body))) () 8 60000))
     (case $answer
       (((SpaceScopedObserved $handle $ticket $reads $writes $results)
          (let $closed (space:abort $socket $answer) (space-test:summary $results)))
        ((SpaceObserved $handle $reads $writes $results) (space-test:summary $results))
        ($failed $failed)))))
(= (space-test:summary ((SpaceRead "large" $rows)))
   (let (Large $id $body) (car-atom $rows) (ObservedRows (size-atom $rows) (str:byte-length $body))))
''')
        worker = Worker(WORKER, module, "transaction-test", language="petta")
        self.addCleanup(worker.close)
        return worker

    @unittest.skipUnless(WORKER and CETTA, "provide native worker and CLI")
    def test_petta_worker_and_cli_share_source_truth_and_callback_semantics(self):
        module = self.fixture.root / "reader-parity.metta"
        source = '''!(import! &self (library rel))
(= (reader-check:less $left $right) (< (index-atom $left 0) (index-atom $right 0)))
(= (reader-check $items)
   (let $ordered (rel:order reader-check:less $items)
     (case (< 1 2) ((True (Verified $ordered)) (False (Wrong $ordered))))))
'''
        module.write_text(source)
        worker = Worker(WORKER, module, "reader-check", language="petta")
        self.addCleanup(worker.close)
        expected = E("Verified", ((1, "a"), (1, "b"), (2, "c")))
        self.assertEqual(worker.call(((2, "c"), (1, "a"), (1, "b"))), expected)
        cli = self.fixture.root / "reader-parity-cli.metta"
        cli.write_text(source+'!(reader-check ((2 "c") (1 "a") (1 "b")))\n')
        result = subprocess.run([CETTA, "--lang", "petta", "--quiet", str(cli)],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip().splitlines()[-1], '(Verified ((1 "a") (1 "b") (2 "c")))')

    @unittest.skipUnless(WORKER and CETTA, "provide native worker and CLI")
    def test_worker_profile_matches_cli_and_unencodable_output_keeps_the_worker(self):
        module = self.fixture.root / "worker-profile.metta"
        module.write_text('''(= (worker-profile bracket) (quote (f [1 2] 3)))
(= (worker-profile unavailable) (new-space))
(= (worker-profile ready) ready)
''')
        worker = Worker(WORKER, module, "worker-profile", language="petta")
        self.addCleanup(worker.close)
        self.assertEqual(worker.call(S("bracket")), E("f", S("[1"), S("2]"), 3))
        with self.assertRaisesRegex(RuntimeError, "unencodable-result"):
            worker.call(S("unavailable"))
        self.assertEqual(worker.call(S("ready")), S("ready"))
        extended = Worker(WORKER, module, "worker-profile", language="petta", language_profile="extended")
        self.addCleanup(extended.close)
        with self.assertRaisesRegex(RuntimeError, "unencodable-result"):
            extended.call(S("bracket"))
        self.assertEqual(extended.call(S("ready")), S("ready"))

    @unittest.skipUnless(WORKER, "provide value worker for MeTTa transactions")
    def test_metta_observation_preserves_pages_and_negative_dependencies(self):
        worker = self.transaction_worker()
        space = self.space()
        rows = [E("Item", n % 7, "λ") for n in range(260)]
        space.batch(*(E("add", row) for row in rows))
        reads = (E("scope", "items", E("Item", Var(0), Var(1)), E("Item", Var(0), Var(1))),
                 E("scope", "absent", E("Missing", Var(2)), Var(2)))
        writes = (E("Item", Var(0), Var(1)), E("Missing", Var(0)))
        observation = worker.call(E("observe", self.fixture.socket, space.describe(), reads, writes, 1024))
        self.assertEqual(observation[0], S("SpaceObserved"))
        self.assertEqual(observation[4], (E("SpaceRead", "items", tuple(rows)), E("SpaceRead", "absent", ())))
        # A new formerly absent row invalidates the whole observation, even
        # though every positive row is unchanged.
        space.add(E("Missing", 1))
        result = worker.call(E("commit", self.fixture.socket, observation, "stale-tx",
                               (E("add", E("Item", 99, "new")),)))
        self.assertEqual(result[2:], (S("error"), S("stale-revision")))
        self.assertEqual(space.match_count(E("Item", 99, Var(0))), 0)

    @unittest.skipUnless(WORKER, "provide value worker for scoped MeTTa transactions")
    def test_metta_scoped_callers_keep_data_and_independent_channels(self):
        worker = self.transaction_worker()
        space = self.client.open("metta-scopes", "native-ordered")
        scopes = [E("Message", channel, Var(0)) for channel in ("a", "b")]
        observations = [worker.call(E("scoped-observe", self.fixture.socket, space.describe(),
                                      (E("scope", "messages", pattern),), (pattern,))) for pattern in scopes]
        for channel, observation in zip(("a", "b"), observations):
            self.assertEqual(observation[0], S("SpaceScopedObserved"))
            self.assertEqual(observation[5], (E("SpaceRead", "messages", ()),))
            result = worker.call(E("commit", self.fixture.socket, observation, "metta-"+channel,
                                   (E("add", E("Message", channel, E("+", 1, 2))),)))
            self.assertEqual(result[2], S("ok"))
        observed = worker.call(E("scoped-observe", self.fixture.socket, space.describe(),
                                 (E("scope", "messages", scopes[0]),), ()))
        self.assertEqual(observed[5], (E("SpaceRead", "messages", (E("Message", "a", E("+", 1, 2)),)),))
        closed = worker.call(E("abort", self.fixture.socket, observed))
        self.assertEqual(closed[2:], (S("ok"), S("closed")))

    @unittest.skipUnless(WORKER, "provide value worker for write coverage")
    def test_open_values_require_a_write_pattern_covering_the_whole_value(self):
        worker = self.transaction_worker()
        space = self.client.open("open-write-scope", "native-ordered")
        cases = [
            (E("Channel", "a", Var(0)), E("Channel", Var(0), 1), False),
            (E("Channel", Var(0), Var(1)), E("Channel", Var(0), 1), True),
            (E("Pair", Var(0), Var(0)), E("Pair", Var(0), Var(1)), False),
            (E("Pair", Var(0), Var(0)), E("Pair", Var(0), Var(0)), True),
        ]
        for method in ("observe", "scoped-observe"):
            for index, (pattern, value, allowed) in enumerate(cases):
                fields = (method, self.fixture.socket, space.describe(), (), (pattern,))
                observed = worker.call(E(*fields, 16)) if method == "observe" else worker.call(E(*fields))
                result = worker.call(E("commit", self.fixture.socket, observed, method+str(index), (E("add", value),)))
                if allowed:
                    self.assertEqual(result[2], S("ok"), (method, index, result))
                else:
                    expected = E("SpaceRefused", S("undeclared-write")) if method == "observe" else (
                        result[0], result[1], S("error"), S("undeclared-write"))
                    self.assertEqual(result, expected, (method, index))

    @unittest.skipUnless(WORKER, "provide value worker for terminal cleanup")
    def test_scoped_commit_early_refusal_releases_its_observation(self):
        worker = self.transaction_worker()
        space = self.client.open("refused-commit", "native-ordered")
        self.client.call("batch", space.uuid, space.id, space.revision, E("ops"), request_id="occupied-request")
        for _ in range(80):
            observation = worker.call(E("scoped-observe", self.fixture.socket, space.describe(), (), (Var(0),)))
            self.assertEqual(observation[0], S("SpaceScopedObserved"))
            result = worker.call(E("commit", self.fixture.socket, observation, "occupied-request", ()))
            self.assertEqual(result[2:], (S("error"), S("reused-id")))
        conservative = worker.call(E("observe", self.fixture.socket, space.describe(), (), (), 16))
        self.assertEqual(worker.call(E("abort", self.fixture.socket, conservative)), E("SpaceAborted"))

    @unittest.skipUnless(WORKER, "provide value worker for MeTTa transactions")
    def test_metta_commit_checks_scopes_without_capturing_pattern_variables(self):
        worker = self.transaction_worker()
        space = self.space()
        writes = (E("Item", Var(0), Var(1)), E("Receipt", Var(0), Var(1)))
        observation = worker.call(E("observe", self.fixture.socket, space.describe(), (), writes, 10))
        operations = (E("add", E("Item", 1, "first")),
                      E("add", E("Item", 2, "second")),
                      E("add", E("Receipt", "operation-1", "completed")))
        rejected = worker.call(E("commit", self.fixture.socket, observation, "outside-tx",
                                 (*operations, E("add", E("Outside", 1)))))
        self.assertEqual(rejected, E("SpaceRefused", S("undeclared-write")))
        self.assertEqual(space.match_count(Var(0)), 0)
        accepted = worker.call(E("commit", self.fixture.socket, observation, "allowed-tx", operations))
        self.assertEqual(accepted[2], S("ok"))
        self.assertEqual(worker.call(E("commit", self.fixture.socket, observation, "allowed-tx", operations)), accepted)
        self.assertEqual(space.match_count(Var(0)), 3)
        # Retiring the protocol receipt must not retire application evidence.
        space.describe()
        for _ in range(260):
            space.batch()
        self.assertEqual(self.client.call("receipt", space.uuid, "allowed-tx"), S("unknown-receipt"))
        self.assertEqual(space.match(E("Receipt", "operation-1", Var(0)), Var(0)), ["completed"])
        again = worker.call(E("commit", self.fixture.socket, observation, "new-request-id", operations))
        self.assertEqual(again[2:], (S("error"), S("stale-revision")))
        self.assertEqual(space.match_count(Var(0)), 3)

    @unittest.skipUnless(WORKER, "provide value worker for MeTTa transactions")
    def test_metta_failed_observation_is_not_a_partial_result(self):
        worker = self.transaction_worker()
        space = self.space()
        space.batch(E("add", E("Item", 1)), E("add", E("Item", 2)))
        reads = (E("scope", "items", E("Item", Var(0)), Var(0)),)
        result = worker.call(E("observe", self.fixture.socket, space.describe(), reads, (), 1))
        self.assertNotEqual(result[0], S("SpaceObserved"))
        self.assertEqual(result[2], S("error"))
        # Every successful observation releases its server-owned snapshot.
        for _ in range(70):
            result = worker.call(E("observe", self.fixture.socket, space.describe(), reads, (), 2))
            self.assertEqual(result[0], S("SpaceObserved"))
            self.assertEqual(result[4], (E("SpaceRead", "items", (1, 2)),))

    @unittest.skipUnless(WORKER, "provide value worker for adaptive pages")
    def test_metta_large_pages_reduce_size_and_keep_the_complete_result(self):
        worker = self.transaction_worker()
        space = self.client.open("adaptive-pages", "native-ordered")
        for number in range(4):
            space.add(E("Large", number, "x"*270000))
        for scoped in (False, True):
            self.assertEqual(worker.call(E("large-read", self.fixture.socket, space.describe(), scoped)),
                             E("ObservedRows", 4, 270000))

    @unittest.skipUnless(WORKER, "provide value worker for durable application receipts")
    def test_metta_application_receipt_survives_protocol_eviction_and_restart(self):
        worker = self.transaction_worker()
        space = self.client.open("application", "native-ordered")
        writes = (E("Value", Var(0)),)
        def begin(fingerprint="same-request"):
            return worker.call(E("begin", self.fixture.socket, space.describe(),
                                 "operation", fingerprint, (), writes))
        first, competitor = begin(), begin()
        self.assertEqual(first[0], S("SpaceTransaction"))
        result = E("+", 1, 2)  # a value, not an instruction to evaluate
        operations = (E("add", E("Value", 7)), E("add", E("Value", 7)))
        accepted = worker.call(E("publish", self.fixture.socket, first, "published", result, operations))
        self.assertEqual(accepted, E("SpaceCommitted", result))
        stale = worker.call(E("publish", self.fixture.socket, competitor, "competitor", result, operations))
        self.assertEqual(stale[2:], (S("error"), S("stale-revision")))
        self.assertEqual(begin(), accepted)
        self.assertEqual(begin("different-request"), E("SpaceRefused", S("reused-operation-identity")))
        space.describe()
        for _ in range(260):
            space.batch()
        self.assertEqual(self.client.call("receipt", space.uuid, "published"), S("unknown-receipt"))
        self.assertEqual(begin(), accepted)
        self.client.call("compact", space.uuid)
        self.client.close()
        self.fixture.restart(crash=True)
        self.client = Client(self.fixture.socket)
        self.addCleanup(self.client.close)
        space = self.client.open("application", "native-ordered")
        self.assertEqual(begin(), accepted)
        self.assertEqual(space.match(E("Value", Var(0)), Var(0)), [7, 7])
        self.assertEqual(space.match_count(E("SpaceReceiptV1", "operation", Var(0), Var(1))), 1)

    @unittest.skipUnless(WORKER, "provide value worker for durable application receipts")
    def test_metta_application_cannot_rewrite_its_receipts(self):
        worker = self.transaction_worker()
        space = self.client.open("receipt-scope", "native-ordered")
        transaction = worker.call(E("begin", self.fixture.socket, space.describe(), "operation", "fp", (), (Var(0),)))
        result = worker.call(E("publish", self.fixture.socket, transaction, "tampered", 7,
                               (E("add", E("SpaceReceiptV1", "other", "fp", 9)),)))
        self.assertEqual(result, E("SpaceRefused", S("reserved-receipt-mutation")))
        self.assertEqual(space.match_count(Var(0)), 0)
        # Imported corruption is reported; no arbitrary matching receipt wins.
        space.batch(E("add", E("SpaceReceiptV1", "operation", "fp", 1)),
                    E("add", E("SpaceReceiptV1", "operation", "fp", 2)))
        result = worker.call(E("begin", self.fixture.socket, space.describe(), "operation", "fp", (), (Var(0),)))
        self.assertEqual(result, E("SpaceRefused", S("duplicate-application-receipt")))

    @unittest.skipUnless(WORKER, "provide value worker for scoped receipts")
    def test_metta_scoped_receipts_release_views_and_survive_independent_writes(self):
        worker = self.transaction_worker()
        space = self.client.open("scoped-receipts", "native-ordered")
        def begin(key="a", fingerprint="fp"):
            return worker.call(E("scoped-begin", self.fixture.socket, space.describe(),
                                 key, fingerprint, (), (E("Value", key, Var(0)),)))
        first, competitor, independent = begin(), begin(), begin("b")
        literal = E("+", 1, 2)
        for key, transaction in (("a", first), ("b", independent)):
            self.assertEqual(transaction[0], S("SpaceTransaction"))
            answer = worker.call(E("publish", self.fixture.socket, transaction, "commit-"+key,
                                   literal, (E("add", E("Value", key, literal)),)))
            self.assertEqual(answer, E("SpaceCommitted", literal))
        answer = worker.call(E("publish", self.fixture.socket, competitor, "racing", literal, ()))
        self.assertEqual(answer[2:], (S("error"), S("scope-conflict")))
        # Cached results and rejections release their service-owned views.
        for _ in range(70):
            self.assertEqual(begin(), E("SpaceCommitted", literal))
            self.assertEqual(begin(fingerprint="different"), E("SpaceRefused", S("reused-operation-identity")))
        for _ in range(260):
            space.batch()
        self.assertEqual(self.client.call("receipt", space.uuid, "commit-a"), S("unknown-receipt"))
        self.client.call("compact", space.uuid)
        self.client.close()
        self.fixture.restart(crash=True)
        self.client = Client(self.fixture.socket)
        self.addCleanup(self.client.close)
        space = self.client.open("scoped-receipts", "native-ordered")
        self.assertEqual(begin(), E("SpaceCommitted", literal))
        self.assertEqual(space.match(E("Value", Var(0), Var(1)), Var(0)), ["a", "b"])
        for number in range(70):
            pending = begin("cancel-"+str(number))
            self.assertEqual(worker.call(E("abort", self.fixture.socket, pending))[2:], (S("ok"), S("closed")))

    @unittest.skipUnless(WORKER, "provide value worker for native scoped waits")
    def test_metta_scoped_wait_uses_owned_native_io(self):
        worker = self.transaction_worker()
        space = self.client.open("metta-watch", "native-ordered")
        def start():
            observation = worker.call(E("scoped-observe", self.fixture.socket, space.describe(),
                                         (E("scope", "inbox", E("Message", "a", Var(0))),), ()))
            pending = worker.call(E("watch", self.fixture.socket, observation, 3000))
            self.assertEqual(pending[0], S("SpaceWaiting"))
            return pending
        waiting = start()
        request = waiting[2]
        space.add(E("Message", "b", 1))
        self.assertEqual(worker.call(E("watch-poll", self.fixture.socket, waiting)), E("io:idle"))
        space.add(E("Message", "a", 2))
        deadline = time.monotonic()+3
        while True:
            result = worker.call(E("watch-poll", self.fixture.socket, waiting))
            if result != E("io:idle"):
                break
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        self.assertEqual(result[0], S("io:event"))
        self.assertEqual(result[2][2:], (S("ok"), E("changed", space.uuid, space.id, space.revision)))
        self.assertEqual(worker.call(E("poll", request)), E("io:unknown", request))
        # More cancellations than observation slots, including cancellations
        # before the server has received the asynchronous request.
        for _ in range(80):
            waiting = start()
            self.assertEqual(worker.call(E("watch-cancel", self.fixture.socket, waiting)), ())
            self.assertEqual(worker.call(E("poll", waiting[2])), E("io:unknown", waiting[2]))

    @unittest.skipUnless(WORKER, "provide value worker for snapshot publication qualification")
    def test_prime_worker_publishes_only_against_its_captured_snapshot(self):
        old = self.space()
        current = self.client.open(old.name)
        current.batch(*(E("add", E("Work", n)) for n in (7, 7, 9)))
        snapshot = old.query(E("Work", Var(0)), Var(0))
        self.assertNotEqual(old.revision, snapshot[4])
        rows, offset = [], 0
        try:
            while True:
                page = old.fetch(snapshot, offset, 2)
                self.assertEqual(page[1], snapshot[4])
                rows.extend(page[3][1:])
                if page[5]:
                    break
                offset = page[4]
        finally:
            old.close_query(snapshot)
        self.assertEqual(rows, [7, 7, 9])
        worker = Worker(WORKER, ROOT / "examples/space-service/prime_worker.metta",
                        "work:transform", language="prime", num_threads=2)
        self.addCleanup(worker.close)
        completed = worker.call(tuple(rows))
        self.assertEqual(completed, (E("Computed", 7, 28), E("Computed", 7, 28), E("Computed", 9, 45)))
        operations = E("ops", *(E("add", row) for row in completed))
        answer = self.client.call("batch", old.uuid, old.id, snapshot[4], operations)
        self.assertEqual(answer[0], S("committed"))
        self.assertEqual(old.match(E("Computed", Var(0), Var(1)), E("Computed", Var(0), Var(1))),
                         list(completed))
        before = old.describe()
        self.denied("stale-revision", self.client.call, "batch", old.uuid, old.id,
                    snapshot[4], operations)
        self.assertEqual(old.describe(), before)
        # A coherent old result remains inspectable; it grants no new write.
        self.assertEqual(worker.call(tuple(rows)), completed)
        fresh = old.query(E("Work", Var(0)), Var(0))
        old.add(E("Work", 11))
        self.denied("stale-revision", self.client.call, "batch", old.uuid, old.id,
                    fresh[4], operations)
        old.close_query(fresh)
        self.assertEqual(old.match_count(E("Computed", Var(0), Var(1))), 3)

    @unittest.skipUnless(WORKER, "provide value worker for dialect value qualification")
    def test_value_worker_preserves_data_and_tensors_in_each_dialect(self):
        module = self.fixture.root / "identity.metta"
        module.write_text("(= (identity $value) $value)\n")
        values = [E("+", 1, 2), E("Data", "literal text", E("same", 7, 7)),
                  csp.Tensor.vector([1., -0., .5])]
        for mode in (None, "petta", "prime"):
            with self.subTest(language=mode):
                worker = Worker(WORKER, module, "identity", language=mode, num_threads=2)
                try:
                    for value in values:
                        self.assertEqual(worker.call(value), value)
                finally:
                    self.assertEqual(worker.close(), 0)

    @unittest.skipUnless(WORKER, "provide value worker for cancellation qualification")
    def test_lost_prime_worker_publishes_nothing_and_can_restart(self):
        module = self.fixture.root / "waiting.metta"
        module.write_text("(= (wait $n) (if (== $n 0) Ready (wait $n)))\n")
        worker = Worker(WORKER, module, "wait", language="prime", timeout=.1)
        self.addCleanup(worker.close)
        space = self.space()
        before = space.describe()
        with self.assertRaises(WorkerUnavailable):
            worker.call(1)
        self.assertIsNone(worker.process)
        self.assertEqual(space.describe(), before)
        self.assertEqual(worker.call(0), S("Ready"))
        self.assertEqual(space.describe(), before)

    @unittest.skipUnless(CETTA, "provide CeTTa binary for native client qualification")
    def test_native_client_correlation_deadline_and_inert_reply(self):
        def probe(response, timeout=1000, trickle=False):
            endpoint = str(self.fixture.root / "probe.sock")
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as listener:
                listener.bind(endpoint)
                listener.listen(1)
                def peer():
                    with listener.accept()[0] as connection:
                        connection.settimeout(5)
                        header=connection.recv(8,socket.MSG_WAITALL)
                        connection.recv(struct.unpack(">I",header[4:])[0],socket.MSG_WAITALL)
                        try:
                            if trickle:
                                for byte in csp.frame(response):
                                    connection.sendall(bytes([byte]))
                                    time.sleep(.04)
                            else:
                                connection.sendall(csp.frame(response))
                        except (BrokenPipeError,ConnectionResetError):
                            pass
                worker=threading.Thread(target=peer,daemon=True)
                worker.start()
                path=self.fixture.root / "probe.metta"
                path.write_text(f'''!(import! &self space_service)
!(space:call "{endpoint}" (request "probe" hello) {timeout})
''')
                started=time.monotonic()
                result=subprocess.run([CETTA,"--lang","petta","--quiet",str(path)],
                                      cwd=ROOT,text=True,capture_output=True,timeout=10)
                duration=time.monotonic()-started
                worker.join(timeout=5)
                self.assertFalse(worker.is_alive())
                Path(endpoint).unlink()
                self.assertEqual(result.returncode,0,result.stderr)
                return result.stdout,duration
        text,_=probe(E("reply","other",S("ok"),7))
        self.assertIn("space:uncertain",text)
        text,_=probe(E("reply","probe",S("invented-status"),7))
        self.assertIn("space:uncertain",text)
        text,_=probe(E("reply","probe",S("ok"),E("+",1,2)))
        self.assertIn("(+ 1 2)",text)
        text,duration=probe(E("reply","probe",S("ok"),7),timeout=90,trickle=True)
        self.assertIn("space:uncertain",text)
        self.assertLess(duration,1)

    @unittest.skipUnless(CETTA, "provide CeTTa binary for native client qualification")
    def test_installed_release_and_relative_library_discovery(self):
        prefix=self.fixture.root / "install"
        arguments=[sys.executable,str(ROOT / "scripts/install_space_service.py"),
                   "--prefix",str(prefix),"--server",SERVICE,"--client",CETTA]
        bridge=os.environ.get("CETTA_MORK_SPACE_BRIDGE_LIB")
        if bridge:
            arguments += ["--bridge",bridge]
        for _ in range(2):
            result=subprocess.run(arguments,cwd=self.fixture.root,text=True,capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr)
        current=prefix / "lib/cetta-space/current"
        state=prefix / "state/cetta-space"
        self.assertTrue(state.is_dir() and not state.is_symlink())
        self.assertEqual(state.stat().st_mode & 0o777,0o700)
        manifest=json.loads((current / "manifest.json").read_text())
        for name,expected in manifest["files"].items():
            self.assertEqual(hashlib.sha256((current / name).read_bytes()).hexdigest(),expected)
        env=os.environ.copy()
        env["CETTA_SPACE_SOCKET"]=self.fixture.socket
        env.pop("CETTA_LIBRARY_ROOT",None)
        for mode in ("he","petta","prime"):
            result=subprocess.run([str(prefix / "bin/cetta-space-client"),"--lang",mode,"--quiet",
                                   str(ROOT / "examples/space-service/client.metta")],cwd=self.fixture.root,
                                  env=env,text=True,capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn("native bag",result.stdout)
        result=subprocess.run([str(prefix / "bin/cetta-space"),"--socket",self.fixture.socket,"hello"],
                              cwd=self.fixture.root,text=True,capture_output=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn("capabilities",result.stdout)
        # An existing release with corrupted bytes must not be silently reused.
        with (current / "space_service_client.py").open("ab") as stream:
            stream.write(b"\n# changed\n")
        result=subprocess.run(arguments,cwd=self.fixture.root,text=True,capture_output=True,timeout=30)
        self.assertNotEqual(result.returncode,0)
        self.assertIn("verification failed",result.stderr)


MEMBER = os.getuid()
NOBODY = 65534
ACCOUNT = E("account", MEMBER, S(pwd.getpwuid(MEMBER).pw_name))
# A send-only mailbox grant: one message and its pending record, correlated by
# shared variables, with the authenticated account as the sender's account.
SENDER = E("from", E("caller"), Var(2))
SHAPE = E("bundle", E("MailboxMessage", 1, Var(0), Var(1), "", SENDER, Var(3)),
          E("MailboxPending", 1, Var(1), "", SENDER, Var(0)))
UNIQUE = E("MailboxMessage", 1, Var(0), Var(4), Var(5), E("from", E("caller"), Var(6)), Var(7))


def letter(ident, to, agent, body, sender=None):
    sender = sender or E("from", ACCOUNT, agent)
    return (E("MailboxMessage", 1, ident, to, "", sender, body),
            E("MailboxPending", 1, to, "", sender, ident))


class PrincipalTests(unittest.TestCase):
    """Another account's view, staged on one login: stores and grants are made
    while this account owns the service, which then restarts under another
    owner so that this account connects as an ordinary member."""

    def setUp(self):
        self.fixture = Fixture(shared=True)
        self.addCleanup(self.fixture.close)

    def connect(self, shared=False):
        client = Client(self.fixture.shared_socket if shared else self.fixture.socket)
        self.addCleanup(client.close)
        return client

    def as_owner(self):
        self.fixture.options.pop("owner_uid", None)
        self.fixture.restart()
        return self.connect()

    def as_member(self):
        self.fixture.options["owner_uid"] = NOBODY
        self.fixture.restart()
        return self.connect(shared=True)

    def denied(self, reason, function, *args, **kwargs):
        with self.assertRaises(csp.ServiceError) as caught:
            function(*args, **kwargs)
        self.assertEqual(caught.exception.reason, S(reason))

    def prepare(self, owner):
        mail, secret, shared = owner.open("mail"), owner.open("secret"), owner.open("shared")
        secret.add(E("private", "diary"))
        shared.add(E("note", "hello"))
        owner.call("grant", mail.uuid, mail.id, E("account", MEMBER), E("rights", S("append")), SHAPE, UNIQUE)
        owner.call("grant", shared.uuid, shared.id, E("account", MEMBER), E("rights", S("read")),
                   S("none"), S("none"))
        return mail, secret, shared

    def test_shared_socket_is_group_only_and_the_owner_is_one_principal(self):
        state = Path(self.fixture.shared_socket).stat()
        self.assertEqual(state.st_mode & 0o777, 0o660)
        self.assertEqual(state.st_gid, os.getgid())
        self.assertEqual(Path(self.fixture.socket).stat().st_mode & 0o777, 0o600)
        private, shared = self.connect(), self.connect(shared=True)
        for client in (private, shared):
            self.assertEqual(client.call("whoami"), E("principal", ACCOUNT, S("owner")))
        space = private.open("same")
        args = (space.uuid, space.id, space.revision, E("ops", E("add", S("once"))))
        first = private.call("batch", *args, request_id="same-request")
        self.assertEqual(shared.call("batch", *args, request_id="same-request"), first)
        self.assertEqual(space.match_count(S("once")), 1)
        journal = (self.fixture.data / "journal.csl").read_bytes()
        self.assertIn(b"event-authorized", journal)
        self.assertNotIn(b"event-from", journal)

    def test_principal_incarnations_are_service_scoped_monotone_and_durable(self):
        owner = self.connect()
        uuid = owner.hello()[2]
        self.assertEqual(owner.principal_ref(), E("principal-ref", uuid, MEMBER, 0))
        first = owner.rotate_principal(E("account", MEMBER))[1]
        self.assertEqual(first[:3], (S("principal-ref"), uuid, MEMBER))
        self.assertGreater(first[3], 0)
        self.fixture.restart(crash=True)
        owner = self.connect()
        second = owner.rotate_principal(E("account", MEMBER))[1]
        self.assertEqual(second[:3], first[:3])
        self.assertGreater(second[3], first[3])

    def test_unbound_member_observes_only_opaque_absence_without_minting(self):
        self.fixture.options["owner_uid"] = NOBODY
        self.fixture.restart()
        member = self.connect(shared=True)
        hello = member.hello()
        self.assertEqual(member.call("whoami"), E("principal", ACCOUNT, S("member")))
        self.assertEqual(member.list_spaces(), [])
        self.assertEqual(member.call("receipt", hello[2], "absent"), S("unknown-receipt"))
        self.denied("missing-origin", member.call, "origin", hello[2],
                    E("origin-ref", hello[2], MEMBER, "absent", "0" * 32))
        self.denied("missing-space", member.open, "absent")
        self.denied("authority-refused", member.principal_ref)
        self.denied("service-identity", member.call, "list", "0" * 32)
        owner = self.as_owner()
        self.assertEqual(owner.rotate_principal(E("account", MEMBER))[1][3], 1)

    def test_rotation_rekeys_grants_tickets_and_request_ids_end_to_end(self):
        owner = self.connect()
        first = owner.rotate_principal(E("account", MEMBER))[1]
        space = owner.open("rotating-member")
        owner.call("grant-ticketed", space.uuid, space.id, E("account", MEMBER),
                   E("rights", S("write")), E("rights", S("write")),
                   S("none"), S("none"))
        old_ticket = owner.call("issue-ticket", space.uuid, space.id,
                                E("account", MEMBER), E("rights", S("write")))[3]
        member = self.as_member()
        self.assertEqual(member.principal_ref(), first)
        space = member.open("rotating-member")
        old_ops = E("ops", E("add", E("incarnation", 1)))
        member.call("batch-ticket", space.uuid, space.id, space.revision,
                    old_ticket, old_ops, request_id="incarnation-write")

        owner = self.as_owner()
        space = owner.open("rotating-member")
        second = owner.rotate_principal(E("account", MEMBER))[1]
        self.assertGreater(second[3], first[3])
        member = self.as_member()
        self.assertEqual(member.principal_ref(), second)
        self.denied("missing-space", member.open, "rotating-member")
        self.assertEqual(member.call("receipt", space.uuid, "incarnation-write"),
                         S("unknown-receipt"))

        owner = self.as_owner()
        space = owner.open("rotating-member")
        owner.call("grant-ticketed", space.uuid, space.id, E("account", MEMBER),
                   E("rights", S("write")), E("rights", S("write")),
                   S("none"), S("none"))
        fresh_ticket = owner.call("issue-ticket", space.uuid, space.id,
                                  E("account", MEMBER), E("rights", S("write")))[3]
        member = self.as_member()
        space = member.open("rotating-member")
        self.denied("ticket-unavailable", member.call, "batch-ticket", space.uuid,
                    space.id, space.revision, old_ticket, E("ops"))
        fresh_ops = E("ops", E("add", E("incarnation", 2)))
        member.call("batch-ticket", space.uuid, space.id, space.revision,
                    fresh_ticket, fresh_ops, request_id="incarnation-write")
        self.assertCountEqual(space.match(E("incarnation", Var(0)), Var(0)), [1, 2])

    def test_member_sees_only_granted_stores(self):
        _, secret, _ = self.prepare(self.connect())
        member = self.as_member()
        self.assertEqual(member.call("whoami"), E("principal", ACCOUNT, S("member")))
        self.assertEqual(member.hello()[4], 0)
        listed = {entry[3]: entry[6:] for entry in member.list_spaces()}
        self.assertEqual(listed, {"mail": (0, 0), "shared": (1, 1)})
        self.denied("missing-space", member.open, "secret")
        self.denied("missing-space", member.open, "absent")
        self.denied("missing-space", member.call, "describe", secret.uuid, secret.id)
        self.denied("missing-space", member.call, "batch", secret.uuid, secret.id, 1, E("ops"))
        mail = member.open("mail")
        self.assertEqual(mail.revision, 0)
        for operation, args in (("describe", ()), ("verify", ()), ("count", (Var(0),)),
                                ("query", (Var(0), Var(0), 10, 1000)), ("watch", (0, 10)),
                                ("batch", (0, E("ops"))), ("drop", (0, 0)),
                                ("grant", (E("account", MEMBER), E("rights"), S("none"), S("none"))),
                                ("append", (E("bundle", S("loose")),))):
            with self.subTest(operation=operation):
                expected = "shape-mismatch" if operation == "append" else "forbidden"
                self.denied(expected, member.call, operation, mail.uuid, mail.id, *args)
        self.denied("forbidden", member.call, "compact", mail.uuid)
        shared = member.open("shared")
        self.assertEqual(shared.match(E("note", Var(0)), Var(0)), ["hello"])
        self.denied("forbidden", member.call, "batch", shared.uuid, shared.id, shared.revision, E("ops"))

    def test_member_append_is_shaped_unique_and_authenticated(self):
        self.prepare(self.connect())
        member = self.as_member()
        mail = member.open("mail")

        def post(bundle):
            return member.call("append", mail.uuid, mail.id, E("bundle", *bundle))

        self.assertEqual(post(letter("m1", "receiver", "writer", "hi")), S("appended"))
        self.assertEqual(post(letter("m1", "receiver", "writer", "hi")), S("duplicate"))
        self.denied("id-conflict", post, letter("m1", "receiver", "writer", "changed"))
        forged = E("from", E("account", 1), "writer")
        self.denied("shape-mismatch", post, letter("m2", "receiver", "writer", "x", sender=forged))
        self.denied("shape-mismatch", post, letter("m3", "receiver", "writer", "x")[:1])
        message, _ = letter("m4", "receiver", "writer", "x")
        _, other = letter("m5", "receiver", "writer", "x")
        self.denied("shape-mismatch", post, (message, other))
        ready = E("MailboxReady", 1, "m6", "receiver", 0, S("none"), "m6")
        self.denied("shape-mismatch", post, letter("m6", "receiver", "writer", "x") + (ready,))
        sender = E("from", ACCOUNT, "writer")
        self.denied("open-value", post, (E("MailboxMessage", 1, Var(9), "receiver", "", sender, "x"),
                                         E("MailboxPending", 1, "receiver", "", sender, Var(9))))
        owner = self.as_owner()
        mail = owner.open("mail")
        self.assertEqual(mail.match(E("MailboxMessage", 1, Var(0), Var(1), Var(2), Var(3), Var(4)),
                                    E("seen", Var(0), Var(1), Var(3), Var(4))),
                         [E("seen", "m1", "receiver", sender, "hi")])
        self.assertEqual(mail.match_count(E("MailboxPending", 1, Var(0), Var(1), Var(2), Var(3))), 1)
        # A pickup consumes the pending record but keeps the message, so an
        # identical retry is still a duplicate and a changed one a conflict.
        mail.batch(E("remove", letter("m1", "receiver", "writer", "hi")[1]))
        member = self.as_member()
        mail = member.open("mail")
        self.assertEqual(post(letter("m1", "receiver", "writer", "hi")), S("duplicate"))
        self.denied("id-conflict", post, letter("m1", "receiver", "writer", "changed"))

        # Distinct IDs sharing the structural string hash still name distinct
        # messages. A candidate collision must not become a duplicate/conflict.
        for message_id in ("!c", '"@'):
            self.assertEqual(post(letter(message_id, "receiver", "writer", "hi")), S("appended"))
            self.assertEqual(post(letter(message_id, "receiver", "writer", "hi")), S("duplicate"))

    @unittest.skipUnless(CETTA, "provide cetta for the MeTTa mailbox client")
    def test_mailbox_post_from_another_account_reaches_the_owner(self):
        owner = self.connect()
        mail = owner.open("agent-mail")
        owner.call("grant", mail.uuid, mail.id, E("account", MEMBER), E("rights", S("append")), SHAPE, UNIQUE)

        def run(socket, source):
            path = self.fixture.root / "client.metta"
            path.write_text("!(import! &self mailbox)\n" + source)
            process = subprocess.run([CETTA, "--lang", "petta", "--quiet", str(path)], cwd=ROOT,
                                     text=True, capture_output=True, timeout=30)
            self.assertEqual(process.returncode, 0, process.stderr)
            return process.stdout

        post = f'!(mailbox:post "{self.fixture.shared_socket}" "agent-mail" "p1" "receiver" "writer" "hello")\n'
        self.as_member()
        self.assertIn('(mailbox:sent "p1" accepted)', run(self.fixture.shared_socket, post))
        self.assertIn('(mailbox:sent "p1" duplicate)', run(self.fixture.shared_socket, post))
        self.as_owner()
        account = f'(account {MEMBER} {pwd.getpwuid(MEMBER).pw_name})'
        self.assertIn(f'(mailbox:delivery "p1" (from {account} "writer") "hello")',
                      run(self.fixture.socket, f'!(mailbox:next "{self.fixture.socket}" "agent-mail" "receiver")\n'))

        # The public helper reaches the same authenticated append boundary
        # through a generation-bound ticket and consumes it with the send.
        owner = self.connect()
        mail = owner.open("agent-mail")
        owner.call("grant-ticketed", mail.uuid, mail.id, E("account", MEMBER),
                   E("rights", S("append")), E("rights", S("append")), SHAPE, UNIQUE)
        issued = owner.call("issue-ticket", mail.uuid, mail.id, E("account", MEMBER),
                            E("rights", S("append")))
        self.as_member()
        post_ticket = (f'!(mailbox:post-ticket "{self.fixture.shared_socket}" "agent-mail" '
                       f'{issued[3]} "p2" "receiver" "writer" "ticketed")\n')
        self.assertIn('(mailbox:sent "p2" accepted)',
                      run(self.fixture.shared_socket, post_ticket))
        owner = self.as_owner()
        mail = owner.open("agent-mail")
        sender = E("from", ACCOUNT, "writer")
        self.assertEqual(
            mail.match(E("MailboxMessage", 1, "p2", "receiver", "", sender, Var(0)), Var(0)),
            ["ticketed"])

    def test_grants_survive_a_checkpoint_and_revocation_removes_access(self):
        _, _, shared = self.prepare(self.connect())
        self.connect().call("compact", shared.uuid)
        member = self.as_member()
        self.assertEqual(member.open("shared").match(E("note", Var(0)), Var(0)), ["hello"])
        owner = self.as_owner()
        shared = owner.open("shared")
        self.assertEqual(owner.call("grant", shared.uuid, shared.id, E("account", MEMBER), E("rights"),
                                    S("none"), S("none")),
                         E("granted", E("account", MEMBER), E("rights")))
        member = self.as_member()
        self.denied("missing-space", member.open, "shared")

    def test_regrant_does_not_reauthorize_cached_reply(self):
        mail, _, _ = self.prepare(self.connect())
        member = self.as_member()
        mail = member.open("mail")
        bundle = E("bundle", *letter("receipt-generation", "receiver", "writer", "one"))
        self.assertEqual(member.call("append", mail.uuid, mail.id, bundle,
                                     request_id="member-authorized"), S("appended"))

        owner = self.as_owner()
        mail = owner.open("mail")
        self.assertEqual(owner.call("grant", mail.uuid, mail.id, E("account", MEMBER),
                                    E("rights"), S("none"), S("none"))[0], S("granted"))
        self.assertEqual(owner.call("grant", mail.uuid, mail.id, E("account", MEMBER),
                                    E("rights", S("append")), SHAPE, UNIQUE)[0], S("granted"))

        member = self.as_member()
        mail = member.open("mail")
        self.denied("authority-refused", member.call, "receipt", mail.uuid,
                    "member-authorized")
        self.denied("authority-refused", member.call, "append", mail.uuid, mail.id, bundle,
                    request_id="member-authorized")
        owner = self.as_owner()
        mail = owner.open("mail")
        self.assertEqual(mail.match_count(E("MailboxMessage", 1, "receipt-generation",
                                            Var(0), Var(1), Var(2), Var(3))), 1)

    def test_one_shot_append_ticket_is_commit_spent_and_durable(self):
        owner = self.connect()
        mail, _, _ = self.prepare(owner)
        self.assertIn(S("one-shot-tickets"), owner.hello()[5])
        owner.call("grant-ticketed", mail.uuid, mail.id, E("account", MEMBER),
                   E("rights", S("append")), E("rights", S("append")), SHAPE, UNIQUE)
        ticket = owner.call("issue-ticket", mail.uuid, mail.id, E("account", MEMBER),
                            E("rights", S("append")))
        self.assertEqual(ticket[:3], (S("ticket"), mail.uuid, mail.id))
        ticket_id = ticket[3]
        bundle = E("bundle", *letter("ticketed", "receiver", "writer", "once"))

        member = self.as_member()
        mail = member.open("mail")
        args = (mail.uuid, mail.id, ticket_id, bundle)
        self.denied("ticket-required", member.call, "append", mail.uuid, mail.id, bundle)
        self.assertEqual(member.call("append-ticket", *args, request_id="ticket-use"),
                         S("appended"))
        self.assertEqual(member.call("append-ticket", *args, request_id="ticket-use"),
                         S("appended"))
        self.assertEqual(member.call("receipt", mail.uuid, "ticket-use"),
                         E("reply", "ticket-use", S("ok"), S("appended")))
        self.denied("ticket-unavailable", member.call, "append-ticket", *args,
                    request_id="copied-ticket")

        owner = self.as_owner()
        mail = owner.open("mail")
        owner.call("compact", mail.uuid)
        member = self.as_member()
        mail = member.open("mail")
        self.assertEqual(member.call("append-ticket", *args, request_id="ticket-use"),
                         S("appended"))
        self.denied("ticket-unavailable", member.call, "append-ticket", *args,
                    request_id="copied-after-restart")
        owner = self.as_owner()
        mail = owner.open("mail")
        self.assertEqual(mail.match_count(E("MailboxMessage", 1, "ticketed",
                                            Var(0), Var(1), Var(2), Var(3))), 1)
        revoked = owner.call("issue-ticket", mail.uuid, mail.id, E("account", MEMBER),
                             E("rights", S("append")))
        self.assertEqual(owner.call("revoke-ticket", mail.uuid, mail.id, revoked[3]),
                         S("ticket-revoked"))
        member = self.as_member()
        mail = member.open("mail")
        self.denied("ticket-unavailable", member.call, "append-ticket", mail.uuid,
                    mail.id, revoked[3], bundle, request_id="revoked-ticket")

    @unittest.skipUnless(CETTA, "provide cetta for the explicit grant helper")
    def test_explicit_root_grant_is_reference_only_and_durable(self):
        owner = self.connect()
        space = owner.open("explicit-grant")
        self.assertIn(S("attenuating-grants"), owner.hello()[5])
        granted = owner.call("grant-root", space.uuid, space.id, E("account", MEMBER),
                             E("rights", S("append")), E("rights", S("append")),
                             S("false"), S("none"), S("none"))
        self.assertEqual(granted[0], S("granted-root"))
        grant = granted[1]
        issued = owner.call("issue-ticket-v2", space.uuid, space.id, grant,
                            E("rights", S("append")))
        self.assertEqual(issued[0], S("ticket-v2"))
        ticket = issued[2]

        member = self.as_member()
        self.denied("missing-space", member.open, "explicit-grant")
        self.denied("missing-space", member.call, "append", space.uuid, space.id,
                    E("bundle", S("ambient-bypass")))

        descriptor = (f'(space "{space.uuid}" {space.id} "{space.name}" '
                      f'native bag {space.revision} {space.count})')
        grant_text = f'(grant-ref "{grant[1]}" {grant[2]} {grant[3]} {grant[4]})'
        ticket_text = f'(ticket-ref "{ticket[1]}" {ticket[2]} {ticket[3]})'
        source = (f'!(import! &self space_service)\n'
                  f'!(space:append-authorized "{self.fixture.shared_socket}" {descriptor} '
                  f'{grant_text} {ticket_text} (bundle explicit-value))\n')
        path = self.fixture.root / "explicit-grant.metta"
        path.write_text(source)
        process = subprocess.run([CETTA, "--lang", "petta", "--quiet", str(path)],
                                 cwd=ROOT, text=True, capture_output=True, timeout=30)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertIn("appended", process.stdout)

        owner = self.as_owner()
        stored = owner.open("explicit-grant")
        self.assertEqual(stored.match_count(S("explicit-value")), 1)

    @unittest.skipUnless(CETTA, "provide cetta for the explicit read-grant helpers")
    def test_explicit_read_grant_carries_snapshot_watch_and_regrant_lifetimes(self):
        owner = self.connect()
        space = owner.open("explicit-read")
        space.batch(E("add", E("visible", 1)), E("add", E("visible", 2)))
        grant = owner.call("grant-root", space.uuid, space.id, E("account", MEMBER),
                           E("rights", S("observe"), S("read")), E("rights"),
                           S("false"), S("none"), S("none"))[1]

        member = self.as_member()
        self.assertIn(S("explicit-read-grants"), member.hello()[5])
        self.denied("missing-space", member.open, "explicit-read")
        self.denied("missing-space", member.call, "query", space.uuid, space.id,
                    E("visible", Var(0)), Var(0), 10, 30000)
        authority = E("authority", grant)
        descriptor = member.call("describe", space.uuid, space.id, authority)
        view = csp.Space(member, descriptor)
        self.assertEqual(view.verify(grant)[0], S("verified"))
        self.assertEqual(view.match_count(E("visible", Var(0)), grant=grant), 2)
        self.assertEqual(view.match(E("visible", Var(0)), Var(0), grant=grant), [1, 2])
        self.assertEqual(view.watch(view.revision, 0, grant)[0], S("unchanged"))

        descriptor_text = (f'(space "{view.uuid}" {view.id} "{view.name}" '
                           f'{view.backend} {view.multiplicity} {view.revision} {view.count})')
        grant_text = f'(grant-ref "{grant[1]}" {grant[2]} {grant[3]} {grant[4]})'
        source = (f'!(import! &self space_service)\n'
                  f'!(space:count-authorized "{self.fixture.shared_socket}" '
                  f'{descriptor_text} {grant_text} (visible $x))\n'
                  f'!(space:watch-authorized "{self.fixture.shared_socket}" '
                  f'{descriptor_text} {grant_text} {view.revision} 0)\n')
        path = self.fixture.root / "explicit-read.metta"
        path.write_text(source)
        process = subprocess.run([CETTA, "--lang", "petta", "--quiet", str(path)],
                                 cwd=ROOT, text=True, capture_output=True, timeout=30)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertIn("2", process.stdout)
        self.assertIn("unchanged", process.stdout)

        owner = self.as_owner()
        current = owner.open("explicit-read")
        owner.call("revoke-grant", current.uuid, current.id, grant)
        fresh = owner.call("grant-root", current.uuid, current.id, E("account", MEMBER),
                           E("rights", S("observe"), S("read")), E("rights"),
                           S("false"), S("none"), S("none"))[1]
        member = self.as_member()
        self.denied("missing-space", member.call, "count", current.uuid, current.id,
                    E("visible", Var(0)), E("authority", grant))
        self.assertEqual(member.call("count", current.uuid, current.id,
                                     E("visible", Var(0)), E("authority", fresh)), 2)

    @unittest.skipUnless(CETTA, "provide cetta for the account-origin helper")
    def test_account_origin_is_service_stamped_historical_and_durable(self):
        owner = self.connect()
        space = owner.open("account-origin")
        pattern = E("bundle", E("attested", E("caller"), E("origin"), Var(0)))
        self.denied("invalid-grant", owner.call, "grant-root", space.uuid, space.id,
                    E("account", MEMBER), E("rights", S("write")), E("rights"),
                    S("false"), pattern, S("none"))
        self.denied("invalid-grant", owner.call, "grant-root", space.uuid, space.id,
                    E("account", MEMBER), E("rights", S("append")), E("rights"),
                    S("false"), E("bundle", E("plain", Var(0))), E("origin"))
        granted = owner.call("grant-root", space.uuid, space.id, E("account", MEMBER),
                             E("rights", S("append")), E("rights"), S("false"),
                             pattern, S("none"))
        grant = granted[1]

        member = self.as_member()
        bundle = E("bundle", E("attested", ACCOUNT, E("origin"), 7))
        self.assertEqual(member.call("append-authorized", space.uuid, space.id, grant,
                                     S("none"), bundle, request_id="origin-one"),
                         S("appended"))

        self.fixture.options.pop("owner_uid", None)
        self.fixture.restart(crash=True)
        owner = self.connect()
        stored = owner.open("account-origin")
        origins = stored.match(E("attested", ACCOUNT, Var(0), 7), Var(0))
        self.assertEqual(len(origins), 1)
        origin = origins[0]
        self.assertEqual(origin[:4], (S("origin-ref"), stored.uuid, MEMBER, "origin-one"))
        self.assertEqual(len(origin[4]), 32)
        attestation = owner.call("origin", stored.uuid, origin)
        self.assertEqual(len(attestation), 5)
        self.assertEqual(attestation[0], S("account-origin"))
        self.assertEqual(attestation[1], origin)
        self.assertEqual(attestation[2][:3], (S("principal-ref"), stored.uuid, MEMBER))
        self.assertGreater(attestation[2][3], 0)
        self.assertEqual(attestation[3], E("origin-authority", stored.id, grant[3], grant[4],
                                           E("rights", S("append")), S("none")))
        self.assertEqual(len(attestation[4]), 64)
        materialized = E("bundle", E("attested", ACCOUNT, origin, 7))
        self.assertEqual(attestation[4], hashlib.sha256(csp.encode(materialized)).hexdigest())

        member = self.as_member()
        forged = E("bundle", E("attested", ACCOUNT, origin, 8))
        self.denied("origin-required", member.call, "append-authorized", space.uuid,
                    space.id, grant, S("none"), forged, request_id="origin-forged")
        bad = E("origin-ref", origin[1], origin[2], origin[3], "0" * 32)
        self.denied("missing-origin", member.call, "origin", space.uuid, bad)

        owner = self.as_owner()
        stored = owner.open("account-origin")
        for index in range(256):
            stored.batch(request_id=f"origin-evict-{index}")
        member = self.as_member()
        self.assertEqual(member.call("receipt", space.uuid, "origin-one"),
                         S("unknown-receipt"))
        self.assertEqual(member.call("origin", space.uuid, origin), attestation)

        owner = self.as_owner()
        stored = owner.open("account-origin")
        owner.call("revoke-grant", stored.uuid, stored.id, grant)
        owner.call("compact", stored.uuid)
        member = self.as_member()
        self.denied("missing-space", member.call, "append-authorized", space.uuid,
                    space.id, grant, S("none"), bundle, request_id="origin-after-revoke")
        self.assertEqual(member.call("origin", space.uuid, origin), attestation)

        source = (f'!(import! &self space_service)\n'
                  f'!(space:origin "{self.fixture.shared_socket}" '
                  f'(origin-ref "{origin[1]}" {origin[2]} "{origin[3]}" "{origin[4]}"))\n')
        path = self.fixture.root / "account-origin.metta"
        path.write_text(source)
        process = subprocess.run([CETTA, "--lang", "petta", "--quiet", str(path)],
                                 cwd=ROOT, text=True, capture_output=True, timeout=30)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertIn("account-origin", process.stdout)

    def test_account_origin_digest_is_checked_during_recovery(self):
        owner = self.connect()
        space = owner.open("origin-digest")
        pattern = E("bundle", E("sealed-origin", E("caller"), E("origin"), Var(0)))
        grant = owner.call("grant-root", space.uuid, space.id, E("account", MEMBER),
                           E("rights", S("append")), E("rights"), S("false"),
                           pattern, S("none"))[1]
        member = self.as_member()
        member.call("append-authorized", space.uuid, space.id, grant, S("none"),
                    E("bundle", E("sealed-origin", ACCOUNT, E("origin"), 9)),
                    request_id="origin-digest-event")
        member.close()
        self.fixture.stop()

        journal = self.fixture.data / "journal.csl"
        data = bytearray(journal.read_bytes())
        position = 0
        changed = False
        while position < len(data):
            self.assertEqual(data[position:position+4], b"CSL1")
            length = struct.unpack(">I", data[position+4:position+8])[0]
            start, end = position + 72, position + 72 + length
            record = csp.decode(bytes(data[start:end]))
            if record[0] == S("event-authorized-v5") and record[9] != S("none"):
                record = tuple(record[:10]) + ("0" * 64,) + tuple(record[11:])
                payload = csp.encode(record)
                self.assertEqual(len(payload), length)
                data[position+8:position+72] = hashlib.sha256(payload).hexdigest().encode()
                data[start:end] = payload
                changed = True
                break
            position = end
        self.assertTrue(changed)
        journal.write_bytes(data)
        self.fixture.start(success=False)
        self.assertNotEqual(self.fixture.process.returncode, 0)
        self.fixture.process = None

    def test_ticket_conflict_preserves_currency_and_regrant_invalidates_it(self):
        owner = self.connect()
        space = owner.open("ticket-write")
        owner.call("grant-ticketed", space.uuid, space.id, E("account", MEMBER),
                   E("rights", S("write")), E("rights", S("write")), S("none"), S("none"))
        ticket = owner.call("issue-ticket", space.uuid, space.id, E("account", MEMBER),
                            E("rights", S("write")))
        ticket_id = ticket[3]

        member = self.as_member()
        space = member.open("ticket-write")
        ops = E("ops", E("add", S("once")))
        self.denied("ticket-required", member.call, "batch", space.uuid, space.id,
                    space.revision, ops)
        self.denied("stale-revision", member.call, "batch-ticket", space.uuid, space.id,
                    space.revision + 1, ticket_id, ops)
        self.assertEqual(member.call("batch-ticket", space.uuid, space.id, space.revision,
                                     ticket_id, ops)[3], space.revision + 1)
        self.denied("ticket-unavailable", member.call, "batch-ticket", space.uuid, space.id,
                    space.revision + 1, ticket_id, E("ops"))

        owner = self.as_owner()
        space = owner.open("ticket-write")
        fresh = owner.call("issue-ticket", space.uuid, space.id, E("account", MEMBER),
                           E("rights", S("write")))
        owner.call("grant", space.uuid, space.id, E("account", MEMBER), E("rights"),
                   S("none"), S("none"))
        owner.call("grant-ticketed", space.uuid, space.id, E("account", MEMBER),
                   E("rights", S("write")), E("rights", S("write")), S("none"), S("none"))
        member = self.as_member()
        space = member.open("ticket-write")
        self.denied("ticket-unavailable", member.call, "batch-ticket", space.uuid, space.id,
                    space.revision, fresh[3], E("ops"))


class QuotaTests(unittest.TestCase):
    def test_optional_principal_limit_refuses_new_accounts_not_rotation(self):
        fixture = Fixture(max_principals=1)
        try:
            with Client(fixture.socket) as client:
                first = client.rotate_principal(E("account", NOBODY))[1]
                second = client.rotate_principal(E("account", NOBODY))[1]
                self.assertGreater(second[3], first[3])
                with self.assertRaises(csp.ServiceError) as caught:
                    client.rotate_principal(E("account", MEMBER + 1))
                self.assertEqual(caught.exception.reason, S("principal-capacity"))
                space = client.open("principal-limit")
                client.call("compact", space.uuid)
            fixture.restart()
            with Client(fixture.socket) as client:
                third = client.rotate_principal(E("account", NOBODY))[1]
                self.assertGreater(third[3], second[3])
                with self.assertRaises(csp.ServiceError) as caught:
                    client.rotate_principal(E("account", MEMBER + 1))
                self.assertEqual(caught.exception.reason, S("principal-capacity"))
        finally:
            fixture.close()

    def test_optional_grant_limit_refuses_instead_of_reusing_identity(self):
        fixture = Fixture(max_grants=1)
        try:
            with Client(fixture.socket) as client:
                space = client.open("one-grant")
                first = client.call("grant-root", space.uuid, space.id,
                                    E("account", MEMBER), E("rights", S("append")),
                                    E("rights"), S("false"), S("none"), S("none"))
                self.assertEqual(first[1][3:], (1, 1))
                with self.assertRaises(csp.ServiceError) as caught:
                    client.call("grant-root", space.uuid, space.id,
                                E("account", MEMBER + 1), E("rights", S("append")),
                                E("rights"), S("false"), S("none"), S("none"))
                self.assertEqual(caught.exception.reason, S("grant-capacity"))
                client.call("compact", space.uuid)
            fixture.restart()
            with Client(fixture.socket) as client:
                space = client.open("one-grant")
                with self.assertRaises(csp.ServiceError) as caught:
                    client.call("grant-root", space.uuid, space.id,
                                E("account", MEMBER + 1), E("rights", S("append")),
                                E("rights"), S("false"), S("none"), S("none"))
                self.assertEqual(caught.exception.reason, S("grant-capacity"))
        finally:
            fixture.close()

    def test_optional_ticket_limit_refuses_instead_of_reusing_identity(self):
        fixture = Fixture(max_tickets=1)
        try:
            with Client(fixture.socket) as client:
                space = client.open("one-ticket")
                client.call("grant-ticketed", space.uuid, space.id, E("account", MEMBER),
                            E("rights", S("append")), E("rights", S("append")),
                            S("none"), S("none"))
                first = client.call("issue-ticket", space.uuid, space.id, E("account", MEMBER),
                                    E("rights", S("append")))
                with self.assertRaises(csp.ServiceError) as caught:
                    client.call("issue-ticket", space.uuid, space.id, E("account", MEMBER),
                                E("rights", S("append")))
                self.assertEqual(caught.exception.reason, S("ticket-capacity"))
                client.call("compact", space.uuid)
            fixture.restart()
            with Client(fixture.socket) as client:
                space = client.open("one-ticket")
                self.assertEqual(first[3], 1)
                with self.assertRaises(csp.ServiceError) as caught:
                    client.call("issue-ticket", space.uuid, space.id, E("account", MEMBER),
                                E("rights", S("append")))
                self.assertEqual(caught.exception.reason, S("ticket-capacity"))
        finally:
            fixture.close()

    def test_optional_decision_limit_refuses_instead_of_forgetting(self):
        fixture = Fixture(max_decisions=1)
        try:
            with Client(fixture.socket) as client:
                space = client.open("one-decision")
                with self.assertRaises(csp.ServiceError) as caught:
                    space.batch()
                self.assertEqual(caught.exception.reason, S("decision-capacity"))
                self.assertEqual(space.describe()[6:], (0, 0))
                client.call("compact", space.uuid)
            fixture.restart()
            with Client(fixture.socket) as client:
                space = client.open("one-decision")
                with self.assertRaises(csp.ServiceError) as caught:
                    space.batch()
                self.assertEqual(caught.exception.reason, S("decision-capacity"))
        finally:
            fixture.close()

    def test_conservative_batch_admission_preserves_state(self):
        fixture = Fixture(max_facts=2,max_live_bytes=1024)
        try:
            with Client(fixture.socket) as client:
                space = client.open("quota")
                space.add(S("one"))
                with self.assertRaises(csp.ServiceError) as caught:
                    space.batch(E("add",S("two")),E("add",S("three")))
                self.assertEqual(caught.exception.reason,S("space-limit"))
                self.assertEqual(space.describe()[6:],(1,1))
        finally:
            fixture.close()

    def test_automatic_compaction_bounds_journal_growth(self):
        fixture = Fixture(max_journal_bytes=4096)
        try:
            with Client(fixture.socket) as client:
                space = client.open("bounded")
                for _ in range(40):
                    space.batch(E("add",E("temporary","x"*1000)),E("remove",E("temporary","x"*1000)))
                self.assertEqual(space.match_count(Var(0)),0)
                client.call("compact",space.uuid)
                state = (fixture.data / "journal.csl").stat()
                size = state.st_size
                time.sleep(.3)
                self.assertEqual((fixture.data / "journal.csl").stat().st_size,size)
                client.close()
                fixture.restart()
                self.assertEqual(client.open("bounded").describe()[6:],(40,0))
                self.assertEqual((fixture.data / "journal.csl").stat().st_ino,state.st_ino)
        finally:
            fixture.close()


if __name__ == "__main__":
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__]))
    if os.environ.get("CETTA_SPACE_TRACE"):
        Path(os.environ["CETTA_SPACE_TRACE"]).write_text(json.dumps(TRACE,indent=2)+"\n")
    sys.exit(not result.wasSuccessful())
