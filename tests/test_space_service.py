#!/usr/bin/env python3
"""Cross-process storage conformance, using an independent CSP1 codec."""
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction
import hashlib
import importlib.util
import json
from pathlib import Path
import os
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
TRACE = []


class Fixture:
    def __init__(self, **options):
        self.temp = tempfile.TemporaryDirectory(prefix="cetta-space-")
        self.root = Path(self.temp.name)
        self.socket = str(self.root / "s.sock")
        self.data = self.root / "data"
        self.options = options
        self.process = None
        self.error = self.root / "stderr"
        self.start()

    def start(self, success=True):
        self.log = self.error.open("ab")
        arguments = [SERVICE, "--socket", self.socket, "--data", str(self.data)]
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
        for backend in ("native", "pathmap", "mork"):
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
        TRACE.append({"kind": "conditional", "before": initial, "after": space.revision,
                      "stale": True, "correlated": True})

    def test_backend_key_projection_verification(self):
        for backend in ("native","pathmap","mork"):
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
        for backend in ("native", "pathmap", "mork"):
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
        for backend in ("native", "pathmap", "mork"):
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
        for backend in ("native","pathmap","mork"):
            space = self.space(backend)
            space.add(value)
            self.client.call("compact",space.uuid)
        self.client.close()
        self.fixture.restart(crash=True)
        for backend in ("native","pathmap","mork"):
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
        for backend in ("native", "pathmap", "mork"):
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
        for backend in ("native", "pathmap", "mork"):
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
        for backend in ("native","pathmap","mork"):
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
            for backend in ("native","pathmap","mork"):
                self.assertEqual(self.space(backend).match_count(Var(0)),1)

    def test_retired_receipt_cannot_make_old_write_apply_twice(self):
        space = self.space()
        before = space.revision
        ops = E("ops", E("add", S("once")))
        space.add(S("once"), request_id="old")
        for i in range(257):
            space.batch(request_id=f"retire-{i}")
        self.assertEqual(self.client.call("receipt", space.uuid, "old"), S("unknown-receipt"))
        self.denied("stale-revision", self.client.call, "batch", space.uuid, space.id, before, ops, request_id="old")
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
    def test_two_cetta_processes_and_both_dialects(self):
        space = self.space()
        for mode in ("he", "petta"):
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
        for mode in ("he","petta"):
            space = self.client.open("helper-"+mode)
            descriptor = f'(space "{space.uuid}" {space.id} "{space.name}" native bag 0 0)'
            path = self.fixture.root / ("helper-"+mode+".metta")
            path.write_text(f'''!(import! &self space_service)
!(space:open "{self.fixture.socket}" "{space.name}" native)
!(space:list "{self.fixture.socket}")
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
            self.assertIn(f'"{space.name}" native bag',process.stdout)
            self.assertEqual(space.match(E("helper",Var(0)),Var(0)),[17])

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
        for mode in ("he","petta"):
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


class QuotaTests(unittest.TestCase):
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
